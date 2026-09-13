// src/plugins/pimplugin.h
#ifndef WILDPALMS_PLUGINS_PIMPLUGIN_H
#define WILDPALMS_PLUGINS_PIMPLUGIN_H

#include <kalburator/plugin/plugin.h>   // Kalburator::Plugin (libkalburator)
#include <kalburator/shape/shape.h>    // Kalburator::Shape::DomainId
#include <QStringList>
#include <memory>

namespace Kalburator::Sync { class SyncBackendBase; struct CollectionInfo; }
namespace Kalburator::Conflict { class ConflictHandler; }
namespace WildPalms::Runtime { class PalmRuntime; class PalmDeviceAccess; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
class QWidget;

namespace WildPalms::Plugins {

/**
 * @brief WP-local base for Palm conduit plugins — the CONDUIT DESCRIPTOR.
 *
 * Sub-project A (2026-06-11 config substrate): the surface the four stock
 * conduits implemented by convention is promoted to virtuals here, so
 * PalmRuntime, the wizard, and the graph enumerate conduits generically
 * instead of dynamic_casting to concrete types. A third-party conduit
 * participates everywhere by subclassing this and joining the load batch.
 */
class PimPlugin : public Kalburator::Plugin {
public:
    // ── lifecycle hooks (pre-existing) ─────────────────────────────
    virtual void setHub(Kalburator::Sync::SyncBackendBase *hub) { Q_UNUSED(hub); }
    virtual void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) {
        Q_UNUSED(runtime);
    }

    // ── identity / declaration ─────────────────────────────────────
    /// Bare conduit id ("calendar", "todo", …). PERSISTED in mapping rows
    /// as sourceBackend — treat as a frozen identifier.
    virtual QString conduitId() const = 0;
    /// Hub-collection domain. Note memo's domain is "note".
    virtual Kalburator::Shape::DomainId domain() const = 0;
    virtual QString primaryDbName() const = 0;
    virtual QStringList claimedDatabases() const { return { primaryDbName() }; }
    virtual QString conduitDisplayName() const = 0;
    virtual QString conduitIconName() const { return QStringLiteral("folder-sync"); }

    // ── capabilities ───────────────────────────────────────────────
    virtual bool supportsCategories() const { return true; }
    /// Which provider collections can serve as a sync target for this
    /// conduit. Default: per-domain type/contentTypes matching (subsumes
    /// the old app/wizard/domainfilter.cpp); override for custom domains
    /// with richer source semantics.
    virtual bool matchesCollection(const Kalburator::Sync::CollectionInfo &c) const;

    // ── factories ──────────────────────────────────────────────────
    virtual std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) = 0;
    /// Default nullptr — a conduit without conflict UI is legal.
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler() { return nullptr; }
    /// Contract: non-null iff supportsCategories(). Default nullptr.
    virtual WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const { return nullptr; }
    /// 16-entry slot-name snapshot; empty if no store / not yet populated.
    virtual QStringList categorySlotNames() const;
    virtual bool hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent) const { Q_UNUSED(parent); return nullptr; }
};

} // namespace WildPalms::Plugins

#endif // WILDPALMS_PLUGINS_PIMPLUGIN_H
