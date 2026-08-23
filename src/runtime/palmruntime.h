#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QPromise>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

namespace Kalburator { class PluginManager; class Plugin; }

class Profile;

#include "palmrunresult.h"
#include "routemapping.h"   // substrate A3 — RouteStatus (m_routeStatuses member)
#include "palm/sync/palmrevisionstore.h"
#include <shaperegistries.h>

class KPilotDeviceLink;

namespace Kalburator::Sync {
    class BackendRegistry;
    class ISyncHost;
    struct SyncMapping;
    struct LogicalCalendar;
    class SyncBackend;
    struct SyncResult;
}

namespace Kalburator::Engine {
    class SyncEngine;
}

namespace Kalburator::Conflict {
    class ConflictHandler;
    class IMassDeleteGuard;
}

namespace Kalburator::Storage {
    class BaselineStore;
}

namespace Kalburator::Sync {
    struct ConflictInfo;
    class SyncConflictStore;
}namespace Kalburator::Conflict {
    struct ConflictRecord;
}

namespace Kalburator::Shape {
    struct Shape;
}

namespace Kalburator::Sinks {
    class GenericSqliteBackend;
    class FilteredCollectionBackend;
}

namespace WildPalms::Plugins { class PimPlugin; }   // substrate A1 — conduit descriptor

// K.8b T13: IBackendPluginV2 forward-decl dropped — the V2 plugin ABI is
// gone. registerPluginForTest overloads removed below (they had no live
// callers after K.8b T6 turned them into no-ops).

namespace WildPalms::Runtime {

class PalmDeviceAccess;

class PalmRuntime : public QObject {
    Q_OBJECT
public:
    explicit PalmRuntime(const QString &profilePath,
                         QObject *parent = nullptr);
    ~PalmRuntime() override;

    /// Open a Palm device on one of the supplied paths. Async — emits
    /// connectionComplete(true, "") on success or connectionComplete(false,
    /// error) on failure. Internally drives PalmDeviceAccess; on success,
    /// loads plugins + sets up engine via finishConnect().
    /// Load the five static Palm plugins via PluginManager::loadInProcess().
    /// Called from the constructor; replaces the old KPluginMetaData
    /// .so discovery loop in finishConnect().
    void registerPalmPlugins();

    void connectDevice(const QStringList &devicePaths);

    /// Cancel an in-progress connect.
    void cancelConnect();

    /// Cancel a running hotSync / fullSync / copyPalmToPC / clobberSync.
    /// Routes QFutureWatcher::cancel() into SyncEngine::onCancelObserved.
    /// No-op if no sync is running.
    void cancelSync();

    void disconnectDevice();
    bool isDeviceConnected() const;

    /// Borrowed pointer to the underlying KPilotDeviceLink. Only valid after
    /// readyForSync(). Returns nullptr if not yet connected. KF6MainWindow uses
    /// this to read handshake info and to feed the legacy m_syncEngine.
    KPilotDeviceLink *deviceLink() const;

    QFuture<PalmRunResult> hotSync();
    QFuture<PalmRunResult> fullSync();
    QFuture<PalmRunResult> copyPalmToPC();
    /// Wipe selected Palm-side databases and re-push hub data in one
    /// operation. mappingIds must reference Palm-direct mappings only;
    /// callers should filter via palmDirectMappingsForDomain(). Returns
    /// per-mapping success/stats via the standard PalmRunResult shape.
    QFuture<PalmRunResult> clobberSync(const QList<QString> &mappingIds);
    QFuture<PalmRunResult> backup();
    QFuture<PalmRunResult> restore();

    QList<QString> enabledPluginIds() const;
    QList<Kalburator::Sync::SyncMapping> palmMappings() const;

    /// Returns the IDs of all enabled mappings whose target backend is the
    /// Palm-side blob backend for the given domain (e.g. "calendar",
    /// "contacts", "memo", "todo"). Used by ClobberDialog to populate
    /// per-conduit checkboxes; the engine never consumes this.
    QList<QString> palmDirectMappingsForDomain(const QString &domain) const;

    /// Returns true iff the given mapping is Palm-direct (targets one of
    /// the Palm-side blob backends). Exposed mostly for testing.
    bool isPalmDirectMapping(const Kalburator::Sync::SyncMapping &m) const;

    bool isRunning() const { return m_running; }

    /// True while a sync dispatch (multi-pass fixpoint loop, mirror, or
    /// clobber) is in flight. UI entry points use this to turn re-entrant
    /// sync clicks into a user-visible no-op instead of a silent console
    /// warning. Covers every path that installs m_activeSyncWatcher.
    bool isSyncRunning() const { return m_syncPromise != nullptr
                                       || m_activeSyncWatcher != nullptr; }

    /// Read-only view of the loaded Palm plugin instances.
    /// Valid after registerPalmPlugins() (called from the constructor).
    const std::vector<std::unique_ptr<Kalburator::Plugin>> &palmPlugins() const
        { return m_palmPlugins; }

    /// All loaded conduit plugins, as descriptors (substrate A1). Stable for
    /// the lifetime of this PalmRuntime. Used by finishConnect, route
    /// translation, and (later) the wizard/graph surfaces. One residual
    /// dynamic_cast to the PimPlugin base replaces the per-concrete-type
    /// cast chains that used to live throughout this class.
    QList<WildPalms::Plugins::PimPlugin*> conduits() const;

    /// True iff backendId names one of the loaded Palm conduits (substrate
    /// A1 — replaces the hardcoded kPalmBackendIds array).
    bool isPalmConduitBackendId(const QString &backendId) const;

    /// Substrate A1 test seam: append an extra conduit descriptor after
    /// construction, then re-run hub-collection creation so the new domain
    /// gets its hub collection (createCollection is idempotent for existing
    /// ids). NOT a production plugin-loading path.
    void appendConduitForTest(std::unique_ptr<WildPalms::Plugins::PimPlugin> conduit);

    // Replace the live mapping list. Caller must ensure isRunning() == false.
    // JSON shape is the same as Profile::syncMappingsJson() — array of objects
    // each round-trippable via syncMappingToJson()/syncMappingFromJson().
    void reloadMappings(const QJsonArray &json);

    /// F.3: Borrow a Profile pointer for category-slot snapshot
    /// write-back. Called by KF6MainWindow::loadProfile() right after
    /// PalmRuntime is constructed. Non-owning — the Profile must
    /// outlive this PalmRuntime. nullptr disables write-back.
    void setProfile(Profile *profile);

    // Non-owning. Caller must ensure the handler outlives this PalmRuntime
    // (or call setConflictHandler(nullptr) before destroying the handler).
    void setConflictHandler(Kalburator::Conflict::ConflictHandler *handler);
    Kalburator::Conflict::ConflictHandler *conflictHandlerForTest() const;

    /// Forward to the embedded SyncEngine. Non-owning; consumer must
    /// outlive the runtime. nullptr clears. See libkalburator's
    /// imassdeleteguard.h for threshold semantics.
    void setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard);

    /// Borrowed pointer to the embedded engine's SyncConflictStore.
    /// Used by the conflict UI to read deferred conflicts. Non-null
    /// since F10 (a per-profile store is attached at construction).
    Kalburator::Sync::SyncConflictStore *syncConflictStore() const;

    /// Shakedown F10 bridge: apply UI-side resolved-but-unapplied
    /// decisions (from ConflictReviewWidget's ConflictStore) into the
    /// embedded engine's SyncConflictStore so that
    /// SyncEngine::rehydratePendingResolutions() replays them on the
    /// next sync. Lives here because WildPalmsCore cannot include the
    /// engine-side synctypes.h (WP-local file collision). Returns the
    /// number of records applied; records with decisions that have no
    /// persisted engine counterpart (currently DeleteBoth) are skipped.
    int applyConflictResolutions(
        const QList<Kalburator::Conflict::ConflictRecord> &resolved);

    /// Convert engine-side `Sync::ConflictInfo` into the UI-side
    /// `Conflict::ConflictRecord` shape consumed by `ConflictReviewDialog`.
    /// Lives here because WildPalmsCore (where the dialog is wired)
    /// can't include the engine-side `synctypes.h` cleanly — a
    /// WP-local `synctypes.h` in `src/core/` collides with the
    /// libkalburator one (see `src/CMakeLists.txt` comment).
    static Kalburator::Conflict::ConflictRecord toConflictRecord(
        const Kalburator::Sync::ConflictInfo &info);


    // Test seams
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);
    // K.8b T13: registerPluginForTest(IBackendPluginV2) overloads removed
    // along with the V2 plugin ABI (the bodies were no-ops since K.8b T6).
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);
    // K.8b T7: BlobBackendAdapter deleted; tests inject SyncBackend directly.
    void registerBackendInstanceForTest(const QString &id,
                                        std::unique_ptr<Kalburator::Sync::SyncBackendBase> backend);

    /// Borrowed reference to PalmRuntime's BackendRegistry. Lifetime ==
    /// PalmRuntime's. AccountController borrows this for provider-supplied
    /// backend registration; AC is constructed AFTER PalmRuntime in
    /// KF6MainWindow::loadProfile() and torn down BEFORE PalmRuntime in
    /// closeProfile() / loadProfile().
    Kalburator::Sync::BackendRegistry &backendRegistry() { return *m_registry; }

    struct ConduitDescriptor {
        QString mappingId;
        QString label;
        QString iconName;
    };
    /// Identity (id/label/icon) for each enabled mapping, resolved via the
    /// loaded plugins. Used to seed the dashboard conduit row before a sync.
    QVector<ConduitDescriptor> conduitDescriptors() const;

    /// Per-mapping route status from the last buildRouteLogicalCalendars
    /// (substrate A3 — replaces the old silent drop of unresolved routes).
    /// Keyed by SyncMapping::id; only well-formed routes are present.
    QHash<QString, WildPalms::Runtime::RouteStatus> routeStatuses() const
    { return m_routeStatuses; }

signals:
    void routeStatusesChanged();
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void mappingSyncStarted(const QString &mappingId, const QString &label,
                            const QString &iconName);
    void mappingSyncProgress(const QString &mappingId, int phase,
                             int current, int total);
    void mappingSyncFinished(const QString &mappingId, int created,
                             int modified, int deleted, bool ok);
    void runLog(QString message);
    void runFinished(PalmRunResult);

    // M6b additions — replace DeviceSession's signal surface for KF6MainWindow.
    void connectionStarted();
    void connectionComplete(bool success, QString error);
    void readyForSync();         // emitted right after deviceConnected once
                                 // plugins are loaded and engine is ready
    void logMessage(QString message);
    // TODO(M6b/Task 5): wire the next three from PalmDeviceAccess /
    // KPilotDeviceLink once KF6MainWindow subscribes to PalmRuntime.
    void errorOccurred(QString error);
    void progressUpdated(int current, int total, QString message);
    void palmScreenMessage(QString message);

    /// Forwarded from the embedded SyncEngine. Fires every time a
    /// mapping with policy=AskUser encounters a conflict that the
    /// engine cannot auto-resolve. The conflict is also persisted
    /// to SyncConflictStore (engine-side); this signal exists so
    /// the WildPalms UI can update a pending-count display in real
    /// time without polling.
    void conflictDetected(const Kalburator::Sync::ConflictInfo &info);

    /// Emitted after every sync run (hotSync / fullSync /
    /// copyPalmToPC / clobberSync / backup / restore). Sub-project D
    /// views connect to this in createMainView to drive
    /// view->refresh(). Fires once per QFuture returned by the public
    /// sync methods.
    void syncCompleted();

private:
    /// Run after PalmDeviceAccess emits connectionComplete(true, "").
    /// Loads plugins, registers backends, sets up default mappings if
    /// none exist, sets up the engine. Emits deviceConnected + readyForSync.
    void finishConnect();
    void ensureHubCollections();

    /// Append one LogicalCalendar per category-route (translated from the
    /// persisted user mappings) to `lcs`. Owned FilteredCollectionBackend
    /// instances land in m_routeViews and are registered with m_registry
    /// under the id "wp-route-<lcId>". No-op when m_mappings is empty.
    void buildRouteLogicalCalendars(QList<Kalburator::Sync::LogicalCalendar> &lcs);

    /// Repopulate m_mappings from the borrowed Profile's persisted
    /// syncMappingsJson(). The Profile is the source of truth for
    /// user-configured (incl. remote DAV) mappings; without this the runtime
    /// starts empty and finishConnect() generates rawfiles defaults for every
    /// slot, silently discarding the user's wiring. No-op when m_profile is
    /// null (test/no-profile paths keep their injected mappings).
    void loadMappingsFromProfile();

    // Mirror direction — local enum avoids pulling synctypes.h into this header.
    enum class MirrorDir { PalmToPC, PCToPalm };

    QFuture<PalmRunResult> runAllMappings(int maxPasses, bool skipUnchanged);
    QFuture<PalmRunResult> runMirror(MirrorDir dir, const QString &modeLabel);

    /// Task 12: run ONE engine pass of the enabled mapping set, then fold its
    /// results into m_syncAccum and either re-dispatch (next hop) or finalize.
    /// Drives the multi-hop fixpoint loop set up by runAllMappings().
    void dispatchSyncPass_();

    /// Resolve a mapping's display label + theme icon name from m_palmPlugins
    /// (matches plugin->pluginId() against mapping.sourceBackend).
    void resolveMappingIdentity(const QString &mappingId,
                                QString &outLabel, QString &outIconName) const;
    // P2: track whether Palm device is source or target in the current mapping.
    // Set in the syncStarted lambda; used by the phaseChanged lambda in the
    // constructor to decide whether to pause or resume the keep-alive tickle.
    bool m_currentPalmIsSource = false;
    bool m_currentPalmIsTarget = false;

    QString m_activeMappingId;   // mapping currently emitting fetch/write progress

    Kalburator::Conflict::ConflictHandler                *m_conflictHandler = nullptr;
    Profile                                              *m_profile = nullptr;   // borrowed; see setProfile
    QString                                                      m_profilePath;
    QString                                                      m_backupRoot;
    // K.8b T16: watcher tracking the in-flight engine future so cancelSync()
    // can call cancel() into SyncEngine::onCancelObserved.
    // QFutureWatcher<void> accepts any QFuture<T> via setFuture().
    QFutureWatcher<void>                                        *m_activeSyncWatcher = nullptr;
    // Task 12: multi-hop fixpoint loop state (one in-flight run at a time).
    // runAllMappings() seeds these and kicks the first pass; dispatchSyncPass_()
    // re-dispatches until shouldContinueSync() says stop, accumulating into
    // m_syncAccum and finalizing the caller's promise once at the end.
    int     m_syncPass     = 0;     // 1-based current pass
    int     m_syncMaxPass  = 1;     // 3 for HotSync, 2 for FullSync
    QList<QString>                              m_syncIds;     // enabled mapping ids
    std::shared_ptr<QPromise<PalmRunResult>>    m_syncPromise; // caller's promise
    PalmRunResult                               m_syncAccum;   // accumulated across passes
    std::unique_ptr<PalmDeviceAccess>                            m_device;
    std::unique_ptr<Kalburator::Sync::BackendRegistry>           m_registry;
    // C: canonical local hub. Declared right after m_registry (and before
    // m_engine) so it is destroyed AFTER the engine — the engine's registry
    // holds a borrowed "wp-hub" pointer, so the hub must outlive the engine.
    std::unique_ptr<Kalburator::Sinks::GenericSqliteBackend>     m_hub;
    // Hub<->remote routing: one FilteredCollectionBackend per category-route.
    // Declared right after m_hub (which it borrows) and BEFORE m_engine so the
    // engine's registered borrowed pointers are still valid at destruction.
    std::vector<std::unique_ptr<Kalburator::Sinks::FilteredCollectionBackend>>
        m_routeViews;
    // O7: per-PalmRuntime shape registries, injected into m_pluginManager
    // (which populates them) and m_engine (which reads them). Declared before
    // both so it is constructed first and destroyed last.
    Kalburator::Shape::ShapeRegistries                           m_shape;
    // Shakedown F10: per-profile engine-side conflict store (SQLite,
    // .state/sync-conflicts.db). The engine borrows this pointer: deferred
    // Unmonitored AskUser conflicts persist here and rehydrate at every
    // runSync entry so UI-applied resolutions replay on the next sync.
    // Declared BEFORE m_engine (which borrows it) so it outlives the engine.
    std::unique_ptr<Kalburator::Sync::SyncConflictStore>         m_engineConflictStore;
    std::unique_ptr<Kalburator::Sync::ISyncHost>                 m_syncHost;
    std::unique_ptr<Kalburator::Engine::SyncEngine>              m_engine;
    std::unique_ptr<Kalburator::Storage::BaselineStore>          m_baselineStore;
    // K.8b T6: PluginManager + owned plugin instances for the five static
    // Palm plugins. m_pluginManager MUST be declared before m_palmPlugins
    // (C++ destructs in reverse declaration order — plugins destruct before
    // manager, which is the correct teardown sequence).
    std::unique_ptr<Kalburator::PluginManager>                   m_pluginManager;
    std::vector<std::unique_ptr<Kalburator::Plugin>>             m_palmPlugins;
    QList<Kalburator::Sync::SyncMapping>                         m_mappings;
    bool                                                         m_running = false;
    // T7: per-profile cached-revision store. Must be declared BEFORE
    // m_ownedBackends so it is destroyed AFTER them (reverse declaration
    // order) — the backends hold a borrowed pointer to this store.
    std::unique_ptr<WildPalms::PalmSync::PalmRevisionStore>      m_palmRevisionStore;
    std::vector<std::unique_ptr<Kalburator::Sync::SyncBackendBase>>  m_ownedBackends;
    /// Substrate A3: per-mapping route status from the last
    /// buildRouteLogicalCalendars(). Keyed by SyncMapping::id.
    QHash<QString, WildPalms::Runtime::RouteStatus>             m_routeStatuses;
    /// Substrate A3: desired category names the connect-time reconciler could
    /// not place (device table full), keyed by primary db name. Diagnostics +
    /// future UI; translateRouteSpec already reports these rows as NoFreeSlot.
    QHash<QString, QStringList>                                 m_categoryNoFreeSlot;
};

/// Decide whether the multi-hop loop should run another pass.
/// `results` are the SyncResults of the pass that just finished.
/// Continues only if some mapping changed data (so a hop may still be
/// pending), the run is healthy, and we are under the cap.
///   passJustFinished : 1-based index of the pass that just completed
///   maxPasses        : hard cap (3 for HotSync)
bool shouldContinueSync(const QList<Kalburator::Sync::SyncResult> &results,
                        int passJustFinished, int maxPasses);

}  // namespace WildPalms::Runtime

#endif
