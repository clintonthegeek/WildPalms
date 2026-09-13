#include "syncmappingspage.h"

#include "syncmappingsgraphview.h"
#include "mappinginspectorpanel.h"

#include "profile.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"

#include <QLabel>
#include <QVBoxLayout>

#include <kalburator/sync/iprovider.h>

namespace WildPalms::AppMapping {

SyncMappingsPage::SyncMappingsPage(Profile *profile,
                                   WildPalms::Runtime::AccountController *accounts,
                                   WildPalms::Runtime::PalmRuntime *palmRuntime,
                                   QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_accounts(accounts)
    , m_palmRuntime(palmRuntime)
{
    buildUi();
    reloadGraph();

    if (m_palmRuntime) {
        connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runStarted,
                this, &SyncMappingsPage::onSyncStarted);
        connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runFinished,
                this, &SyncMappingsPage::onSyncFinished);
        if (m_palmRuntime->isRunning())
            onSyncStarted();
    }

    if (m_accounts) {
        connect(m_accounts, &WildPalms::Runtime::AccountController::providersChanged,
                this, &SyncMappingsPage::reloadGraph);
        // Providers connect asynchronously; their collections only arrive when
        // discovery finishes. Without this, opening the page before connect
        // completes shows providers stuck on "Connecting…" with no calendars
        // to wire. Refresh the graph as each provider's state changes.
        connect(m_accounts, &WildPalms::Runtime::AccountController::connectStateChanged,
                this, &SyncMappingsPage::reloadGraph);
    }
}

void SyncMappingsPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_readOnlyBanner = new QLabel(
        tr("A sync is in progress — mapping changes are locked"), this);
    m_readOnlyBanner->setStyleSheet(
        QStringLiteral("background:#c07000;color:white;padding:6px;"));
    m_readOnlyBanner->setAlignment(Qt::AlignCenter);
    m_readOnlyBanner->setVisible(false);
    outer->addWidget(m_readOnlyBanner);

    m_graphView = new SyncMappingGraphView(this);
    outer->addWidget(m_graphView, /*stretch*/1);

    m_inspector = new MappingInspectorPanel(this);
    m_inspector->setMaximumHeight(120);
    outer->addWidget(m_inspector);

    connect(m_graphView, &SyncMappingGraphView::edgeSelected,
            this, &SyncMappingsPage::onEdgeSelected);
    connect(m_inspector, &MappingInspectorPanel::mappingEdited,
            this, &SyncMappingsPage::onMappingEdited);
}

void SyncMappingsPage::reloadGraph()
{
    // Snapshot.
    QHash<QString, QStringList> snapshot;
    if (m_profile) {
        for (const QString &db : {QStringLiteral("DatebookDB"),
                                  QStringLiteral("AddressDB"),
                                  QStringLiteral("MemoDB"),
                                  QStringLiteral("ToDoDB")}) {
            const auto names = m_profile->categorySlotNames(db);
            if (!names.isEmpty())
                snapshot.insert(db, names);
        }
    }

    // Providers.
    QList<SyncMappingGraphView::ProviderEntry> providerEntries;
    if (m_accounts) {
        for (const auto &provider : m_accounts->providerSummaries()) {
            SyncMappingGraphView::ProviderEntry e;
            e.providerId  = provider.id;
            e.displayName = provider.displayName;
            e.collections = provider.collections;
            const auto state = provider.state;
            using S = WildPalms::Runtime::AccountController::ConnectionState;
            if (state == S::Connecting)
                e.busyText = tr("Connecting…");
            else if (state == S::Error)
                e.busyText = tr("Error: %1").arg(provider.errorMessage);
            providerEntries.append(e);
        }
    }

    m_graphView->setSnapshot(snapshot);
    m_graphView->setProviders(providerEntries);
    m_graphView->setMappings(m_profile ? m_profile->syncMappingsJson()
                                       : QJsonArray());
    m_graphView->rebuild();

    m_inspector->setSelectedMapping(QString(), {});
}

void SyncMappingsPage::onSyncStarted()
{
    setReadOnlyBannerVisible(true);
    m_graphView->setReadOnly(true);
}

void SyncMappingsPage::onSyncFinished()
{
    setReadOnlyBannerVisible(false);
    m_graphView->setReadOnly(false);
}

void SyncMappingsPage::setReadOnlyBannerVisible(bool visible)
{
    if (m_readOnlyBanner) m_readOnlyBanner->setVisible(visible);
}

void SyncMappingsPage::onMappingEdited(const QString &mappingId,
                                       const QJsonObject &updatedJson)
{
    QJsonArray current = m_graphView->mappings();
    QJsonArray rewritten;
    for (const auto v : current) {
        QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString() == mappingId)
            obj = updatedJson;
        rewritten.append(obj);
    }
    m_graphView->setMappings(rewritten);
    m_graphView->rebuild();
}

void SyncMappingsPage::onEdgeSelected(const QString &mappingId)
{
    if (mappingId.isEmpty()) {
        m_inspector->setSelectedMapping(QString(), {});
        return;
    }
    for (const auto v : m_graphView->mappings()) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString() == mappingId) {
            m_inspector->setSelectedMapping(mappingId, obj);
            return;
        }
    }
    m_inspector->setSelectedMapping(QString(), {});
}

QJsonArray SyncMappingsPage::currentMappings() const
{
    return m_graphView->mappings();
}

void SyncMappingsPage::applyTo(Profile *profile)
{
    if (!profile) return;
    const QJsonArray mappings = currentMappings();
    profile->setSyncMappingsJson(mappings);
    profile->save();
    if (m_palmRuntime && !m_palmRuntime->isRunning())
        m_palmRuntime->reloadMappings(mappings);
}

} // namespace WildPalms::AppMapping
