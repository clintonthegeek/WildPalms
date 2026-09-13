// Phase J Task 10: PalmRuntime stress test.
// Runs 10 hotSync iterations against mock backends and asserts:
//   - no engine error across all iterations
//   - no resource leak (isRunning() returns false after each sync)
//   - no deadlock (each QTRY_VERIFY_WITH_TIMEOUT expires at most once)
//
// This guards against any regression in the post-K.8b engine teardown
// path (PalmRuntime::cancelSync, QFutureWatcher ownership, etc.).

#include <QTest>
#include <QFuture>
#include <QTemporaryDir>

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>
#include <kalburator/sync/backendregistry.h>
// K.8b T7: BlobBackendAdapter deleted; inject via BlobSyncBackendWrapper.
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

class TstRuntimeStress : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // O7: no global seeding — each PalmRuntime loads stock + WP plugins into
        // its own ShapeRegistries (see PalmRuntime::registerPalmPlugins).
    }

    void ten_hotSync_iterations_no_leak();
};

void TstRuntimeStress::ten_hotSync_iterations_no_leak()
{
    constexpr int kIterations = 10;

    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    // Source: one record in collection "palm:calendar/0".
    auto palmBlobOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *palmBlob = palmBlobOwned.get();
    {
        CollectionInfo ci;
        ci.id   = QStringLiteral("palm:calendar/0");
        ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);

        BackendRecord rec;
        rec.id   = QStringLiteral("stress-event-001");
        rec.data = QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n"
                              "UID:stress-event-001@palm\r\nSUMMARY:Stress Event\r\n"
                              "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
                              "END:VEVENT\r\nEND:VCALENDAR\r\n");
        palmBlob->createRecord(QStringLiteral("palm:calendar/0"), rec);
    }
    runtime.registerBackendInstanceForTest(QStringLiteral("palm-calendar"),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlobOwned), QStringLiteral("palm-calendar")));

    // Target: blob backend (starts empty, accumulates records).
    auto pcBlobOwned = std::make_unique<MockBlobBackend>();
    {
        CollectionInfo ci;
        ci.id   = QStringLiteral("pc-calendar/0");
        ci.name = QStringLiteral("PC Calendar");
        pcBlobOwned->createCollection(ci);
    }
    runtime.registerBackendInstanceForTest(QStringLiteral("pc-calendar"),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcBlobOwned), QStringLiteral("pc-calendar")));

    {
        SyncMapping m;
        m.id             = QStringLiteral("stress-mapping");
        m.sourceBackend  = QStringLiteral("palm-calendar");
        m.targetBackend  = QStringLiteral("pc-calendar");
        m.sourceCalendar = QStringLiteral("palm:calendar/0");
        m.targetCalendar = QStringLiteral("pc-calendar/0");
        m.mode           = SyncMode::TwoWay;
        m.enabled        = true;
        runtime.setMappingsForTest({m});
    }

    for (int i = 0; i < kIterations; ++i) {
        QVERIFY2(!runtime.isRunning(),
                 qPrintable(QStringLiteral("isRunning() true before iteration %1").arg(i)));

        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

        QVERIFY2(future.resultAt(0).success,
                 qPrintable(QStringLiteral("sync failed on iteration %1: %2")
                     .arg(i).arg(future.resultAt(0).errorMessage)));

        // After each sync the engine watcher must be released.
        QVERIFY2(!runtime.isRunning(),
                 qPrintable(QStringLiteral("isRunning() true after iteration %1").arg(i)));
    }
}

QTEST_GUILESS_MAIN(TstRuntimeStress)
#include "tst_runtime_stress.moc"
