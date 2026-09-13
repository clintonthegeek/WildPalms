// src/app/patchbay/patchbaypage.cpp
#include "patchbaypage.h"

#include <QHBoxLayout>

#include "patchbayinspector.h"
#include "syncpatchbayview.h"

#include "profile.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "plugins/pimplugin.h"

#include <kalburator/sync/iprovider.h>

namespace WildPalms::AppPatchbay {

PatchbayPage::PatchbayPage(Profile *profile,
                           WildPalms::Runtime::AccountController *accounts,
                           WildPalms::Runtime::PalmRuntime *palmRuntime,
                           QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_accounts(accounts)
    , m_runtime(palmRuntime)
{
    m_model = new PatchbayModel(this);
    m_view = new SyncPatchbayView(this);
    m_inspector = new PatchbayInspector(this);
    m_inspector->setFixedWidth(260);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_inspector);

    if (m_runtime)
        m_deviceConnected = m_runtime->isDeviceConnected();

    // model → persistence (write-through, spec §8/§11)
    connect(m_model, &PatchbayModel::mappingsChanged, this,
            [this](const QJsonArray &rows) {
                m_profile->setSyncMappingsJson(rows);
                m_profile->save();
                if (m_runtime && !m_runtime->isRunning())
                    m_runtime->reloadMappings(rows);
            });
    connect(m_model, &PatchbayModel::desiredCategoriesChanged, this,
            [this](const QString &dbName, const QStringList &names) {
                m_profile->setDesiredCategoryNames(dbName, names);
            });

    // sources → model (rebuild triggers, spec §8)
    if (m_accounts) {
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::providersChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::connectStateChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_accounts,
                &WildPalms::Runtime::AccountController::accountsReady,
                this, &PatchbayPage::refreshInputs);
    }
    if (m_runtime) {
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::routeStatusesChanged,
                this, &PatchbayPage::refreshInputs);
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::deviceConnected,
                this, [this] { m_deviceConnected = true; refreshInputs(); });
        connect(m_runtime, &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
                this, [this] { m_deviceConnected = false; refreshInputs(); });
    }

    // view ↔ inspector
    connect(m_view, &SyncPatchbayView::wireSelected,
            this, &PatchbayPage::onWireSelected);
    connect(m_view, &SyncPatchbayView::addCategoryRequested,
            this, &PatchbayPage::onAddCategoryRequested);
    connect(m_inspector, &PatchbayInspector::mappingEdited, this,
            [this](const QString &id, const QJsonObject &changes) {
                m_model->updateMapping(id, changes);
            });

    refreshInputs();
    m_view->setModel(m_model);
}

PatchbayPage::~PatchbayPage()
{
    // Sever source connections explicitly: AccountController teardown has
    // bitten before (see 1be66a3 — provider signals into half-destroyed
    // widgets). Borrowed pointers must not be dereferenced after this.
    if (m_accounts) disconnect(m_accounts, nullptr, this, nullptr);
    if (m_runtime)  disconnect(m_runtime,  nullptr, this, nullptr);
}

PatchbayModel::Inputs PatchbayPage::gatherInputs() const
{
    PatchbayModel::Inputs in;
    in.mappings = m_profile->syncMappingsJson();

    if (m_runtime) {
        for (auto *plugin : m_runtime->conduits()) {
            ConduitFacts f;
            f.conduitId = plugin->conduitId();
            f.domain = plugin->domain().toString();
            f.dbName = plugin->primaryDbName();
            f.displayName = plugin->conduitDisplayName();
            f.matchesCollection =
                [plugin](const Kalburator::Sync::CollectionInfo &c) {
                    return plugin->matchesCollection(c);
                };
            in.conduits << f;
            in.slotSnapshot.insert(f.dbName,
                                   m_profile->categorySlotNames(f.dbName));
            in.desiredCategories.insert(
                f.dbName, m_profile->desiredCategoryNames(f.dbName));
        }
        in.routeStatuses = m_runtime->routeStatuses();
        in.deviceConnected = m_deviceConnected;
        in.deviceName = m_profile->name();
    }

    if (m_accounts) {
        for (auto *provider : m_accounts->providers()) {
            PatchbayModel::ProviderEntry e;
            e.providerId = provider->id();
            e.displayName = provider->displayName();
            const auto state = m_accounts->stateFor(e.providerId);
            if (state == WildPalms::Runtime::AccountController::
                             ConnectionState::Connecting)
                e.busyText = QStringLiteral("Connecting…");
            else if (state == WildPalms::Runtime::AccountController::
                                  ConnectionState::Error)
                e.busyText = QStringLiteral("Error: %1")
                                 .arg(m_accounts->errorFor(e.providerId));
            else
                e.collections = m_accounts->collectionsFor(e.providerId);
            in.providers << e;
        }
    }
    return in;
}

void PatchbayPage::refreshInputs()
{
    m_model->setInputs(gatherInputs());
}

void PatchbayPage::onWireSelected(const QString &mappingId)
{
    if (mappingId.isEmpty()) {
        m_inspector->setSelectedMapping({}, {}, {}, {});
        return;
    }
    const QJsonObject row = m_model->mappingById(mappingId);
    // category name for the status text, if this is a category route
    QString category;
    const QString srcCal =
        row.value(QLatin1String("sourceCalendar")).toString();
    const int nameAt = srcCal.indexOf(QLatin1String("/name:"));
    if (nameAt > 0)
        category = srcCal.mid(nameAt + 6);
    m_inspector->setSelectedMapping(mappingId, row,
                                    m_model->statusFor(mappingId), category);
}

void PatchbayPage::onAddCategoryRequested(const QString &domain,
                                          const QPointF &scenePos)
{
    Q_UNUSED(scenePos);
    m_view->openCategoryEditor(domain);
}

} // namespace WildPalms::AppPatchbay
