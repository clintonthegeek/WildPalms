#include <QtTest/QtTest>
#include <QSignalSpy>

#include <kalburator/shape/shape.h>
#include <kalburator/calendar/syncoperation.h>

#include "categorymappingstore.h"
#include "mockpalmdatabaseaccess.h"
#include "palmcalendarbackend.h"
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCalendar::PalmCalendarBackend;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestPalmCalendarBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void slotFromCalendarIdParsing();
    void calendarIdForSlotFormatting();
    void loadCalendarsEmptyStoreYieldsOnlyUnfiled();
    void loadCalendarsWithPopulatedSlots();
    void loadCalendarsUnknownCollectionFails();

    void fetchItemsReturnsOnlyMatchingSlot();
    void fetchItemsSkipsDeletedRecords();
    void fetchItemsInvalidCalendarIdFails();
    void pushItemsCreatesNewRecordsWithCorrectSlot();
    void pushItemsUpdatesExistingRecord();
    void pushItemsWithNonEventSkipsAndReportsFailed();
    void deleteItemsRemovesFromDevice();
    void deleteItemsMissingRecordReportsFailed();
    void pushThenFetchRoundTripsIncidence();
    void pushToUnnamedSlotStillStores();
    void deleteItemsInvalidCalendarIdFails();
    void fetchItemsEmptySlotReturnsEmpty();
    void deleteRecord_usesDatebookDBCanonicalName();
};

void TestPalmCalendarBackend::identity()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QCOMPARE(backend.backendType(), QStringLiteral("palm-calendar"));
    QCOMPARE(backend.nativeShapes().size(), 1);
    QCOMPARE(backend.nativeShapes().first().domain.toString(), QStringLiteral("calendar"));
    QCOMPARE(backend.nativeShapes().first().encoding.toString(), QStringLiteral("palm-datebook"));
}

void TestPalmCalendarBackend::slotFromCalendarIdParsing()
{
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/0")), 0);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/15")), 15);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/16")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("palm:calendar/abc")), -1);
    QCOMPARE(PalmCalendarBackend::slotFromCalendarId(
                 QStringLiteral("local:memo:1")), -1);
}

void TestPalmCalendarBackend::calendarIdForSlotFormatting()
{
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(0),
             QStringLiteral("palm:calendar/0"));
    QCOMPARE(PalmCalendarBackend::calendarIdForSlot(7),
             QStringLiteral("palm:calendar/7"));
}

void TestPalmCalendarBackend::loadCalendarsEmptyStoreYieldsOnlyUnfiled()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 1);
    QCOMPARE(discovered[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(discovered[0][1].toString(), QStringLiteral("palm:calendar/0"));

    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("palm:datebook"));
    QCOMPARE(finished[0][1].toBool(), true);
}

void TestPalmCalendarBackend::loadCalendarsWithPopulatedSlots()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Kids"));
    store.setSlotName(QStringLiteral("DatebookDB"), 5, QStringLiteral("Travel"));

    PalmCalendarBackend backend(&dev, &store);
    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("palm:datebook"));

    QCOMPARE(discovered.size(), 4);  // 0 + 1 + 3 + 5
    QStringList ids;
    for (const auto &args : discovered) {
        ids << args[1].toString();
    }
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/0")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/1")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/3")));
    QVERIFY(ids.contains(QStringLiteral("palm:calendar/5")));
}

void TestPalmCalendarBackend::loadCalendarsUnknownCollectionFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    QSignalSpy discovered(&backend, &PalmCalendarBackend::calendarDiscovered);
    QSignalSpy finished (&backend, &PalmCalendarBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("palm:memos"));

    QCOMPARE(discovered.size(), 0);
    QCOMPARE(finished.size(),   1);
    QCOMPARE(finished[0][1].toBool(), false);
    QCOMPARE(finished[0][2].toString(),
             QStringLiteral("not a Palm calendar collection"));
}

#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>

#include "datebookcodec.h"
#include "palmrecord.h"

using KCalendarCore::Event;
using KCalendarCore::Incidence;
using Kalburator::Sync::SyncOperation;
using WildPalms::PalmCalendar::DatebookCodec;
using WildPalms::PalmSync::PalmRecord;

namespace {

/// Push a minimal all-day event via the codec directly so we can stage
/// records on the mock device without going through the backend. This
/// keeps fetchItems tests independent of pushItems.
PalmRecord stageDatebookRecord(MockPalmDatabaseAccess &dev, int slot,
                               const QString &summary,
                               std::uint8_t extraAttrs = 0)
{
    auto ev = Event::Ptr::create();
    ev->setSummary(summary);
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime));
    auto rec = DatebookCodec::encode(ev, slot);
    rec.attributes |= extraAttrs;
    dev.createDatabase(QStringLiteral("DatebookDB"));
    const auto id = dev.createRecord(QStringLiteral("DatebookDB"), rec);
    rec.recordId = id;
    return rec;
}

} // namespace

void TestPalmCalendarBackend::fetchItemsReturnsOnlyMatchingSlot()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    stageDatebookRecord(dev, 3, QStringLiteral("Work event"));
    stageDatebookRecord(dev, 3, QStringLiteral("Another work"));
    stageDatebookRecord(dev, 7, QStringLiteral("Personal event"));

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/3"));
    QVERIFY(op);
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->fetchedItems().size(), 2);
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsSkipsDeletedRecords()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    stageDatebookRecord(dev, 0, QStringLiteral("Live"));
    stageDatebookRecord(dev, 0, QStringLiteral("Dead"),
                        /*extraAttrs=*/PalmRecord::AttrDeleted);

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/0"));
    QCOMPARE(op->fetchedItems().size(), 1);
    QCOMPARE(op->fetchedItems().first()->summary(), QStringLiteral("Live"));
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsInvalidCalendarIdFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto *op = backend.fetchItems(QStringLiteral("local:memo:1"));
    QCOMPARE(op->state(), SyncOperation::Failed);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsCreatesNewRecordsWithCorrectSlot()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("New meeting"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 7, 1), QTime(0, 0), Qt::LocalTime));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/9"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 1);
    QCOMPARE(op->failedUids().size(),    0);

    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 9);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsUpdatesExistingRecord()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    // Stage an existing record.
    const auto existing = stageDatebookRecord(dev, 2, QStringLiteral("Old"));
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 1);

    // Build an event with the existing record ID.
    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("New"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 7, 2), QTime(0, 0), Qt::LocalTime));
    ev->setCustomProperty("KCalendarCore",
                          QByteArray(DatebookCodec::RecordIdProperty),
                          QString::number(existing.recordId));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/2"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    // Still only one record (update, not create).
    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);

    // Decode it and check the summary updated.
    const auto rt = DatebookCodec::decode(stored.first());
    QVERIFY(rt.isValid());
    QCOMPARE(rt.event->summary(), QStringLiteral("New"));
    op->deleteLater();
}

void TestPalmCalendarBackend::pushItemsWithNonEventSkipsAndReportsFailed()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto todo = KCalendarCore::Todo::Ptr::create();
    todo->setSummary(QStringLiteral("Not an event"));
    todo->setUid(QStringLiteral("not-an-event"));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/0"),
                                 { todo.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 0);
    QCOMPARE(op->failedUids().size(),    1);
    QCOMPARE(op->failedUids().first(),   QStringLiteral("not-an-event"));
    op->deleteLater();
}

void TestPalmCalendarBackend::deleteItemsRemovesFromDevice()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    const auto staged = stageDatebookRecord(dev, 0, QStringLiteral("Doomed"));
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 1);

    const auto uid = QStringLiteral("palm-datebook-%1").arg(staged.recordId);
    auto *op = backend.deleteItems(QStringLiteral("palm:calendar/0"),
                                   QStringList{ uid });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids(), QStringList{ uid });
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 0);
    op->deleteLater();
}

void TestPalmCalendarBackend::deleteItemsMissingRecordReportsFailed()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    dev.createDatabase(QStringLiteral("DatebookDB"));

    auto *op = backend.deleteItems(
        QStringLiteral("palm:calendar/0"),
        QStringList{ QStringLiteral("palm-datebook-999") });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 0);
    QCOMPARE(op->failedUids().size(),    1);
    op->deleteLater();
}

void TestPalmCalendarBackend::pushThenFetchRoundTripsIncidence()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    store.setSlotName(QStringLiteral("DatebookDB"), 7, QStringLiteral("Trips"));
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("Flight"));
    ev->setDtStart(QDateTime(QDate(2026, 8, 15), QTime(10, 0), Qt::LocalTime));
    ev->setDtEnd  (QDateTime(QDate(2026, 8, 15), QTime(14, 0), Qt::LocalTime));
    ev->setDescription(QStringLiteral("Gate B12"));

    auto *pushOp = backend.pushItems(QStringLiteral("palm:calendar/7"),
                                     { ev.staticCast<Incidence>() });
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    pushOp->deleteLater();

    auto *fetchOp = backend.fetchItems(QStringLiteral("palm:calendar/7"));
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    QCOMPARE(fetchOp->fetchedItems().size(), 1);

    const auto rt = fetchOp->fetchedItems().first();
    QCOMPARE(rt->summary(),     QStringLiteral("Flight"));
    QCOMPARE(rt->description(), QStringLiteral("Gate B12"));
    QCOMPARE(rt.staticCast<Event>()->dtStart().time(), QTime(10, 0));
    fetchOp->deleteLater();
}

void TestPalmCalendarBackend::pushToUnnamedSlotStillStores()
{
    // Slot 12 is not in the store, but we can still push to it.
    // loadCalendars won't surface the slot, but the record persists.
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto ev = Event::Ptr::create();
    ev->setSummary(QStringLiteral("Unnamed slot"));
    ev->setAllDay(true);
    ev->setDtStart(QDateTime(QDate(2026, 9, 1), QTime(0, 0), Qt::LocalTime));

    auto *op = backend.pushItems(QStringLiteral("palm:calendar/12"),
                                 { ev.staticCast<Incidence>() });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    op->deleteLater();

    const auto stored = dev.readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(stored.size(), 1);
    QCOMPARE(static_cast<int>(stored.first().category), 12);
}

void TestPalmCalendarBackend::deleteItemsInvalidCalendarIdFails()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    auto *op = backend.deleteItems(
        QStringLiteral("local:memo:1"),
        QStringList{ QStringLiteral("palm-datebook-1") });
    QCOMPARE(op->state(), SyncOperation::Failed);
    op->deleteLater();
}

void TestPalmCalendarBackend::fetchItemsEmptySlotReturnsEmpty()
{
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    // Stage records for slot 3 only; fetch slot 4.
    stageDatebookRecord(dev, 3, QStringLiteral("foo"));

    auto *op = backend.fetchItems(QStringLiteral("palm:calendar/4"));
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QVERIFY(op->fetchedItems().isEmpty());
    op->deleteLater();
}

void TestPalmCalendarBackend::deleteRecord_usesDatebookDBCanonicalName()
{
    // Regression guard: deleteItems must route the delete to "DatebookDB"
    // (the canonical Palm database name) regardless of how the record UID
    // was encoded. If the backend decoded the UID into a non-canonical name
    // (e.g. "DatebookdbDB") the mock device would return false and the
    // record would survive — the guard below catches that regression.
    MockPalmDatabaseAccess dev;
    CategoryMappingStore store;
    PalmCalendarBackend backend(&dev, &store);

    const auto staged = stageDatebookRecord(dev, 0, QStringLiteral("ToDelete"));
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 1);

    const QString uid =
        QStringLiteral("palm-datebook-%1").arg(staged.recordId);
    auto *op = backend.deleteItems(QStringLiteral("palm:calendar/0"),
                                   QStringList{ uid });
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids(), QStringList{ uid });

    // The record must actually be gone from the device — not a silent no-op.
    QCOMPARE(dev.readAllRecords(QStringLiteral("DatebookDB")).size(), 0);
    op->deleteLater();
}

QTEST_MAIN(TestPalmCalendarBackend)
#include "tst_palmcalendarbackend.moc"
