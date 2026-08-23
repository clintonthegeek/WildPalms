// tests/runtime/tst_kf6mainwindow_conflict_apply.cpp
//
// Shakedown F10: "Apply Resolutions (Sync)" must push resolved-but-unapplied
// decisions from the UI ConflictStore into the engine's SyncConflictStore so
// SyncEngine::rehydratePendingResolutions() replays them on the next sync.

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../../src/kf6/kf6mainwindow.h"
#include "../wildpalms_qtest_main.h"

#include <synctypes.h>
#include <conflictstore.h>
#include <syncconflictstore.h>

#include "runtime/palmruntime.h"
#include "../../src/app/wizard/newprofilewizard.h"
#include "../../src/runtime/profileregistry.h"

#include <KSharedConfig>

class TstKf6MainWindowConflictApply : public QObject
{
    Q_OBJECT
private slots:
    void applyWithoutRuntime_isSafeNoOp();
    void resolutionsReachEngineStore();
    void unmappedDecisionIsNotApplied();

private:
    // Loads a real profile (all-local wizard result) so the window owns a
    // PalmRuntime with an attached engine conflict store.
    void loadProfileOnWindow(KF6MainWindow &win, QTemporaryDir &dir);
};

namespace {
std::unique_ptr<WildPalms::Runtime::ProfileRegistry>
makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wprc"));
    auto r = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/wp-root"));
    return r;
}

Kalburator::Sync::ConflictInfo makeInfo(const QString &mappingId)
{
    Kalburator::Sync::ConflictInfo info;
    info.mappingId = mappingId;
    info.sourceId  = QStringLiteral("test:1");
    info.targetId  = QStringLiteral("test:1");
    return info;
}
} // namespace

void TstKf6MainWindowConflictApply::loadProfileOnWindow(
    KF6MainWindow &win, QTemporaryDir &dir)
{
    auto reg = makeRegistry(dir);
    win.setProfileRegistryForTest(std::move(reg));
    win.setRunProfileWizardForTest([]() {
        WildPalms::Wizard::Result r;
        r.state.profileName = QStringLiteral("Conflicts");
        for (const auto &pid : { QStringLiteral("calendar"),
                                  QStringLiteral("contacts"),
                                  QStringLiteral("memo"),
                                  QStringLiteral("todo") }) {
            WildPalms::Wizard::MappingSpec m;
            m.pluginId = pid;
            m.kind     = WildPalms::Wizard::TargetKind::RawFiles;
            r.state.mappings.append(m);
        }
        return r;
    });
    win.runNewProfileForTest();
    QVERIFY(win.palmRuntimeForTest() != nullptr);
    QVERIFY(win.palmRuntimeForTest()->syncConflictStore() != nullptr);
}

void TstKf6MainWindowConflictApply::applyWithoutRuntime_isSafeNoOp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KF6MainWindow win;   // no profile loaded → no runtime, no engine store

    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m1")));
    auto *ui = win.conflictStoreForTest();
    ui->resolveConflict(ui->pendingConflicts().first().conflictId,
                        Kalburator::Conflict::ConflictDecision::UseSource);

    QCOMPARE(win.applyConflictResolutionsForTest(), 0);
}

void TstKf6MainWindowConflictApply::resolutionsReachEngineStore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KF6MainWindow win;
    loadProfileOnWindow(win, dir);

    auto *engineStore = win.palmRuntimeForTest()->syncConflictStore();

    // Seed the ENGINE-side store the way the engine does during a sync,
    // then mirror detection into the UI store exactly as the real signal
    // path does (the id round-trips via ConflictInfo.conflictId).
    Kalburator::Sync::ConflictInfo info;
    info.mappingId = QStringLiteral("mapping-1");
    info.sourceId  = QStringLiteral("rec-A");
    info.targetId  = QStringLiteral("rec-A");
    info.sourceDescription = QStringLiteral("src");
    info.targetDescription = QStringLiteral("tgt");
    info.detectedAt = QDateTime::currentDateTime();
    const QString cid = engineStore->recordConflict(info);
    QVERIFY(!cid.isEmpty());
    info.conflictId = cid;

    win.runConflictDetectedForTest(info);

    // User resolves in the review UI…
    auto *ui = win.conflictStoreForTest();
    QCOMPARE(ui->resolvedUnappliedConflicts().size(), 0);
    ui->resolveConflict(cid, Kalburator::Conflict::ConflictDecision::UseSource);
    QCOMPARE(ui->resolvedUnappliedConflicts().size(), 1);

    // …and hits "Apply Resolutions (Sync)".
    QCOMPARE(win.applyConflictResolutionsForTest(), 1);

    // The decision landed in the engine store and is marked applied in the
    // UI store.
    const auto resolved = engineStore->resolvedConflicts();
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().info.conflictId, cid);
    QCOMPARE(resolved.first().resolution,
             Kalburator::Sync::ConflictResolution::SourceWins);
    QCOMPARE(resolved.first().info.sourceId, QStringLiteral("rec-A"));
    QVERIFY(ui->resolvedUnappliedConflicts().isEmpty());
}

void TstKf6MainWindowConflictApply::unmappedDecisionIsNotApplied()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KF6MainWindow win;
    loadProfileOnWindow(win, dir);

    auto *engineStore = win.palmRuntimeForTest()->syncConflictStore();

    Kalburator::Sync::ConflictInfo info;
    info.mappingId = QStringLiteral("mapping-1");
    info.sourceId  = QStringLiteral("rec-B");
    info.targetId  = QStringLiteral("rec-B");
    const QString cid = engineStore->recordConflict(info);
    QVERIFY(!cid.isEmpty());
    info.conflictId = cid;
    win.runConflictDetectedForTest(info);

    // DeleteBoth has no persisted engine counterpart yet — it must be left
    // unapplied rather than silently mis-mapped.
    auto *ui = win.conflictStoreForTest();
    ui->resolveConflict(cid, Kalburator::Conflict::ConflictDecision::DeleteBoth);
    QCOMPARE(win.applyConflictResolutionsForTest(), 0);

    bool found = false;
    for (const auto &rc : engineStore->resolvedConflicts())
        if (rc.info.conflictId == cid) found = true;
    QVERIFY(!found);
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowConflictApply)
#include "tst_kf6mainwindow_conflict_apply.moc"
