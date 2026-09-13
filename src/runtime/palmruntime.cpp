#include "palmruntime.h"
#include "palmruntimeadapters.h"
#include "palmticklephase.h"
#include "palmdeviceaccess.h"
#include "palm/kpilotlink.h"
#include "palm/kpilotdevicelink.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QtConcurrent>

#include <kalburator/sync/backendregistry.h>
#include <kalburator/calendar/syncbackend.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/calendar/syncconflictstore.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/conflict/conflictrecord.h>
#include <kalburator/engine/imassdeleteguard.h>
#include <kalburator/shape/shape.h>
// K.8b T13: ibackendplugin_v2.h include removed — V2 plugin ABI deleted.
#include "palm/device/pilotlinkpalmdatabaseaccess.h"

// O7: stock domain/infra plugins, loaded in one batch with WP's plugins
// (mirrors PlanStan's composition root). DAV provider plugins are omitted —
// registerStandardContributions() seeds those backend contributions.
#include <kalburator/universal/universalstorageplugin.h>
#include <kalburator/blob/blobplugin.h>
#include <kalburator/note/noteplugin.h>
#include <kalburator/todo/todoplugin.h>
#include <kalburator/contacts/contactsplugin.h>
#include <kalburator/calendar/calendarplugin.h>

// K.8b T6: in-process plugin loading via PluginManager.
// Kalburator::Sync exposes src/plugin/ on its PUBLIC include path,
// so headers are reachable without a path prefix.
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/manifest.h>
#include <kalburator/plugin/stock_plugins.h>
#include <kalburator/shape/domainregistry.h>
#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"
#include "plugins/pimplugin.h"

#include "profile.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/logicalcalendar.h>
#include <kalburator/sync/syncmappinggenerator.h>

#include <kalburator/universal/filteredcollectionbackend.h>
#include <kalburator/shape/recordfilter.h>

#include "routemapping.h"
#include "conduitcatalog.h"
#include "categoryreconciler.h"
#include "palm/sync/palmchangedetection.h"

#include "standardcontributions.h"
#include <kalburator/runtime/collectionruntime.h>

namespace {

// Substrate A1: the hardcoded kPalmBackendIds array is gone —
// PalmRuntime::isPalmConduitBackendId() derives the set from the loaded
// conduit descriptors.

static QString sanitizeForFilesystem(const QString &id)
{
    QString s = id;
    s.replace(QLatin1Char(':'), QLatin1Char('_'))
     .replace(QLatin1Char('/'), QLatin1Char('_'));
    return s;
}

// O7: stock domain/infra plugins DEFINE their canonical domain.
static Kalburator::PluginManifest mkStockManifest(const QString &id,
                                                  QStringList defines = {})
{
    Kalburator::PluginManifest m;
    m.id                      = id;
    m.version                 = QStringLiteral("1.0");
    m.displayName             = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains          = std::move(defines);
    return m;
}

// O7: WP Palm plugins contribute a (domain,palm) peer shape + palm<->peer edges
// via shapeContributions(); applyPlugin requires the augmented domain to be in
// the manifest, so each REQUIRES its canonical domain. The stock definer must be
// in the SAME loadInProcess batch (resolve() only sees the current batch and
// orders requirers after definers) — see registerPalmPlugins().
static Kalburator::PluginManifest mkPalmManifest(const QString &id,
                                                 const QString &domain)
{
    Kalburator::PluginManifest m;
    m.id                      = id;
    m.version                 = QStringLiteral("1.0");
    m.displayName             = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.requiresDomains         = { domain };
    return m;
}

}  // namespace

#include "palmruntime.moc"

namespace WildPalms::Runtime {

PalmRuntimeAssembly::PalmRuntimeAssembly(const QString &profilePath)
    : m_profilePath(profilePath)
    , m_backupRoot(QDir(profilePath).filePath(QStringLiteral("backup")))
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
{}

PalmRuntimeAssembly::~PalmRuntimeAssembly() = default;

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , PalmRuntimeAssembly(profilePath)
{
    qRegisterMetaType<PalmRunResult>();
    QDir(profilePath).mkpath(QStringLiteral(".state"));

    // Register provider contributions into this runtime's local registry.
    // ProviderManager no longer auto-registers these (K.8a T6); the
    // application layer is responsible for seeding contributions.
    // F.1c.1 T2: the same registration is needed for KF6MainWindow's
    // app-level registry (used by NewProfileWizard for pre-profile
    // discovery), so the calls live in a shared free function.
    WildPalms::Runtime::registerStandardContributions(m_registry.get());

    // C Task 2: stand up the per-profile SQLite hub. The .state/ directory
    // is created before any runtime-owned stores are opened.
    m_hub = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(
        QDir(profilePath).filePath(QStringLiteral(".state/hub.db")));

    // T7: per-profile Palm revision token store. .state/ dir already exists.
    m_palmRevisionStore = std::make_unique<WildPalms::PalmSync::PalmRevisionStore>(
        QDir(profilePath).filePath(QStringLiteral(".state/palm-revisions.ini")));
    m_registry->registerBackendInstance(QStringLiteral("wp-hub"), m_hub.get());

    // K.8b T6: load the five static Palm plugins in-process.
    registerPalmPlugins();

    // C Task 2: create per-domain canon collections in the hub.
    ensureHubCollections();

    // Sub-project D: dispatch setHub/setRuntime to every PimPlugin.
    // Non-PIM plugins (plucker) inherit Kalburator::Plugin directly
    // and dynamic_cast to nullptr — they are skipped naturally.
    for (auto &p : m_palmPlugins) {
        if (auto *pim = dynamic_cast<WildPalms::Plugins::PimPlugin*>(p.get())) {
            pim->setHub(m_hub.get());
            pim->setRuntime(this);
        }
    }

    QObject::connect(this, &PalmRuntime::runStarted,
            this, [this]() { m_running = true; });
    QObject::connect(this, &PalmRuntime::runFinished,
            this, [this]() { m_running = false; });
}

PalmRuntime::~PalmRuntime() = default;

QList<Kalburator::Runtime::ProviderSnapshot> PalmRuntime::providerSnapshots() const
{
    return m_collectionRuntime ? m_collectionRuntime->snapshot().providers
                                : QList<Kalburator::Runtime::ProviderSnapshot>{};
}

QFuture<bool> PalmRuntime::connectProviders()
{
    if (!m_collectionRuntime) {
        QString error;
        if (!initializeCollectionRuntime(error))
            return QtFuture::makeReadyValueFuture(false);
    }
    return m_collectionRuntime->connectProviders();
}

bool PalmRuntime::addProvider(const Kalburator::Sync::BackendConfiguration &config,
                              QString &errorMessage)
{
    if (!m_collectionRuntime && !initializeCollectionRuntime(errorMessage))
        return false;
    return m_collectionRuntime->addProvider(config, errorMessage);
}

bool PalmRuntime::updateProvider(const Kalburator::Sync::BackendConfiguration &config,
                                 QString &errorMessage)
{
    if (!m_collectionRuntime && !initializeCollectionRuntime(errorMessage))
        return false;
    return m_collectionRuntime->updateProvider(config, errorMessage);
}

bool PalmRuntime::removeProvider(const QString &providerId, QString &errorMessage)
{
    if (!m_collectionRuntime && !initializeCollectionRuntime(errorMessage))
        return false;
    return m_collectionRuntime->removeProvider(providerId, errorMessage);
}

bool PalmRuntime::initializeCollectionRuntime(QString &errorMessage)
{
    if (m_collectionRuntime)
        return true;

    if (m_profilePath.isEmpty()) {
        errorMessage = QStringLiteral("Palm runtime has no profile path");
        return false;
    }
    QDir(m_profilePath).mkpath(QStringLiteral(".state"));

    Kalburator::Runtime::RuntimeDefinition definition;
    // CollectionRuntime expects a database filename, while PalmRuntime's
    // profile path is the containing directory used by the legacy stores.
    definition.storagePath = QDir(m_profilePath).filePath(
        QStringLiteral(".state/.wildpalms-runtime.db"));
    definition.policy.skipUnchangedMappings = true;
    definition.policy.maxConcurrentMappings = 1;
    definition.policy.confirmMassDelete = [this](const QString &mappingId,
                                                  const QString &targetBackendId,
                                                  int proposedDeletes,
                                                  int baselineCount) {
        if (!m_massDeleteGuard)
            return true;
        return m_massDeleteGuard->confirmMassDelete(mappingId, targetBackendId,
                                                    proposedDeletes, baselineCount);
    };

    if (m_profile)
        definition.providers = m_profile->accounts();

    // CollectionRuntime loads the stock libkalburator extensions itself. The
    // Palm conduit objects are consumer-owned and remain alive for the
    // lifetime of this facade, so pass only those extension objects across
    // the neutral plugin boundary.
    for (const auto &plugin : m_palmPlugins) {
        if (auto *pim = dynamic_cast<WildPalms::Plugins::PimPlugin *>(plugin.get())) {
            definition.pluginExtensions.append({
                plugin.get(),
                mkPalmManifest(QStringLiteral("wildpalms.") + pim->conduitId(),
                               pim->domain().toString())});
        }
    }

    auto lease = QSharedPointer<WildPalms::Runtime::PalmExternalResourceLease>::create(
        [this](QString &error) {
            if (!m_device || !m_device->isConnected()) {
                error = QStringLiteral("Palm device is not connected");
                return false;
            }
            error.clear();
            return true;
        },
        [this](QString &error) {
            if (!m_device || !m_device->isConnected()) {
                error = QStringLiteral("Palm device was disconnected");
                return false;
            }
            error.clear();
            return true;
        },
        [this](QString &error) {
            if (m_device)
                m_device->flushWrites();
            error.clear();
            return true;
        },
        [this]() {
            // CollectionRuntime owns engine cancellation. The device lease
            // has no second cancellation channel; this callback is reserved
            // for Palm-specific link teardown if one is needed later.
            Q_UNUSED(this);
        },
        [](const Kalburator::Runtime::RunResult &) {});
    definition.resources.append({QStringLiteral("palm-device"), lease});

    // The hub is created before the runtime so plugin views can borrow the
    // same object. The first topology commit transfers ownership to the
    // runtime through the opaque endpoint factory.
    definition.backendFactories.append(
        QSharedPointer<WildPalms::Runtime::PalmBackendFactory>::create(
                QStringLiteral("hub"), [this]() -> std::unique_ptr<Kalburator::Sync::SyncBackendBase> {
                if (!m_hub)
                    return {};
                return std::unique_ptr<Kalburator::Sync::SyncBackendBase>(m_hub.release());
            }));

    for (auto *conduit : conduits()) {
        const QString backendId = conduit->conduitId();
        definition.backendFactories.append(
            QSharedPointer<WildPalms::Runtime::PalmBackendFactory>::create(
                QStringLiteral("palm:") + backendId,
                [this, conduit]() -> std::unique_ptr<Kalburator::Sync::SyncBackendBase> {
                    if (!m_device)
                        return {};
                    // The current production graph has already materialized
                    // these Palm adapters. Transfer that exact object to the
                    // public runtime instead of creating a second device
                    // backend over the same DLP link.
                    if (m_registry) {
                        auto *existing = m_registry->backendInstance(conduit->conduitId());
                        for (auto it = m_ownedBackends.begin(); it != m_ownedBackends.end(); ++it) {
                            if (it->get() == existing) {
                                auto backend = std::move(*it);
                                m_ownedBackends.erase(it);
                                return backend;
                            }
                        }
                    }
                    auto backend = conduit->createPalmBackend(m_device.get());
                    if (auto *cd = dynamic_cast<WildPalms::PalmSync::PalmChangeDetection *>(backend.get()))
                        cd->setPalmRevisionStore(m_palmRevisionStore.get());
                    return backend;
                }));
    }

    for (auto it = m_injectedBackends.cbegin(); it != m_injectedBackends.cend(); ++it) {
        const QString id = it.key();
        definition.backendFactories.append(
            QSharedPointer<WildPalms::Runtime::PalmBackendFactory>::create(
                QStringLiteral("injected:") + id,
                [this, id]() -> std::unique_ptr<Kalburator::Sync::SyncBackendBase> {
                    const auto injected = m_injectedBackends.value(id);
                    for (auto owned = m_ownedBackends.begin(); owned != m_ownedBackends.end(); ++owned) {
                        if (owned->get() == injected) {
                            auto backend = std::move(*owned);
                            m_ownedBackends.erase(owned);
                            return backend;
                        }
                    }
                    return {};
                }));
    }

    for (const auto &spec : std::as_const(m_routeSpecs)) {
        if (spec.kind != WildPalms::Runtime::RouteSpec::Kind::Filtered)
            continue;
        const QString factoryId = QStringLiteral("route:") + spec.lcId;
        const auto filter = [spec]() {
            Kalburator::Shape::RecordFilter filter;
            filter.property = Kalburator::Shape::PropertyId{QStringLiteral("categories")};
            filter.op = Kalburator::Shape::RecordFilter::Op::Contains;
            filter.value = spec.categoryName;
            return filter;
        }();
        auto *hub = m_hub.get();
        definition.backendFactories.append(
            QSharedPointer<WildPalms::Runtime::PalmBackendFactory>::create(
                factoryId,
                [this, hub, spec, filter]()
                    -> std::unique_ptr<Kalburator::Sync::SyncBackendBase> {
                    if (!hub)
                        return {};
                    auto backend = std::make_unique<Kalburator::Sinks::FilteredCollectionBackend>(
                        hub, QStringLiteral("wp-hub"), spec.hubCollectionId,
                        QStringLiteral("route-") + spec.categoryName, filter, nullptr);
                    // Compatibility consumers may inspect the registry, but
                    // CollectionRuntime owns the endpoint after this return.
                    m_registry->registerBackendInstance(spec.lcId, backend.get());
                    return backend;
                }));
    }

    m_collectionRuntime = Kalburator::Runtime::CollectionRuntime::create(
        definition, errorMessage);
    if (!m_collectionRuntime)
        return false;

    m_collectionRuntime->setEventSink([this](const Kalburator::Runtime::RuntimeEvent &event) {
        using Kind = Kalburator::Runtime::RuntimeEvent::Kind;
        if (event.kind == Kind::ConflictDetected)
            Q_EMIT conflictDetected(event.conflict);
        else if (event.kind == Kind::RunProgress)
            Q_EMIT runProgress(event.progress, 100, event.message);
        else if (event.kind == Kind::RunStarted)
            Q_EMIT runLog(QStringLiteral("Runtime sync started"));
        else if (event.kind == Kind::ProviderStateChanged && !event.providerError.isEmpty())
            Q_EMIT runLog(event.providerError);
    });
    return true;
}

bool PalmRuntime::applyCollectionRuntimeTopology(QString &errorMessage)
{
    if (!m_collectionRuntime) {
        errorMessage = QStringLiteral("CollectionRuntime is not initialized");
        return false;
    }

    Kalburator::Runtime::TopologyDefinition desired;
    desired.replaceProviders = true;
    if (m_profile)
        desired.providers = m_profile->accounts();

    desired.endpoints.append({QStringLiteral("wp-hub"), QStringLiteral("hub"), {}, {}, {}, {}});
    for (auto *conduit : conduits()) {
        if (!m_device)
            continue;
        const QString id = conduit->conduitId();
        desired.endpoints.append({id, QStringLiteral("palm:") + id, {}, {},
                                  QStringLiteral("palm-device"),
                                  {QStringLiteral("palm:") + conduit->domain().toString()}});
    }
    for (const auto &spec : std::as_const(m_routeSpecs)) {
        if (spec.kind == WildPalms::Runtime::RouteSpec::Kind::Filtered)
            desired.endpoints.append({spec.lcId, QStringLiteral("route:") + spec.lcId,
                                      {}, {}, {}, {QStringLiteral("route-") + spec.categoryName}});
    }

    QSet<QString> endpointIds;
    for (const auto &endpoint : std::as_const(desired.endpoints))
        endpointIds.insert(endpoint.id);
    for (const auto &mapping : std::as_const(m_mappings)) {
        for (const auto &backendId : {mapping.sourceBackend, mapping.targetBackend}) {
            if (endpointIds.contains(backendId) || backendId.isEmpty())
                continue;
            if (m_injectedBackends.contains(backendId)) {
                desired.endpoints.append({backendId, QStringLiteral("injected:") + backendId,
                                          {}, {}, {}, {}});
                endpointIds.insert(backendId);
                continue;
            }
            const int separator = backendId.indexOf(QLatin1Char(':'));
            if (separator <= 0)
                continue;
            const QString providerId = backendId.left(separator);
            desired.endpoints.append({backendId, {}, {}, providerId, {}, {}});
            endpointIds.insert(backendId);
        }
    }
    desired.mappings = m_mappings;
    const auto result = m_collectionRuntime->applyTopology(desired);
    if (!result.committed) {
        errorMessage = result.errorMessage;
        return false;
    }
    m_collectionRuntimeTopologyReady = true;
    return true;
}

void PalmRuntime::registerPalmPlugins()
{
    // Substrate A1: fresh conduit instances from the single-source-of-truth
    // catalog (each PalmRuntime owns its own set so createPalmBackend() /
    // createConflictHandler() can hold per-runtime state). conduitId()/domain()
    // drive the manifest entries below — adding a conduit to createStockConduits
    // is the only edit needed for it to load here.
    auto conduitPlugins = WildPalms::Runtime::createStockConduits();

    // O7 (v0.57): one PluginManager + ONE loadInProcess batch, populating this
    // PalmRuntime's own m_shape (no process-global singletons). The batch holds
    // the stock domain/infra plugins (which DEFINE the canonical domains + peer
    // shapes ical/vcard4/canon/ical-vtodo) plus WP's four plugins (which REQUIRE
    // those domains and contribute the (domain,palm) peer + palm<->peer edges).
    // resolve() orders requirers after definers within the batch. Mirrors
    // PlanStan/src/app/appcontroller.cpp. DAV provider plugins are intentionally
    // omitted — registerStandardContributions() (above) already seeds the
    // CalDav/CardDav/Akonadi backend contributions. Per-instance m_shape means a
    // second PalmRuntime re-runs this into its own fresh registries (the old
    // s_globalRegistrationDone guard is no longer needed).
    Kalburator::PluginManager pluginManager(m_registry.get(), m_shape);

    static Kalburator::UniversalStoragePlugin  s_universal;
    static Kalburator::Blob::BlobPlugin        s_blob;
    static Kalburator::Note::NotePlugin        s_note;
    static Kalburator::Todo::TodoPlugin        s_todo;
    static Kalburator::Contacts::ContactsPlugin s_contacts;
    static Kalburator::Calendar::CalendarPlugin s_calendar;

    QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> items{
        { &s_universal, mkStockManifest(QStringLiteral("kalburator.universal-storage")) },
        { &s_blob,      mkStockManifest(QStringLiteral("kalburator.blob"),     {QStringLiteral("blob")}) },
        { &s_note,      mkStockManifest(QStringLiteral("kalburator.note"),     {QStringLiteral("note")}) },
        { &s_todo,      mkStockManifest(QStringLiteral("kalburator.todo"),     {QStringLiteral("todo")}) },
        { &s_contacts,  mkStockManifest(QStringLiteral("kalburator.contacts"), {QStringLiteral("contacts")}) },
        { &s_calendar,  mkStockManifest(QStringLiteral("kalburator.calendar"), {QStringLiteral("calendar")}) },
    };
    // Substrate A1: WP conduit manifests derived from each descriptor —
    // mkPalmManifest("wildpalms.<conduitId>", <domain>). Reproduces the old
    // static table (memo: id "memo", domain "note").
    for (const auto &c : conduitPlugins)
        items.append({ c.get(),
            mkPalmManifest(QStringLiteral("wildpalms.") + c->conduitId(),
                           c->domain().toString()) });

    if (!pluginManager.loadInProcess(items)) {
        qWarning() << "[PalmRuntime] plugin load rejected:"
                   << pluginManager.rejected().size();
        return;
    }

    m_enabledPluginIds.clear();
    for (const auto &item : items)
        m_enabledPluginIds.append(item.second.id);

    for (auto &c : conduitPlugins)
        m_palmPlugins.push_back(std::move(c));
}

QList<WildPalms::Plugins::PimPlugin*> PalmRuntime::conduits() const
{
    // Substrate A1: one dynamic_cast to the PimPlugin base replaces the five
    // per-concrete-type cast chains that used to live throughout this class.
    // Non-PIM plugins (e.g. plucker) inherit Kalburator::Plugin directly and
    // cast to nullptr — they are skipped naturally.
    QList<WildPalms::Plugins::PimPlugin*> out;
    for (const auto &p : m_palmPlugins)
        if (auto *c = dynamic_cast<WildPalms::Plugins::PimPlugin*>(p.get()))
            out.append(c);
    return out;
}

bool PalmRuntime::isPalmConduitBackendId(const QString &backendId) const
{
    for (auto *c : conduits())
        if (c->conduitId() == backendId) return true;
    return false;
}

void PalmRuntime::appendConduitForTest(
    std::unique_ptr<WildPalms::Plugins::PimPlugin> conduit)
{
    m_palmPlugins.push_back(std::move(conduit));
    ensureHubCollections();   // idempotent: createCollection on an existing
                              // id only re-registers the same shape (no dup row).
}

void PalmRuntime::ensureHubCollections()
{
    using Kalburator::Shape::Shape;
    using Kalburator::Shape::DomainId;
    using Kalburator::Shape::EncodingId;

    // Substrate A1: one hub collection per conduit descriptor's domain
    // (calendar, contacts, note, todo). Must run after registerPalmPlugins()
    // populates m_palmPlugins — it does (ctor calls them in that order).
    for (auto *c : conduits()) {
        const QString dom = c->domain().toString();
        Kalburator::Sync::CollectionInfo info;
        info.id   = dom;
        info.name = dom;
        info.type = dom;
        m_hub->createCollection(
            info,
            Shape{ DomainId{dom}, EncodingId{QStringLiteral("canon")} });
    }
}

void PalmRuntime::buildRouteLogicalCalendars()
{
    using Kalburator::Sync::LogicalCalendar;
    using Kalburator::Sync::CalendarBackendBinding;
    using Kalburator::Sync::BackendRole;

    // Substrate A3: translate each persisted row against the conduit
    // descriptors. Names-first category rows ("palm:<domain>/name:<X>") carry
    // their device-binding state in t.status — recorded per mapping for the
    // UI; the route is still materialized whenever a spec is produced (filtering
    // works by name even before the device slot is bound).
    m_routeStatuses.clear();
    m_routeSpecs.clear();
    const auto cs = conduits();
    QList<LogicalCalendar> baseCalendars;
    for (auto *conduit : cs) {
        if (!m_registry->backendInstance(conduit->conduitId()))
            continue;
        const QString domain = conduit->domain().toString();
        LogicalCalendar lc;
        lc.id = QStringLiteral("wp-") + domain;
        lc.domain = Kalburator::Shape::DomainId{domain};
        lc.displayName = domain;
        lc.syncEnabled = true;
        CalendarBackendBinding hub;
        hub.backendId = QStringLiteral("wp-hub");
        hub.calendarId = domain;
        hub.role = BackendRole::Primary;
        lc.bindings.append(hub);
        CalendarBackendBinding palm;
        palm.backendId = conduit->conduitId();
        palm.calendarId = QStringLiteral("palm:") + domain;
        palm.role = BackendRole::Sync1;
        palm.syncOrder = 1;
        lc.bindings.append(palm);
        baseCalendars.append(lc);
    }
    auto translated = Kalburator::Sync::generateMappings(
        baseCalendars, Kalburator::Sync::SyncTopology::Star);
    for (const auto &persisted : m_mappings) {
        const auto t = WildPalms::Runtime::translateRouteSpec(persisted, cs);
        if (t.status != WildPalms::Runtime::RouteStatus::NotARoute)
            m_routeStatuses.insert(persisted.id, t.status);
        if (!t.spec) continue;
        const auto &s = *t.spec;
        m_routeSpecs.insert(s.lcId, s);
        auto route = persisted;
        route.id = s.kind == WildPalms::Runtime::RouteSpec::Kind::Filtered
            ? s.lcId
            : QStringLiteral("wp-route-") + persisted.id;
        if (s.kind == WildPalms::Runtime::RouteSpec::Kind::Filtered) {
            route.sourceBackend = s.lcId;
            route.sourceCalendar = QStringLiteral("route-") + s.categoryName;
        } else {
            route.sourceBackend = QStringLiteral("wp-hub");
            route.sourceCalendar = s.hubCollectionId;
        }
        translated.append(route);
    }
    m_mappings = std::move(translated);
    Q_EMIT routeStatusesChanged();
}

void PalmRuntime::connectDevice(const QStringList &devicePaths)
{
    if (!m_device) {
        m_device = std::make_unique<PalmDeviceAccess>(this);

        QObject::connect(m_device.get(), &PalmDeviceAccess::connectionStarted,
                this, &PalmRuntime::connectionStarted);

        QObject::connect(m_device.get(), &PalmDeviceAccess::connectionComplete,
                this, [this](bool ok, const QString &err) {
                    Q_EMIT connectionComplete(ok, err);
                    if (ok) {
                        finishConnect();
                    }
                });

        QObject::connect(m_device.get(), &PalmDeviceAccess::deviceDisconnected,
                this, &PalmRuntime::deviceDisconnected);

        QObject::connect(m_device.get(), &PalmDeviceAccess::logMessage,
                this, &PalmRuntime::logMessage);
    }

    m_device->connectDevice(devicePaths);
}

void PalmRuntime::cancelConnect()
{
    if (m_device) m_device->cancelConnect();
}

void PalmRuntime::cancelSync()
{
    if (m_collectionRuntimeTopologyReady && m_collectionRuntime) {
        m_collectionRuntime->cancel();
    }
}

// P4 Investigation finding (2026-05-27):
// DAV mappings ARE persisted in mappings.conf and ARE loaded into m_mappings by
// loadMappingsFromProfile() at the top of finishConnect(). This is Branch B, not A:
// the mapping rows reference CalDAV backend IDs (e.g. "<uuid>:TBS") that are only
// registered into BackendRegistry after AccountController's async ProviderManager
// connectAll() completes — which happens long after finishConnect() emits readyForSync.
// Result: the first auto-sync runs with the mapping list intact but the DAV SyncBackend
// pointers unresolvable, so the engine silently skips those mappings and only syncs
// the rawfiles fallbacks. Fix = gate readyForSync (or auto-sync) until all provider
// connections have completed (i.e. until BackendRegistry holds the DAV backend entries).
void PalmRuntime::finishConnect()
{
    if (!m_device) return;

    // TODO(C-remote): in sub-project C the Star-mapping block below REPLACES
    // m_mappings unconditionally, so the user's persisted remote (CalDAV/etc.)
    // mappings are intentionally superseded by Palm<->hub for now. The next
    // sub-project reintroduces them as hub<->remote bindings; this load call is
    // kept as the seam where that merge will happen.
    loadMappingsFromProfile();

    // Substrate A3: reconcile desired category names against each device table
    // BEFORE the backends read AppInfo below, writing new slots when claimed —
    // so createPalmBackend's AppInfo read sees the final table. Desired set =
    // the profile's persisted desired names ∪ the category names referenced by
    // enabled route rows ("palm:<domain>/name:<X>").
    m_categoryNoFreeSlot.clear();
    for (auto *c : conduits()) {
        if (!c->supportsCategories() || !m_profile) continue;
        const QString db = c->primaryDbName();
        QStringList desired = m_profile->desiredCategoryNames(db);
        const QString prefix = QStringLiteral("palm:")
            + c->domain().toString() + QStringLiteral("/name:");
        for (const auto &m : m_mappings) {
            if (!m.enabled || m.sourceBackend != c->conduitId()) continue;
            if (m.sourceCalendar.startsWith(prefix)) {
                const QString n = m.sourceCalendar.mid(prefix.size());
                if (!n.isEmpty() && !desired.contains(n, Qt::CaseInsensitive))
                    desired.append(n);
            }
        }
        if (desired.isEmpty()) continue;

        const QByteArray block = m_device->readAppBlock(db);
        if (block.isEmpty()) continue;   // no AppInfo (e.g. fake device) -> no-op
        const auto r = WildPalms::Runtime::reconcileCategories(block, desired);
        if (!r.updatedAppInfoBlock.isEmpty()) {
            if (!m_device->writeAppBlock(db, r.updatedAppInfoBlock)) {
                qWarning() << "[PalmRuntime] AppInfo write failed for" << db;
                // Shakedown F8: reconciliation failures were console-only.
                Q_EMIT logMessage(QStringLiteral(
                    "Category write failed for %1 — category slots may be "
                    "missing on the device").arg(db));
            } else {
                qDebug() << "[PalmRuntime] created" << r.bound.size()
                         << "category binding(s) on" << db;
            }
        }
        if (!r.noFreeSlot.isEmpty()) {
            m_categoryNoFreeSlot.insert(db, r.noFreeSlot);
            qWarning() << "[PalmRuntime] no free category slot on" << db
                       << "for" << r.noFreeSlot;
            Q_EMIT logMessage(QStringLiteral(
                "Device table %1 has no free category slot for: %2")
                    .arg(db, r.noFreeSlot.join(QStringLiteral(", "))));
        }
    }

    // Substrate A1: enumerate conduit descriptors instead of casting to each
    // concrete plugin type. createPalmBackend populates the plugin's internal
    // CategoryMappingStore from the live AppInfo block; categorySlotNames()
    // (read immediately after, same iteration) captures the snapshot for the
    // F.3 Profile write-back.
    for (auto *c : conduits()) {
        const QString id = c->conduitId();
        std::unique_ptr<Kalburator::Sync::SyncBackendBase> ownedBackend =
            c->createPalmBackend(m_device.get());
        if (!ownedBackend) {
            qWarning() << "[PalmRuntime::finishConnect] Plugin" << id
                       << "returned null backend";
            continue;
        }

        if (m_profile && c->supportsCategories()) {
            const QStringList slotNames = c->categorySlotNames();
            if (slotNames.size() == 16)
                m_profile->setCategorySlotNames(c->primaryDbName(), slotNames);
        }

        m_registry->registerBackendInstance(id, ownedBackend.get());
        // T7: inject the per-profile revision store into any backend that
        // inherits PalmChangeDetection. Returns nullptr today (no backend
        // inherits it yet — Tasks 8–11); the cast is a no-op until then.
        if (auto *cd = dynamic_cast<WildPalms::PalmSync::PalmChangeDetection*>(ownedBackend.get()))
            cd->setPalmRevisionStore(m_palmRevisionStore.get());
        m_ownedBackends.push_back(std::move(ownedBackend));

        qDebug() << "[PalmRuntime::finishConnect] Registered backend plugin:" << id;
    }

    // Route rows are translated into runtime-owned specs. CollectionRuntime
    // materializes the physical Palm, hub, and filtered-route endpoints once
    // the complete desired topology is applied below.
    buildRouteLogicalCalendars();

    QString runtimeError;
    if (initializeCollectionRuntime(runtimeError)
        && applyCollectionRuntimeTopology(runtimeError)) {
        qDebug() << "[PalmRuntime] CollectionRuntime topology committed";
    } else {
        qWarning() << "[PalmRuntime] CollectionRuntime cutover preparation failed:"
                   << runtimeError;
    }

    Q_EMIT deviceConnected();
    Q_EMIT readyForSync();
}

void PalmRuntime::reloadMappings(const QJsonArray &json)
{
    m_mappings.clear();
    for (const auto &v : json) {
        if (!v.isObject())
            continue;
        m_mappings.append(Kalburator::Sync::syncMappingFromJson(v.toObject()));
    }
}

void PalmRuntime::disconnectDevice() {
    if (m_running) {
        // Cannot disconnect while a sync/backup/restore is in flight — the
        // running lambda holds a raw KPilotLink* captured from m_device->link().
        // Tearing down m_device here would leave that pointer dangling.
        // The UI should disable the disconnect action while isRunning(); this
        // is the safety net for any caller that bypasses that guard.
        qWarning() << "[PalmRuntime] disconnectDevice() ignored — run in flight";
        return;
    }
    if (m_device) m_device->disconnectDevice();   // emits deviceDisconnected via forward
    m_device.reset();
}

bool PalmRuntime::isDeviceConnected() const {
    return m_device != nullptr;
}

KPilotDeviceLink *PalmRuntime::deviceLink() const {
    if (!m_device) return nullptr;
    return qobject_cast<KPilotDeviceLink*>(m_device->link());
}

void PalmRuntime::setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess> device) {
    m_device = std::move(device);
    // K.8b T6: palm backends are now registered in finishConnect(), not via
    // registerPluginForTest().  Invoke finishConnect() here so tests that
    // inject a pre-built device still get their backends wired up.
    // finishConnect() already emits deviceConnected() at its end — no second
    // emit here (that would be a double-emission and confuse any listeners
    // that gate on that signal arriving exactly once).
    finishConnect();
}

// K.8b T13: registerPluginForTest overloads removed (signatures deleted in
// palmruntime.h). Bodies were no-ops since K.8b T6; no live callers.

void PalmRuntime::registerBackendInstanceForTest(const QString &id,
                                                  std::unique_ptr<Kalburator::Sync::SyncBackendBase> backend)
{
    if (!backend) return;
    m_registry->registerBackendInstance(id, backend.get());
    m_injectedBackends.insert(id, backend.get());
    m_ownedBackends.push_back(std::move(backend));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    // Injected backends use the same runtime topology path as connected
    // production. The external Palm lease is simply idle when no device was
    // supplied, so these tests do not need a second consumer engine graph.
    QString error;
    if (initializeCollectionRuntime(error) && applyCollectionRuntimeTopology(error))
        return;
    qWarning() << "[PalmRuntime] injected topology was not committed:" << error;
}

void PalmRuntime::setProfile(Profile *profile)
{
    m_profile = profile;
    QString runtimeError;
    if (!m_collectionRuntime)
        initializeCollectionRuntime(runtimeError);
    // Make saved mappings available immediately (before device connect), so
    // palmMappings() and any pre-connect logic reflect the persisted set.
    // finishConnect() reloads again to stay authoritative across reconnects.
    loadMappingsFromProfile();
}

void PalmRuntime::loadMappingsFromProfile()
{
    if (!m_profile) return;   // test/no-profile: keep injected mappings as-is
    m_mappings.clear();
    const QJsonArray saved = m_profile->syncMappingsJson();
    for (const auto &v : saved) {
        if (v.isObject())
            m_mappings.append(Kalburator::Sync::syncMappingFromJson(v.toObject()));
    }
}

void PalmRuntime::setConflictHandler(
    Kalburator::Conflict::ConflictHandler *handler)
{
    m_conflictHandler = handler;
}

void PalmRuntime::setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard)
{
    m_massDeleteGuard = guard;
    if (m_collectionRuntime) {
        Kalburator::Runtime::RuntimePolicy policy;
        policy.skipUnchangedMappings = true;
        policy.maxConcurrentMappings = 1;
        policy.confirmMassDelete = [this](const QString &mappingId,
                                          const QString &targetBackendId,
                                          int proposedDeletes,
                                          int baselineCount) {
            if (!m_massDeleteGuard)
                return true;
            return m_massDeleteGuard->confirmMassDelete(mappingId, targetBackendId,
                                                        proposedDeletes, baselineCount);
        };
        QString error;
        if (!m_collectionRuntime->updatePolicy(policy, error))
            qWarning() << "[PalmRuntime] runtime mass-delete policy update failed:" << error;
        return;
    }
}

Kalburator::Conflict::ConflictHandler *
PalmRuntime::conflictHandlerForTest() const
{
    return m_conflictHandler;
}

Kalburator::Sync::SyncConflictStore *
PalmRuntime::syncConflictStore() const
{
    auto *self = const_cast<PalmRuntime *>(this);
    if (!self->m_compatConflictStore) {
        self->m_compatConflictStore = std::make_unique<Kalburator::Sync::SyncConflictStore>(
            QDir(self->m_profilePath).filePath(QStringLiteral(".state/sync-conflicts.db")));
    }
    return self->m_compatConflictStore.get();
}

Kalburator::Conflict::ConflictRecord
PalmRuntime::toConflictRecord(const Kalburator::Sync::ConflictInfo &info)
{
    Kalburator::Conflict::ConflictRecord rec;
    rec.conflictId  = info.conflictId.isEmpty()
                      ? Kalburator::Conflict::ConflictRecord::generateId()
                      : info.conflictId;
    rec.conduitId     = info.mappingId;
    rec.syncSessionId = info.mappingId;
    rec.detectedAt    = info.detectedAt.isValid()
                        ? info.detectedAt
                        : QDateTime::currentDateTime();
    rec.type        = Kalburator::Conflict::ConflictType::BothModified;

    rec.source.id            = info.sourceId;
    rec.source.description   = info.sourceDescription;
    rec.source.content       = info.sourceIcalData.toUtf8();
    rec.source.contentType   = QStringLiteral("text/calendar");
    rec.source.lastModified  = info.sourceModified;

    rec.target.id            = info.targetId;
    rec.target.description   = info.targetDescription;
    rec.target.content       = info.targetIcalData.toUtf8();
    rec.target.contentType   = QStringLiteral("text/calendar");
    rec.target.lastModified  = info.targetModified;

    return rec;
}

QList<QString> PalmRuntime::enabledPluginIds() const {
    return m_enabledPluginIds;
}

QList<Kalburator::Sync::SyncMapping> PalmRuntime::palmMappings() const {
    return m_mappings;
}

bool PalmRuntime::isPalmDirectMapping(
    const Kalburator::Sync::SyncMapping &m) const
{
    return isPalmConduitBackendId(m.targetBackend);
}

QList<QString> PalmRuntime::palmDirectMappingsForDomain(
    const QString &domain) const
{
    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (!m.enabled) continue;
        if (m.targetBackend != domain) continue;
        if (!isPalmDirectMapping(m)) continue;
        ids.append(m.id);
    }
    return ids;
}

void PalmRuntime::resolveMappingIdentity(const QString &mappingId,
                                         QString &outLabel,
                                         QString &outIconName) const
{
    outLabel = mappingId;
    outIconName = QStringLiteral("view-list-details");
    // Substrate A1: resolve identity from the conduit descriptors — each
    // descriptor owns its display name + theme icon, subsuming the old kIcons
    // hash and the per-concrete-type cast chain. (The old hash's "plucker"
    // entry was dead: plucker is not a loaded Palm conduit.)
    for (const auto &m : m_mappings) {
        if (m.id != mappingId)
            continue;
        for (auto *c : conduits()) {
            if (c->conduitId() == m.sourceBackend) {
                outLabel = c->conduitDisplayName();
                outIconName = c->conduitIconName();
                break;
            }
        }
        return;
    }
}

QVector<PalmRuntime::ConduitDescriptor> PalmRuntime::conduitDescriptors() const
{
    QVector<ConduitDescriptor> out;
    for (const auto &m : m_mappings) {
        if (!m.enabled)
            continue;
        ConduitDescriptor d;
        d.mappingId = m.id;
        resolveMappingIdentity(m.id, d.label, d.iconName);
        out.append(d);
    }
    return out;
}

// F3/F11 shakedown remediation helpers. Every public entry that emits
// runStarted must either dispatch (the engine watchers finalize with exactly
// one runFinished) or emit its own matching runFinished before returning —
// a runStarted with no runFinished leaves the dashboard in its Syncing state
// forever.
static PalmRunResult makeRejectedResult(const QString &message) {
    PalmRunResult r;
    r.success      = false;
    r.errorMessage = message;
    r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
    return r;
}

static QFuture<PalmRunResult> makeReadyFuture(PalmRunResult r) {
    return QtFuture::makeReadyValueFuture(std::move(r));
}

// Re-entrancy backstop shared by the public entry points (hotSync/fullSync/
// runMirror/clobberSync). The UI guards on isSyncRunning() first; this keeps
// programmatic callers from starting a second active watcher.
static bool syncAlreadyRunning_(const PalmRuntime *self) {
    if (!self->isSyncRunning())
        return false;
    qWarning() << "[PalmRuntime] sync request ignored: a sync is already "
                  "in flight — rejecting the re-entrant call";
    return true;
}

QFuture<PalmRunResult> PalmRuntime::runCollectionRuntime(
    const Kalburator::Runtime::RunRequest &request)
{
    if (!m_collectionRuntime || !m_collectionRuntimeTopologyReady)
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("CollectionRuntime topology is not ready")));

    return m_collectionRuntime->run(request).then(
        [this](const Kalburator::Runtime::RunResult &result) {
            PalmRunResult out;
            out.startTime = QDateTime::currentDateTimeUtc();
            out.endTime = out.startTime;
            out.success = result.success;
            out.cancelled = result.cancelled;
            out.errorMessage = result.errorMessage;
            if (!out.success)
                qWarning() << "[PalmRuntime] CollectionRuntime run failed:" << out.errorMessage;
            for (const auto &mapping : result.mappings) {
                PalmRunResult::PluginStats stats;
                stats.created = mapping.targetStats.created;
                stats.updated = mapping.targetStats.updated;
                stats.deleted = mapping.targetStats.deleted;
                stats.unchanged = mapping.targetStats.unchanged;
                stats.errors = mapping.success ? 0 : 1;
                QString statsKey = mapping.mappingId;
                for (const auto &configured : m_mappings) {
                    if (configured.id == mapping.mappingId) {
                        statsKey = configured.targetBackend;
                        break;
                    }
                }
                out.perPluginStats.insert(statsKey, stats);
                if (!mapping.success && !mapping.errorMessage.isEmpty())
                    Q_EMIT runLog(QStringLiteral("Mapping failed: %1")
                                      .arg(mapping.errorMessage));
                Q_EMIT mappingSyncFinished(mapping.mappingId, 0, 0, 0,
                                           mapping.success && !mapping.cancelled);
            }
            Q_EMIT runFinished(out);
            Q_EMIT syncCompleted();
            return out;
        });
}

QFuture<PalmRunResult> PalmRuntime::hotSync() {
    if (syncAlreadyRunning_(this))
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));

    Q_EMIT runStarted(QStringLiteral("HotSync"));
    if (m_mappings.isEmpty()) {
        // F11: this early return used to strand the runStarted — the
        // dashboard spun in "Syncing…" forever. Emit the matching pair.
        const auto r = makeRejectedResult(QStringLiteral(
            "No sync targets configured — nothing to sync"));
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return makeReadyFuture(r);
    }
    if (m_collectionRuntimeTopologyReady) {
        Kalburator::Runtime::RunRequest request;
        request.selection = Kalburator::Runtime::RunSelection::allEnabled();
        request.intent = Kalburator::Runtime::RunIntent::Normal;
        return runCollectionRuntime(request);
    }
    const auto r = makeRejectedResult(QStringLiteral(
        "Sync topology is not committed"));
    Q_EMIT runFinished(r);
    Q_EMIT syncCompleted();
    return makeReadyFuture(r);
}

QFuture<PalmRunResult> PalmRuntime::fullSync()
{
    if (syncAlreadyRunning_(this))
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));

    Q_EMIT runStarted(QStringLiteral("FullSync"));
    if (m_collectionRuntimeTopologyReady) {
        Kalburator::Runtime::RunRequest request;
        request.selection = Kalburator::Runtime::RunSelection::allEnabled();
        request.intent = Kalburator::Runtime::RunIntent::FullRediff;
        return runCollectionRuntime(request);
    }
    const auto r = makeRejectedResult(QStringLiteral(
        "Sync topology is not committed"));
    Q_EMIT runFinished(r);
    Q_EMIT syncCompleted();
    return makeReadyFuture(r);
}

QFuture<PalmRunResult> PalmRuntime::runMirror(MirrorDir dir, const QString &modeLabel)
{
    if (syncAlreadyRunning_(this))
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));

    Q_EMIT runStarted(modeLabel);

    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (m.enabled) ids.append(m.id);
    }
    if (ids.isEmpty()) {
        // F11: emit the matching runStarted/runFinished pair (see hotSync).
        const auto r = makeRejectedResult(QStringLiteral(
            "No sync targets configured — nothing to sync"));
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return makeReadyFuture(r);
    }

    if (m_collectionRuntimeTopologyReady) {
        Kalburator::Runtime::RunRequest request;
        request.selection = Kalburator::Runtime::RunSelection::allEnabled();
        request.intent = Kalburator::Runtime::RunIntent::Mirror;
        request.mirrorDirection = dir == MirrorDir::PalmToPC
            ? Kalburator::Runtime::MirrorDirection::SourceToTarget
            : Kalburator::Runtime::MirrorDirection::TargetToSource;
        return runCollectionRuntime(request);
    }

    const auto notReady = makeRejectedResult(QStringLiteral(
        "Sync topology is not committed"));
    Q_EMIT runFinished(notReady);
    Q_EMIT syncCompleted();
    return makeReadyFuture(notReady);

}

QFuture<PalmRunResult> PalmRuntime::copyPalmToPC()
{
    return runMirror(MirrorDir::PalmToPC, QStringLiteral("CopyPalmToPC"));
}

QFuture<PalmRunResult> PalmRuntime::clobberSync(const QList<QString> &mappingIds)
{
    const auto kLabel = QStringLiteral("ClobberSync");

    if (syncAlreadyRunning_(this))
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));

    Q_EMIT runStarted(kLabel);

    if (mappingIds.isEmpty()) {
        // F11: emit the matching runStarted/runFinished pair (see hotSync).
        const auto r = makeRejectedResult(QStringLiteral(
            "No Palm databases selected — nothing to clobber"));
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return makeReadyFuture(r);
    }

    if (m_collectionRuntimeTopologyReady) {
        Kalburator::Runtime::RunRequest request;
        request.selection = Kalburator::Runtime::RunSelection::exactSet(mappingIds);
        request.intent = Kalburator::Runtime::RunIntent::DestructiveRebuild;
        return runCollectionRuntime(request);
    }

    const auto notReady = makeRejectedResult(QStringLiteral(
        "Sync topology is not committed"));
    Q_EMIT runFinished(notReady);
    Q_EMIT syncCompleted();
    return makeReadyFuture(notReady);

}

QFuture<PalmRunResult> PalmRuntime::backup()
{
    Q_EMIT runStarted(QStringLiteral("Backup"));

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("backup: no device connected");
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return QtFuture::makeReadyValueFuture(r);
    }

    // Pause the TickleWorker BEFORE the first DLP call (listDatabases).
    // dlp_ReadDBList() iterates 100+ databases and takes several seconds;
    // the 5-second tickle timer fires mid-loop and corrupts the DLP session.
    // pauseTickle() uses BlockingQueuedConnection so the tickle thread is
    // fully stopped before we proceed.
    m_device->pauseTickle();

    // Snapshot the DB list on the calling (main) thread — KPilotLink is not
    // thread-safe; all link calls must happen before we hand off to the pool.
    const QStringList databases = link->listDatabases();
    const QString backupDir = m_backupRoot;

    return QtConcurrent::run([this, databases, backupDir, link]() -> PalmRunResult {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        QDir().mkpath(backupDir);

        auto &stats = r.perPluginStats[QStringLiteral("device")];
        for (const QString &dbName : databases) {
            const QString destPath = QDir(backupDir).filePath(
                sanitizeForFilesystem(dbName) + QStringLiteral(".pdb"));
            if (link->retrieveDatabase(dbName, destPath)) {
                ++stats.created;
            } else {
                // Hard failure — the DLP session state may be corrupted.
                // Stop the loop rather than issuing further DLP calls.
                ++stats.errors;
                r.success = false;
                break;
            }
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        QMetaObject::invokeMethod(this, [this, r]() {
            if (m_device) m_device->resumeTickle();
            Q_EMIT runFinished(r);
            Q_EMIT syncCompleted();
        });
        return r;
    });
}

QFuture<PalmRunResult> PalmRuntime::restore()
{
    Q_EMIT runStarted(QStringLiteral("Restore"));

    KPilotLink *link = m_device ? m_device->link() : nullptr;
    if (!link) {
        PalmRunResult r;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        r.success = false;
        r.errorMessage = QStringLiteral("restore: no device connected");
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return QtFuture::makeReadyValueFuture(r);
    }

    const QString backupDir = m_backupRoot;

    m_device->pauseTickle();

    return QtConcurrent::run([this, backupDir, link]() -> PalmRunResult {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = true;

        const QStringList files = QDir(backupDir).entryList(
            QStringList{QStringLiteral("*.pdb"), QStringLiteral("*.prc")},
            QDir::Files);

        auto &stats = r.perPluginStats[QStringLiteral("device")];
        for (const QString &fileName : files) {
            const QString filePath = QDir(backupDir).filePath(fileName);
            if (link->installFile(filePath)) {
                ++stats.created;
            } else {
                ++stats.errors;
                r.success = false;
                break;
            }
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        QMetaObject::invokeMethod(this, [this, r]() {
            if (m_device) m_device->resumeTickle();
            Q_EMIT runFinished(r);
            Q_EMIT syncCompleted();
        });
        return r;
    });
}

// Compatibility bridge for the legacy conflict-store test seam. Connected
// production resolutions go through CollectionRuntime above.
int PalmRuntime::applyConflictResolutions(
    const QList<Kalburator::Conflict::ConflictRecord> &resolved)
{
    if (m_collectionRuntime && m_collectionRuntimeTopologyReady) {
        int applied = 0;
        for (const auto &rec : resolved) {
            Kalburator::Sync::ConflictResolution resolution;
            switch (rec.decision) {
            case Kalburator::Conflict::ConflictDecision::UseSource:
                resolution = Kalburator::Sync::ConflictResolution::SourceWins; break;
            case Kalburator::Conflict::ConflictDecision::UseTarget:
                resolution = Kalburator::Sync::ConflictResolution::TargetWins; break;
            case Kalburator::Conflict::ConflictDecision::UseBoth:
                resolution = Kalburator::Sync::ConflictResolution::Duplicate; break;
            case Kalburator::Conflict::ConflictDecision::Merge:
                resolution = Kalburator::Sync::ConflictResolution::CustomMerge; break;
            case Kalburator::Conflict::ConflictDecision::Skip:
                resolution = Kalburator::Sync::ConflictResolution::Skip; break;
            default:
                continue;
            }
            if (m_collectionRuntime->resolveConflict(
                    rec.conflictId, resolution, QString::fromUtf8(rec.mergedContent)))
                ++applied;
            else
                qWarning() << "[PalmRuntime] runtime conflict resolution failed:"
                           << rec.conflictId;
        }
        return applied;
    }
    if (!m_compatConflictStore)
        return 0;
    int applied = 0;
    for (const auto &rec : resolved) {
        Kalburator::Sync::ConflictResolution res;
        switch (rec.decision) {
        case Kalburator::Conflict::ConflictDecision::UseSource:
            res = Kalburator::Sync::ConflictResolution::SourceWins; break;
        case Kalburator::Conflict::ConflictDecision::UseTarget:
            res = Kalburator::Sync::ConflictResolution::TargetWins; break;
        case Kalburator::Conflict::ConflictDecision::UseBoth:
            res = Kalburator::Sync::ConflictResolution::Duplicate; break;
        case Kalburator::Conflict::ConflictDecision::Merge:
            // O52 caveat: the merged payload is not persisted, so a
            // replayed merge falls back to the engine's automatic merger.
            res = Kalburator::Sync::ConflictResolution::CustomMerge; break;
        case Kalburator::Conflict::ConflictDecision::Skip:
            // Engine-side Skip means "leave unchanged" — recording it moves
            // the row out of unresolved so it stops re-presenting.
            res = Kalburator::Sync::ConflictResolution::Skip; break;
        default:
            continue;   // DeleteBoth has no persisted counterpart yet
        }
        m_compatConflictStore->resolveConflict(rec.conflictId, res);
        ++applied;
    }
    return applied;
}

}  // namespace WildPalms::Runtime
