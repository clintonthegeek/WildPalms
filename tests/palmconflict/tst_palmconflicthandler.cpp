#include <QtTest/QtTest>

#include <kalburator/conflict/conflictpolicy.h>
#include <kalburator/conflict/conflictrecord.h>

#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmbackendconfig.h"
#include "palmconflicthandler.h"

using Kalburator::Conflict::AutoResolveStrategy;
using Kalburator::Conflict::ConflictDecision;
using Kalburator::Conflict::ConflictPolicy;
using Kalburator::Conflict::ConflictRecord;
using Kalburator::Conflict::ConflictType;
using Kalburator::Conflict::FallbackBehavior;
using Kalburator::Conflict::PromptStrategy;
using Kalburator::Conflict::RecordSnapshot;
using WildPalms::PalmConflict::ConnectionBehavior;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::PalmConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

ConflictRecord makeBothModifiedConflict(
    const QString &sourceId, const QString &targetId,
    const QDateTime &sourceTime, const QDateTime &targetTime)
{
    ConflictRecord cr;
    cr.type = ConflictType::BothModified;
    cr.source.id = sourceId;
    cr.source.content = QByteArrayLiteral("src");
    cr.source.lastModified = sourceTime;
    cr.target.id = targetId;
    cr.target.content = QByteArrayLiteral("tgt");
    cr.target.lastModified = targetTime;
    return cr;
}

} // namespace

class TestPalmConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void sourceAlwaysWinsPolicyYieldsUseSource();
    void targetAlwaysWinsPolicyYieldsUseTarget();
    void newerWinsRespectsTimestamps();
    void deferFallbackAccumulatesPending();
    void skipFallbackReturnsSkip();
    void nonPalmIdsFallThroughUnchanged();
    void archivedSourceSurvivesModifiedVsDeleted();
    void archivedTargetSurvivesDeletedVsModified();
    void nonArchivedRecordGetsDeleted();
    void secretSourceOverridesDuplicateAll();
    void secretTargetOverridesDuplicateAll();
    void neitherSecretLeavesDuplicateAll();
    void categoryTieBreakFavoursNonUnfiledSide();
    void categoryTieBreakInactiveWhenTimestampsDiffer();
    void keepAliveMatchesConfig();
    void nullConfigKeepsConnectionAlive();
    void onSyncStartClearsPendingAndOverlay();
};

void TestPalmConflictHandler::sourceAlwaysWinsPolicyYieldsUseSource()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(cr.decision, ConflictDecision::UseSource);
}

void TestPalmConflictHandler::targetAlwaysWinsPolicyYieldsUseTarget()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
}

void TestPalmConflictHandler::newerWinsRespectsTimestamps()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    const auto now = QDateTime::currentDateTimeUtc();
    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        /*sourceTime=*/now,
        /*targetTime=*/now.addSecs(-60));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
}

void TestPalmConflictHandler::deferFallbackAccumulatesPending()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);
    handler.onSyncStart();

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::deferAll();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Pending);
    QCOMPARE(handler.pendingConflicts().size(), 1);
}

void TestPalmConflictHandler::skipFallbackReturnsSkip()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::Skip;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Skip);
}

void TestPalmConflictHandler::nonPalmIdsFallThroughUnchanged()
{
    // Non-Palm ids: no prefix "palm:", so decodeRecordId fails and
    // overlays should be no-ops. Base policy decision stands.
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        QStringLiteral("local:memo:1"),
        QStringLiteral("local:memo:1"),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QVERIFY(handler.lastOverlay().isEmpty());
}

void TestPalmConflictHandler::archivedSourceSurvivesModifiedVsDeleted()
{
    // Palm source record is live and archived; other side deleted it.
    // Base TargetAlwaysWins on ModifiedVsDeleted → DeleteBoth. Overlay
    // must preserve the archived source via UseSource.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord archived;
    archived.recordId = 7;
    archived.attributes = PalmRecord::AttrArchived;
    archived.data = QByteArrayLiteral("archived-body");
    archived.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), archived);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Conflict::ConflictType::ModifiedVsDeleted;
    cr.source.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 7);
    cr.source.content = QByteArrayLiteral("archived-body");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = QStringLiteral("local:memo:7");
    cr.target.content.clear();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("archive"));
}

void TestPalmConflictHandler::archivedTargetSurvivesDeletedVsModified()
{
    // Palm target record is live and archived; source (some other
    // backend) deleted its copy. Base SourceAlwaysWins on
    // DeletedVsModified → DeleteBoth. Overlay flips to UseTarget.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord archived;
    archived.recordId = 11;
    archived.attributes = PalmRecord::AttrArchived;
    archived.data = QByteArrayLiteral("archived-body");
    archived.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), archived);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Conflict::ConflictType::DeletedVsModified;
    cr.source.id = QStringLiteral("local:memo:11");
    cr.source.content.clear();
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 11);
    cr.target.content = QByteArrayLiteral("archived-body");
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("archive"));
}

void TestPalmConflictHandler::nonArchivedRecordGetsDeleted()
{
    // Control: Palm record live but NOT archived. Overlay must not
    // fire — base decision stands.
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmRecord plain;
    plain.recordId = 13;
    plain.attributes = 0;
    plain.data = QByteArrayLiteral("plain");
    plain.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), plain);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr;
    cr.type = Kalburator::Conflict::ConflictType::ModifiedVsDeleted;
    cr.source.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 13);
    cr.source.content = QByteArrayLiteral("plain");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.id = QStringLiteral("local:memo:13");
    cr.target.content.clear();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();

    auto policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::DeleteBoth);
    QVERIFY(handler.lastOverlay().isEmpty());
}

void TestPalmConflictHandler::secretSourceOverridesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord secret;
    secret.recordId = 21;
    secret.attributes = PalmRecord::AttrSecret;
    secret.data = QByteArrayLiteral("secret-src");
    secret.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), secret);

    PalmRecord visible;
    visible.recordId = 22;
    visible.attributes = 0;
    visible.data = QByteArrayLiteral("plain-tgt");
    visible.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), visible);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 21),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 22),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("secret"));
}

void TestPalmConflictHandler::secretTargetOverridesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord visible;
    visible.recordId = 31;
    visible.attributes = 0;
    visible.data = QByteArrayLiteral("plain-src");
    visible.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), visible);

    PalmRecord secret;
    secret.recordId = 32;
    secret.attributes = PalmRecord::AttrSecret;
    secret.data = QByteArrayLiteral("secret-tgt");
    secret.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), secret);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 31),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 32),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("secret"));
}

void TestPalmConflictHandler::neitherSecretLeavesDuplicateAll()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord a;
    a.recordId = 41;
    a.data = QByteArrayLiteral("a");
    a.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), a);

    PalmRecord b;
    b.recordId = 42;
    b.data = QByteArrayLiteral("b");
    b.lastModified = QDateTime::currentDateTimeUtc();
    dev.createRecord(QStringLiteral("MemoDB"), b);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 41),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 42),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::DuplicateAll;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseBoth);
    QVERIFY(handler.lastOverlay().isEmpty());
}

void TestPalmConflictHandler::categoryTieBreakFavoursNonUnfiledSide()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord unfiled;
    unfiled.recordId = 51;
    unfiled.category = 0;
    unfiled.data = QByteArrayLiteral("u");
    unfiled.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), unfiled);

    PalmRecord categorised;
    categorised.recordId = 52;
    categorised.category = 3;
    categorised.data = QByteArrayLiteral("c");
    categorised.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), categorised);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 51),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 52),
        QDateTime::fromMSecsSinceEpoch(1000),
        QDateTime::fromMSecsSinceEpoch(1000));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    // Base NewerWins on equal timestamps returns UseTarget (upstream
    // ties go to target); overlay should flip to UseTarget (categorised
    // side) anyway — the assertion is that the categorised side wins.
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
    QCOMPARE(handler.lastOverlay(), QStringLiteral("category"));
}

void TestPalmConflictHandler::categoryTieBreakInactiveWhenTimestampsDiffer()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord unfiled;
    unfiled.recordId = 61;
    unfiled.category = 0;
    unfiled.data = QByteArrayLiteral("u");
    unfiled.lastModified = QDateTime::fromMSecsSinceEpoch(2000);
    dev.createRecord(QStringLiteral("MemoDB"), unfiled);

    PalmRecord categorised;
    categorised.recordId = 62;
    categorised.category = 3;
    categorised.data = QByteArrayLiteral("c");
    categorised.lastModified = QDateTime::fromMSecsSinceEpoch(1000);
    dev.createRecord(QStringLiteral("MemoDB"), categorised);

    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 61),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 62),
        QDateTime::fromMSecsSinceEpoch(2000),
        QDateTime::fromMSecsSinceEpoch(1000));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;

    // Timestamps differ → NewerWins base decision (UseSource on
    // source-newer) stands. Category overlay inactive.
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QVERIFY(handler.lastOverlay().isEmpty());
}

void TestPalmConflictHandler::keepAliveMatchesConfig()
{
    MockPalmDatabaseAccess dev;

    PalmBackendConfig keep;
    keep.connectionBehavior = ConnectionBehavior::KeepAlive;
    PalmConflictHandler hKeep(&dev, &keep);
    QVERIFY(hKeep.shouldKeepConnectionAlive());

    PalmBackendConfig disconnect;
    disconnect.connectionBehavior = ConnectionBehavior::DisconnectAndDefer;
    PalmConflictHandler hDisconnect(&dev, &disconnect);
    QVERIFY(!hDisconnect.shouldKeepConnectionAlive());

    PalmBackendConfig timeout;
    timeout.connectionBehavior = ConnectionBehavior::TimeoutThenDefer;
    PalmConflictHandler hTimeout(&dev, &timeout);
    QVERIFY(hTimeout.shouldKeepConnectionAlive());
}

void TestPalmConflictHandler::nullConfigKeepsConnectionAlive()
{
    MockPalmDatabaseAccess dev;
    PalmConflictHandler h(&dev, nullptr);
    QVERIFY(h.shouldKeepConnectionAlive());
}

void TestPalmConflictHandler::onSyncStartClearsPendingAndOverlay()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    // Accumulate a pending conflict via deferAll.
    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());
    handler.handleConflict(cr, ConflictPolicy::deferAll());
    QCOMPARE(handler.pendingConflicts().size(), 1);

    handler.onSyncStart();
    QVERIFY(handler.pendingConflicts().isEmpty());
    QVERIFY(handler.lastOverlay().isEmpty());
}

QTEST_MAIN(TestPalmConflictHandler)
#include "tst_palmconflicthandler.moc"
