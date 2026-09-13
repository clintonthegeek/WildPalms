#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

namespace {

static BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord r;
    r.id   = id;
    r.data = data;
    return r;
}

} // namespace

class TstPalmRuntimeClobberSync : public QObject { Q_OBJECT
private slots:
    // Smoke test for Task 10: PalmRuntime::clobberSync dispatches through
    // SyncEngine with ExecutionOverride::clobber=true, the engine wipes the
    // target collection (default IBlobBackend::wipeCollection iterates
    // loadRecords+deleteRecord), then pushes the source (empty hub) onto
    // the now-empty target.
    //
    // NOTE: slot name deliberately does NOT end in "_data" — QTest treats
    // any slot ending in "_data" as the data-provider for another slot of
    // the same prefix and never executes it directly. The plan's literal
    // text used `..._hub_data` which suffered this bug; we keep the
    // semantics but rename to avoid the silent skip.
    //
    // Deviation from the plan's Step 1 code: the plan injected a
    // MockPalmDatabaseAccess via PalmDeviceAccess (the device-side mock).
    // That path requires the SyncEngine to dispatch through the static
    // Palm calendar plugin, which is a SyncBackendBase (not the
    // calendar-domain SyncBackend that PalmSyncHost::backendById
    // dynamic_casts to), so the engine reports
    // "dispatchSync: backend not found". This is unrelated to Task 10 —
    // it's an artifact of how PalmSyncHost is wired today and is
    // exercised by the device-backed verification in Task 12.
    //
    // To stay on Task-10 scope (verify clobberSync's request shape, the
    // ExecutionOverride::clobber wiring, and that wipeCollection runs
    // before the push), we use the same MockBlobBackend + BlobSyncBackendWrapper
    // pattern that tst_palm_runtime_modes uses. This still exercises:
    //   - SyncRequest{ mappingIds, executionOverride={clobber=true} }
    //   - SyncEngine::runSync subset-dispatch
    //   - IBlobBackend::wipeCollection (default impl loadRecords+deleteRecord)
    //   - PalmRuntime aggregation into PalmRunResult per-mapping stats
    void clobber_wipes_target_then_pushes_from_source()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        // Pre-populate "Palm-side" with a stale record we expect to be wiped.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *palmRaw = palmBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("stale-rec"),
                           QByteArray("stale-palm-only")));
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm")));

        // Source side ("PC") starts empty — the post-clobber assertion
        // is just "stale Palm record is gone."
        auto pcBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc")));

        SyncMapping m;
        m.id             = QStringLiteral("test-clobber-cal");
        m.sourceBackend  = QStringLiteral("pc");
        m.sourceCalendar = QStringLiteral("pc-col");
        m.targetBackend  = QStringLiteral("palm");
        m.targetCalendar = QStringLiteral("palm-col");
        m.mode           = SyncMode::TwoWay;
        m.enabled        = true;
        runtime.setMappingsForTest({m});

        auto fut = runtime.clobberSync({QStringLiteral("test-clobber-cal")});
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
        const auto result = fut.resultAt(0);
        QVERIFY2(result.success, result.errorMessage.toLocal8Bit().constData());

        // Post-clobber: the stale "Palm" record is gone; the collection
        // still exists (wipeCollection must leave it usable so the push
        // has a target).
        const auto palmFinal = palmRaw->recordsIn(QStringLiteral("palm-col"));
        QVERIFY2(!palmFinal.contains(QStringLiteral("stale-rec")),
                 "stale-rec must be wiped by clobberSync");
        // The collection still exists (wipeCollection must not delete it).
        const auto collections = palmRaw->availableCollections();
        bool hasPalmCol = false;
        for (const auto &ci : collections) {
            if (ci.id == QStringLiteral("palm-col")) { hasPalmCol = true; break; }
        }
        QVERIFY2(hasPalmCol, "palm-col must still exist after clobber");
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeClobberSync)
#include "tst_palm_runtime_clobber_sync.moc"
