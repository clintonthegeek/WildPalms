// K.8b T16: Verify PalmRuntime::cancelSync() — mid-sync cancel restoration.
//
// This test covers:
//   1. cancelSync() is a no-op (no crash) when no sync is running.
//   2. cancelSync() is a no-op when called after a sync has completed.
//   3. Plan 8 B.3: cancelSync() mid-run still emits runFinished and
//      finishes the returned future, on both the single-mapping (mirror)
//      and multi-mapping (hotSync) paths. Qt6's QFuture::then() drops its
//      continuation when the source future cancels, so result delivery
//      must not ride a .then() — it rides the watcher's finished slot.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QThread>
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/plugin/stock_plugins.h>
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

/// MockBlobBackend whose loadRecords stalls long enough for the test
/// thread to cancel the run mid-flight (deterministic cancel window).
class SlowLoadBlobBackend : public Kalburator::Sync::MockBlobBackend {
public:
    QList<BackendRecord> loadRecords(const QString &collectionId) override {
        QThread::msleep(250);
        return Kalburator::Sync::MockBlobBackend::loadRecords(collectionId);
    }
};

class TstPalmRuntimeCancelSync : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // O7: no global seeding needed — each PalmRuntime self-loads stock + WP
        // plugins into its own ShapeRegistries (see PalmRuntime::registerPalmPlugins).
        qRegisterMetaType<WildPalms::Runtime::PalmRunResult>();
    }

    void cancelSync_noSync_nocrash()
    {
        // cancelSync() must be a safe no-op when nothing is running.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());
        QCOMPARE(runtime.isRunning(), false);

        // Must not crash.
        runtime.cancelSync();

        QCOMPARE(runtime.isRunning(), false);
    }

    void cancelSync_afterSync_nocrash()
    {
        // cancelSync() called after a sync completes must be a safe no-op:
        // the watcher cleans itself up on finished, so calling cancel on it
        // after the fact should not crash.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // Minimal backend setup for a real (fast) hotSync.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm-calendar")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-calendar/0");
            ci.name = QStringLiteral("PC Calendar");
            pcBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc-calendar")));

        {
            SyncMapping m;
            m.id             = QStringLiteral("cancel-test-mapping");
            m.sourceBackend  = QStringLiteral("palm-calendar");
            m.targetBackend  = QStringLiteral("pc-calendar");
            m.sourceCalendar = QStringLiteral("palm:calendar/0");
            m.targetCalendar = QStringLiteral("pc-calendar/0");
            m.mode           = SyncMode::TwoWay;
            m.enabled        = true;
            runtime.setMappingsForTest({m});
        }

        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

        // Sync is done; watcher has been cleaned up.  cancelSync must not crash.
        QCOMPARE(runtime.isRunning(), false);
        runtime.cancelSync();
    }

    void cancelSync_midRun_mirror_emitsRunFinished()
    {
        // Plan 8 B.2/B.3 regression: the single-mapping (mirror) path must
        // deliver runFinished and finish its future even when the run is
        // cancelled mid-flight. Pre-migration this rode a .then() off the
        // engine future, which Qt6 drops on cancel — the UI hung.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime runtime(profileDir.path());
        setupSlowCalendarFixture(runtime);

        QSignalSpy finishedSpy(&runtime, &PalmRuntime::runFinished);
        auto future = runtime.copyPalmToPC();
        runtime.cancelSync();   // backend loadRecords stalls 250ms; cancel wins

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);

        // B.4: read via resultAt(0), not results() (empty after cancel).
        const PalmRunResult r = future.resultAt(0);
        QVERIFY(!r.success);
    }

    void cancelSync_midRun_hotSync_emitsRunFinished()
    {
        // Same contract on the multi-mapping path (B.1 / runAllMappings).
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime runtime(profileDir.path());
        setupSlowCalendarFixture(runtime);

        QSignalSpy finishedSpy(&runtime, &PalmRuntime::runFinished);
        auto future = runtime.hotSync();
        runtime.cancelSync();

        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    }

private:
    /// One palm + one pc calendar backend with a single enabled mapping;
    /// the palm side's loadRecords stalls 250ms so the test thread can
    /// cancel deterministically mid-run.
    void setupSlowCalendarFixture(PalmRuntime &runtime)
    {
        auto palmBlob = std::make_unique<SlowLoadBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm-calendar")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-calendar/0");
            ci.name = QStringLiteral("PC Calendar");
            pcBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc-calendar")));

        SyncMapping m;
        m.id             = QStringLiteral("cancel-test-mapping");
        m.sourceBackend  = QStringLiteral("palm-calendar");
        m.targetBackend  = QStringLiteral("pc-calendar");
        m.sourceCalendar = QStringLiteral("palm:calendar/0");
        m.targetCalendar = QStringLiteral("pc-calendar/0");
        m.mode           = SyncMode::TwoWay;
        m.enabled        = true;
        runtime.setMappingsForTest({m});
    }
};

QTEST_GUILESS_MAIN(TstPalmRuntimeCancelSync)
#include "tst_palm_runtime_cancel_sync.moc"
