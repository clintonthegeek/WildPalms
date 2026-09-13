#include "routemapping.h"

#include "plugins/pimplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include <kalburator/types/synctypes.h>

namespace WildPalms::Runtime {

RouteTranslation translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QList<WildPalms::Plugins::PimPlugin*> &conduits)
{
    RouteTranslation out;
    if (!p.enabled) return out;

    WildPalms::Plugins::PimPlugin *conduit = nullptr;
    for (auto *c : conduits)
        if (c->conduitId() == p.sourceBackend) { conduit = c; break; }
    if (!conduit) return out;

    const QString domain = conduit->domain().toString();

    RouteSpec spec;
    spec.domain             = domain;
    spec.hubCollectionId    = domain;
    spec.remoteBackendId    = p.targetBackend;
    spec.remoteCollectionId = p.targetCalendar;
    spec.lcId               = QStringLiteral("wp-route-") + p.id;

    if (p.sourceCalendar.isEmpty()) {
        spec.kind         = RouteSpec::Kind::Direct;
        spec.categoryName = QString();
        out.spec   = spec;
        out.status = RouteStatus::Active;
        return out;
    }

    const QString prefix =
        QStringLiteral("palm:") + domain + QStringLiteral("/name:");
    if (!p.sourceCalendar.startsWith(prefix)) return out;   // malformed
    const QString name = p.sourceCalendar.mid(prefix.size());
    if (name.isEmpty()) return out;

    spec.kind         = RouteSpec::Kind::Filtered;
    spec.categoryName = name;
    out.spec          = spec;
    out.categoryName  = name;

    // Status from the conduit's reconciled category store. The route is
    // produced regardless — hub<->remote filtering works by name; the status
    // reports the device-side binding state. NB: CategoryMappingStore::
    // slotForName returns UnfiledSlot (0) for BOTH the "Unfiled" name and a
    // not-found name, so a real on-device binding is slot > 0 (named routes
    // never target Unfiled — that is the empty/Direct case).
    auto *store = conduit->categoryStore();
    const QString db = conduit->primaryDbName();
    if (!store || store->populatedSlots(db).isEmpty()) {
        out.status = RouteStatus::WaitingForDevice;
    } else if (store->slotForName(db, name) > 0) {
        out.status = RouteStatus::Active;
    } else {
        out.status = RouteStatus::NoFreeSlot;
    }
    return out;
}

} // namespace WildPalms::Runtime
