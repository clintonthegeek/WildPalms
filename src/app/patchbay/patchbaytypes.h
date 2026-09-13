// src/app/patchbay/patchbaytypes.h
#pragma once

#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <functional>

#include <kalburator/types/collectioninfo.h>

namespace WildPalms::AppPatchbay {

/// Route-domain colors (spec §14). Domain hue identity is fixed.
inline QColor domainColor(const QString &domain)
{
    if (domain == QLatin1String("calendar")) return QColor(0x5b, 0x8d, 0xd9);
    if (domain == QLatin1String("contacts")) return QColor(0x5f, 0xb8, 0x78);
    if (domain == QLatin1String("note"))     return QColor(0xd9, 0xa4, 0x5b);
    if (domain == QLatin1String("todo"))     return QColor(0xb0, 0x7f, 0xd4);
    return QColor(0x8a, 0x93, 0xa3);
}

/// Value-copied conduit descriptor facts (decouples the model from
/// PimPlugin headers; tests construct these directly).
struct ConduitFacts {
    QString conduitId;     ///< persisted as sourceBackend ("calendar", "memo", …)
    QString domain;        ///< route domain ("calendar", "note", …) — PimPlugin::domain().toString()
    QString dbName;        ///< primaryDbName ("DatebookDB", …)
    QString displayName;   ///< conduitDisplayName()
    std::function<bool(const Kalburator::Sync::CollectionInfo &)> matchesCollection;
};

enum class PortKind { WholeDomain, Category, PalmSlot, RemoteCollection, AddCategory };

struct PortDesc {
    QString id;            ///< side-less id, see grammar below
    QString label;
    QString domain;
    PortKind kind = PortKind::RemoteCollection;
    bool waiting = false;     ///< category port: RouteStatus::WaitingForDevice
    bool noFreeSlot = false;  ///< category port: RouteStatus::NoFreeSlot
};

/// Port id grammar (anchor ids add "@l"/"@r" suffix at the item layer):
///   hub whole-domain:  "dom:<domain>"
///   hub category:      "cat:<domain>/<categoryName>"
///   hub add-category:  "add:<domain>"
///   palm slot:         "slot:<dbName>/<index>"
///   palm whole-DB:     "db:<dbName>"
///   remote collection: "col:<collectionId>|<domain>"

struct BandDesc {
    QString domain;        ///< empty for remote nodes' single band
    QString title;
    QString footer;        ///< e.g. "first sync pending"; empty allowed
    QList<PortDesc> ports;
};

enum class NodeKind { Palm, Hub, Remote, GhostRemote };

struct NodeDesc {
    QString id;            ///< "palm" | "hub" | "remote:<providerId>" | "ghost:<providerId>"
    NodeKind kind = NodeKind::Remote;
    QString title;
    QString subtitle;      ///< connection state / busy text / last sync
    bool ghosted = false;
    QList<BandDesc> bands;
};

enum class WireState { TwoWay, OneWayUpload, OneWayDownload, Disabled, Broken };

struct WireDesc {
    QString mappingId;
    QString sourcePortId;  ///< hub port ("dom:…" or "cat:…")
    QString targetNodeId;  ///< "remote:<providerId>" or "ghost:<providerId>"
    QString targetPortId;  ///< "col:<collectionId>|<domain>"
    QString domain;
    WireState state = WireState::TwoWay;
    QString beadGlyph;     ///< "✗" for Broken in Part 1; run history arrives in Part 2
};

enum class StrandState { Solid, Ghost };

struct StrandDesc {
    QString id;            ///< "strand:<hubPortId>"
    QString palmPortId;    ///< "slot:<db>/<i>", or "db:<db>" for ghost/whole-domain
    QString hubPortId;
    QString domain;
    StrandState state = StrandState::Solid;
    bool wholeDomain = false;
};

} // namespace WildPalms::AppPatchbay
