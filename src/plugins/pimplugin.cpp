#include "pimplugin.h"

#include "palm/calendar/categorymappingstore.h"
#include <kalburator/types/collectioninfo.h>

namespace WildPalms::Plugins {

bool PimPlugin::matchesCollection(const Kalburator::Sync::CollectionInfo &c) const
{
    const QString d = domain().toString();
    if (d == QLatin1String("calendar")) {
        // contentTypes are authoritative when reported (DAV types everything
        // "calendar", including tasks-only collections); bare type is the
        // fallback for providers that don't report components (Akonadi).
        if (!c.contentTypes.isEmpty())
            return c.contentTypes.contains(QStringLiteral("VEVENT"));
        return c.type == QLatin1String("calendar");
    }
    if (d == QLatin1String("todo"))
        return c.type == QLatin1String("todos")
            || c.contentTypes.contains(QStringLiteral("VTODO"));
    if (d == QLatin1String("contacts"))
        return c.type == QLatin1String("contacts")
            || c.contentTypes.contains(QStringLiteral("VCARD"));
    if (d == QLatin1String("note"))
        return c.type == QLatin1String("memos");
    // Generic fallback for third-party domains: match the domain name.
    return c.type == d;
}

QStringList PimPlugin::categorySlotNames() const
{
    auto *store = categoryStore();
    if (!store) return {};
    return store->sixteenSlotNames(primaryDbName());
}

} // namespace WildPalms::Plugins
