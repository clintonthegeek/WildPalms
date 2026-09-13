#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
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

#include "../blobsyncbackendwrapper.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

class TstPalmRuntimeRouteFirstSync : public QObject { Q_OBJECT
private slots:
    void initTestCase() {}

    void palm_slot3_record_lands_on_mock_remote_workcal()
    {
        // 1. Stand up the runtime + persist a route mapping.
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u1");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/name:Work");
        m[QStringLiteral("targetBackend")]   = QStringLiteral("mock-remote");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("WorkCal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        // 2. Name slot 3 = "Work" in the calendar plugin's CategoryMappingStore.
        using WildPalms::CalendarPlugin::CalendarBackendPlugin;
        CalendarBackendPlugin *cal = nullptr;
        for (const auto &p : runtime.palmPlugins())
            if (auto *c = dynamic_cast<CalendarBackendPlugin*>(p.get())) cal = c;
        QVERIFY(cal);
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));

        // 3. Register a MockBlobBackend as the "mock-remote" backend with
        //    collection "WorkCal". Shape is (calendar, ical) — the same shape the
        //    real CalDAV backend exposes and that the engine uses on the remote side
        //    of a calendar route mapping.
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
        }
        runtime.registerBackendInstanceForTest(
            QStringLiteral("mock-remote"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(mockRemoteBlob), QStringLiteral("mock-remote"), calIcalShape));

        // 4. Build a Palm record in DatebookDB category slot 3.
        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        mockDb->createDatabase(QStringLiteral("DatebookDB"));
        auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        event->setSummary(QStringLiteral("Sprint planning"));
        event->setDtStart(QDateTime(QDate(2026,6,1), QTime(9,0), QTimeZone::utc()));
        event->setDtEnd  (QDateTime(QDate(2026,6,1), QTime(10,0), QTimeZone::utc()));
        WildPalms::PalmSync::PalmRecord pr =
            WildPalms::PalmCalendar::DatebookCodec::encode(event, /*slot*/ 3);
        pr.recordId = 101;
        mockDb->createRecord(QStringLiteral("DatebookDB"), pr);

        // 5. Connect mock device — triggers finishConnect → builds route LCs.
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        // 6. Hot sync. WorkCal should gain the record.
        auto fut = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
        QVERIFY(fut.resultAt(0).success);

        const auto remoteRecords = mockRemoteRaw->loadRecords(QStringLiteral("WorkCal"));
        QCOMPARE(remoteRecords.size(), 1);
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeRouteFirstSync)
#include "tst_palm_runtime_route_first_sync.moc"
