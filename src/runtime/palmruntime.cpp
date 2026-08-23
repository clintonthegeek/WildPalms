#include "palmruntime.h"
#include "palmticklephase.h"
#include "palmdeviceaccess.h"
#include "palm/kpilotlink.h"
#include "palm/kpilotdevicelink.h"

#include <QPromise>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QtConcurrent>

#include "backendregistry.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "conflicthandlerregistry.h"
#include "syncbackend.h"
#include "synctypes.h"
#include "syncconflictstore.h"
#include "collectioninfo.h"
#include "conflictrecord.h"
#include <isynchost.h>
#include <baselinestore.h>
#include <isyncconfigstore.h>
#include <imassdeleteguard.h>
#include "shape.h"
// K.8b T13: ibackendplugin_v2.h include removed — V2 plugin ABI deleted.
#include "palm/device/pilotlinkpalmdatabaseaccess.h"

// O7: stock domain/infra plugins, loaded in one batch with WP's plugins
// (mirrors PlanStan's composition root). DAV provider plugins are omitted —
// registerStandardContributions() seeds those backend contributions.
#include <universalstorageplugin.h>
#include <blobplugin.h>
#include <noteplugin.h>
#include <todoplugin.h>
#include <contactsplugin.h>
#include <calendarplugin.h>

// K.8b T6: in-process plugin loading via PluginManager.
// Kalburator::Sync exposes src/plugin/ on its PUBLIC include path,
// so headers are reachable without a path prefix.
#include "pluginmanager.h"
#include "manifest.h"
#include "stock_plugins.h"
#include "domainregistry.h"
#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"
#include "plugins/pimplugin.h"

#include "profile.h"

#include <genericsqlitebackend.h>
#include <logicalcalendar.h>
#include <syncmappinggenerator.h>

#include <filteredcollectionbackend.h>
#include <recordfilter.h>

#include "routemapping.h"
#include "conduitcatalog.h"
#include "categoryreconciler.h"
#include "palm/sync/palmchangedetection.h"

#include "standardcontributions.h"

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

// ──────────────────────────────────────────────────────────────────────────────
// PalmSyncHost
// Minimal ISyncHost backed by BackendRegistry. backendById()/backends() are
// inherited from the v0.69 registry-backed base defaults (Plan 8 step 1),
// which match the overrides this class carried before byte-for-byte.
// ──────────────────────────────────────────────────────────────────────────────
class PalmSyncHost final : public Kalburator::Sync::ISyncHost {
public:
    explicit PalmSyncHost(Kalburator::Sync::BackendRegistry *registry) {
        setBackendRegistry(registry);
    }
    Kalburator::Sync::ISyncConfigStore* configStore() override { return nullptr; }
};

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

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
    , m_backupRoot(QDir(profilePath).filePath(QStringLiteral("backup")))
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
    , m_baselineStore(std::make_unique<Kalburator::Storage::BaselineStore>(
          QDir(profilePath).filePath(QStringLiteral(".state/.wildpalms-blob-baselines.db"))))
{
    qRegisterMetaType<PalmRunResult>();

    // Register provider contributions into this runtime's local registry.
    // ProviderManager no longer auto-registers these (K.8a T6); the
    // application layer is responsible for seeding contributions.
    // F.1c.1 T2: the same registration is needed for KF6MainWindow's
    // app-level registry (used by NewProfileWizard for pre-profile
    // discovery), so the calls live in a shared free function.
    WildPalms::Runtime::registerStandardContributions(m_registry.get());

    // C Task 2: stand up the per-profile SQLite hub. The .state/ dir already
    // exists because BaselineStore uses it (.state/.wildpalms-blob-baselines.db).
    m_hub = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(
        QDir(profilePath).filePath(QStringLiteral(".state/hub.db")));

    // T7: per-profile Palm revision token store. .state/ dir already exists.
    m_palmRevisionStore = std::make_unique<WildPalms::PalmSync::PalmRevisionStore>(
        QDir(profilePath).filePath(QStringLiteral(".state/palm-revisions.ini")));
    m_registry->registerBackendInstance(QStringLiteral("wp-hub"), m_hub.get());

    m_syncHost = std::make_unique<PalmSyncHost>(m_registry.get());
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get(), m_shape);
    m_engine->setBaselineStore(m_baselineStore.get());
    // Shakedown F10: attach the per-profile conflict store. Without it the
    // engine's deferred conflicts were never persisted and
    // rehydratePendingResolutions() early-returned — UI resolutions could
    // never replay on the next sync.
    m_engineConflictStore =
        std::make_unique<Kalburator::Sync::SyncConflictStore>(
            QDir(profilePath).filePath(QStringLiteral(".state/sync-conflicts.db")));
    m_engine->setSyncConflictStore(m_engineConflictStore.get());
    // No-op today (handler set after construction), but keeps this consistent
    // with the re-install pattern required at every engine-construction site.
    if (m_conflictHandler)
        m_engine->conflictRegistry()->setDefaultHandler(m_conflictHandler);

    QObject::connect(m_engine.get(),
                     &Kalburator::Sync::SyncEngine::conflictDetected,
                     this, &PalmRuntime::conflictDetected);

    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::syncStarted,
                     this, [this](const QString &mappingId) {
        m_activeMappingId = mappingId;
        QString label, icon;
        resolveMappingIdentity(mappingId, label, icon);

        // P2: Determine whether Palm is source, target, or both in this
        // mapping. Palm backend IDs are the conduit descriptors' conduitId()
        // values ("calendar", "contacts", "memo", "todo").
        QSet<QString> palmIds;
        for (auto *c : conduits())
            palmIds.insert(c->conduitId());
        m_currentPalmIsSource = false;
        m_currentPalmIsTarget = false;
        for (const auto &m : m_mappings) {
            if (m.id == mappingId) {
                m_currentPalmIsSource = palmIds.contains(m.sourceBackend);
                m_currentPalmIsTarget = palmIds.contains(m.targetBackend);
                break;
            }
        }

        Q_EMIT mappingSyncStarted(mappingId, label, icon);
    });

    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::phaseChanged,
                     this, [this](Kalburator::Engine::SyncEngine::SyncPhase phase) {
        // P2: Pause tickle only during phases that issue DLP calls to the
        // Palm device. CalDAV / network fetch phases keep the tickle alive
        // so the Palm doesn't think the connection dropped.
        if (m_device) {
            if (shouldPauseTickle(phase, m_currentPalmIsSource, m_currentPalmIsTarget))
                m_device->pauseTickle();
            else
                m_device->resumeTickle();
        }
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::progressUpdated,
                     this, [this](int current, int total, const QString &message) {
        Q_EMIT runProgress(current, total, message);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::fetchProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/0, current, total);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::writeProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/1, current, total);
    });

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
    m_pluginManager =
        std::make_unique<Kalburator::PluginManager>(m_registry.get(), m_shape);

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

    if (!m_pluginManager->loadInProcess(items)) {
        qWarning() << "[PalmRuntime] plugin load rejected:"
                   << m_pluginManager->rejected().size();
        return;
    }

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

void PalmRuntime::buildRouteLogicalCalendars(
    QList<Kalburator::Sync::LogicalCalendar> &lcs)
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
    const auto cs = conduits();
    for (const auto &persisted : m_mappings) {
        const auto t = WildPalms::Runtime::translateRouteSpec(persisted, cs);
        if (t.status != WildPalms::Runtime::RouteStatus::NotARoute)
            m_routeStatuses.insert(persisted.id, t.status);
        if (!t.spec) continue;
        const auto &s = *t.spec;

        QString primaryBackendId = QStringLiteral("wp-hub");
        QString primaryColId     = s.hubCollectionId;

        if (s.kind == WildPalms::Runtime::RouteSpec::Kind::Filtered) {
            Kalburator::Shape::RecordFilter filter;
            filter.property = Kalburator::Shape::PropertyId{QStringLiteral("categories")};
            filter.op       = Kalburator::Shape::RecordFilter::Op::Contains;
            filter.value    = s.categoryName;

            const QString virtualColId =
                QStringLiteral("route-") + s.categoryName;

            // v0.59 ctor: parentBackend, parentBackendId ("wp-hub" matches how
            // the hub was registered in the PalmRuntime ctor), parentCollectionId,
            // virtualCollectionId, filter, optional registry (passed so the FCB
            // auto-nulls its parent on BackendRegistry::backendInstanceUnregistered
            // — clean failure instead of UB if the hub is ever unregistered).
            auto view = std::make_unique<Kalburator::Sinks::FilteredCollectionBackend>(
                m_hub.get(),
                QStringLiteral("wp-hub"),
                s.hubCollectionId,
                virtualColId,
                filter,
                m_registry.get());

            m_registry->registerBackendInstance(s.lcId, view.get());
            m_routeViews.push_back(std::move(view));

            primaryBackendId = s.lcId;
            primaryColId     = virtualColId;
        }
        // else Kind::Direct: Primary stays wp-hub:<domain>. No wrapper needed.

        LogicalCalendar lc;
        lc.id          = s.lcId;
        lc.domain      = Kalburator::Shape::DomainId{s.domain};
        lc.displayName = s.lcId;
        lc.syncEnabled = true;

        CalendarBackendBinding primary;
        primary.backendId  = primaryBackendId;
        primary.calendarId = primaryColId;
        primary.role       = BackendRole::Primary;
        lc.bindings.append(primary);

        CalendarBackendBinding sync;
        sync.backendId  = s.remoteBackendId;
        sync.calendarId = s.remoteCollectionId;
        sync.role       = BackendRole::Sync1;
        sync.syncOrder  = 1;
        lc.bindings.append(sync);

        lcs.append(lc);
    }
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
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
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
            if (!m_device->writeAppBlock(db, r.updatedAppInfoBlock))
                qWarning() << "[PalmRuntime] AppInfo write failed for" << db;
            else
                qDebug() << "[PalmRuntime] created" << r.bound.size()
                         << "category binding(s) on" << db;
        }
        if (!r.noFreeSlot.isEmpty()) {
            m_categoryNoFreeSlot.insert(db, r.noFreeSlot);
            qWarning() << "[PalmRuntime] no free category slot on" << db
                       << "for" << r.noFreeSlot;
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

    // C Task 4: build domain-level Palm<->hub Star mappings via generateMappings.
    // Each connected Palm backend is wired to its corresponding hub collection
    // using LogicalCalendar/generateMappings (Star topology = hub-and-spoke).
    // The hub (wp-hub) is Primary; each Palm backend collection is Sync1.
    using Kalburator::Sync::LogicalCalendar;
    using Kalburator::Sync::CalendarBackendBinding;
    using Kalburator::Sync::BackendRole;
    QList<LogicalCalendar> lcs;
    // Substrate A1: per-conduit Palm<->hub Star wiring from the descriptors.
    // (palmId, hubCol, palmCol) = (conduitId, domain, "palm:" + domain) —
    // reproduces the old static table exactly (memo: id "memo", domain "note").
    for (auto *c : conduits()) {
        const QString palmId  = c->conduitId();
        const QString hubCol  = c->domain().toString();
        const QString palmCol = QStringLiteral("palm:") + hubCol;
        if (!m_registry->backendInstance(palmId)) continue;   // backend not connected this session
        LogicalCalendar lc;
        lc.id = QStringLiteral("wp-%1").arg(hubCol);
        lc.domain = Kalburator::Shape::DomainId{hubCol};
        lc.displayName = hubCol;
        lc.syncEnabled = true;
        CalendarBackendBinding hubB;
        hubB.backendId = QStringLiteral("wp-hub");
        hubB.calendarId = hubCol;
        hubB.role = BackendRole::Primary;
        lc.bindings.append(hubB);
        CalendarBackendBinding palmB;
        palmB.backendId = palmId;
        palmB.calendarId = palmCol;
        palmB.role = BackendRole::Sync1;
        palmB.syncOrder = 1;
        lc.bindings.append(palmB);
        lcs.append(lc);
    }
    // generateMappings emits TwoWay mappings with conflictPolicy=AskUser. This
    // is a deliberate change from the old per-slot RawFiles default
    // (LastWriteWins): on the hub topology, Palm<->hub conflicts surface to
    // WildPalms' existing conflict handlers / deferred conflict store for review
    // rather than silently auto-resolving and risking data loss.
    // Translate persisted user mappings into per-route LCs. Each Filtered
    // route materializes a FilteredCollectionBackend wrapping the hub; each
    // Direct route binds the LC's Primary to wp-hub directly. lcs is appended
    // to in place.
    buildRouteLogicalCalendars(lcs);

    m_mappings = Kalburator::Sync::generateMappings(lcs, Kalburator::Sync::SyncTopology::Star);
    m_engine->setSyncMappings(m_mappings);

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
    if (m_engine)
        m_engine->setSyncMappings(m_mappings);
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
    m_ownedBackends.push_back(std::move(backend));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    m_engine->setSyncMappings(m_mappings);
}

void PalmRuntime::setProfile(Profile *profile)
{
    m_profile = profile;
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
    if (m_engine)
        m_engine->setSyncMappings(m_mappings);
}

void PalmRuntime::setConflictHandler(
    Kalburator::Conflict::ConflictHandler *handler)
{
    m_conflictHandler = handler;
    if (m_engine) {
        m_engine->conflictRegistry()->setDefaultHandler(handler);
    }
}

void PalmRuntime::setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard)
{
    if (m_engine) {
        m_engine->setMassDeleteGuard(guard);
    }
}

Kalburator::Conflict::ConflictHandler *
PalmRuntime::conflictHandlerForTest() const
{
    if (!m_engine) return nullptr;
    return m_engine->conflictRegistry()->handlerFor(QString{});
}

Kalburator::Sync::SyncConflictStore *
PalmRuntime::syncConflictStore() const
{
    return m_engine ? m_engine->syncConflictStore() : nullptr;
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
    QList<QString> ids;
    if (m_pluginManager) {
        for (const auto &lp : m_pluginManager->loaded())
            ids.append(lp.id);
    }
    return ids;
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
// programmatic callers from clobbering m_syncPromise/m_syncIds/m_syncAccum
// and stranding an in-flight loop's future.
static bool syncAlreadyRunning_(const PalmRuntime *self) {
    if (!self->isSyncRunning())
        return false;
    qWarning() << "[PalmRuntime] sync request ignored: a sync is already "
                  "in flight — rejecting the re-entrant call";
    return true;
}

QFuture<PalmRunResult> PalmRuntime::runAllMappings(int maxPasses, bool skipUnchanged)
{
    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (m.enabled)
            ids.append(m.id);
    }
    if (ids.isEmpty()) {
        // F11: the caller already emitted runStarted — answer it, or the
        // dashboard never leaves its Syncing state.
        const auto r = makeRejectedResult(QStringLiteral(
            "No enabled sync targets — nothing to sync"));
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();
        return makeReadyFuture(r);
    }

    // Re-entrancy guard: a multi-pass loop is already in flight (m_syncPromise
    // is set until the loop finalizes). Starting another run here would clobber
    // m_syncPromise/m_syncIds/m_syncAccum and strand the first caller's future.
    // The public entry points pre-guard on isSyncRunning() without emitting
    // runStarted; this backstop deliberately emits NO signals — the in-flight
    // run owns the current runStarted/runFinished pair.
    if (m_syncPromise) {
        qWarning() << "[PalmRuntime] runAllMappings() called while a sync loop is "
                      "already in flight — ignoring the re-entrant request";
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));
    }

    // P2 Investigation (2026-05-27): pauseTickle() rationale
    // Historical context (May 2, 2026, commit 0fefb6b): The original TickleWorker
    // ran on a separate thread and interleaved dlp_GetSysDateTime() with DLP calls
    // from QtConcurrent pool threads, corrupting the DLP session state. The fix was
    // to synchronize by pausing the tickle before bulk DLP work.
    //
    // Current architecture: PalmDeviceAccess owns m_linkThread (a QThread). All
    // DLP calls marshal to m_implOwner on m_linkThread via BlockingQueuedConnection,
    // and PalmTickle is parented to m_implOwner, so both tickle timer and DLP calls
    // run on m_linkThread's event loop. BlockingQueuedConnection blocks the caller
    // until the link thread finishes, guaranteeing serialization — the tickle timer
    // can ONLY fire between (or outside) DLP operations, never during them.
    //
    // Hazard assessment: Cross-thread race (the original issue) is eliminated.
    // However, if dlp_GetSysDateTime and in-progress DLP operations have protocol-level
    // conflicts on the Palm wire (independent of thread safety), the pause is still
    // necessary. Task 2.2 narrowed this to apply-phase only via phaseChanged signal;
    // the blanket pre-run pause is removed — the tickle is now paused/resumed per
    // phase via the phaseChanged lambda in the constructor.

    // Task 12: multi-hop fixpoint loop. WildPalms sync is a depth-1 star
    // (Palm — Hub — Remote), so a single pass of the mapping set crosses only
    // ONE hop. Re-run the set until a pass moves no data (or fails/cancels/caps)
    // so both hops propagate in one user action.
    //
    // Mode is applied only after the guards, so an early-return (no mappings /
    // re-entrant) never leaks settings into the next run or clobbers an in-flight loop.
    m_engine->setSkipUnchangedMappings(skipUnchanged);
    m_syncMaxPass = maxPasses;

    m_syncIds   = ids;
    m_syncPass  = 0;
    m_syncAccum = PalmRunResult{};
    m_syncAccum.success   = true;
    m_syncAccum.startTime = QDateTime::currentDateTimeUtc();

    // K.8b T16 + Plan 8 B.3: the watcher (set up per-pass in dispatchSyncPass_)
    // both propagates cancelSync() into SyncEngine::onCancelObserved AND delivers
    // the result. Qt6's QFuture::then() drops its continuation when the source
    // future is canceled, so runFinished must be emitted from the watcher's
    // finished slot — it fires on both completion and cancel, on this object's
    // thread (no invokeMethod marshalling needed). The caller's promise is held
    // in m_syncPromise and finalized once, at the END of the loop.
    m_syncPromise = std::make_shared<QPromise<PalmRunResult>>();
    m_syncPromise->start();
    QFuture<PalmRunResult> resultFuture = m_syncPromise->future();

    dispatchSyncPass_();          // device stays connected across all passes
    return resultFuture;
}

void PalmRuntime::dispatchSyncPass_()
{
    ++m_syncPass;

    // Plan 8 B.1: canonical subset dispatch (was runSyncFuture(ids, …)).
    Kalburator::Sync::SyncRequest req;
    req.mappingIds = m_syncIds;
    req.behavior   = Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored;
    auto engineFuture = m_engine->runSync(req);

    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    auto *watcher = new QFutureWatcher<void>(this);
    m_activeSyncWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<void>::finished,
            this, [this, watcher, engineFuture]() {
        // B.4: read via resultAt(0), not results() (empty after cancel).
        // The multi-mapping iface adds cancellation-marker results even
        // when canceled (setAddResultsIfCanceledEnabled); guard anyway.
        QList<Kalburator::Sync::SyncResult> results;
        if (engineFuture.resultCount() > 0)
            results = engineFuture.resultAt(0);

        // Fold this pass into m_syncAccum (accumulated across all passes).
        PalmRunResult::PluginStats stats;
        int linkLostCount = 0;
        bool anyCancelled = engineFuture.isCanceled();
        for (const auto &sr : results) {
            if (sr.cancelled)
                anyCancelled = true;
            if (!sr.success && !sr.cancelled && !sr.skipped) {
                m_syncAccum.success = false;
                if (sr.errorMessage.contains(QLatin1String("Palm link"),
                                             Qt::CaseInsensitive)) {
                    ++linkLostCount;
                } else if (m_syncAccum.errorMessage.isEmpty()
                           && !sr.errorMessage.isEmpty()) {
                    m_syncAccum.errorMessage = sr.errorMessage;
                }
            }
            stats.created   += sr.targetStats.created;
            stats.updated   += sr.targetStats.updated;
            stats.deleted   += sr.targetStats.deleted;
            stats.unchanged += sr.targetStats.unchanged;
            stats.errors    += (sr.success ? 0 : 1);
        }
        // Layer B: collapse N "Palm link lost" errors into one summary so
        // the UI shows a single message instead of repeating the same string.
        if (linkLostCount > 0 && m_syncAccum.errorMessage.isEmpty()) {
            m_syncAccum.errorMessage = QStringLiteral(
                "HotSync aborted: Palm device disconnected "
                "(%1 of %2 mappings affected)").arg(linkLostCount).arg(results.size());
        }
        // A cancelled run is not a successful one (F13: but it is not an
        // error either — the flag lets the UI pick a neutral tone).
        if (anyCancelled) {
            m_syncAccum.success = false;
            m_syncAccum.cancelled = true;
            if (m_syncAccum.errorMessage.isEmpty())
                m_syncAccum.errorMessage = QStringLiteral("Sync cancelled");
        }
        // Accumulate per-plugin stats across passes (fold into existing entry).
        if (!results.isEmpty()) {
            auto &acc = m_syncAccum.perPluginStats[QStringLiteral("calendar")];
            acc.created   += stats.created;
            acc.updated   += stats.updated;
            acc.deleted   += stats.deleted;
            acc.unchanged += stats.unchanged;
            acc.errors    += stats.errors;
        }

        // Per-mapping finished — chips fill their counts here (run-end only;
        // the engine has no per-mapping completion signal). Emitted per pass so
        // the UI reflects each hop's movement.
        for (int i = 0; i < results.size() && i < m_syncIds.size(); ++i) {
            const auto &sr = results[i];
            const auto &ts = sr.targetStats;
            Q_EMIT mappingSyncFinished(m_syncIds[i], ts.created, ts.updated, ts.deleted,
                                       sr.success && !sr.cancelled);
        }

        if (m_activeSyncWatcher == watcher)
            m_activeSyncWatcher = nullptr;
        watcher->deleteLater();

        // Loop or finalize. The device link is a PalmRuntime member and stays
        // connected across passes — flushWrites/resumeTickle/runFinished/
        // syncCompleted/promise.finish happen ONCE, only when the loop ends.
        if (WildPalms::Runtime::shouldContinueSync(results, m_syncPass, m_syncMaxPass)) {
            dispatchSyncPass_();          // next hop; prior engine run is complete
            return;
        }

        // Single finalize funnel for ALL stop reasons (fixpoint reached, cap hit,
        // mapping failure, or cancel — shouldContinueSync returned false for one of
        // them). Everything below MUST run exactly once per run.
        // Do not add an early return between the shouldContinueSync check and here.
        m_syncAccum.endTime = QDateTime::currentDateTimeUtc();
        if (m_device) m_device->flushWrites();    // close last mapping's DB before EndOfSync
        if (m_device) m_device->resumeTickle();
        m_activeMappingId.clear();
        Q_EMIT runFinished(m_syncAccum);
        Q_EMIT syncCompleted();

        m_syncPromise->addResult(m_syncAccum);
        m_syncPromise->finish();
        m_syncPromise.reset();
    });
    watcher->setFuture(engineFuture);
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
    return runAllMappings(/*maxPasses=*/3, /*skipUnchanged=*/true);
}

QFuture<PalmRunResult> PalmRuntime::fullSync()
{
    if (syncAlreadyRunning_(this))
        return makeReadyFuture(makeRejectedResult(
            QStringLiteral("A sync is already running")));

    Q_EMIT runStarted(QStringLiteral("FullSync"));
    // Clear all baselines so the engine treats this as a fresh first sync.
    for (const auto &m : m_mappings)
        m_baselineStore->clearMappingV3(m.id);
    // Task 12: full re-diff every pass (no skip-unchanged); 2 passes suffice for
    // the depth-1 star.
    return runAllMappings(/*maxPasses=*/2, /*skipUnchanged=*/false);
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

    using Direction = Kalburator::Sync::ExecutionOverride::Direction;
    Kalburator::Sync::ExecutionOverride ov;
    ov.direction = (dir == MirrorDir::PalmToPC) ? Direction::MirrorAToB
                                                 : Direction::MirrorBToA;

    // P2: Blanket pauseTickle() removed here (same as runAllMappings).
    // Tickle is now paused/resumed per phase via the phaseChanged lambda
    // in the constructor. The resumeTickle() in
    // the then() lambda below remains as a safety net.

    // For M3: calendar-only, single mapping. Dispatch only the first enabled
    // mapping; Plan 3 (M4) will add multi-mapping iteration once other plugins
    // are re-enabled.
    //
    // Plan 8 B.2: canonical single-mapping dispatch (was
    // runSyncFuture(id, ov)). The ==1 shape is the only one that consults
    // executionOverride.direction in full.
    Kalburator::Sync::SyncRequest req;
    req.mappingIds        = { ids.first() };
    req.executionOverride = ov;
    auto engineFuture = m_engine->runSync(req);

    // K.8b T16 + Plan 8 B.3: watcher-based result delivery (see
    // runAllMappings). On this path the cancel caveat is acute: the
    // canonical single-mapping branch .then()-wraps dispatchSingleNative's
    // future, and the wrapper loses the F2 Task 23 cancellation result —
    // after a cancel the engine future may carry NO result at all.
    auto promise = std::make_shared<QPromise<PalmRunResult>>();
    promise->start();
    QFuture<PalmRunResult> resultFuture = promise->future();

    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    auto *watcher = new QFutureWatcher<void>(this);
    m_activeSyncWatcher = watcher;
    QObject::connect(watcher, &QFutureWatcher<void>::finished,
            this, [this, watcher, engineFuture, promise]() {
        // B.4: resultAt(0), not results() (empty after cancel).
        QList<Kalburator::Sync::SyncResult> results;
        if (engineFuture.resultCount() > 0)
            results = engineFuture.resultAt(0);

        Kalburator::Sync::SyncResult sr;
        if (!results.isEmpty()) {
            sr = results.first();
        } else {
            sr.success   = false;
            sr.cancelled = engineFuture.isCanceled();
            if (!sr.cancelled)
                sr.errorMessage = QStringLiteral("Sync engine returned no result");
        }

        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();
        r.success   = sr.success && !sr.cancelled;
        r.cancelled = sr.cancelled;   // F13
        // K.9: propagate engine error message to the UI (see runAllMappings).
        if (!r.success)
            r.errorMessage = sr.cancelled ? QStringLiteral("Sync cancelled")
                                          : sr.errorMessage;

        PalmRunResult::PluginStats stats;
        stats.created   = sr.targetStats.created;
        stats.updated   = sr.targetStats.updated;
        stats.deleted   = sr.targetStats.deleted;
        stats.unchanged = sr.targetStats.unchanged;
        stats.errors    = r.success ? 0 : 1;
        r.perPluginStats.insert(QStringLiteral("calendar"), stats);

        r.endTime = QDateTime::currentDateTimeUtc();
        if (m_device) m_device->flushWrites();    // close last mapping's DB before EndOfSync
        if (m_device) m_device->resumeTickle();
        Q_EMIT runFinished(r);
        Q_EMIT syncCompleted();

        promise->addResult(r);
        promise->finish();
        if (m_activeSyncWatcher == watcher)
            m_activeSyncWatcher = nullptr;
        watcher->deleteLater();
    });
    watcher->setFuture(engineFuture);

    return resultFuture;
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

    Kalburator::Sync::SyncRequest req;
    req.mappingIds = mappingIds;
    req.behavior   = Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored;

    Kalburator::Sync::ExecutionOverride ov;
    ov.clobber = true;
    req.executionOverride = ov;

    auto engineFuture = m_engine->runSync(req);

    // Same cancellation-watcher pattern as runAllMappings.
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    m_activeSyncWatcher = new QFutureWatcher<void>(this);
    QObject::connect(m_activeSyncWatcher,
                     &QFutureWatcher<void>::finished, this, [this]() {
        if (m_activeSyncWatcher) {
            m_activeSyncWatcher->deleteLater();
            m_activeSyncWatcher = nullptr;
        }
    });
    m_activeSyncWatcher->setFuture(engineFuture);

    return engineFuture.then(
        [this, ids = mappingIds](QList<Kalburator::Sync::SyncResult> results) {
            PalmRunResult r;
            r.startTime = QDateTime::currentDateTimeUtc();
            r.success = std::all_of(results.begin(), results.end(),
                [](const auto &sr){ return sr.success; });
            r.cancelled = std::any_of(results.begin(), results.end(),
                [](const auto &sr){ return sr.cancelled; });   // F13
            if (!r.success) {
                for (const auto &sr : results) {
                    if (!sr.success) {
                        r.errorMessage = sr.errorMessage;
                        break;
                    }
                }
            }
            // Multi-domain reporting: aggregate per target backend.
            // SyncResult does not carry the target backend id directly,
            // so look it up from the request's mappings (results align
            // by index with the dispatched ids).
            for (int i = 0; i < results.size(); ++i) {
                const auto &sr = results[i];
                PalmRunResult::PluginStats stats;
                stats.created   = sr.targetStats.created;
                stats.updated   = sr.targetStats.updated;
                stats.deleted   = sr.targetStats.deleted;
                stats.unchanged = sr.targetStats.unchanged;
                stats.errors    = sr.success ? 0 : 1;
                QString key;
                if (i < ids.size()) {
                    const QString &mid = ids[i];
                    for (const auto &m : m_mappings) {
                        if (m.id == mid) {
                            key = m.targetBackend;
                            break;
                        }
                    }
                }
                if (key.isEmpty())
                    key = QStringLiteral("clobber");
                r.perPluginStats.insert(key, stats);
            }
            r.endTime = QDateTime::currentDateTimeUtc();
            QMetaObject::invokeMethod(this, [this, r]() {
                if (m_device) m_device->flushWrites();
                if (m_device) m_device->resumeTickle();
                Q_EMIT runFinished(r);
                Q_EMIT syncCompleted();
            });
            return r;
        });
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

// Shakedown F10: bridge UI conflict decisions into the engine's
// SyncConflictStore. Lives here (not in KF6MainWindow) because WildPalmsCore
// cannot include the engine-side synctypes.h (WP-local file collision).
int PalmRuntime::applyConflictResolutions(
    const QList<Kalburator::Conflict::ConflictRecord> &resolved)
{
    if (!m_engineConflictStore)
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
        m_engineConflictStore->resolveConflict(rec.conflictId, res);
        ++applied;
    }
    return applied;
}

bool shouldContinueSync(const QList<Kalburator::Sync::SyncResult> &results,
                        int passJustFinished, int maxPasses)
{
    if (passJustFinished >= maxPasses) return false;        // cap reached
    bool anyChange = false;
    for (const auto &sr : results) {
        if (sr.cancelled) return false;                     // cancelled -> stop
        if (!sr.success && !sr.skipped) return false;       // failure -> stop
        if (sr.sourceStats.hasChanges() || sr.targetStats.hasChanges())
            anyChange = true;
    }
    return anyChange;                                       // loop only if data moved
}

}  // namespace WildPalms::Runtime
