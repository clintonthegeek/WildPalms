#ifndef WILDPALMS_RUNTIME_PALMRUNTIMEASSEMBLY_H
#define WILDPALMS_RUNTIME_PALMRUNTIMEASSEMBLY_H

#include <QDir>
#include <QHash>
#include <QList>
#include <QString>
#include <memory>
#include <vector>

#include "palmrunresult.h"
#include "routemapping.h"
#include "palm/sync/palmrevisionstore.h"
#include <kalburator/shape/shaperegistries.h>

namespace Kalburator { class PluginManager; class Plugin; }
namespace Kalburator::Sync {
class BackendRegistry;
class SyncBackendBase;
class SyncConflictStore;
struct SyncMapping;
}
namespace Kalburator::Runtime { class CollectionRuntime; }
namespace Kalburator::Conflict { class IMassDeleteGuard; }
namespace Kalburator::Sinks {
class GenericSqliteBackend;
}
namespace WildPalms::Runtime { class PalmDeviceAccess; }

namespace WildPalms::Runtime {

/// Owns the mutable Palm collection runtime graph. PalmRuntime remains the
/// public command/event facade and no longer declares these collaborators.
class PalmRuntimeAssembly {
protected:
    explicit PalmRuntimeAssembly(const QString &profilePath);
    ~PalmRuntimeAssembly();

    QString m_profilePath;
    QString m_backupRoot;
    std::unique_ptr<PalmDeviceAccess> m_device;
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_registry;
    std::unique_ptr<Kalburator::Sinks::GenericSqliteBackend> m_hub;
    Kalburator::Shape::ShapeRegistries m_shape;
    std::unique_ptr<Kalburator::Sync::SyncConflictStore> m_compatConflictStore;
    // Public runtime owner used by the WP-009 cutover. The conflict store is
    // retained only for the explicit legacy test accessor.
    std::unique_ptr<Kalburator::Runtime::CollectionRuntime> m_collectionRuntime;
    std::vector<std::unique_ptr<Kalburator::Plugin>> m_palmPlugins;
    QStringList m_enabledPluginIds;
    QList<Kalburator::Sync::SyncMapping> m_mappings;
    bool m_running = false;
    std::unique_ptr<WildPalms::PalmSync::PalmRevisionStore> m_palmRevisionStore;
    Kalburator::Conflict::IMassDeleteGuard *m_massDeleteGuard = nullptr;
    std::vector<std::unique_ptr<Kalburator::Sync::SyncBackendBase>> m_ownedBackends;
    QHash<QString, Kalburator::Sync::SyncBackendBase *> m_injectedBackends;
    QHash<QString, WildPalms::Runtime::RouteStatus> m_routeStatuses;
    QHash<QString, WildPalms::Runtime::RouteSpec> m_routeSpecs;
    QHash<QString, QStringList> m_categoryNoFreeSlot;
};

} // namespace WildPalms::Runtime

#endif
