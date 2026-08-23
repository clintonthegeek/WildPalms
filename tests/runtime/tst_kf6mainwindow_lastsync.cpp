// tests/runtime/tst_kf6mainwindow_lastsync.cpp
//
// Shakedown F14: a completed sync run must stamp Profile::lastSyncTime
// (dashboard showed "Last sync: Never" forever). F13 companion: cancelled
// runs must not be treated as errors — and must not stamp either.

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../../src/kf6/kf6mainwindow.h"
#include "../wildpalms_qtest_main.h"

#include "runtime/palmrunresult.h"

#include "../../src/app/wizard/newprofilewizard.h"
#include "../../src/profile.h"
#include "../../src/runtime/profileregistry.h"

#include <KSharedConfig>

class TstKf6MainWindowLastSync : public QObject
{
    Q_OBJECT
private slots:
    void successfulRunStampsLastSyncTime();
    void failedOrCancelledRunDoesNotStamp();

private:
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
} // namespace

void TstKf6MainWindowLastSync::loadProfileOnWindow(
    KF6MainWindow &win, QTemporaryDir &dir)
{
    auto reg = makeRegistry(dir);
    win.setProfileRegistryForTest(std::move(reg));
    win.setRunProfileWizardForTest([]() {
        WildPalms::Wizard::Result r;
        r.state.profileName = QStringLiteral("Stamp");
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
}

void TstKf6MainWindowLastSync::successfulRunStampsLastSyncTime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KF6MainWindow win;
    loadProfileOnWindow(win, dir);

    const QString path = win.currentProfilePathForTest();
    QVERIFY(!path.isEmpty());

    WildPalms::Runtime::PalmRunResult r;
    r.success  = true;
    r.startTime = QDateTime::currentDateTimeUtc().addSecs(-5);
    r.endTime   = QDateTime::currentDateTimeUtc();
    win.runPalmFinishedForTest(r);

    // Persisted to profile.conf and visible through a fresh load.
    // (profile.conf stores ISODate at second precision.)
    Profile persisted(path);
    QVERIFY(persisted.load());
    QCOMPARE(persisted.lastSyncTime().toSecsSinceEpoch(),
             r.endTime.toSecsSinceEpoch());
}

void TstKf6MainWindowLastSync::failedOrCancelledRunDoesNotStamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    KF6MainWindow win;
    loadProfileOnWindow(win, dir);
    const QString path = win.currentProfilePathForTest();

    WildPalms::Runtime::PalmRunResult failed;
    failed.success = false;
    failed.errorMessage = QStringLiteral("boom");
    failed.endTime = QDateTime::currentDateTimeUtc();
    win.runPalmFinishedForTest(failed);

    WildPalms::Runtime::PalmRunResult cancelled;
    cancelled.success = false;
    cancelled.cancelled = true;
    cancelled.errorMessage = QStringLiteral("Sync cancelled");
    cancelled.endTime = QDateTime::currentDateTimeUtc();
    win.runPalmFinishedForTest(cancelled);

    Profile persisted(path);
    QVERIFY(persisted.load());
    QVERIFY(!persisted.lastSyncTime().isValid());
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowLastSync)
#include "tst_kf6mainwindow_lastsync.moc"
