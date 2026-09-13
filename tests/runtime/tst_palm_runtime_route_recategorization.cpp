#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "plugins/calendar/calendarbackendplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"
#include "palm/calendar/datebookcodec.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/shape/shape.h>
#include <kalburator/universal/genericsqlitebackend.h>

#include "../blobsyncbackendwrapper.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

class TstPalmRuntimeRouteRecategorization : public QObject { Q_OBJECT
private slots:
    void initTestCase() {}

    void recategorization_moves_record_between_route_remotes()
    {
        // 1. Stand up the runtime and install two route mappings:
        //    slot 3 ("Work") → WorkCal, slot 4 ("Home") → HomeCal.
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        QJsonObject mWork;
        mWork[QStringLiteral("id")]              = QStringLiteral("u-work");
        mWork[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        mWork[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/name:Work");
        mWork[QStringLiteral("targetBackend")]   = QStringLiteral("mock-remote");
        mWork[QStringLiteral("targetCalendar")]  = QStringLiteral("WorkCal");
        mWork[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        mWork[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        mWork[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        mWork[QStringLiteral("enabled")]         = true;

        QJsonObject mHome;
        mHome[QStringLiteral("id")]              = QStringLiteral("u-home");
        mHome[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        mHome[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/name:Home");
        mHome[QStringLiteral("targetBackend")]   = QStringLiteral("mock-remote");
        mHome[QStringLiteral("targetCalendar")]  = QStringLiteral("HomeCal");
        mHome[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        mHome[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        mHome[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        mHome[QStringLiteral("enabled")]         = true;

        QJsonArray arr; arr.append(mWork); arr.append(mHome);
        runtime.reloadMappings(arr);

        // 2. Name slot 3 = "Work", slot 4 = "Home" in the calendar plugin's
        //    CategoryMappingStore.
        using WildPalms::CalendarPlugin::CalendarBackendPlugin;
        CalendarBackendPlugin *cal = nullptr;
        for (const auto &p : runtime.palmPlugins())
            if (auto *c = dynamic_cast<CalendarBackendPlugin*>(p.get())) cal = c;
        QVERIFY(cal);
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 4, QStringLiteral("Home"));

        // 3. Register a single MockBlobBackend as "mock-remote" with two
        //    collections: WorkCal and HomeCal.  Both carry shape (calendar, ical)
        //    — the same shape the real CalDAV backend exposes.
        const Shape calIcalShape{ DomainId{QStringLiteral("calendar")},
                                   EncodingId{QStringLiteral("ical")} };

        auto mockRemoteBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *mockRemoteRaw = mockRemoteBlob.get();
        {
            CollectionInfo workcal;
            workcal.id   = QStringLiteral("WorkCal");
            workcal.name = QStringLiteral("WorkCal");
            workcal.type = QStringLiteral("calendar");
            mockRemoteBlob->createCollection(workcal);

            CollectionInfo homecal;
            homecal.id   = QStringLiteral("HomeCal");
            homecal.name = QStringLiteral("HomeCal");
            homecal.type = QStringLiteral("calendar");
            mockRemoteBlob->createCollection(homecal);
        }
        runtime.registerBackendInstanceForTest(
            QStringLiteral("mock-remote"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(mockRemoteBlob), QStringLiteral("mock-remote"), calIcalShape));

        // 4. Build a Palm record in DatebookDB category slot 3 ("Work").
        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        mockDb->createDatabase(QStringLiteral("DatebookDB"));
        auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        event->setSummary(QStringLiteral("Team standup"));
        event->setDtStart(QDateTime(QDate(2026,6,2), QTime(9,0), QTimeZone::utc()));
        event->setDtEnd  (QDateTime(QDate(2026,6,2), QTime(9,30), QTimeZone::utc()));
        WildPalms::PalmSync::PalmRecord pr =
            WildPalms::PalmCalendar::DatebookCodec::encode(event, /*slot*/ 3);
        pr.recordId = 201;
        mockDb->createRecord(QStringLiteral("DatebookDB"), pr);

        // 5. Connect mock device — triggers finishConnect → builds route LCs.
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        // 6. First hotSync: the slot-3 record lands on WorkCal; HomeCal stays empty.
        {
            auto fut = runtime.hotSync();
            QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
            QVERIFY(fut.resultAt(0).success);
        }
        QCOMPARE(mockRemoteRaw->loadRecords(QStringLiteral("WorkCal")).size(), 1);
        QCOMPARE(mockRemoteRaw->loadRecords(QStringLiteral("HomeCal")).size(), 0);

        // 7. Mutate the hub canon: change the record's categories from ["Work"]
        //    to ["Home"].  This simulates a future hub-side UI recategorization.
        {
            auto *hubRaw = dynamic_cast<Kalburator::Sinks::GenericSqliteBackend*>(
                runtime.backendRegistry().backendInstance(QStringLiteral("wp-hub")));
            QVERIFY(hubRaw);

            const auto hubRecords = hubRaw->loadRecords(QStringLiteral("calendar"));
            QCOMPARE(hubRecords.size(), 1);

            BackendRecord modified = hubRecords.first();

            // Rewrite the categories array in the canon-JSON payload.
            QJsonDocument doc = QJsonDocument::fromJson(modified.data);
            QVERIFY(!doc.isNull());
            QJsonObject obj = doc.object();
            QJsonArray newCats;
            newCats.append(QStringLiteral("Home"));
            obj.insert(QStringLiteral("categories"), newCats);
            modified.data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

            // Update contentHash and lastModified so the engine detects a change.
            modified.contentHash = QString::fromUtf8(
                QCryptographicHash::hash(modified.data, QCryptographicHash::Sha256).toHex());
            modified.lastModified = QDateTime::currentDateTimeUtc();

            QVERIFY(hubRaw->updateRecord(modified));
        }

        // 8. Second hotSync: the record must move off WorkCal and onto HomeCal.
        {
            auto fut = runtime.hotSync();
            QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
            QVERIFY(fut.resultAt(0).success);
        }
        QCOMPARE(mockRemoteRaw->loadRecords(QStringLiteral("WorkCal")).size(), 0);
        QCOMPARE(mockRemoteRaw->loadRecords(QStringLiteral("HomeCal")).size(), 1);
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeRouteRecategorization)
#include "tst_palm_runtime_route_recategorization.moc"
