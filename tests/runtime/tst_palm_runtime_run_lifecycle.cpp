// Shakedown F3/F11 remediation: run-lifecycle integrity.
//
// Covers:
//   1. hotSync/fullSync/copyPalmToPC with zero mappings emit the matching
//      runStarted/runFinished pair (plus syncCompleted) instead of stranding
//      the dashboard in its Syncing state forever.
//   2. Re-entrant sync requests while a run is in flight are rejected via
//      the returned future ("A sync is already running") WITHOUT emitting a
//      second runStarted/runFinished pair — the in-flight run owns it.
//   3. isSyncRunning() tracks an in-flight run.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QThread>
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include "mockblobbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "synctypes.h"
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

/// MockBlobBackend whose loadRecords stalls long enough for the test to
/// issue a second (re-entrant) sync request mid-run.
class SlowLoadBlobBackend : public Kalburator::Sync::MockBlobBackend {
public:
    QList<BackendRecord> loadRecords(const QString &collectionId) override {
        QThread::msleep(250);
        return Kalburator::Sync::MockBlobBackend::loadRecords(collectionId);
    }
};

class TstPalmRuntimeRunLifecycle : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<WildPalms::Runtime::PalmRunResult>();
    }

    void hotSync_emptyMappings_emitsRunFinished()
    {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());   // no mappings

        QSignalSpy started(&rt, &PalmRuntime::runStarted);
        QSignalSpy finished(&rt, &PalmRuntime::runFinished);
        QSignalSpy completed(&rt, &PalmRuntime::syncCompleted);

        auto fut = rt.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);

        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 1);       // F11 regression: used to be 0
        QCOMPARE(completed.count(), 1);

        const auto r = fut.result();
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains(QStringLiteral("No sync targets")));
    }

    void fullSync_emptyMappings_emitsRunFinished()
    {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        QSignalSpy started(&rt, &PalmRuntime::runStarted);
        QSignalSpy finished(&rt, &PalmRuntime::runFinished);
        QSignalSpy completed(&rt, &PalmRuntime::syncCompleted);

        auto fut = rt.fullSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);

        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!fut.result().success);
    }

    void copyPalmToPC_emptyMappings_emitsRunFinished()
    {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        QSignalSpy started(&rt, &PalmRuntime::runStarted);
        QSignalSpy finished(&rt, &PalmRuntime::runFinished);
        QSignalSpy completed(&rt, &PalmRuntime::syncCompleted);

        auto fut = rt.copyPalmToPC();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);

        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!fut.result().success);
    }

    void reentrantHotSync_rejectedWithoutNewSignals()
    {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        auto palmBlob = std::make_unique<SlowLoadBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);
        }
        rt.registerBackendInstanceForTest(QStringLiteral("palm-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm-calendar")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-calendar/0");
            ci.name = QStringLiteral("PC Calendar");
            pcBlob->createCollection(ci);
        }
        rt.registerBackendInstanceForTest(QStringLiteral("pc-calendar"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc-calendar")));

        {
            SyncMapping m;
            m.id             = QStringLiteral("lifecycle-test-mapping");
            m.sourceBackend  = QStringLiteral("palm-calendar");
            m.targetBackend  = QStringLiteral("pc-calendar");
            m.sourceCalendar = QStringLiteral("palm:calendar/0");
            m.targetCalendar = QStringLiteral("pc-calendar/0");
            m.mode           = SyncMode::TwoWay;
            m.enabled        = true;
            rt.setMappingsForTest({m});
        }

        QSignalSpy started(&rt, &PalmRuntime::runStarted);
        QSignalSpy finished(&rt, &PalmRuntime::runFinished);

        auto first = rt.hotSync();

        // Inside the deterministic slow-loadRecords window the run is live…
        QTRY_VERIFY_WITH_TIMEOUT(rt.isSyncRunning(), 5000);
        // …so the re-entrant request must be rejected without disturbing
        // the signal pair owned by the in-flight run.
        auto second = rt.hotSync();
        QVERIFY(second.isFinished());
        QVERIFY(!second.result().success);
        QVERIFY(second.result().errorMessage.contains(
                    QStringLiteral("already running")));
        QCOMPARE(started.count(), 1);
        QCOMPARE(finished.count(), 0);

        QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), 10000);
        QCOMPARE(rt.isSyncRunning(), false);
        // Exactly ONE pair total for the whole episode.
        QTRY_VERIFY_WITH_TIMEOUT(started.count() == 1
                                 && finished.count() == 1, 5000);
    }
};

QTEST_GUILESS_MAIN(TstPalmRuntimeRunLifecycle)
#include "tst_palm_runtime_run_lifecycle.moc"
