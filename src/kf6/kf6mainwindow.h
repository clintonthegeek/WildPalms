#ifndef KF6MAINWINDOW_H
#define KF6MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QMap>
#include <functional>
#include <memory>
#include "runtime/palmrunresult.h"
#include "runtime/profileregistry.h"
#include "profilemenucontroller.h"
#include "../widgets/dashboard/syncstatusmodel.h"

// Forward declarations
struct DeviceFingerprint;
class QTimer;
class QDockWidget;
class QPushButton;
class KPageWidget;
class KPageWidgetItem;
class ActionManager;
class LogWidget;
class KPilotDeviceLink;
class Profile;
class DashboardWidget;
namespace WildPalms::AppPatchbay { class PatchbayPage; }
class KStatusNotifierItem;
class PalmDeviceMonitor;
class AutoSyncOrchestrator;

namespace WildPalms::Runtime {
    class PalmRuntime;
    class AccountController;
    class MassDeleteGuardPresenter;
    // ProfileRegistry is fully included above; no forward declaration needed.
}

namespace Kalburator::Sync {
    struct ConflictInfo;
    class BackendRegistry;
}

namespace Kalburator::Conflict {
    class ConflictStore;
}

namespace WildPalms::Wizard {
    struct Result;
}


/**
 * @brief KDE Frameworks 6 native main window for Wild Palms
 *
 * Implements a KPageWidget icon-sidebar layout (like KDE System Settings)
 * with pages for Sync/Dashboard, Memos, Contacts, Calendar, and Tasks.
 * A QDockWidget at the bottom holds the log panel.
 *
 * Uses KXmlGuiWindow for proper KDE desktop integration with XMLGUI
 * menus and toolbars.
 */
class KF6MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit KF6MainWindow(QWidget *parent = nullptr);
    ~KF6MainWindow() override;

    // Test seam — read-only view of the per-Palm-plugin KPageWidget map.
    const QMap<QString, KPageWidgetItem *> &palmPluginPagesForTest() const
        { return m_palmPluginPages; }

    // F.1a test seams (used by T15)
    void setProfileRegistryForTest(
        std::unique_ptr<WildPalms::Runtime::ProfileRegistry> reg);
    QString runStartupForTest();

    // Test seams (F.1b T10): read-only accessors used by Forget tests.
    WildPalms::Runtime::ProfileRegistry *profileRegistryForTest() const {
        return m_profileRegistry.get();
    }
    void runLoadProfileForTest(const QString &path) { loadProfile(path); }
    // Non-inline: Profile is forward-declared here; definition is in .cpp
    // where profile.h is included.
    QString currentProfileIdForTest() const;
    QString currentProfilePathForTest() const;

    // Test seams (F.2 sub-project D).
    int pendingConflictCountForTest() const { return m_pendingConflictCount; }
    QPushButton *conflictBadgeForTest() const { return m_conflictBadge; }
    void runConflictDetectedForTest(const Kalburator::Sync::ConflictInfo &info) {
        onConflictDetected(info);
    }
    Kalburator::Conflict::ConflictStore *conflictStoreForTest() const {
        return m_uiConflictStore.get();
    }
    // Shakedown F10 seams: reach the loaded runtime / run the resolution
    // bridge without opening ConflictReviewDialog.
    WildPalms::Runtime::PalmRuntime *palmRuntimeForTest() const {
        return m_palmRuntime.get();
    }
    int applyConflictResolutionsForTest() {
        return applyConflictResolutionsToEngine();
    }
    // Shakedown F14 seam: drive the run-finished path without hardware.
    void runPalmFinishedForTest(WildPalms::Runtime::PalmRunResult result) {
        onPalmRunFinished(result);
    }

    // F.1c.1 test seam — install a stub wizard runner that returns a
    // pre-built Result without actually exec()ing a QWizard.
    void setRunProfileWizardForTest(
        std::function<WildPalms::Wizard::Result()> fn);
    void runNewProfileForTest() { onNewProfile(); }

    // F.1d test seams
    QString renderMismatchMessageForTest(const DeviceFingerprint &expected,
                                          const DeviceFingerprint &connected) const;
    bool    runMismatchCheckForTest(const DeviceFingerprint &expected,
                                     const DeviceFingerprint &connected);

protected:
    void closeEvent(QCloseEvent *event) override;

    /// Test seam: pops the Forget confirm dialog. Production override
    /// runs the real QDialog (see kf6mainwindow.cpp); tests override
    /// to return preset values. Returns true if user clicked Forget;
    /// outDeleteFiles is set to the checkbox state.
    virtual bool confirmForgetProfile(
        const WildPalms::Runtime::ProfileEntry &entry,
        bool *outDeleteFiles);

    // F.1d virtual seam — override in tests to capture dialog opens.
    // Returns false (reject) to simulate disconnect. Real path calls QMessageBox::exec.
    virtual bool openMismatchDialogForTest(const DeviceFingerprint &expected,
                                            const DeviceFingerprint &connected);

private Q_SLOTS:
    // Device connection
    void onConnectDevice();
    void onConnectionStarted();
    void onConnectionComplete(bool success, const QString &error);
    void onDeviceDisconnected();
    void onDisconnectDevice();
    void onDevicePoll();
    void onCancelConnection();
    void startListening(const QString &devicePath);
    void stopListening();
    void startConnection(const QString &devicePath);
    void onDeviceReady(const QString &userName, const QString &deviceName);
    void onReadyForSync();
    void onListDatabases();
    void onSetUserInfo();
    void onDeviceInfo();

    // Profile management
    void onNewProfile();
    void onCloseProfile();
    void onProfileSettings();
    void onImportProfile();
    void onSwitchProfile(const QString &id);
    void onForgetProfile(const QString &id);

    // Sync operations
    void onHotSync();
    void onFullSync();
    void onCopyPalmToPC();
    void onClobberPalmFromPC();
    void onBackup();
    void onRestore();
    void onChangeSyncFolder();
    void onOpenSyncFolder();
    void onInstallFiles();

    // PalmRuntime callbacks
    void onSessionPalmScreen(const QString &message);

    // Misc
    void onAbout();
    void onSettings();
    void onClearLog();

    // F16: File→Quit must actually quit even when close hides to tray
    void appQuitRequested();

    // View management
    void onToggleLogPanel(bool visible);
    void onPageChanged(KPageWidgetItem *current, KPageWidgetItem *previous);
    void onFocusLog();

    // Auto-sync detection
    void onAutoDeviceDetected(Profile *profile, const QStringList &ports);
    void onUnregisteredDeviceDetected(const QString &usbSerial,
                                       const QString &userName,
                                       quint32 userId);

    // M2/M3 — PalmRuntime callbacks
    void onPalmRunStarted(const QString &label);
    void onPalmRunFinished(WildPalms::Runtime::PalmRunResult result);

    // M5a — keepAlive from KalburatorInteractiveConflictHandler
    void onPalmConflictHandlerKeepAlive();

    // F.3 — open SettingsDialog deep-linked to the Sync Mappings graph page
    void onConfigureMappings();

    // F.2 sub-project D — conflict badge
    void onConflictDetected(const Kalburator::Sync::ConflictInfo &info);
    void onConflictBadgeClicked();

private:
    // UI setup
    void setupUI();
    void setupActions();
    void setupConnections();
    void createCentralLayout();
    void updateMenuState(bool connected);
    void updateWindowTitle();
    void updateProfileMenuState();

    // Profile management
    void loadProfile(const QString &path);
    void closeProfile();
    /// Remove + delete the Sync Patchbay page (if present) while its borrowed
    /// controllers are still alive. Safe to call when no page exists.
    void destroyPatchbayPage();
    QString resolveStartupProfile();

    // Device handling
    void startConnectionMultiPort(const QStringList &devicePaths);
    bool handleDeviceFingerprint(const struct DeviceFingerprint &connectedDevice);
    void registerDeviceWithCurrentProfile(const struct DeviceFingerprint &fingerprint);
    int countDatabaseRecords(const QString &dbName);

    // State persistence
    void saveWindowState();
    void restoreWindowState();

    // F.2 sub-project D — conflict badge helper
    void refreshConflictBadge();
    /// Shakedown F10: push resolved-but-unapplied decisions from
    /// m_uiConflictStore into the engine's SyncConflictStore so they
    /// replay on the next sync. Returns the number applied.
    int applyConflictResolutionsToEngine();

    // Dashboard redesign — push device/profile/conduit state into the model.
    void pushProfileInfoToStatusModel();

    // F.1c.1 — NewProfileWizard integration. runProfileWizard() is the
    // test seam: production exec()s the real wizard; tests set
    // m_runWizardOverride to inject a pre-built Result. Returns an
    // empty-name Result on Cancel.
    virtual WildPalms::Wizard::Result runProfileWizard();
    bool writeWizardResultToProfile(const QString &path,
                                    const WildPalms::Wizard::Result &r);
    /// Shared tail of "New Profile…" and the first-run path (shakedown F1):
    /// register a profile for the wizard Result, persist it, and load it.
    /// Returns false (with cleanup done) if registration or persistence
    /// failed; true also means loadProfile() has run.
    bool createAndLoadProfileFromWizard(const WildPalms::Wizard::Result &r);

    /// Shakedown F3/F11: re-entrant sync clicks are a user-visible no-op.
    /// Logs to the log dock + status bar and returns true when a sync is
    /// already in flight (caller then returns without dispatching).
    bool reportSyncAlreadyRunning(const QString &opLabel);

    // KPageWidget layout
    KPageWidget *m_pageWidget;
    QDockWidget *m_logDock;
    LogWidget *m_logWidget;

    // Status header strip (above plugin pages)
    DashboardWidget *m_dashboardWidget;
    SyncStatusModel *m_syncStatusModel = nullptr;

    // Dynamic plugin pages (keyed by plugin id), populated synchronously in loadProfile()
    QMap<QString, KPageWidgetItem *> m_palmPluginPages;

    // Sync Patchbay (Part 1) — three-tier mapping editor/monitor. Borrows
    // m_currentProfile / m_accountController / m_palmRuntime; MUST be
    // destroyed before any of them (1be66a3 teardown lesson) — see
    // destroyPatchbayPage(), called at the top of loadProfile()/closeProfile()
    // and ~KF6MainWindow.
    WildPalms::AppPatchbay::PatchbayPage *m_patchbayPage = nullptr;
    KPageWidgetItem *m_patchbayPageItem = nullptr;

    // PalmRuntime owns the hotSync path.
    std::unique_ptr<WildPalms::Runtime::PalmRuntime> m_palmRuntime;

    // AccountController is profile-scoped, recreated alongside
    // m_palmRuntime in loadProfile(). Borrows m_palmRuntime->backendRegistry(),
    // m_currentProfile, and m_palmRuntime — torn down BEFORE m_palmRuntime
    // and m_currentProfile in closeProfile()/loadProfile() to avoid dangling
    // borrowed pointers.
    std::unique_ptr<WildPalms::Runtime::AccountController> m_accountController;

    // Action manager
    ActionManager *m_actionManager;

    // F.1a: App-level profile registry
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> m_profileRegistry;

    // F.1b: Switch ▸ / Forget ▸ menu controller
    std::unique_ptr<ProfileMenuController> m_profileMenuController;

    // Mass-delete guard: constructed once, registered with PalmRuntime on
    // every loadProfile call. Lifetime matches the window.
    std::unique_ptr<WildPalms::Runtime::MassDeleteGuardPresenter> m_massDeleteGuard;

    // F.2 sub-project D — conflict badge
    int          m_pendingConflictCount = 0;
    QPushButton *m_conflictBadge = nullptr;

    // F.1c.1 — App-level BackendRegistry. Lifetime equals KF6MainWindow.
    // The NewProfileWizard borrows this to construct transient IProvider
    // instances for collection discovery before any profile is loaded.
    // (Per-profile BackendRegistry remains owned by PalmRuntime.)
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_appBackendRegistry;

    // F.1c.1 — test seam for runProfileWizard().
    std::function<WildPalms::Wizard::Result()> m_runWizardOverride;

    // UI-side ConflictStore. The engine writes to its own SQLite
    // Sync::SyncConflictStore (a different type); we mirror each
    // detected conflict into this in-memory store so
    // ConflictReviewDialog has something to display when the badge
    // is clicked. Owned by this window; lifetime spans the session.
    std::unique_ptr<Kalburator::Conflict::ConflictStore> m_uiConflictStore;

    QString m_syncPath;

    // Last used connection settings
    QString m_lastUsedDevicePath;
    QString m_lastUsedBaudRate;

    // Device listening mode
    QTimer *m_devicePollTimer = nullptr;
    bool m_listeningForDevice = false;
    QString m_listeningDevicePath;

    // Current async operation
    QString m_currentPalmRunLabel;

    // Profile
    std::unique_ptr<Profile> m_currentProfile;

    // M5a: stored as QObject* to avoid including libkalburator headers in this
    // header (include-guard collision with WP-local QSyncCore headers).
    // Actual type: KalburatorInteractiveConflictHandler (QObject subclass).
    QObject *m_palmConflictHandler = nullptr;

    // Auto-detection and auto-sync
    PalmDeviceMonitor *m_deviceMonitor = nullptr;
    AutoSyncOrchestrator *m_autoSync = nullptr;

    // Gate for auto-sync-on-connect: both device and accounts must be ready
    // before the first auto-sync fires. Reset on every loadProfile().
    bool m_deviceReadyForSync = false;
    bool m_accountsReadyForSync = false;

    // System tray
    KStatusNotifierItem *m_trayIcon = nullptr;
    bool m_minimizeToTray = true;
    // F16: one-time explanation that closing hides to tray, and a flag
    // letting File→Quit bypass the hide-to-tray closeEvent.
    bool m_trayHintShown = false;
    bool m_forceQuit = false;
    void updateTrayState(const QString &status);
};

#endif // KF6MAINWINDOW_H
