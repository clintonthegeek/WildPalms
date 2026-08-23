#include "kf6mainwindow.h"
#include "kf6settings.h"
#include "actionmanager.h"
#include "autosyncorchestrator.h"

#include "../app/logwidget.h"
#include "../palm/palmdevicemonitor.h"
#include "../settingsdialog.h"

#include "../wildpalms_version.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../profile.h"

#include "../runtime/accountcontroller.h"
#include "../runtime/clobberdialog.h"
#include "../runtime/palmruntime.h"
#include "../runtime/palmrunresult.h"
#include "../runtime/profileregistry.h"
#include "../runtime/massdeleteguardpresenter.h"
#include "../sync/syncstate.h"

#include "../plugins/calendar/calendarbackendplugin.h"
#include "../plugins/memo/memobackendplugin.h"
#include "../plugins/todos/todobackendplugin.h"
// contactsbackendplugin.h and webcalbackendplugin.h are intentionally omitted:
// those plugins have hasMainView()=false and never appear in the cast loop below.
#include "../app/conflict/kalburatorinteractiveconflicthandler.h"
// Widget includes
#include "../widgets/dashboard/dashboardwidget.h"
#include "../app/patchbay/patchbaypage.h"
#include "../widgets/dialogs/profilepropertiesdialog.h"
#include "../widgets/dialogs/conflictreviewdialog.h"

#include "../app/wizard/newprofilewizard.h"
#include "../app/wizard/wizardstate.h"
#include "../runtime/standardcontributions.h"

#include <backendregistry.h>
#include <backendconfiguration.h>

#include "conflictstore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QApplication>
#include <QStatusBar>
#include <QDockWidget>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDebug>
#include <QFutureWatcher>
#include <cstring>

#include <KActionCollection>
#include <KLocalizedString>
#include <KNotification>
#include <KPageWidget>
#include <KPageWidgetItem>
#include <KStatusNotifierItem>

#include <pi-dlp.h>

KF6MainWindow::KF6MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    // KPageWidget layout
    , m_pageWidget(nullptr)
    , m_logDock(nullptr)
    , m_logWidget(nullptr)
    , m_dashboardWidget(nullptr)
    // Action manager
    , m_actionManager(nullptr)
    , m_syncPath()
    // Last used connection settings
    , m_lastUsedDevicePath()
    , m_lastUsedBaudRate()
    // Device listening mode (m_devicePollTimer and m_listeningForDevice have in-class initializers)
    , m_listeningDevicePath()
{
    setObjectName(QStringLiteral("KF6MainWindow"));

    // F.2 sub-project D follow-up: own a UI-side ConflictStore that
    // mirrors engine-detected conflicts so the badge dialog has data.
    m_uiConflictStore =
        std::make_unique<Kalburator::Conflict::ConflictStore>(this);

    // F.1c.1 — app-level BackendRegistry. Used by NewProfileWizard for
    // pre-profile discovery (PalmRuntime's per-profile registry doesn't
    // exist yet at that point).
    m_appBackendRegistry =
        std::make_unique<Kalburator::Sync::BackendRegistry>();
    WildPalms::Runtime::registerStandardContributions(
        m_appBackendRegistry.get());

    // Setup UI
    setupUI();
    setupActions();

    // F.1a: construct registry immediately after action manager is ready
    m_profileRegistry = std::make_unique<WildPalms::Runtime::ProfileRegistry>(this);

    // F.1b: construct menu controller before setupGUI so KXmlGui finds
    // file_switch_profile / file_forget_profile in the action collection
    m_profileMenuController = std::make_unique<ProfileMenuController>(
        m_profileRegistry.get(),
        actionCollection(),
        this);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::switchRequested,
            this, &KF6MainWindow::onSwitchProfile);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::forgetRequested,
            this, &KF6MainWindow::onForgetProfile);

    m_massDeleteGuard = std::make_unique<WildPalms::Runtime::MassDeleteGuardPresenter>(this);

    // Auto-detection
    m_deviceMonitor = new PalmDeviceMonitor(this);
    m_autoSync = new AutoSyncOrchestrator(this);
    m_autoSync->setDeviceMonitor(m_deviceMonitor);
    m_autoSync->setLogWidget(m_logWidget);
    m_autoSync->setProfileRegistry(m_profileRegistry.get());

    // Now setup connections after all objects are created
    setupConnections();

    // Auto-sync orchestrator — detection and profile resolution only
    connect(m_autoSync, &AutoSyncOrchestrator::deviceDetected,
            this, &KF6MainWindow::onAutoDeviceDetected);
    connect(m_autoSync, &AutoSyncOrchestrator::profileCreated,
            this, [this](const QString &path, const QString &userName) {
                m_logWidget->logInfo(i18n("Created profile for %1 at %2", userName, path));
            });
    connect(m_autoSync, &AutoSyncOrchestrator::unregisteredDeviceDetected,
            this, &KF6MainWindow::onUnregisteredDeviceDetected);
    connect(m_autoSync, &AutoSyncOrchestrator::error,
            m_logWidget, &LogWidget::logError);
    connect(m_autoSync, &AutoSyncOrchestrator::statusChanged,
            this, &KF6MainWindow::updateTrayState);

    // In development builds, load the RC file directly from the source tree.
    // In installed builds, setupGUI finds it at the standard KDE location.
#ifdef WILDPALMS_DATA_DIR
    setupGUI(Default, QStringLiteral(WILDPALMS_DATA_DIR "/wildpalmsui.rc"));
#else
    setupGUI(Default, QStringLiteral("wildpalmsui.rc"));
#endif

    // Replace the KDE-branded About dialogs with our own.
    // setupGUI(Default) creates a standard Help menu with "About KDE" and
    // "About Wild Palms" (KAboutApplicationDialog). We use KDE Frameworks as
    // a toolkit, but Wild Palms is not a KDE project.
    if (QAction *aboutApp = actionCollection()->action(QStringLiteral("help_about_app"))) {
        aboutApp->disconnect();
        connect(aboutApp, &QAction::triggered, this, &KF6MainWindow::onAbout);
    }
    if (QAction *aboutKDE = actionCollection()->action(QStringLiteral("help_about_kde"))) {
        aboutKDE->setVisible(false);
    }

    // System tray
    m_trayIcon = new KStatusNotifierItem(this);
    m_trayIcon->setIconByName(QStringLiteral("phone"));
    m_trayIcon->setToolTipTitle(i18n("Wild Palms"));
    m_trayIcon->setToolTipSubTitle(i18n("Listening for Palm devices"));
    m_trayIcon->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_trayIcon->setStatus(KStatusNotifierItem::Active);
    m_trayIcon->setStandardActionsEnabled(true);

    // Load minimize-to-tray preference (default: true = closing minimizes to tray)
    m_minimizeToTray = KF6Settings::instance().minimizeToTray();

    // Status bar — conflict badge (hidden until conflicts arrive)
    m_conflictBadge = new QPushButton(this);
    m_conflictBadge->setFlat(true);
    m_conflictBadge->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_conflictBadge->setVisible(false);
    statusBar()->addPermanentWidget(m_conflictBadge);
    connect(m_conflictBadge, &QPushButton::clicked,
            this, &KF6MainWindow::onConflictBadgeClicked);

    statusBar()->showMessage(i18n("Ready - No device connected"));

    // Log initial message
    m_logWidget->logInfo(QStringLiteral("Wild Palms %1 initialized").arg(WILDPALMS_VERSION_STRING));

    // Restore window state
    restoreWindowState();

    // F.1a: registry-driven startup (replaces QSettings defaultProfilePath lookup).
    // Deferred via singleShot(0) so that virtual dispatch works correctly from
    // subclasses (constructors run before the most-derived vtable is in place).
    // Tests call runStartupForTest() directly and never spin the event loop,
    // so this lambda is never invoked during tests.
    QTimer::singleShot(0, this, [this]() { resolveStartupProfile(); });

    // Initialize menu state
    updateMenuState(false);
    updateProfileMenuState();

    // Start udev monitoring for Palm devices
    if (!m_deviceMonitor->start()) {
        m_logWidget->logWarning(i18n("Failed to start udev monitor. "
                                      "Use Device → Connect for manual connection."));
    } else {
        m_logWidget->logInfo(i18n("Listening for Palm USB devices..."));
    }
}

KF6MainWindow::~KF6MainWindow()
{
    // Destroy the Sync Patchbay page before the member unique_ptrs it borrows
    // (m_accountController / m_palmRuntime / m_currentProfile) are torn down.
    // The dtor body runs before member destruction, so the controllers are
    // still alive here; the page dtor severs its connections to them safely.
    destroyPatchbayPage();

    // Stop udev monitor
    if (m_deviceMonitor) {
        m_deviceMonitor->stop();
    }

    // Stop any pending timers first
    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    // Disconnect device if connected (don't delete - let Qt handle it)
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_palmRuntime->disconnectDevice();
    }

    // m_currentProfile is std::unique_ptr — RAII handles deletion.
}

void KF6MainWindow::appQuitRequested()
{
    // F16: File→Quit used to call QWidget::close(), which closeEvent
    // silently converted into "hide to tray" — the window vanished, the
    // process lived on. Route real quit through a bypass flag.
    m_forceQuit = true;
    close();
}

void KF6MainWindow::closeEvent(QCloseEvent *event)
{
    // F16: guard an in-flight sync — closing mid-run risks half-written
    // data on both sides.
    if (m_palmRuntime && m_palmRuntime->isSyncRunning()) {
        const auto answer = QMessageBox::question(this,
            i18n("Sync In Progress"),
            i18n("A sync is currently in progress.\n"
                 "Closing now may leave records half-written.\n\n"
                 "Close anyway?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            m_forceQuit = false;
            event->ignore();
            return;
        }
    }

    if (!m_forceQuit && m_minimizeToTray && m_trayIcon) {
        hide();
        if (!m_trayHintShown) {
            // F16: closing used to look like the app quit (no explanation,
            // no confirmation). Explain once what actually happened.
            m_trayHintShown = true;
            QMessageBox::information(this,
                i18n("Wild Palms Keeps Running"),
                i18n("The window was sent to the system tray.\n\n"
                     "Wild Palms is still running and listening for Palm "
                     "devices.\nUse File → Quit to exit completely."));
        }
        event->ignore();
        return;
    }
    saveWindowState();
    if (m_currentProfile) {
        m_currentProfile->save();
    }
    event->accept();
}

void KF6MainWindow::setupUI()
{
    setWindowTitle(i18n("Wild Palms - Palm Pilot Synchronization"));
    setMinimumSize(1000, 700);

    createCentralLayout();
}

void KF6MainWindow::createCentralLayout()
{
    // Status header strip (120 px, sits between toolbar and plugin pages)
    m_dashboardWidget = new DashboardWidget(this);

    m_syncStatusModel = new SyncStatusModel(this);
    m_dashboardWidget->setModel(m_syncStatusModel);

    // Live udev presence (independent of any profile/runtime).
    connect(m_deviceMonitor, &PalmDeviceMonitor::palmDetected,
            this, [this](const QStringList &, const QString &) {
                m_syncStatusModel->onDeviceDetected();
            });
    connect(m_deviceMonitor, &PalmDeviceMonitor::palmDisconnected,
            this, [this](const QString &) {
                m_syncStatusModel->onDeviceLost();
            });

    // Panel action requests.
    connect(m_syncStatusModel, &SyncStatusModel::syncRequested,
            this, &KF6MainWindow::onHotSync);
    connect(m_syncStatusModel, &SyncStatusModel::cancelRequested,
            this, [this]() { if (m_palmRuntime) m_palmRuntime->cancelSync(); });
    connect(m_syncStatusModel, &SyncStatusModel::resolveConflictsRequested,
            this, &KF6MainWindow::onConflictBadgeClicked);

    // Plugin page area with icon sidebar
    m_pageWidget = new KPageWidget(this);
    m_pageWidget->setFaceType(KPageWidget::List);
    // Stack: dashboard header on top, page widget below
    auto *central = new QWidget(this);
    auto *vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_dashboardWidget);
    vbox->addWidget(m_pageWidget, 1);
    setCentralWidget(central);

    // Create log widget inside a QDockWidget at the bottom
    m_logWidget = new LogWidget(this);
    m_logDock = new QDockWidget(i18n("Log"), this);
    m_logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock->setWidget(m_logWidget);
    m_logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
}

void KF6MainWindow::setupActions()
{
    m_actionManager = new ActionManager(this, this);
}

void KF6MainWindow::setupConnections()
{
    // Connect action manager signals
    connect(m_actionManager, &ActionManager::newProfileRequested,
            this, &KF6MainWindow::onNewProfile);
    connect(m_actionManager, &ActionManager::importProfileRequested,
            this, &KF6MainWindow::onImportProfile);
    connect(m_actionManager, &ActionManager::closeProfileRequested,
            this, &KF6MainWindow::onCloseProfile);
    connect(m_actionManager, &ActionManager::profileSettingsRequested,
            this, &KF6MainWindow::onProfileSettings);
    connect(m_actionManager, &ActionManager::configureMappingsRequested,
            this, &KF6MainWindow::onConfigureMappings);
    connect(m_actionManager, &ActionManager::settingsRequested,
            this, &KF6MainWindow::onSettings);

    connect(m_actionManager, &ActionManager::connectRequested,
            this, &KF6MainWindow::onConnectDevice);
    connect(m_actionManager, &ActionManager::disconnectRequested,
            this, &KF6MainWindow::onDisconnectDevice);
    connect(m_actionManager, &ActionManager::cancelConnectionRequested,
            this, &KF6MainWindow::onCancelConnection);
    connect(m_actionManager, &ActionManager::listDatabasesRequested,
            this, &KF6MainWindow::onListDatabases);
    connect(m_actionManager, &ActionManager::setUserInfoRequested,
            this, &KF6MainWindow::onSetUserInfo);
    connect(m_actionManager, &ActionManager::deviceInfoRequested,
            this, &KF6MainWindow::onDeviceInfo);

    connect(m_actionManager, &ActionManager::hotSyncRequested,
            this, &KF6MainWindow::onHotSync);
    connect(m_actionManager, &ActionManager::fullSyncRequested,
            this, &KF6MainWindow::onFullSync);
    connect(m_actionManager, &ActionManager::copyPalmToPCRequested,
            this, &KF6MainWindow::onCopyPalmToPC);
    connect(m_actionManager, &ActionManager::clobberPalmFromPCRequested,
            this, &KF6MainWindow::onClobberPalmFromPC);
    connect(m_actionManager, &ActionManager::backupRequested,
            this, &KF6MainWindow::onBackup);
    connect(m_actionManager, &ActionManager::restoreRequested,
            this, &KF6MainWindow::onRestore);

    // Shakedown F15: the Navigate actions emitted signals nothing consumed
    // — Ctrl+1..5 did literally nothing. Wire them to page switching.
    connect(m_actionManager, &ActionManager::viewDashboardRequested,
            this, [this]() {
                if (m_patchbayPageItem)
                    m_pageWidget->setCurrentPage(m_patchbayPageItem);
            });
    const QHash<QString, void (ActionManager::*)()> navMap{
        { QStringLiteral("memo"),     &ActionManager::viewMemosRequested },
        { QStringLiteral("contacts"), &ActionManager::viewContactsRequested },
        { QStringLiteral("calendar"), &ActionManager::viewCalendarRequested },
        { QStringLiteral("todo"),     &ActionManager::viewTasksRequested },
    };
    for (auto it = navMap.constBegin(); it != navMap.constEnd(); ++it) {
        connect(m_actionManager, it.value(), this, [this, key = it.key()]() {
            const auto pageIt = m_palmPluginPages.constFind(key);
            if (pageIt != m_palmPluginPages.constEnd())
                m_pageWidget->setCurrentPage(pageIt.value());
        });
    }

    // Shakedown F10/F15: Review Conflicts... was permanently disabled and
    // its signal never connected. It now opens the same review dialog as
    // the badge; enablement follows the pending count (refreshConflictBadge).
    connect(m_actionManager, &ActionManager::showConflictsRequested,
            this, &KF6MainWindow::onConflictBadgeClicked);
    connect(m_actionManager, &ActionManager::changeSyncFolderRequested,
            this, &KF6MainWindow::onChangeSyncFolder);
    connect(m_actionManager, &ActionManager::openSyncFolderRequested,
            this, &KF6MainWindow::onOpenSyncFolder);
    connect(m_actionManager, &ActionManager::installFilesRequested,
            this, &KF6MainWindow::onInstallFiles);

    connect(m_actionManager, &ActionManager::clearLogRequested,
            this, &KF6MainWindow::onClearLog);

    // Log dock toggle - sync the action checkmark with dock visibility
    connect(m_actionManager->toggleLogPanelAction(), &QAction::toggled,
            this, &KF6MainWindow::onToggleLogPanel);
    connect(m_logDock, &QDockWidget::visibilityChanged,
            m_actionManager->toggleLogPanelAction(), &QAction::setChecked);

    // Focus log
    connect(m_actionManager, &ActionManager::focusLogRequested,
            this, &KF6MainWindow::onFocusLog);

    // KPageWidget page changes
    connect(m_pageWidget, &KPageWidget::currentPageChanged,
            this, &KF6MainWindow::onPageChanged);
}

void KF6MainWindow::saveWindowState()
{
    KF6Settings &settings = KF6Settings::instance();
    settings.setWindowGeometry(saveGeometry());
    settings.setWindowState(saveState());
    settings.setLogPanelVisible(m_logDock->isVisible());

    // Save current plugin page index
    int pageIndex = 0;
    KPageWidgetItem *current = m_pageWidget->currentPage();
    int idx = 0;
    for (auto it = m_palmPluginPages.constBegin(); it != m_palmPluginPages.constEnd(); ++it) {
        if (it.value() == current) {
            pageIndex = idx;
            break;
        }
        ++idx;
    }
    settings.setCurrentTabIndex(pageIndex);

    settings.sync();
}

void KF6MainWindow::restoreWindowState()
{
    KF6Settings &settings = KF6Settings::instance();

    // Restore window geometry
    QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    QByteArray state = settings.windowState();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    // Restore log dock visibility
    bool logVisible = settings.logPanelVisible();
    m_logDock->setVisible(logVisible);
    m_actionManager->toggleLogPanelAction()->setChecked(logVisible);

    // Restore current plugin page
    int pageIndex = settings.currentTabIndex();
    if (!m_palmPluginPages.isEmpty()) {
        int idx = 0;
        for (auto it = m_palmPluginPages.constBegin(); it != m_palmPluginPages.constEnd(); ++it) {
            if (idx == pageIndex) {
                m_pageWidget->setCurrentPage(it.value());
                break;
            }
            ++idx;
        }
    }
}

void KF6MainWindow::onToggleLogPanel(bool visible)
{
    m_logDock->setVisible(visible);
}

void KF6MainWindow::onFocusLog()
{
    m_logDock->setVisible(true);
    m_logDock->raise();
    m_logWidget->setFocus();
    m_actionManager->toggleLogPanelAction()->setChecked(true);
}

void KF6MainWindow::onPageChanged(KPageWidgetItem *current, KPageWidgetItem *previous)
{
    Q_UNUSED(previous)
    Q_UNUSED(current)
    // Plugin views handle their own data loading via loadFromPath()
    // when a profile is loaded. Lazy per-tab refresh can be added later
    // as an optimization.
}

void KF6MainWindow::updateMenuState(bool connected)
{
    // Also consider connected if the orchestrator has an active session
    if (!connected && m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        connected = true;
    }
    bool hasProfile = m_currentProfile != nullptr;
    m_actionManager->updateConnectionState(connected, hasProfile);
    m_actionManager->updateProfileState(hasProfile);
}

void KF6MainWindow::updateWindowTitle()
{
    // Shakedown F18: loading a profile used to replace the startup title
    // ("Wild Palms - Palm Pilot Synchronization") with a bare
    // "Wild Palms - <profile>". Keep one consistent form.
    QString title = i18n("Wild Palms - Palm Pilot Synchronization");
    if (m_currentProfile) {
        title += QStringLiteral(" — ") + m_currentProfile->name();
    }
    setWindowTitle(title);
}

void KF6MainWindow::updateProfileMenuState()
{
    bool hasProfile = m_currentProfile != nullptr;
    m_actionManager->updateProfileState(hasProfile);

    // Shakedown F15: the label used to be driven by a legacy SyncState
    // pending-conflict count (a different, stale source) that fought the
    // badge. Drive it from the live runtime conflict count instead.
    m_actionManager->updateConflictCount(m_pendingConflictCount);
    m_actionManager->showConflictsAction()->setEnabled(
        hasProfile && m_pendingConflictCount > 0);
}


// ========== Profile Management ==========

void KF6MainWindow::destroyPatchbayPage()
{
    // Remove + delete the page while its borrowed controllers are still
    // alive (the page dtor severs connections to them). removePage deletes
    // the page widget.
    if (m_patchbayPageItem) {
        m_pageWidget->removePage(m_patchbayPageItem);
        m_patchbayPageItem = nullptr;
        m_patchbayPage = nullptr;
    }
}

void KF6MainWindow::loadProfile(const QString &path)
{
    // The Sync Patchbay borrows the about-to-be-replaced profile/runtime/
    // accounts — tear it down first (1be66a3 lesson).
    destroyPatchbayPage();

    // AccountController borrows the old profile + runtime; it
    // must be reset BEFORE the old Profile is deleted and the old
    // PalmRuntime is replaced.
    if (m_accountController) {
        m_accountController.reset();
    }

    if (m_currentProfile) {
        m_currentProfile->save();
    }

    m_currentProfile = std::make_unique<Profile>(path);

    if (!m_currentProfile->isValid()) {
        m_logWidget->logError(i18n("Invalid profile path: %1", path));
        m_currentProfile.reset();
        updateWindowTitle();
        updateProfileMenuState();
        return;
    }

    bool isNewProfile = !m_currentProfile->exists();

    if (isNewProfile) {
        m_currentProfile->initialize();
        m_logWidget->logInfo(i18n("Created new profile at: %1", path));

        if (!m_lastUsedDevicePath.isEmpty()) {
            m_currentProfile->setDevicePath(m_lastUsedDevicePath);
            m_currentProfile->setBaudRate(m_lastUsedBaudRate);
            m_currentProfile->save();
            m_logWidget->logInfo(i18n("Using device settings: %1 at %2 bps",
                                      m_lastUsedDevicePath, m_lastUsedBaudRate));
        }
    }

    m_syncPath = path;

    // F.1a: bump last-active in the registry as soon as we know the profile is valid
    if (m_profileRegistry && !m_currentProfile->id().isEmpty()) {
        m_profileRegistry->setLastActive(m_currentProfile->id());
        if (m_profileMenuController)
            m_profileMenuController->setActiveProfileId(
                m_currentProfile->id());
    }

    // (Re)create PalmRuntime for the new profile path.
    //
    // Pass the profile root, NOT stateDirectoryPath(). PalmRuntime uses the
    // root for rawfiles/<plugin>/<col>/ and backup/ (per F.1a §4.2 layout)
    // and computes <root>/.state internally for the baselines DB.
    m_palmRuntime = std::make_unique<WildPalms::Runtime::PalmRuntime>(
        m_currentProfile->syncFolderPath(), this);
    // F.3: borrow Profile pointer so PalmRuntime::finishConnect() can
    // write the per-DB category-slot snapshot into the profile.
    m_palmRuntime->setProfile(m_currentProfile.get());
    m_palmRuntime->setMassDeleteGuard(m_massDeleteGuard.get());
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runStarted,
            this, &KF6MainWindow::onPalmRunStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runFinished,
            this, &KF6MainWindow::onPalmRunFinished);
    // Shakedown F12: engine diagnostics (transcoding warnings, pass
    // announcements, per-mapping failures) now flow into the Log dock.
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runLog,
            m_logWidget, &LogWidget::logInfo);

    // Dashboard redesign — feed the SyncStatusModel from this profile's runtime.
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::connectionStarted,
            m_syncStatusModel, &SyncStatusModel::onConnectionStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::connectionComplete,
            m_syncStatusModel, &SyncStatusModel::onConnectionComplete);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
            m_syncStatusModel, &SyncStatusModel::onDeviceLost);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runStarted,
            m_syncStatusModel, &SyncStatusModel::onRunStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runProgress,
            m_syncStatusModel, &SyncStatusModel::onRunProgress);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runFinished,
            m_syncStatusModel, &SyncStatusModel::onRunFinished);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncStarted,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncProgress,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncProgress);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncFinished,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncFinished);

    // Phase Ic: AccountController borrows registry + profile + runtime.
    m_deviceReadyForSync = false;
    m_accountsReadyForSync = false;
    m_accountController = std::make_unique<WildPalms::Runtime::AccountController>(
        m_currentProfile->syncFolderPath(),
        &m_palmRuntime->backendRegistry(),
        m_currentProfile.get(),
        m_palmRuntime.get(),
        this);
    // Gate auto-sync until accounts have finished their async connections.
    // accountsReady() fires on the GUI thread once all providers are done
    // (or immediately if there are none). At that point, if the device
    // signalled readyForSync first we fire the deferred auto-sync.
    connect(m_accountController.get(),
            &WildPalms::Runtime::AccountController::accountsReady,
            this, [this]() {
                m_accountsReadyForSync = true;
                if (m_deviceReadyForSync
                    && m_currentProfile
                    && m_currentProfile->autoSyncOnConnect()) {
                    m_logWidget->logInfo(
                        i18n("Accounts ready — starting deferred auto-sync"));
                    QTimer::singleShot(0, this, &KF6MainWindow::onHotSync);
                }
            });

    // Construct and install the libkalburator-side conflict handler.
    // Recreate per profile so it points at the new engine. Direct construction
    // is now safe — src/sync/qsynccore/ is gone and the include-guard collision
    // that previously required the conflictdialogbridge TU isolation no longer
    // exists.
    delete m_palmConflictHandler;
    m_palmConflictHandler = nullptr;
    auto *handler = new KalburatorInteractiveConflictHandler(
        nullptr,   // ConflictStore — keep nullptr; M5b chore deferred
        this,      // parentWidget for the dialog
        this);     // QObject parent for lifetime management
    m_palmRuntime->setConflictHandler(handler);
    m_palmConflictHandler = handler;

    // Tickle the device link while the dialog is open (matches legacy handler).
    connect(handler, &KalburatorInteractiveConflictHandler::keepAliveRequested,
            this, [this]() { onPalmConflictHandlerKeepAlive(); });

    // F.2 sub-project D: wire conflict signal + reset badge for fresh profile.
    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::conflictDetected,
            this, &KF6MainWindow::onConflictDetected);

    // Reset badge + UI conflict store for the freshly-loaded profile.
    // SyncConflictStore (engine-side, SQLite) may have unresolved
    // entries from a previous session; the badge populates as new
    // conflicts come in this session.
    if (m_uiConflictStore) m_uiConflictStore->clear();
    m_pendingConflictCount = 0;
    refreshConflictBadge();

    // Wire Palm plugin pages synchronously now that PalmRuntime has loaded plugins.
    for (auto *page : m_palmPluginPages.values()) {
        m_pageWidget->removePage(page);
    }
    m_palmPluginPages.clear();

    // Substrate A1: register a page for every conduit descriptor that declares
    // a main view, instead of casting to each concrete type. (This also wires
    // the contacts ContactView, which hasMainView()=true but the old per-type
    // cast loop silently omitted.) Label/icon come from the descriptor.
    for (auto *c : m_palmRuntime->conduits()) {
        if (!c->hasMainView()) continue;
        QWidget *view = c->createMainView(this);
        if (!view) continue;
        auto *page = new KPageWidgetItem(view, c->conduitDisplayName());
        page->setIcon(QIcon::fromTheme(c->conduitIconName()));
        page->setHeaderVisible(false);
        m_pageWidget->addPage(page);
        m_palmPluginPages.insert(c->conduitId(), page);
    }

    // Sync Patchbay (Part 1): the three-tier mapping editor as a central
    // page. Replaces nothing yet — the F.3 Settings page retires in Part 2
    // (spec §11). Built now that profile + runtime + accounts all exist.
    m_patchbayPage = new WildPalms::AppPatchbay::PatchbayPage(
        m_currentProfile.get(), m_accountController.get(),
        m_palmRuntime.get(), this);
    m_patchbayPageItem =
        new KPageWidgetItem(m_patchbayPage, i18n("Patchbay"));
    m_patchbayPageItem->setIcon(
        QIcon::fromTheme(QStringLiteral("network-connect")));
    m_pageWidget->addPage(m_patchbayPageItem);
    m_pageWidget->setCurrentPage(m_patchbayPageItem);

    // Connection mode (USB serial vs network) is now encoded in the
    // devicePaths passed to PalmRuntime::connectDevice().

    // Add to recent profiles
    KF6Settings::instance().addRecentProfile(path);

    // Auto-set as default if none exists
    if (KF6Settings::instance().defaultProfilePath().isEmpty()) {
        KF6Settings::instance().setDefaultProfilePath(path);
        m_logWidget->logInfo(i18n("Set as default profile"));
    }

    KF6Settings::instance().sync();

    // Tell plugin views to load data from the profile's sync folder
    for (auto it = m_palmPluginPages.constBegin(); it != m_palmPluginPages.constEnd(); ++it) {
        QWidget *view = it.value()->widget();
        QMetaObject::invokeMethod(view, "loadFromPath", Q_ARG(QString, m_syncPath));
    }

    // Update UI — check for active connection (manual or auto-sync)
    bool connected = m_palmRuntime && m_palmRuntime->isDeviceConnected();
    updateWindowTitle();
    updateMenuState(connected);
    pushProfileInfoToStatusModel();
    // If a device is already connected when this profile loads (e.g. switching
    // profiles with the Palm attached), no fresh connectionComplete signal will
    // arrive, so tell the model we are connected. setState() is idempotent if a
    // real signal also fires later.
    if (connected)
        m_syncStatusModel->onConnectionComplete(true, QString());

    m_logWidget->logInfo(i18n("Loaded profile: %1", m_currentProfile->name()));
    m_logWidget->logInfo(i18n("Sync folder: %1", m_syncPath));
}

void KF6MainWindow::closeProfile()
{
    // Patchbay borrows accounts/runtime/profile — destroy it first.
    destroyPatchbayPage();

    // AccountController teardown precedes Profile teardown.
    if (m_accountController) {
        m_accountController.reset();
    }

    if (m_currentProfile) {
        m_currentProfile->save();
    }
    m_currentProfile.reset();

    if (m_profileMenuController)
        m_profileMenuController->setActiveProfileId(QString());

    m_syncPath.clear();

    updateWindowTitle();
    updateProfileMenuState();

    m_logWidget->logInfo(i18n("Profile closed"));
}

// ========== Device Connection ==========

void KF6MainWindow::onConnectDevice()
{
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Connect to Palm Device"));
    dialog.setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *infoLabel = new QLabel(
        i18n("Enter the device path and press Connect.\n"
             "Press the HotSync button on your Palm - Wild Palms will wait for it."));
    layout->addWidget(infoLabel);

    QFormLayout *formLayout = new QFormLayout();

    QComboBox *deviceCombo = new QComboBox();
    deviceCombo->setEditable(true);
    deviceCombo->addItem(QStringLiteral("/dev/ttyUSB0"));
    deviceCombo->addItem(QStringLiteral("/dev/ttyUSB1"));
    deviceCombo->addItem(QStringLiteral("/dev/pilot"));
    deviceCombo->addItem(QStringLiteral("usb:"));

    if (m_currentProfile && !m_currentProfile->devicePath().isEmpty()) {
        deviceCombo->setCurrentText(m_currentProfile->devicePath());
    } else if (!m_lastUsedDevicePath.isEmpty()) {
        deviceCombo->setCurrentText(m_lastUsedDevicePath);
    }

    formLayout->addRow(i18n("Device:"), deviceCombo);

    QComboBox *baudCombo = new QComboBox();
    baudCombo->addItem(QStringLiteral("115200"));
    baudCombo->addItem(QStringLiteral("57600"));
    baudCombo->addItem(QStringLiteral("38400"));
    baudCombo->addItem(QStringLiteral("19200"));
    baudCombo->addItem(QStringLiteral("9600"));

    if (m_currentProfile && !m_currentProfile->baudRate().isEmpty()) {
        baudCombo->setCurrentText(m_currentProfile->baudRate());
    } else if (!m_lastUsedBaudRate.isEmpty()) {
        baudCombo->setCurrentText(m_lastUsedBaudRate);
    }

    formLayout->addRow(i18n("Baud Rate:"), baudCombo);
    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(i18n("Connect"));
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString devicePath = deviceCombo->currentText();
    QString baudRate = baudCombo->currentText();

    m_lastUsedDevicePath = devicePath;
    m_lastUsedBaudRate = baudRate;

    if (m_currentProfile) {
        m_currentProfile->setDevicePath(devicePath);
        m_currentProfile->setBaudRate(baudRate);
        m_currentProfile->save();
    }

    bool deviceExists = devicePath.startsWith(QStringLiteral("usb:")) || QFile::exists(devicePath);

    if (deviceExists) {
        startConnection(devicePath);
    } else {
        startListening(devicePath);
    }
}

void KF6MainWindow::startListening(const QString &devicePath)
{
    m_listeningForDevice = true;
    m_listeningDevicePath = devicePath;

    m_logWidget->logInfo(i18n("Waiting for HotSync on %1...", devicePath));
    m_logWidget->logInfo(i18n("Press the HotSync button on your Palm device."));
    statusBar()->showMessage(i18n("Waiting for HotSync on %1...", devicePath));

    if (!m_devicePollTimer) {
        m_devicePollTimer = new QTimer(this);
        connect(m_devicePollTimer, &QTimer::timeout, this, &KF6MainWindow::onDevicePoll);
    }

    m_devicePollTimer->start(500);

    // cancelConnectionAction now dispatches to cancelSync() when isRunning()
    // (K.8b T16) or cancelConnect() during the connection handshake.
    m_actionManager->cancelConnectionAction()->setEnabled(true);
    updateMenuState(false);
}

void KF6MainWindow::stopListening()
{
    if (!m_listeningForDevice) {
        return;
    }

    m_listeningForDevice = false;
    m_listeningDevicePath.clear();

    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    m_logWidget->logInfo(i18n("Stopped listening for HotSync"));
    statusBar()->showMessage(i18n("Ready"));

    m_actionManager->cancelConnectionAction()->setEnabled(false);
    updateMenuState(false);
}

void KF6MainWindow::onDevicePoll()
{
    if (!m_listeningForDevice || m_listeningDevicePath.isEmpty()) {
        return;
    }

    if (QFile::exists(m_listeningDevicePath)) {
        m_logWidget->logInfo(i18n("Device %1 detected!", m_listeningDevicePath));

        m_devicePollTimer->stop();
        m_listeningForDevice = false;

        QString devicePath = m_listeningDevicePath;
        m_listeningDevicePath.clear();

        startConnection(devicePath);
    }
}

void KF6MainWindow::startConnectionMultiPort(const QStringList &devicePaths)
{
    if (devicePaths.isEmpty()) {
        m_logWidget->logError(i18n("No device paths provided"));
        return;
    }

    if (!m_palmRuntime) {
        m_logWidget->logError(i18n("Cannot connect: PalmRuntime not initialized"));
        return;
    }

    // Wire signals (Qt::UniqueConnection prevents duplicate wiring on
    // repeated connect attempts to the same PalmRuntime instance).
    // NOTE: Qt::UniqueConnection requires PMF slots; with lambdas it
    // returns an invalid connection and silently fails to wire.
    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::connectionStarted,
            this, &KF6MainWindow::onConnectionStarted,
            Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::connectionComplete,
            this, &KF6MainWindow::onConnectionComplete,
            Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::readyForSync,
            this, &KF6MainWindow::onReadyForSync, Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
            this, &KF6MainWindow::onDeviceDisconnected,
            Qt::UniqueConnection);

    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::logMessage,
            m_logWidget, &LogWidget::logInfo, Qt::UniqueConnection);

    m_logWidget->logInfo(i18n("Connecting to %1...", devicePaths.join(QStringLiteral(", "))));
    m_palmRuntime->connectDevice(devicePaths);

    updateMenuState(false);
    if (m_actionManager) {
        m_actionManager->cancelConnectionAction()->setEnabled(true);
    }
}

void KF6MainWindow::startConnection(const QString &devicePath)
{
    startConnectionMultiPort(QStringList{devicePath});
}

void KF6MainWindow::onConnectionStarted()
{
    statusBar()->showMessage(i18n("Connecting…"));
}

void KF6MainWindow::onDeviceDisconnected()
{
    updateMenuState(false);
    statusBar()->showMessage(i18n("Disconnected"));

    KNotification *notification = new KNotification(
        QStringLiteral("deviceDisconnected"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Device Disconnected"));
    notification->setText(i18n("Palm device has been disconnected"));
    notification->setIconName(QStringLiteral("network-disconnect"));
    notification->sendEvent();
}

void KF6MainWindow::onConnectionComplete(bool success, const QString &error)
{
    m_actionManager->cancelConnectionAction()->setEnabled(false);

    if (!success) {
        m_logWidget->logError(i18n("Connection failed: %1", error));
        statusBar()->showMessage(i18n("Connection failed"));
        updateMenuState(false);
        return;
    }

    m_logWidget->logInfo(i18n("Connection established!"));
    statusBar()->showMessage(i18n("Connected"));

    // Send notification
    KNotification *notification = new KNotification(QStringLiteral("deviceConnected"), KNotification::CloseOnTimeout, this);
    notification->setTitle(i18n("Device Connected"));
    notification->setText(i18n("Palm device connected successfully"));
    notification->setIconName(QStringLiteral("network-connect"));
    notification->sendEvent();

    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("Connected but no device link available"));
        updateMenuState(false);
        return;
    }

    // Use handshake data (captured during connection on the worker thread —
    // no DLP calls on the main thread while tickle may be running)
    if (deviceLink->handshakeUserInfoValid()) {
        QString userName = deviceLink->handshakeUserName();
        quint32 userId = deviceLink->handshakeUserId();

        m_logWidget->logInfo(i18n("User: %1 (ID: %2)", userName, userId));

        DeviceFingerprint connectedDevice;
        connectedDevice.userId = userId;
        connectedDevice.userName = userName;
        if (m_autoSync && !m_autoSync->currentUsbSerial().isEmpty())
            connectedDevice.usbSerialNumber = m_autoSync->currentUsbSerial();
        if (deviceLink->handshakeSysInfoValid()) {
            connectedDevice.romVersion = deviceLink->handshakeRomVersion();
            connectedDevice.productId = deviceLink->handshakeProductId();
        }
        if (deviceLink->handshakeStorageInfoValid()) {
            connectedDevice.modelName = deviceLink->handshakeCardName();
            connectedDevice.manufacturer = deviceLink->handshakeManufacturer();
            connectedDevice.romSize = deviceLink->handshakeRomSize();
            connectedDevice.ramSize = deviceLink->handshakeRamSize();
            connectedDevice.ramFree = deviceLink->handshakeRamFree();
        }

        if (m_currentProfile) {
            if (!handleDeviceFingerprint(connectedDevice)) {
                deviceLink->closeConnection();
                updateMenuState(false);
                return;
            }
        } else {
            QString knownProfile;
            if (m_profileRegistry) {
                const auto serialEntry = m_profileRegistry->findBySerial(connectedDevice.usbSerialNumber);
                knownProfile = serialEntry.isValid() ? serialEntry.path : QString();
            }
            if (!knownProfile.isEmpty()) {
                int ret = QMessageBox::question(this, i18n("Known Device"),
                    i18n("This device (%1) is registered with profile:\n%2\n\nLoad that profile?",
                         connectedDevice.displayString(),
                         QFileInfo(knownProfile).fileName()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

                if (ret == QMessageBox::Yes) {
                    loadProfile(knownProfile);
                }
            } else {
                m_logWidget->logWarning(i18n("No profile loaded. Device data won't be saved. Use File → New Profile."));
            }
        }

        if (userName.isEmpty() && userId == 0) {
            m_logWidget->logWarning(i18n("This appears to be an uninitialized device (no user info)"));

            QString newUserName = QInputDialog::getText(this, i18n("Initialize Device"),
                i18n("This Palm has no user info. Enter a username:"),
                QLineEdit::Normal, QStringLiteral("PalmUser"));

            if (!newUserName.isEmpty()) {
                struct PilotUser user;
                memset(&user, 0, sizeof(user));
                strncpy(user.username, newUserName.toLatin1().constData(), sizeof(user.username) - 1);
                user.userID = static_cast<unsigned long>(QDateTime::currentSecsSinceEpoch());

                if (deviceLink->writeUserInfo(user)) {
                    m_logWidget->logInfo(i18n("Device initialized: %1 (ID: %2)",
                                              newUserName, user.userID));

                    if (m_currentProfile) {
                        DeviceFingerprint newFp;
                        newFp.userId = user.userID;
                        newFp.userName = newUserName;
                        if (deviceLink->handshakeSysInfoValid()) {
                            newFp.romVersion = deviceLink->handshakeRomVersion();
                            newFp.productId = deviceLink->handshakeProductId();
                        }
                        if (deviceLink->handshakeStorageInfoValid()) {
                            newFp.modelName = deviceLink->handshakeCardName();
                            newFp.manufacturer = deviceLink->handshakeManufacturer();
                            newFp.romSize = deviceLink->handshakeRomSize();
                            newFp.ramSize = deviceLink->handshakeRamSize();
                            newFp.ramFree = deviceLink->handshakeRamFree();
                        }
                        registerDeviceWithCurrentProfile(newFp);
                    }
                } else {
                    m_logWidget->logError(i18n("Failed to write user info to device"));
                }
            }
        }
    } else {
        m_logWidget->logWarning(i18n("Could not read user info from device"));
    }

    // System info display (from handshake)
    if (deviceLink->handshakeSysInfoValid()) {
        m_logWidget->logInfo(i18n("Palm OS: %1.%2, Product ID: %3",
                                  deviceLink->handshakeRomVersion() >> 16,
                                  (deviceLink->handshakeRomVersion() >> 8) & 0xFF,
                                  deviceLink->handshakeProductId()));
    }

    // Storage info display (from handshake)
    if (deviceLink->handshakeStorageInfoValid()) {
        QString ramTotal = DeviceFingerprint::formatMemorySize(deviceLink->handshakeRamSize());
        QString ramFree = DeviceFingerprint::formatMemorySize(deviceLink->handshakeRamFree());
        m_logWidget->logInfo(i18n("Device: %1 by %2, RAM: %3/%4",
                                  deviceLink->handshakeCardName(),
                                  deviceLink->handshakeManufacturer(),
                                  ramFree, ramTotal));
    }

    updateMenuState(true);
    pushProfileInfoToStatusModel();
}

bool KF6MainWindow::handleDeviceFingerprint(const DeviceFingerprint &connectedDevice)
{
    if (!m_currentProfile) return true;

    DeviceFingerprint expectedDevice = m_currentProfile->deviceFingerprint();

    if (!expectedDevice.isValid()) {
        registerDeviceWithCurrentProfile(connectedDevice);
        return true;
    }

    const auto result = expectedDevice.compare(connectedDevice);

    if (result == DeviceFingerprint::MatchResult::Match) {
        m_logWidget->logInfo(i18n("Device fingerprint verified"));

        // Merge extended info from connected device into stored fingerprint
        // so ramFree updates and initially-missing fields get filled
        bool changed = false;
        if (connectedDevice.hasExtendedInfo()) {
            if (expectedDevice.modelName.isEmpty() && !connectedDevice.modelName.isEmpty()) {
                expectedDevice.modelName = connectedDevice.modelName;
                changed = true;
            }
            if (expectedDevice.manufacturer.isEmpty() && !connectedDevice.manufacturer.isEmpty()) {
                expectedDevice.manufacturer = connectedDevice.manufacturer;
                changed = true;
            }
            if (expectedDevice.romVersion == 0 && connectedDevice.romVersion != 0) {
                expectedDevice.romVersion = connectedDevice.romVersion;
                changed = true;
            }
            if (expectedDevice.productId.isEmpty() && !connectedDevice.productId.isEmpty()) {
                expectedDevice.productId = connectedDevice.productId;
                changed = true;
            }
            if (expectedDevice.romSize == 0 && connectedDevice.romSize != 0) {
                expectedDevice.romSize = connectedDevice.romSize;
                changed = true;
            }
            if (expectedDevice.ramSize == 0 && connectedDevice.ramSize != 0) {
                expectedDevice.ramSize = connectedDevice.ramSize;
                changed = true;
            }
            // Always update ramFree (it's a snapshot)
            if (connectedDevice.ramFree != 0 && connectedDevice.ramFree != expectedDevice.ramFree) {
                expectedDevice.ramFree = connectedDevice.ramFree;
                changed = true;
            }
        }
        if (changed) {
            m_currentProfile->setDeviceFingerprint(expectedDevice);
            m_currentProfile->save();
        }

        return true;
    }

    if (result == DeviceFingerprint::MatchResult::Indeterminate) {
        m_logWidget->logInfo(i18n("Device fingerprint indeterminate — assuming match"));
        registerDeviceWithCurrentProfile(connectedDevice);
        return true;
    }

    // MismatchKnown — open structured dialog via virtual seam.
    return openMismatchDialogForTest(expectedDevice, connectedDevice);
}

QString KF6MainWindow::renderMismatchMessageForTest(
    const DeviceFingerprint &expected,
    const DeviceFingerprint &connected) const
{
    QString msg;
    msg += i18n("This profile is associated with a different Palm device.\n\n");
    msg += QStringLiteral("<table>");
    msg += QStringLiteral("<tr><th></th><th>%1</th><th>%2</th></tr>")
        .arg(i18n("Expected"), i18n("Connected"));
    const auto rows = DeviceFingerprint::comparisonRows(expected, connected);
    for (const auto &r : rows) {
        msg += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>%3</td></tr>")
            .arg(r.label.toHtmlEscaped(),
                 r.lhs.toHtmlEscaped(),
                 r.rhs.toHtmlEscaped());
    }
    msg += QStringLiteral("</table>");
    return msg;
}

bool KF6MainWindow::runMismatchCheckForTest(
    const DeviceFingerprint &expected,
    const DeviceFingerprint &connected)
{
    using MR = DeviceFingerprint::MatchResult;
    const auto r = expected.compare(connected);
    if (r == MR::Match || r == MR::Indeterminate) return true;
    // MismatchKnown: delegate to virtual seam (test captures; real path exec's dialog).
    return openMismatchDialogForTest(expected, connected);
}

bool KF6MainWindow::openMismatchDialogForTest(
    const DeviceFingerprint &expected,
    const DeviceFingerprint &connected)
{
    const QString message = renderMismatchMessageForTest(expected, connected);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(i18n("Wrong Device"));
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *continueBtn = msgBox.addButton(i18n("Continue Anyway"), QMessageBox::AcceptRole);
    QPushButton *switchBtn   = msgBox.addButton(i18n("Switch Profile"),   QMessageBox::ActionRole);
    QPushButton *abortBtn    = msgBox.addButton(i18n("Disconnect"),        QMessageBox::RejectRole);
    Q_UNUSED(abortBtn)

    msgBox.exec();

    if (msgBox.clickedButton() == continueBtn) {
        m_logWidget->logWarning(i18n("User chose to continue with mismatched device"));
        return true;
    } else if (msgBox.clickedButton() == switchBtn) {
        QString profilePath;
        if (m_profileRegistry) {
            const auto switchEntry = m_profileRegistry->findBySerial(connected.usbSerialNumber);
            profilePath = switchEntry.isValid() ? switchEntry.path : QString();
        }
        if (!profilePath.isEmpty()) {
            m_logWidget->logInfo(i18n("Switching to profile: %1", profilePath));
            loadProfile(profilePath);
            return true;
        } else {
            QMessageBox::information(this, i18n("No Profile Found"),
                i18n("No profile is registered for this device.\n"
                     "You can create a new profile using File → New Profile."));
            return false;
        }
    } else {
        m_logWidget->logInfo(i18n("User chose to disconnect"));
        return false;
    }
}

void KF6MainWindow::registerDeviceWithCurrentProfile(const DeviceFingerprint &fingerprint)
{
    if (!m_currentProfile) return;

    m_currentProfile->setDeviceFingerprint(fingerprint);
    m_currentProfile->save();

    if (!fingerprint.usbSerialNumber.isEmpty() && m_profileRegistry) {
        m_profileRegistry->bindSerial(
            m_currentProfile->id(), fingerprint.usbSerialNumber);
    }

    m_logWidget->logInfo(i18n("Device registered: %1", fingerprint.displayString()));
}

void KF6MainWindow::onDisconnectDevice()
{
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting..."));

        KPilotDeviceLink *deviceLink = m_palmRuntime->deviceLink();
        if (deviceLink && deviceLink->isConnected()) {
            m_logWidget->logInfo(i18n("Ending sync session..."));
            deviceLink->endSync();
        }

        // Tear down PalmRuntime's device.
        m_palmRuntime->disconnectDevice();

        statusBar()->showMessage(i18n("Disconnected"));
        m_logWidget->logInfo(i18n("Disconnected from device"));
    }
    updateMenuState(false);
}

void KF6MainWindow::onCancelConnection()
{
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    if (m_palmRuntime) {
        if (m_palmRuntime->isRunning()) {
            // K.8b T16: mid-sync cancel — routes into SyncEngine::onCancelObserved
            // via QFutureWatcher::cancel() propagation.
            m_palmRuntime->cancelSync();
            m_logWidget->logInfo(i18n("Sync cancelled by user"));
        } else {
            m_palmRuntime->cancelConnect();
            m_logWidget->logInfo(i18n("Connection cancelled by user"));
        }
    }
    m_actionManager->cancelConnectionAction()->setEnabled(false);
}

void KF6MainWindow::onAutoDeviceDetected(Profile *profile, const QStringList &ports)
{
    // Guard: if we're already busy with a sync, ignore
    if (m_palmRuntime && m_palmRuntime->isRunning()) {
        m_logWidget->logWarning(i18n("Device detected but sync already in progress — ignoring"));
        delete profile;
        return;
    }

    // If an idle connection exists, disconnect it first
    if (m_palmRuntime && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Disconnecting previous session for new device"));
        m_palmRuntime->disconnectDevice();
    }

    if (profile) {
        // Device's USB serial is registered to a known profile. Respect
        // the user's explicit choice: only auto-switch when no profile is
        // currently open. If a different profile is open, log clearly and
        // keep it — silently swapping out the user's selection meant
        // tests against a fresh profile were actually running against the
        // serial-registered one, with no visible warning.
        if (!m_currentProfile) {
            QString profilePath = profile->syncFolderPath();
            delete profile;  // loadProfile creates its own copy
            loadProfile(profilePath);
        } else if (m_currentProfile->syncFolderPath() != profile->syncFolderPath()) {
            m_logWidget->logWarning(i18n(
                "Device is registered to profile '%1', but '%2' is currently open. "
                "Using the open profile. Close it first if you want to switch.",
                profile->name(), m_currentProfile->name()));
            delete profile;
        } else {
            // Serial-matched profile is the one already open — nothing to do.
            delete profile;
        }
        startConnectionMultiPort(ports);
    } else {
        // Unknown device — connect first, then we'll read identity and create profile
        m_logWidget->logInfo(i18n("New Palm device detected — connecting to identify..."));
        startConnectionMultiPort(ports);
    }
}

void KF6MainWindow::onUnregisteredDeviceDetected(const QString &usbSerial,
                                                  const QString &userName,
                                                  quint32 userId)
{
    Q_UNUSED(userName)
    Q_UNUSED(userId)

    QString prompt = i18n("An unrecognised Palm device was detected.\n\n"
                          "USB serial: %1\n\n"
                          "Create a new profile for this device?",
                          usbSerial.isEmpty() ? i18n("(unknown)") : usbSerial);

    int ret = QMessageBox::question(this, i18n("New Palm Device"), prompt,
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::Yes);
    if (ret != QMessageBox::Yes) {
        if (m_logWidget) {
            m_logWidget->logInfo(i18n("User declined to create profile for new device."));
        }
        return;
    }

    Profile *p = m_autoSync->createProfileForDevice(usbSerial, userName, userId);
    if (!p) {
        QMessageBox::warning(this, i18n("Profile Creation Failed"),
                             i18n("Could not create a profile for this device."));
        return;
    }
    // Hand off to the normal "device detected with profile" path.
    Q_EMIT m_autoSync->deviceDetected(p, QStringList{ m_autoSync->currentUsbSerial() });
}

void KF6MainWindow::onDeviceReady(const QString &userName, const QString &deviceName)
{
    m_logWidget->logInfo(i18n("Device ready: %1 (%2)", userName, deviceName));

    // If no profile is loaded and auto-sync is available, try to create one
    if (!m_currentProfile && m_autoSync) {
        Profile *newProfile = m_autoSync->findOrCreateProfile(
            QString(), userName, 0);
        if (newProfile) {
            QString profilePath = newProfile->syncFolderPath();
            delete newProfile;
            loadProfile(profilePath);
        }
    }
}

void KF6MainWindow::onReadyForSync()
{
    m_deviceReadyForSync = true;

    if (!m_currentProfile || !m_currentProfile->autoSyncOnConnect())
        return;

    // If AccountController has already finished its async provider connections,
    // start the sync immediately. Otherwise record that the device is ready and
    // let the accountsReady() handler fire the sync once accounts catch up.
    if (m_accountsReadyForSync) {
        m_logWidget->logInfo(i18n("Auto-sync enabled — starting HotSync"));
        QTimer::singleShot(0, this, &KF6MainWindow::onHotSync);
    } else {
        m_logWidget->logInfo(
            i18n("Auto-sync enabled — waiting for account connections"));
    }
}

void KF6MainWindow::onListDatabases()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    m_logWidget->logInfo(i18n("=== Database List ==="));

    QStringList databases = deviceLink->listDatabases();

    if (databases.isEmpty()) {
        m_logWidget->logWarning(i18n("No databases found (or device disconnected)"));
        return;
    }

    QSet<QString> pimDatabases = {QStringLiteral("MemoDB"), QStringLiteral("AddressDB"),
                                  QStringLiteral("DatebookDB"), QStringLiteral("ToDoDB")};
    QStringList pimList, otherList;

    for (const QString &db : databases) {
        if (pimDatabases.contains(db)) {
            int count = countDatabaseRecords(db);
            pimList << QStringLiteral("%1 (%2 records)").arg(db).arg(count);
        } else {
            otherList << db;
        }
    }

    m_logWidget->logInfo(i18n("PIM Databases:"));
    for (const QString &db : pimList) {
        m_logWidget->logInfo(QStringLiteral("  - %1").arg(db));
    }

    m_logWidget->logInfo(i18n("Other Databases: %1 total", otherList.size()));
    for (const QString &db : otherList) {
        m_logWidget->logInfo(QStringLiteral("  - %1").arg(db));
    }

    m_logWidget->logInfo(i18n("=== Total: %1 databases ===", databases.size()));
}

int KF6MainWindow::countDatabaseRecords(const QString &dbName)
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) return 0;

    int dbHandle = deviceLink->openDatabase(dbName);
    if (dbHandle < 0) return 0;

    QList<PilotRecord*> records = deviceLink->readAllRecords(dbHandle);
    int count = records.size();

    for (PilotRecord *r : records) {
        delete r;
    }

    deviceLink->closeDatabase(dbHandle);
    return count;
}

void KF6MainWindow::onSetUserInfo()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (!deviceLink->readUserInfo(user)) {
        m_logWidget->logError(i18n("Failed to read current user info"));
        return;
    }

    QString currentName = QString::fromLatin1(user.username);

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Set User Info"));
    dialog.setMinimumWidth(300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QFormLayout *formLayout = new QFormLayout();

    QLineEdit *nameEdit = new QLineEdit(currentName);
    formLayout->addRow(i18n("Username:"), nameEdit);

    QLabel *idLabel = new QLabel(QString::number(user.userID));
    formLayout->addRow(i18n("User ID:"), idLabel);

    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) return;

    QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, i18n("Error"), i18n("Username cannot be empty"));
        return;
    }

    strncpy(user.username, newName.toLatin1().constData(), sizeof(user.username) - 1);

    if (deviceLink->writeUserInfo(user)) {
        m_logWidget->logInfo(i18n("User info updated: %1", newName));
        QMessageBox::information(this, i18n("Success"), i18n("User info updated successfully!"));

        if (m_currentProfile) {
            DeviceFingerprint fp;
            fp.userId = user.userID;
            fp.userName = newName;
            registerDeviceWithCurrentProfile(fp);
        }
    } else {
        m_logWidget->logError(i18n("Failed to write user info"));
        QMessageBox::warning(this, i18n("Error"), i18n("Failed to update user info on device"));
    }
}

void KF6MainWindow::onDeviceInfo()
{
    KPilotDeviceLink *deviceLink = m_palmRuntime ? m_palmRuntime->deviceLink() : nullptr;
    if (!deviceLink) {
        m_logWidget->logError(i18n("No device connected"));
        return;
    }

    struct PilotUser user;
    memset(&user, 0, sizeof(user));
    deviceLink->readUserInfo(user);

    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));
    deviceLink->readSysInfo(sysInfo);

    QString osVersion = QStringLiteral("%1.%2.%3")
        .arg(sysInfo.romVersion >> 16)
        .arg((sysInfo.romVersion >> 8) & 0xFF)
        .arg(sysInfo.romVersion & 0xFF);

    // Read storage info for live device details
    struct CardInfo cardInfo;
    memset(&cardInfo, 0, sizeof(cardInfo));
    bool hasStorageInfo = deviceLink->readStorageInfo(0, cardInfo);

    QStringList databases = deviceLink->listDatabases();

    QString info = QStringLiteral(
        "<h3>Device Information</h3>"
        "<table>"
        "<tr><td><b>Username:</b></td><td>%1</td></tr>"
        "<tr><td><b>User ID:</b></td><td>%2</td></tr>"
        "<tr><td><b>Palm OS:</b></td><td>%3</td></tr>"
        "<tr><td><b>Product ID:</b></td><td>%4</td></tr>"
        "<tr><td><b>Databases:</b></td><td>%5</td></tr>"
        "</table>")
        .arg(QString::fromLatin1(user.username))
        .arg(user.userID)
        .arg(osVersion)
        .arg(QString::fromLatin1(sysInfo.prodID))
        .arg(databases.size());

    if (hasStorageInfo) {
        info += QStringLiteral(
            "<h4>Storage</h4>"
            "<table>"
            "<tr><td><b>Device Model:</b></td><td>%1</td></tr>"
            "<tr><td><b>Manufacturer:</b></td><td>%2</td></tr>"
            "<tr><td><b>ROM Size:</b></td><td>%3</td></tr>"
            "<tr><td><b>RAM Size:</b></td><td>%4</td></tr>"
            "<tr><td><b>Free RAM:</b></td><td>%5</td></tr>"
            "</table>")
            .arg(QString::fromLatin1(cardInfo.name))
            .arg(QString::fromLatin1(cardInfo.manufacturer))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.romSize))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.ramSize))
            .arg(DeviceFingerprint::formatMemorySize(cardInfo.ramFree));
    }

    info += i18n("<h4>PIM Databases</h4><ul>");
    QStringList pimDbs = {QStringLiteral("MemoDB"), QStringLiteral("AddressDB"),
                          QStringLiteral("DatebookDB"), QStringLiteral("ToDoDB")};
    for (const QString &db : pimDbs) {
        int count = countDatabaseRecords(db);
        info += QStringLiteral("<li>%1: %2 records</li>").arg(db).arg(count);
    }
    info += QStringLiteral("</ul>");

    QMessageBox::information(this, i18n("Device Information"), info);
}

// ========== Profile Slots ==========

void KF6MainWindow::onNewProfile()
{
    const auto r = runProfileWizard();
    if (r.state.profileName.isEmpty()) return;   // cancelled

    createAndLoadProfileFromWizard(r);
}

bool KF6MainWindow::createAndLoadProfileFromWizard(
    const WildPalms::Wizard::Result &r)
{
    const auto entry = m_profileRegistry->registerNew(r.state.profileName);
    if (!entry.isValid()) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not create profile."));
        return false;
    }

    if (!writeWizardResultToProfile(entry.path, r)) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not write profile files. Check the log."));
        // Best-effort rollback per F.1c spec §4.5.
        m_profileRegistry->unregister(entry.id);
        QDir(entry.path).removeRecursively();
        return false;
    }

    loadProfile(entry.path);
    return true;
}

WildPalms::Wizard::Result KF6MainWindow::runProfileWizard()
{
    if (m_runWizardOverride) return m_runWizardOverride();

    WildPalms::Wizard::NewProfileWizard wiz(
        m_profileRegistry.get(),
        m_appBackendRegistry.get(),
        this);
    if (wiz.exec() != QDialog::Accepted)
        return WildPalms::Wizard::Result{};   // empty profileName == cancel
    return wiz.result();
}

void KF6MainWindow::setRunProfileWizardForTest(
    std::function<WildPalms::Wizard::Result()> fn)
{
    m_runWizardOverride = std::move(fn);
}

bool KF6MainWindow::writeWizardResultToProfile(
    const QString &path,
    const WildPalms::Wizard::Result &r)
{
    Profile p(path);
    if (!p.load()) {
        // load() returned false: profile.conf was just registerNew'd, so
        // it should always load. If it doesn't, fail loudly.
        m_logWidget->logError(i18n("New Profile: profile.conf at %1 "
                                    "failed to load", path));
        return false;
    }

    // Name + sync folder (registerNew already wrote name = id; the wizard's
    // chosen name overrides for display).
    p.setName(r.state.profileName);
    p.setSyncFolderPath(path);

    // Accounts — convert WizardAccount → BackendConfiguration. Force id to
    // the wizard-local UUID so MappingSpec.accountRef matches the on-disk
    // account id (F.1c spec §10.2).
    QList<Kalburator::Sync::BackendConfiguration> accounts;
    for (const auto &pa : r.state.accounts) {
        auto cfg = pa.config;
        cfg.id   = pa.id;
        if (cfg.type.isEmpty()) cfg.type = pa.kind;
        accounts.append(cfg);
    }
    p.setAccounts(accounts);

    // Mappings — one wildcard row per non-RawFiles MappingSpec. RawFiles
    // entries produce nothing (PalmRuntime::finishConnect auto-generates).
    QJsonArray rows;
    for (const auto &m : r.state.mappings) {
        if (m.kind == WildPalms::Wizard::TargetKind::RawFiles) continue;
        QJsonObject row;
        row[QStringLiteral("id")] = QStringLiteral(
            "default-%1-%2-%3").arg(m.pluginId, m.accountRef, m.collectionId);
        row[QStringLiteral("sourceBackend")]  = m.pluginId;
        row[QStringLiteral("sourceCalendar")] = QString();   // wildcard
        // "<providerId>:<collectionId>" — the BackendRegistry instance id
        // ProviderManager registers provider-collection backends under
        // (same convention as SyncMappingsGraphView). A bare account uuid
        // resolves to no backend at dispatch time.
        row[QStringLiteral("targetBackend")]  =
            QStringLiteral("%1:%2").arg(m.accountRef, m.collectionId);
        row[QStringLiteral("targetCalendar")] = m.collectionId;
        row[QStringLiteral("mode")]           = QStringLiteral("TwoWay");
        row[QStringLiteral("conflictPolicy")] = QStringLiteral("LastWriteWins");
        row[QStringLiteral("enabled")]        = true;
        rows.append(row);
    }
    p.setSyncMappingsJson(rows);

    return p.save();
}

void KF6MainWindow::onCloseProfile()
{
    closeProfile();
}

void KF6MainWindow::onImportProfile()
{
    const QString path = QFileDialog::getExistingDirectory(this,
        i18n("Import Profile"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) return;

    const auto entry = m_profileRegistry->registerExisting(path);
    if (!entry.isValid()) {
        QMessageBox::warning(this, i18n("Import Profile"),
            i18n("Could not import \"%1\".\n\n"
                 "The folder must contain a valid profile.conf with "
                 "an id matching the folder name, and the id must "
                 "not already be registered.", path));
        return;
    }
    loadProfile(entry.path);
}

void KF6MainWindow::onSwitchProfile(const QString &id)
{
    if (id.isEmpty()) return;
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) {
        m_logWidget->logError(
            i18n("Cannot switch: profile not found"));
        return;
    }
    if (!QDir(e.path).exists()) {
        QMessageBox::warning(this, i18n("Switch Profile"),
            i18n("Profile directory no longer exists: %1\n"
                 "Use File → Profile → Forget to remove it from "
                 "the registry.", e.path));
        return;
    }
    loadProfile(e.path);
}

bool KF6MainWindow::confirmForgetProfile(
    const WildPalms::Runtime::ProfileEntry &entry,
    bool *outDeleteFiles)
{
    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Forget Profile"));
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        i18n("Remove profile \"%1\" from the registry?\n\n"
             "Folder: %2", entry.name, entry.path), &dlg));
    auto *deleteCheck = new QCheckBox(
        i18n("Also delete files at the folder above"), &dlg);
    deleteCheck->setChecked(false);
    layout->addWidget(deleteCheck);
    auto *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(i18n("Forget"));
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    const bool ok = (dlg.exec() == QDialog::Accepted);
    if (outDeleteFiles) *outDeleteFiles = deleteCheck->isChecked();
    return ok;
}

void KF6MainWindow::onForgetProfile(const QString &id)
{
    if (id.isEmpty()) return;
    if (m_currentProfile && m_currentProfile->id() == id) {
        m_logWidget->logError(i18n(
            "Cannot forget the currently-loaded profile. "
            "Close it first."));
        return;
    }
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) return;

    bool wantDelete = false;
    if (!confirmForgetProfile(e, &wantDelete)) return;

    const QString pathCopy = e.path;
    if (!m_profileRegistry->unregister(id)) {
        m_logWidget->logError(i18n(
            "Failed to remove profile from registry"));
        return;
    }
    if (wantDelete) {
        QDir d(pathCopy);
        if (d.exists() && !d.removeRecursively()) {
            QMessageBox::warning(this, i18n("Forget Profile"),
                i18n("Removed from registry, but could not delete "
                     "files at: %1", pathCopy));
        }
    }
}

// ========== F.1a: Profile registry helpers ==========

QString KF6MainWindow::resolveStartupProfile()
{
    const QString lastId = m_profileRegistry->lastActiveId();
    if (!lastId.isEmpty()) {
        const auto e = m_profileRegistry->entry(lastId);
        if (e.isValid() && QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    // F.1b: stale or missing last-active falls through to
    // auto-load-most-recent.
    const auto entries = m_profileRegistry->entries();
    for (const auto &e : entries) {
        if (QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    // Empty registry (or every entry stale) — first-run onboarding.
    // Shakedown F1: this used to be the bare name-prompt stopgap, which
    // created hollow profiles (no accounts, no bindings) and — combined
    // with F11 — set users up for a dashboard that hung in "Syncing…".
    // Route through the real accounts-first wizard instead.
    const auto r = runProfileWizard();
    if (r.state.profileName.isEmpty())
        return QString();   // user cancelled the wizard
    if (!createAndLoadProfileFromWizard(r))
        return QString();
    return m_currentProfile ? m_currentProfile->syncFolderPath() : QString();
}

void KF6MainWindow::setProfileRegistryForTest(
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> reg)
{
    m_profileRegistry = std::move(reg);
    // Rebuild the menu controller so it holds a valid registry pointer.
    m_profileMenuController = std::make_unique<ProfileMenuController>(
        m_profileRegistry.get(),
        actionCollection(),
        this);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::switchRequested,
            this, &KF6MainWindow::onSwitchProfile);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::forgetRequested,
            this, &KF6MainWindow::onForgetProfile);
}

QString KF6MainWindow::runStartupForTest()
{
    return resolveStartupProfile();
}

// F.1b T10: currentProfileIdForTest() — non-inline because Profile is only
// forward-declared in the header; profile.h is included in this .cpp.
QString KF6MainWindow::currentProfileIdForTest() const
{
    return m_currentProfile ? m_currentProfile->id() : QString();
}

QString KF6MainWindow::currentProfilePathForTest() const
{
    return m_currentProfile ? m_currentProfile->syncFolderPath() : QString();
}

void KF6MainWindow::onProfileSettings()
{
    if (!m_currentProfile) {
        m_logWidget->logError(i18n("No profile loaded"));
        return;
    }

    auto *dlg = new ProfilePropertiesDialog(m_currentProfile.get(), this);
    connect(dlg, &ProfilePropertiesDialog::settingsChanged, this, [this]() {
        updateWindowTitle();
    });
    connect(dlg, &ProfilePropertiesDialog::renameRequested,
            this, [this](const QString &id, const QString &newName) {
        if (m_profileRegistry->rename(id, newName)) {
            if (m_currentProfile && m_currentProfile->id() == id)
                m_currentProfile->setName(newName);
            updateWindowTitle();
        } else {
            m_logWidget->logError(i18n("Failed to rename profile"));
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ========== Sync Operations ==========

bool KF6MainWindow::reportSyncAlreadyRunning(const QString &opLabel)
{
    if (!m_palmRuntime || !m_palmRuntime->isSyncRunning())
        return false;
    // Shakedown F3/F11: a second sync click while a run is in flight used
    // to warn console-only and reset the conduit chips. Make it a
    // user-visible no-op instead.
    m_logWidget->logWarning(
        i18n("%1 ignored — a sync is already running", opLabel));
    statusBar()->showMessage(i18n("A sync is already running"), 4000);
    return true;
}

void KF6MainWindow::onHotSync()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("HotSync: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("HotSync")))
        return;
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->hotSync());
}

void KF6MainWindow::onFullSync()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("FullSync: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("FullSync")))
        return;
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->fullSync());
}

void KF6MainWindow::onCopyPalmToPC()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("CopyPalmToPC: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("Copy Palm → PC")))
        return;
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->copyPalmToPC());
}

void KF6MainWindow::onClobberPalmFromPC()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        if (m_logWidget)
            m_logWidget->logError(i18n("Clobber Palm from PC: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("Clobber Palm from PC")))
        return;

    WildPalms::Runtime::ClobberDialog::DomainMappings dm;
    for (const auto &domain : {QStringLiteral("calendar"),
                               QStringLiteral("contacts"),
                               QStringLiteral("memo"),
                               QStringLiteral("todo")}) {
        const auto ids = m_palmRuntime->palmDirectMappingsForDomain(domain);
        if (!ids.isEmpty()) dm.insert(domain, ids);
    }
    if (dm.isEmpty()) {
        if (m_logWidget)
            m_logWidget->logError(i18n("Clobber Palm from PC: no Palm-direct mappings configured"));
        return;
    }

    WildPalms::Runtime::ClobberDialog dlg(dm, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto ids = dlg.selectedMappingIds();
    if (ids.isEmpty()) return;

    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->clobberSync(ids));
}

void KF6MainWindow::onBackup()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("Backup: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("Backup")))
        return;
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->backup());
}

void KF6MainWindow::onRestore()
{
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logError(i18n("Restore: no Palm device connected"));
        return;
    }
    if (reportSyncAlreadyRunning(i18n("Restore")))
        return;
    if (QMessageBox::question(this, i18n("Restore"),
            i18n("Restore is destructive. All Palm records not in the backup WILL BE DELETED. Continue?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcher<WildPalms::Runtime::PalmRunResult>::finished,
            watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->restore());
}

void KF6MainWindow::onChangeSyncFolder()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }

    // Shakedown F15: this menu item used to invoke the F1 stopgap
    // name-prompt (creating a hollow new profile!) instead of picking a
    // folder. Show a real directory picker and persist the choice.
    const QString picked = QFileDialog::getExistingDirectory(this,
        i18n("Change Sync Folder"),
        m_currentProfile->syncFolderPath().isEmpty()
            ? QDir::homePath() : m_currentProfile->syncFolderPath());
    if (picked.isEmpty() || picked == m_currentProfile->syncFolderPath())
        return;

    m_currentProfile->setSyncFolderPath(picked);
    if (!m_currentProfile->save()) {
        m_logWidget->logError(i18n("Could not save the new sync folder"));
        return;
    }
    m_logWidget->logInfo(i18n("Sync folder changed to %1 — reloading profile", picked));
    loadProfile(m_currentProfile->syncFolderPath());
}

void KF6MainWindow::onOpenSyncFolder()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_syncPath));
}

void KF6MainWindow::onInstallFiles()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        i18n("Select Palm Files to Install"),
        QString(),
        i18n("Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)"));

    if (files.isEmpty()) {
        return;
    }

    QString installFolder = m_currentProfile->installFolderPath();
    QDir installDir(installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    int copiedCount = 0;
    int failCount = 0;

    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }

        if (QFile::copy(filePath, destPath)) {
            m_logWidget->logInfo(i18n("Queued for install: %1", fileInfo.fileName()));
            copiedCount++;
        } else {
            m_logWidget->logError(i18n("Failed to copy %1 to install folder", fileInfo.fileName()));
            failCount++;
        }
    }

    if (failCount == 0) {
        QMessageBox::information(this, i18n("Files Queued"),
            i18n("%1 file(s) queued for installation.\n\n"
                 "They will be installed on the next sync.", copiedCount));
    } else {
        QMessageBox::warning(this, i18n("Files Queued"),
            i18n("%1 file(s) queued, %2 failed to copy.\nCheck the log for details.",
                 copiedCount, failCount));
    }
}

void KF6MainWindow::onPalmRunStarted(const QString &label)
{
    m_currentPalmRunLabel = label;
    m_logWidget->logInfo(i18n("=== Starting %1 via PalmRuntime ===", label));
}

void KF6MainWindow::onPalmConflictHandlerKeepAlive()
{
    // Refresh the tickle when the conflict dialog is open for a long time.
    // The legacy code called m_session->resumeTickle(); now go through
    // the device link which emits to PalmTickle's start() (idempotent).
    if (m_palmRuntime && m_palmRuntime->deviceLink()) {
        m_palmRuntime->deviceLink()->resumeTickle();
    }
}

void KF6MainWindow::onConfigureMappings()
{
    if (!m_currentProfile) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("No profile loaded."));
        return;
    }

    SettingsDialog dialog(this, m_currentProfile.get());
    dialog.setProfileRegistry(m_profileRegistry.get());
    dialog.setAccountController(m_accountController.get());
    dialog.setPalmRuntime(m_palmRuntime.get());
    dialog.navigateToSyncMappings();
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}

// ========== F.2 sub-project D — conflict badge ==========

void KF6MainWindow::onConflictDetected(const Kalburator::Sync::ConflictInfo &info)
{
    ++m_pendingConflictCount;

    if (m_uiConflictStore) {
        // The ConflictInfo→ConflictRecord conversion lives on
        // PalmRuntime because WildPalmsCore can't include the
        // engine-side synctypes.h cleanly (WP-local synctypes.h
        // collision — see src/CMakeLists.txt comment near
        // add_subdirectory(app/mapping)).
        m_uiConflictStore->addConflict(
            WildPalms::Runtime::PalmRuntime::toConflictRecord(info));
    }

    refreshConflictBadge();
}

void KF6MainWindow::onConflictBadgeClicked()
{
    if (!m_palmRuntime) return;
    auto *dlg = new ConflictReviewDialog(m_uiConflictStore.get(),
                                          nullptr, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Shakedown F10: "Apply Resolutions (Sync)" used to be wired to
    // nothing — user decisions died in the UI store and the next sync
    // re-presented the same conflicts. Bridge them into the engine's
    // SyncConflictStore so rehydratePendingResolutions() replays them.
    connect(dlg, &ConflictReviewDialog::applyResolutionsRequested,
            this, [this]() { applyConflictResolutionsToEngine(); });
    connect(dlg, &QDialog::finished, this, [this]() {
        // Drop applied/resolved records so the next sync sees a clean
        // pending list; refresh the badge from the store.
        if (m_uiConflictStore) {
            m_uiConflictStore->removeAppliedConflicts();
            m_pendingConflictCount = m_uiConflictStore->pendingCount();
        } else {
            m_pendingConflictCount = 0;
        }
        refreshConflictBadge();
    });
    dlg->show();
}

int KF6MainWindow::applyConflictResolutionsToEngine()
{
    if (!m_uiConflictStore)
        return 0;
    if (!m_palmRuntime) {
        m_logWidget->logError(i18n(
            "Cannot apply conflict resolutions: no sync runtime loaded"));
        return 0;
    }

    const auto resolved = m_uiConflictStore->resolvedUnappliedConflicts();
    // The mapping + engine-store write live on PalmRuntime: WildPalmsCore
    // cannot include the engine-side synctypes.h (WP-local collision).
    const int applied = m_palmRuntime->applyConflictResolutions(resolved);

    if (applied > 0) {
        for (const auto &rec : resolved)
            m_uiConflictStore->markApplied(rec.conflictId, true);
        m_logWidget->logInfo(i18n(
            "Applied %1 conflict resolution(s) — they will take effect on "
            "the next sync", applied));
    }
    if (resolved.size() > applied)
        m_logWidget->logWarning(i18n(
            "%1 conflict resolution(s) could not be applied (unsupported "
            "decision)", resolved.size() - applied));
    return applied;
}

void KF6MainWindow::refreshConflictBadge()
{
    if (m_syncStatusModel)
        m_syncStatusModel->onConflictCountChanged(m_pendingConflictCount);

    // Shakedown F15: keep the menu action's label + enablement in step with
    // the live pending count.
    if (m_actionManager) {
        m_actionManager->updateConflictCount(m_pendingConflictCount);
        m_actionManager->showConflictsAction()->setEnabled(
            m_currentProfile != nullptr && m_pendingConflictCount > 0);
    }

    if (!m_conflictBadge) return;
    if (m_pendingConflictCount > 0) {
        m_conflictBadge->setText(
            i18n("%1 conflicts pending", m_pendingConflictCount));
        m_conflictBadge->setVisible(true);
    } else {
        m_conflictBadge->setVisible(false);
    }
}

void KF6MainWindow::pushProfileInfoToStatusModel()
{
    if (!m_syncStatusModel) return;
    if (m_currentProfile) {
        const DeviceFingerprint fp = m_currentProfile->deviceFingerprint();
        QString name = fp.isValid() ? fp.displayString() : i18n("No device registered");
        QString details;
        if (fp.isValid() && fp.hasExtendedInfo()) {
            QStringList parts;
            const QString os = fp.palmOSVersionString();
            if (!os.isEmpty()) parts << i18n("Palm OS %1", os);
            if (fp.ramFree != 0) parts << i18n("%1 free", DeviceFingerprint::formatMemorySize(fp.ramFree));
            details = parts.join(QStringLiteral(" · "));
        }
        m_syncStatusModel->setDeviceInfo(name, details);

        QString plan;
        if (m_currentProfile->autoSyncOnConnect()) {
            plan = (m_currentProfile->defaultSyncType() == QStringLiteral("fullsync"))
                ? i18n("Auto-sync (FullSync) on connect")
                : i18n("Auto-sync (HotSync) on connect");
        }
        m_syncStatusModel->setProfileInfo(m_currentProfile->name(),
                                          m_currentProfile->lastSyncTime(), plan);
    } else {
        m_syncStatusModel->setDeviceInfo(QString(), QString());
        m_syncStatusModel->setProfileInfo(QString(), QDateTime(), QString());
    }

    // Seed the conduit chip row from the runtime's enabled mappings.
    if (m_palmRuntime) {
        const auto descs = m_palmRuntime->conduitDescriptors();
        QVector<SyncStatusModel::ConduitSeed> seeds;
        for (const auto &d : descs)
            seeds.append({ d.mappingId, d.label, d.iconName });
        m_syncStatusModel->seedConduits(seeds);
    }
}

void KF6MainWindow::onPalmRunFinished(WildPalms::Runtime::PalmRunResult result)
{
    const QString op = m_currentPalmRunLabel.isEmpty()
                       ? i18n("Palm operation") : m_currentPalmRunLabel;
    if (result.cancelled) {
        // Shakedown F13: a user cancel is not an error.
        m_logWidget->logWarning(i18n("%1 cancelled", op));
        statusBar()->showMessage(i18n("%1 cancelled", op), 4000);
    } else if (result.success) {
        m_logWidget->logInfo(i18n("%1 completed successfully", op));
        statusBar()->showMessage(i18n("%1 complete", op));

        // Shakedown F14: stamp the profile so "Last sync" stops reading
        // "Never" forever.
        if (m_currentProfile && result.endTime.isValid()) {
            m_currentProfile->setLastSyncTime(result.endTime);
            m_currentProfile->save();
            pushProfileInfoToStatusModel();
        }
    } else {
        m_logWidget->logError(i18n("%1 finished with errors: %2",
                                   op, result.errorMessage));
        statusBar()->showMessage(i18n("%1 finished with errors", op));
    }

    // M6b Task 5 fix: honor ConnectionMode::DisconnectAfterSync.
    // Pre-M6b this was driven by DeviceSession::setConnectionMode (since
    // deleted in M6b Task 6). PalmRuntime doesn't model post-sync policy,
    // so KF6MainWindow checks the profile and disconnects here.
    if (m_currentProfile
        && m_currentProfile->connectionMode() == ConnectionMode::DisconnectAfterSync
        && m_palmRuntime
        && m_palmRuntime->isDeviceConnected()) {
        m_logWidget->logInfo(i18n("Auto-disconnecting per profile policy (DisconnectAfterSync)"));
        m_palmRuntime->disconnectDevice();
    }
}

void KF6MainWindow::onSessionPalmScreen(const QString &message)
{
    m_logWidget->logInfo(i18n("[Palm Screen] %1", message));
}

// ========== Misc ==========

void KF6MainWindow::onAbout()
{
    QMessageBox::about(this, i18n("About Wild Palms"),
        i18n("<h3>Wild Palms %1</h3>"
             "<p>Modern Palm OS synchronization for Linux.</p>"
             "<p>Copyright &copy; 2024–2026 Clinton Ignatov</p>"
             "<p>Licensed under the "
             "<a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">GNU General Public License v3.0</a> "
             "or later.</p>"
             "<hr>"
             "<p style='font-size:small;'>Built with "
             "<a href=\"https://www.qt.io\">Qt %2</a>, "
             "<a href=\"https://develop.kde.org/frameworks/\">KDE Frameworks 6</a>, and "
             "<a href=\"https://github.com/desrod/pilot-link\">pilot-link</a>.</p>",
             QString::fromLatin1(WILDPALMS_VERSION_STRING),
             QString::fromLatin1(QT_VERSION_STR)));
}

void KF6MainWindow::onSettings()
{
    SettingsDialog dialog(this, m_currentProfile.get());
    dialog.setProfileRegistry(m_profileRegistry.get());
    // F.3: feed AC + PalmRuntime so the dialog can show the
    // Accounts + Sync Mappings pages.
    dialog.setAccountController(m_accountController.get());
    dialog.setPalmRuntime(m_palmRuntime.get());
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}

void KF6MainWindow::onClearLog()
{
    if (m_logWidget) {
        m_logWidget->clear();
        m_logWidget->logInfo(i18n("Log cleared"));
    }
}

void KF6MainWindow::updateTrayState(const QString &status)
{
    if (!m_trayIcon) return;
    m_trayIcon->setToolTipSubTitle(status);
}
