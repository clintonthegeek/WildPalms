// tst_palm_mass_delete_guard_e2e.cpp
//
// Sub-project E of palm-sync-honesty: end-to-end verification that the
// libkalburator mass-delete guard fires when a hotSync would otherwise
// destroy records on the source side (Palm) because the PC-side backend
// was emptied between syncs.
//
// Scenario reproduces the original a8f686f data-loss event:
//   1. Palm (source) seeded with 20 records.
//   2. PC (target) is empty.
//   3. First hotSync: engine creates all 20 records in PC + saves baselines.
//   4. All records deleted from PC backend (simulates user rm -rf rawfiles).
//   5. Second hotSync: perRecordDiff sees hasS=true, hasT=false, hasB=true
//      for all 20 → engine proposes 20 deletes to source (Palm).
//   6. Mass-delete guard fires: 20 > 10 absolute threshold.
//   7. guardFiresAgainstPCEmptiedScenario: asserts invocations=1, proposed=20, baseline=20.
//   8. guardDenyPreservesPalmRecords: guard returns false → Palm records intact.
//
// Backend layer: MockBlobBackend (in-memory) + BlobSyncBackendWrapper.
// This is the same fixture pattern as tst_palm_runtime_hotsync.cpp and
// tst_palm_runtime_modes.cpp.

#include <QTest>
#include <QCryptographicHash>
#include <QFuture>
#include <QTemporaryDir>

#include "runtime/palmruntime.h"
#include "runtime/massdeleteguardpresenter.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>
#include <kalburator/sync/backendregistry.h>
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;

namespace {

// ----------------------------------------------------------------------------
// RecordingGuard: MassDeleteGuardPresenter subclass that records all args
// passed to promptUser and returns a preset answer.
// ----------------------------------------------------------------------------
class RecordingGuard : public WildPalms::Runtime::MassDeleteGuardPresenter
{
public:
    using MassDeleteGuardPresenter::MassDeleteGuardPresenter;

    int     invocations  = 0;
    QString lastMappingId;
    QString lastBackendId;
    int     lastProposed = -1;
    int     lastBaseline = -1;
    bool    nextAnswer   = false; // deny by default (safe)

protected:
    bool promptUser(const QString &mappingId,
                    const QString &targetBackendId,
                    int proposedDeletes,
                    int baselineCount) override
    {
        ++invocations;
        lastMappingId = mappingId;
        lastBackendId = targetBackendId;
        lastProposed  = proposedDeletes;
        lastBaseline  = baselineCount;
        return nextAnswer;
    }
};

// ----------------------------------------------------------------------------
// Constants used by both test slots.
// ----------------------------------------------------------------------------
constexpr int  kRecordCount  = 20;
constexpr auto kMappingId    = "mass-delete-guard-e2e-mapping";
constexpr auto kSrcBackend   = "palm-source";
constexpr auto kTgtBackend   = "pc-target";
constexpr auto kSrcCol       = "palm-collection";
constexpr auto kTgtCol       = "pc-collection";

// Build a record with a stable contentHash (SHA256 of data).
BackendRecord makeRecord(int index)
{
    BackendRecord r;
    r.id   = QStringLiteral("rec-%1").arg(index);
    r.data = QStringLiteral("payload for record %1").arg(index).toUtf8();
    r.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(r.data, QCryptographicHash::Sha256).toHex());
    return r;
}

// Seed the palm MockBlobBackend with kRecordCount records.
void seedPalm(MockBlobBackend *palm)
{
    CollectionInfo ci;
    ci.id   = QString::fromLatin1(kSrcCol);
    ci.name = QStringLiteral("Palm Collection");
    palm->createCollection(ci);

    for (int i = 1; i <= kRecordCount; ++i)
        palm->createRecord(QString::fromLatin1(kSrcCol), makeRecord(i));
}

// Set up the (empty) PC MockBlobBackend.
void initPC(MockBlobBackend *pc)
{
    CollectionInfo ci;
    ci.id   = QString::fromLatin1(kTgtCol);
    ci.name = QStringLiteral("PC Collection");
    pc->createCollection(ci);
}

// Build the test mapping.
SyncMapping makeMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSrcBackend);
    m.targetBackend  = QString::fromLatin1(kTgtBackend);
    m.sourceCalendar = QString::fromLatin1(kSrcCol);
    m.targetCalendar = QString::fromLatin1(kTgtCol);
    m.mode           = SyncMode::TwoWay;
    m.enabled        = true;
    return m;
}

} // namespace

class TstPalmMassDeleteGuardE2E : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void guardFiresAgainstPCEmptiedScenario();
    void guardDenyPreservesPalmRecords();
};

void TstPalmMassDeleteGuardE2E::initTestCase()
{
    // O7: no global seeding — each PalmRuntime loads stock + WP plugins into
    // its own ShapeRegistries (see PalmRuntime::registerPalmPlugins).
}

// ---------------------------------------------------------------------------
// Slot 1: Guard is invoked with expected counts when PC is emptied.
// ---------------------------------------------------------------------------
void TstPalmMassDeleteGuardE2E::guardFiresAgainstPCEmptiedScenario()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    PalmRuntime runtime(tmp.path());

    // --- Build palm (source) backend ---
    auto palmOwned = std::make_unique<MockBlobBackend>();
    seedPalm(palmOwned.get());
    runtime.registerBackendInstanceForTest(
        QString::fromLatin1(kSrcBackend),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmOwned), QString::fromLatin1(kSrcBackend)));

    // --- Build PC (target) backend; keep raw pointer for post-sync manipulation ---
    auto pcOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *pcBlob = pcOwned.get();
    initPC(pcBlob);
    runtime.registerBackendInstanceForTest(
        QString::fromLatin1(kTgtBackend),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcOwned), QString::fromLatin1(kTgtBackend)));

    // --- Register mapping ---
    runtime.setMappingsForTest({makeMapping()});

    // --- First sync: Palm → PC (creates 20 records in PC, saves baselines) ---
    {
        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
        QVERIFY2(future.resultAt(0).success,
                 "First hotSync must succeed to establish baselines");
    }

    // Sanity: all 20 records should now exist in PC.
    QCOMPARE(pcBlob->recordsIn(QString::fromLatin1(kTgtCol)).size(), kRecordCount);

    // --- Simulate rm -rf: delete all records from the PC backend ---
    for (int i = 1; i <= kRecordCount; ++i)
        pcBlob->deleteRecord(QStringLiteral("rec-%1").arg(i));

    QCOMPARE(pcBlob->recordsIn(QString::fromLatin1(kTgtCol)).size(), 0);

    // --- Install guard (deny) and run second sync ---
    RecordingGuard guard(nullptr);
    guard.nextAnswer = false; // deny the mass delete
    runtime.setMassDeleteGuard(&guard);

    {
        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
        // Success/fail is secondary; what matters is that the guard fired.
    }

    // --- Assert guard was invoked exactly once with the expected counts ---
    QVERIFY2(guard.invocations == 1,
             qPrintable(QStringLiteral("Expected guard.invocations=1 but got %1. "
                 "The engine may not have reached the source applyBatch delete path. "
                 "Possible causes: (a) equalRecords returned false (hash mismatch → "
                 "conflict path instead of delete), (b) source applyBatch not called, "
                 "or (c) proposed count <= 10 (threshold not met).")
                 .arg(guard.invocations)));

    QCOMPARE(guard.lastProposed, kRecordCount);
    QCOMPARE(guard.lastBaseline, kRecordCount);
}

// ---------------------------------------------------------------------------
// Slot 2: When guard denies, Palm records are preserved.
// ---------------------------------------------------------------------------
void TstPalmMassDeleteGuardE2E::guardDenyPreservesPalmRecords()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    PalmRuntime runtime(tmp.path());

    // --- Build palm (source) backend; keep raw pointer for post-sync assertion ---
    auto palmOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *palmBlob = palmOwned.get();
    seedPalm(palmBlob);
    runtime.registerBackendInstanceForTest(
        QString::fromLatin1(kSrcBackend),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmOwned), QString::fromLatin1(kSrcBackend)));

    // --- Build PC (target) backend ---
    auto pcOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *pcBlob = pcOwned.get();
    initPC(pcBlob);
    runtime.registerBackendInstanceForTest(
        QString::fromLatin1(kTgtBackend),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(pcOwned), QString::fromLatin1(kTgtBackend)));

    runtime.setMappingsForTest({makeMapping()});

    // --- First sync: establish baselines ---
    {
        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
        QVERIFY2(future.resultAt(0).success, "First hotSync must succeed");
    }

    // --- Simulate rm -rf: delete all records from PC ---
    for (int i = 1; i <= kRecordCount; ++i)
        pcBlob->deleteRecord(QStringLiteral("rec-%1").arg(i));

    // --- Install guard (deny) ---
    RecordingGuard guard(nullptr);
    guard.nextAnswer = false; // deny: deletes should be skipped
    runtime.setMassDeleteGuard(&guard);

    // --- Second sync ---
    {
        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
    }

    // Guard must have fired (same scenario as slot 1).
    QVERIFY2(guard.invocations == 1,
             qPrintable(QStringLiteral("Guard was not invoked; Palm records may "
                 "have been deleted without consulting the guard. "
                 "invocations=%1").arg(guard.invocations)));

    // Because the guard denied, no deletes should have been applied to the Palm.
    const auto remaining = palmBlob->recordsIn(QString::fromLatin1(kSrcCol));
    QCOMPARE(remaining.size(), kRecordCount);
}

QTEST_GUILESS_MAIN(TstPalmMassDeleteGuardE2E)
#include "tst_palm_mass_delete_guard_e2e.moc"
