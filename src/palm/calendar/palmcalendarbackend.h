#ifndef WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
#define WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H

#include <kalburator/calendar/syncbackend.h>
#include <kalburator/calendar/syncoperation.h>   // Kalburator::Sync::{Fetch,Push,Delete}Operation complete-type for covariant overrides

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmCalendar {

class CategoryMappingStore;

/**
 * @brief Calendar-typed SyncBackend wrapping PalmBackend's datebook.
 *
 * Exposes the Palm DatebookDB as a collection `"palm:datebook"` and
 * surfaces virtual sub-calendars per category slot:
 *   - `palm:calendar/0`   "Unfiled" (always present)
 *   - `palm:calendar/<N>` N in 1..15, present iff
 *     `CategoryMappingStore::slotName("DatebookDB", N)` is non-empty.
 *
 * Records are routed to/from virtual calendars by PalmRecord.category.
 *
 * Lifetime: does NOT own the IPalmDatabaseAccess or CategoryMappingStore.
 * Caller retains ownership; both must outlive the backend.
 *
 * Scope (Phase E.6): loadCalendars, fetchItems, pushItems, deleteItems
 * fully implemented. Legacy scaffolding (storeCalendars, startSync,
 * removeItem) satisfies the abstract interface; Calendar CRUD at the
 * sub-calendar level returns false (Palm slots are implicit).
 */
class PalmCalendarBackend : public Kalburator::Sync::SyncBackend
{
    Q_OBJECT
public:
    /// Collection ID the backend responds to.
    static constexpr const char *CollectionId = "palm:datebook";
    /// Palm database name this backend wraps.
    static constexpr const char *DatabaseName = "DatebookDB";
    /// Prefix of every virtual sub-calendar ID.
    static constexpr const char *CalendarIdPrefix = "palm:calendar/";

    explicit PalmCalendarBackend(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        CategoryMappingStore *categoryStore,
        QObject *parent = nullptr);
    ~PalmCalendarBackend() override;

    // ========== Identity ==========
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // ========== Discovery ==========
    void loadCalendars(const QString &collectionId) override;

    // ========== Legacy pure-virtual scaffolding (Task 6) ==========
    void storeCalendars(
        const QString &collectionId,
        const QList<KCalendarCore::MemoryCalendar *> &calendars) override;
    void startSync(
        const QString &collectionId,
        KCalendarCore::MemoryCalendar *calendar,
        const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
        const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
        const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    // ========== Blob-level disconnect guard (Layer B) ==========
    bool loadRecordsOrError(const QString &collectionId,
                            QList<Kalburator::Sync::BackendRecord> &records,
                            QString &error) override;

    // ========== Operation-based API (Task 5) ==========
    Kalburator::Sync::FetchOperation *fetchItems(
        const QString &calendarId) override;
    Kalburator::Sync::PushOperation *pushItems(
        const QString &calendarId,
        const QList<KCalendarCore::Incidence::Ptr> &items) override;
    Kalburator::Sync::DeleteOperation *deleteItems(
        const QString &calendarId, const QStringList &uids) override;

    // Exposed for testing: parse slot from calendarId "palm:calendar/<N>".
    // Returns -1 on bad ID.
    static int slotFromCalendarId(const QString &calendarId);
    static QString calendarIdForSlot(int slot);

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    CategoryMappingStore *m_categoryStore = nullptr;
};

} // namespace WildPalms::PalmCalendar

#endif // WILDPALMS_CALENDAR_PALMCALENDARBACKEND_H
