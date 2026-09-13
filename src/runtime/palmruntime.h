#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
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
#include <kalburator/runtime/collectionruntime.h>
#include "routemapping.h"   // substrate A3 — RouteStatus (m_routeStatuses member)
#include "palm/sync/palmrevisionstore.h"
#include <kalburator/shape/shaperegistries.h>
#include "palmruntimeassembly.h"

class KPilotDeviceLink;

namespace Kalburator::Sync {
    class BackendRegistry;
    struct SyncMapping;
    struct LogicalCalendar;
    class SyncBackend;
}

namespace Kalburator::Conflict {
    class ConflictHandler;
    class IMassDeleteGuard;
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
}
namespace Kalburator::Runtime { struct RunRequest; }

namespace WildPalms::Plugins { class PimPlugin; }   // substrate A1 — conduit descriptor

// K.8b T13: IBackendPluginV2 forward-decl dropped — the V2 plugin ABI is
// gone. registerPluginForTest overloads removed below (they had no live
// callers after K.8b T6 turned them into no-ops).

namespace WildPalms::Runtime {

class PalmDeviceAccess;

class PalmRuntime : public QObject, private PalmRuntimeAssembly {
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
    /// Routes cancellation into the runtime-owned coordinator.
    /// No-op if no sync is running.
    void cancelSync();

    void disconnectDevice();
    bool isDeviceConnected() const;

    /// Borrowed pointer to the underlying KPilotDeviceLink. Only valid after
    /// readyForSync(). Returns nullptr if not yet connected. KF6MainWindow uses
    /// this to read handshake information for device-management views.
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

    /// Provider lifecycle owned by the public collection runtime. These are
    /// the facade operations used by profile/account management; callers do
    /// not need a ProviderManager or BackendRegistry.
    QList<Kalburator::Runtime::ProviderSnapshot> providerSnapshots() const;
    bool hasCollectionRuntime() const { return m_collectionRuntime != nullptr; }
    QFuture<bool> connectProviders();
    bool addProvider(const Kalburator::Sync::BackendConfiguration &config,
                     QString &errorMessage);
    bool updateProvider(const Kalburator::Sync::BackendConfiguration &config,
                        QString &errorMessage);
    bool removeProvider(const QString &providerId, QString &errorMessage);

    /// Returns the IDs of all enabled mappings whose target backend is the
    /// Palm-side blob backend for the given domain (e.g. "calendar",
    /// "contacts", "memo", "todo"). Used by ClobberDialog to populate
    /// per-conduit checkboxes; the engine never consumes this.
    QList<QString> palmDirectMappingsForDomain(const QString &domain) const;

    /// Returns true iff the given mapping is Palm-direct (targets one of
    /// the Palm-side blob backends). Exposed mostly for testing.
    bool isPalmDirectMapping(const Kalburator::Sync::SyncMapping &m) const;

    bool isRunning() const { return m_running; }

    /// True while a runtime command is in flight.
    bool isSyncRunning() const { return m_running; }

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

    /// Update CollectionRuntime's mass-delete policy. Non-owning; the guard
    /// must outlive this runtime. nullptr restores allow-by-default behavior.
    void setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard);

    /// Compatibility accessor for the former conflict-store test seam.
    /// Connected production conflict state is owned by CollectionRuntime.
    Kalburator::Sync::SyncConflictStore *syncConflictStore() const;

    /// Apply UI-side resolved decisions to CollectionRuntime. This remains
    /// here because WildPalmsCore cannot include the engine-side synctypes.h.
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

    /// Per-mapping route status from the last route-spec translation
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

    /// Forwarded from CollectionRuntime. Fires every time a
    /// mapping with policy=AskUser encounters a conflict that the
    /// engine cannot auto-resolve. This signal exists so
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
    bool initializeCollectionRuntime(QString &errorMessage);
    bool applyCollectionRuntimeTopology(QString &errorMessage);
    void ensureHubCollections();
    /// Translate persisted category-route rows into runtime route specs and
    /// statuses. Physical route backends are materialized by CollectionRuntime.
    void buildRouteLogicalCalendars();

    /// Repopulate m_mappings from the borrowed Profile's persisted
    /// syncMappingsJson(). The Profile is the source of truth for
    /// user-configured (incl. remote DAV) mappings; without this the runtime
    /// starts empty and finishConnect() generates rawfiles defaults for every
    /// slot, silently discarding the user's wiring. No-op when m_profile is
    /// null (test/no-profile paths keep their injected mappings).
    void loadMappingsFromProfile();

    // Mirror direction — local enum avoids pulling synctypes.h into this header.
    enum class MirrorDir { PalmToPC, PCToPalm };

    QFuture<PalmRunResult> runCollectionRuntime(
        const Kalburator::Runtime::RunRequest &request);
    QFuture<PalmRunResult> runMirror(MirrorDir dir, const QString &modeLabel);

    /// Resolve a mapping's display label + theme icon name from m_palmPlugins
    /// (matches plugin->pluginId() against mapping.sourceBackend).
    void resolveMappingIdentity(const QString &mappingId,
                                QString &outLabel, QString &outIconName) const;
    bool m_collectionRuntimeTopologyReady = false;

    Kalburator::Conflict::ConflictHandler                *m_conflictHandler = nullptr;
    Profile                                              *m_profile = nullptr;   // borrowed; see setProfile
    // The watcher is the sole active-run/cancellation handle; it accepts any
    // QFuture<T> via setFuture().
    // C: canonical local hub. The compatibility engine's registry holds a
    // borrowed "wp-hub" pointer, so the hub must outlive that engine.
    // CollectionRuntime materializes filtered route endpoints from m_routeSpecs.
    // O7: per-PalmRuntime shape registries used by plugin loading and the
    // compatibility engine.
    // Compatibility-only conflict-store accessor state.
    // K.8b T6: owned plugin instances for the five static Palm plugins. The
    // setup-time PluginManager is a local in registerPalmPlugins().
    // T7: per-profile cached-revision store. Must be declared BEFORE
    // m_ownedBackends so it is destroyed AFTER them (reverse declaration
    // order) — the backends hold a borrowed pointer to this store.
    /// Substrate A3: per-mapping route status from the last
    /// buildRouteLogicalCalendars(). Keyed by SyncMapping::id.
    /// Substrate A3: desired category names the connect-time reconciler could
    /// not place (device table full), keyed by primary db name. Diagnostics +
    /// future UI; translateRouteSpec already reports these rows as NoFreeSlot.
};

}  // namespace WildPalms::Runtime

#endif
