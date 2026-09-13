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
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/calendar/syncconflictstore.h>
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

    void reentrantHotSync_rejectedWithoutNewSignals()    {
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

    void mappingFailure_emitsRunLog()
    {
        // Shakedown F12: per-mapping failures must surface via runLog
        // (piped into the Log dock by KF6MainWindow), not only in the
        // folded end-of-run summary.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        auto palmBlob = std::make_unique<SlowLoadBlobBackend>();
        auto *rawPalm = palmBlob.get();
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
        auto *rawPc = pcBlob.get();
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
            m.id             = QStringLiteral("log-test-mapping");
            m.sourceBackend  = QStringLiteral("palm-calendar");
            m.targetBackend  = QStringLiteral("pc-calendar");
            m.sourceCalendar = QStringLiteral("palm:calendar/0");
            m.targetCalendar = QStringLiteral("pc-calendar/0");
            m.mode           = SyncMode::TwoWay;
            m.enabled        = true;
            rt.setMappingsForTest({m});
        }

        QSignalSpy logSpy(&rt, &PalmRuntime::runLog);

        // Deterministic failure: seed one record on the Palm side so the
        // sync MUST write to the target, then break the target's create.
        Kalburator::Sync::BackendRecord seed;
        seed.id          = QStringLiteral("seed-1");
        seed.type        = QStringLiteral("event");
        seed.displayName = QStringLiteral("Seed");
        seed.data        = QByteArray("seed");
        rawPalm->createRecord(QStringLiteral("palm:calendar/0"), seed);
        rawPc->setFailNext(MockBlobBackend::FailurePoint::OnCreateRecord, 1);

        auto fut = rt.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 15000);

        QVERIFY(!fut.result().success);
        bool sawMappingFailureLine = false;
        for (const auto &v : logSpy)
            if (v.first().toString().contains(QStringLiteral("failed")))
                sawMappingFailureLine = true;
        QVERIFY(sawMappingFailureLine);
    }

    void perConduitStats_notFoldedUnderCalendar()
    {
        // Shakedown F17: perPluginStats used to fold every mapping under a
        // hardcoded "calendar" key. The key must reflect the actual
        // mapping's backend.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm:calendar/0");
            ci.name = QStringLiteral("Unfiled");
            palmBlob->createCollection(ci);
            Kalburator::Sync::BackendRecord seed;
            seed.id          = QStringLiteral("seed-1");
            seed.type        = QStringLiteral("event");
            seed.displayName = QStringLiteral("Seed");
            seed.data        = QByteArray("seed");
            palmBlob->createRecord(QStringLiteral("palm:calendar/0"), seed);
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
            m.id             = QStringLiteral("stats-test-mapping");
            m.sourceBackend  = QStringLiteral("palm-calendar");
            m.targetBackend  = QStringLiteral("pc-calendar");
            m.sourceCalendar = QStringLiteral("palm:calendar/0");
            m.targetCalendar = QStringLiteral("pc-calendar/0");
            m.mode           = SyncMode::TwoWay;
            m.enabled        = true;
            rt.setMappingsForTest({m});
        }

        auto fut = rt.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 15000);

        QVERIFY(fut.result().success);
        const auto &stats = fut.result().perPluginStats;
        QVERIFY2(stats.contains(QStringLiteral("pc-calendar")),
                 qPrintable(QStringList(stats.keys()).join(QStringLiteral(","))));
        QCOMPARE(stats[QStringLiteral("pc-calendar")].created, 1);
        QVERIFY(!stats.contains(QStringLiteral("calendar")));
    }

    void engineConflictStore_attachedAndFunctional()    {
        // Shakedown F10 prerequisite: the engine's SyncConflictStore must
        // be attached per profile so deferred conflicts persist and UI
        // resolutions can replay on the next sync.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        auto *store = rt.syncConflictStore();
        QVERIFY(store != nullptr);
        QVERIFY(store->isOpen());

        Kalburator::Sync::ConflictInfo info;
        info.mappingId = QStringLiteral("mapping-1");
        info.sourceId  = QStringLiteral("rec-1");
        info.targetId  = QStringLiteral("rec-1");
        info.detectedAt = QDateTime::currentDateTime();
        const QString cid = store->recordConflict(info);
        QVERIFY(!cid.isEmpty());
        QCOMPARE(store->unresolvedConflicts().size(), 1);

        store->resolveConflict(cid,
            Kalburator::Sync::ConflictResolution::TargetWins);
        QCOMPARE(store->unresolvedConflicts().size(), 0);
        const auto resolved = store->resolvedConflicts();
        QCOMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().resolution,
                 Kalburator::Sync::ConflictResolution::TargetWins);
    }
};

QTEST_GUILESS_MAIN(TstPalmRuntimeRunLifecycle)
#include "tst_palm_runtime_run_lifecycle.moc"
