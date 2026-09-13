// tests/runtime/tst_kf6mainwindow_conflict_badge.cpp
#include <QtTest/QtTest>
#include <QPushButton>

#include "../../src/kf6/kf6mainwindow.h"
#include "../wildpalms_qtest_main.h"

#include <kalburator/types/synctypes.h>
#include <kalburator/conflict/conflictstore.h>

class TstKf6MainWindowConflictBadge : public QObject
{
    Q_OBJECT
private slots:
    void badgeHiddenWhenZeroConflicts();
    void badgeShowsCountAfterConflictDetected();
    void badgeIncrementsAcrossMultipleSignals();
    void detectedConflictAddedToUiConflictStore();
};

namespace {
Kalburator::Sync::ConflictInfo makeInfo(const QString &id)
{
    Kalburator::Sync::ConflictInfo info;
    info.mappingId = id;
    info.sourceId  = QStringLiteral("test:1");
    info.targetId  = QStringLiteral("test:1");
    return info;
}
} // namespace

void TstKf6MainWindowConflictBadge::badgeHiddenWhenZeroConflicts()
{
    KF6MainWindow win;
    QCOMPARE(win.pendingConflictCountForTest(), 0);
    QVERIFY(!win.conflictBadgeForTest()->isVisible() ||
            win.conflictBadgeForTest()->isHidden());
}

void TstKf6MainWindowConflictBadge::badgeShowsCountAfterConflictDetected()
{
    KF6MainWindow win;
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m1")));
    QCOMPARE(win.pendingConflictCountForTest(), 1);
    QVERIFY(win.conflictBadgeForTest()->text().contains(QStringLiteral("1")));
}

void TstKf6MainWindowConflictBadge::badgeIncrementsAcrossMultipleSignals()
{
    KF6MainWindow win;
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m1")));
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m2")));
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m3")));
    QCOMPARE(win.pendingConflictCountForTest(), 3);
    QVERIFY(win.conflictBadgeForTest()->text().contains(QStringLiteral("3")));
}

void TstKf6MainWindowConflictBadge::detectedConflictAddedToUiConflictStore()
{
    KF6MainWindow win;

    auto *store = win.conflictStoreForTest();
    QVERIFY2(store != nullptr, "KF6MainWindow must own a UI ConflictStore "
                                "wired to ConflictReviewDialog");
    QCOMPARE(store->count(), 0);

    Kalburator::Sync::ConflictInfo info = makeInfo(QStringLiteral("m1"));
    info.sourceDescription = QStringLiteral("src");
    info.targetDescription = QStringLiteral("tgt");
    info.detectedAt        = QDateTime::currentDateTime();

    win.runConflictDetectedForTest(info);

    QCOMPARE(store->count(), 1);
    QCOMPARE(store->pendingCount(), 1);

    const auto pending = store->pendingConflicts();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().conduitId, QStringLiteral("m1"));
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowConflictBadge)
#include "tst_kf6mainwindow_conflict_badge.moc"
