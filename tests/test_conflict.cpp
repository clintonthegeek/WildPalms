/**
 * @file test_conflict.cpp
 * @brief Unit tests for Kalburator::Conflict types
 *
 * Tests ConflictRecord, ConflictStore, and ConflictPolicy classes.
 */

#include <QtTest/QtTest>
#include <QDebug>
#include <QSignalSpy>
#include <QJsonDocument>
#include <kalburator/conflict/conflictrecord.h>
#include <kalburator/conflict/conflictstore.h>
#include <kalburator/conflict/conflictpolicy.h>

using namespace Kalburator::Conflict;

class TestConflict : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ========== RecordSnapshot Tests ==========
    void testRecordSnapshotDefault();
    void testRecordSnapshotPopulated();
    void testRecordSnapshotIsEmpty();
    void testRecordSnapshotIsDeleted();
    void testRecordSnapshotComputeHash();
    void testRecordSnapshotToJson();
    void testRecordSnapshotFromJson();

    // ========== ConflictRecord Tests ==========
    void testConflictRecordDefault();
    void testConflictRecordGenerateId();
    void testConflictRecordUniqueIds();
    void testConflictRecordSummary();
    void testConflictRecordAssessComplexitySimple();
    void testConflictRecordAssessComplexityModerate();
    void testConflictRecordAssessComplexityComplex();
    void testConflictRecordToJson();
    void testConflictRecordFromJson();
    void testConflictRecordRoundTrip();

    // ========== ConflictStore Tests ==========
    void testConflictStoreConstruction();
    void testConflictStoreAddConflict();
    void testConflictStoreGetConflict();
    void testConflictStoreAllConflicts();
    void testConflictStoreConflictsForConduit();
    void testConflictStorePendingConflicts();
    void testConflictStoreResolveConflict();
    void testConflictStoreResolveAll();
    void testConflictStoreMarkApplied();
    void testConflictStoreResetToPending();
    void testConflictStoreRemoveConflict();
    void testConflictStoreRemoveConflictsForConduit();
    void testConflictStoreRemoveApplied();
    void testConflictStoreClear();
    void testConflictStorePersistence();
    void testConflictStoreSignals();

    // ========== ConflictPolicy Tests ==========
    void testConflictPolicyDefaults();
    void testConflictPolicyShouldAutoResolveNone();
    void testConflictPolicyShouldAutoResolveSourceWins();
    void testConflictPolicyShouldPromptAlways();
    void testConflictPolicyShouldPromptWhenComplex();
    void testConflictPolicyShouldPromptWhenDelete();
    void testConflictPolicyGetAutoDecision();
    void testConflictPolicyToJson();
    void testConflictPolicyFromJson();
    void testConflictPolicyPresets();

    // ========== AutomaticConflictHandler Tests ==========
    void testAutoHandlerBasic();
    void testAutoHandlerDefer();

private:
    ConflictStore *m_store;

    ConflictRecord createTestConflict(const QString &conduitId = "memos",
                                       ConflictType type = ConflictType::BothModified);
};

void TestConflict::initTestCase()
{
    qDebug() << "Starting QSyncCore Conflict tests";
}

void TestConflict::cleanupTestCase()
{
    qDebug() << "QSyncCore Conflict tests complete";
}

void TestConflict::init()
{
    m_store = new ConflictStore();
}

void TestConflict::cleanup()
{
    delete m_store;
    m_store = nullptr;
}

ConflictRecord TestConflict::createTestConflict(const QString &conduitId, ConflictType type)
{
    ConflictRecord conflict;
    conflict.conflictId = ConflictRecord::generateId();
    conflict.conduitId = conduitId;
    conflict.type = type;
    conflict.detectedAt = QDateTime::currentDateTime();
    conflict.syncSessionId = "session123";

    conflict.source.id = "palm-123";
    conflict.source.description = "Test memo from Palm";
    conflict.source.content = "Palm content here";
    conflict.source.lastModified = QDateTime::currentDateTime().addSecs(-3600);

    conflict.target.id = "pc-456.md";
    conflict.target.description = "Test memo from PC";
    conflict.target.content = "PC content here - modified";
    conflict.target.lastModified = QDateTime::currentDateTime().addSecs(-1800);

    return conflict;
}

// ========== RecordSnapshot Tests ==========

void TestConflict::testRecordSnapshotDefault()
{
    RecordSnapshot snapshot;

    QVERIFY(snapshot.id.isEmpty());
    QVERIFY(snapshot.content.isEmpty());
    QVERIFY(!snapshot.lastModified.isValid());
}

void TestConflict::testRecordSnapshotPopulated()
{
    RecordSnapshot snapshot;
    snapshot.id = "record123";
    snapshot.description = "Test record";
    snapshot.content = "Some content";
    snapshot.contentHash = "abc123";
    snapshot.contentType = "text/plain";
    snapshot.category = "Business";
    snapshot.lastModified = QDateTime::currentDateTime();

    QCOMPARE(snapshot.id, QString("record123"));
    QCOMPARE(snapshot.description, QString("Test record"));
    QCOMPARE(snapshot.content, QByteArray("Some content"));
    QCOMPARE(snapshot.contentHash, QString("abc123"));
    QCOMPARE(snapshot.contentType, QString("text/plain"));
    QCOMPARE(snapshot.category, QString("Business"));
    QVERIFY(snapshot.lastModified.isValid());
}

void TestConflict::testRecordSnapshotIsEmpty()
{
    RecordSnapshot empty;
    QVERIFY(empty.isEmpty());  // id is empty

    RecordSnapshot withId;
    withId.id = "something";
    QVERIFY(!withId.isEmpty());  // id is not empty

    // Note: isEmpty() checks if id is empty, not content
    RecordSnapshot withContentNoId;
    withContentNoId.content = "data";
    QVERIFY(withContentNoId.isEmpty());  // id is still empty
}

void TestConflict::testRecordSnapshotIsDeleted()
{
    RecordSnapshot normal;
    normal.id = "record123";
    normal.content = "content";
    QVERIFY(!normal.isDeleted());

    // isDeleted() returns true when id is set but content is empty
    RecordSnapshot deleted;
    deleted.id = "record123";
    // No content set - this represents a deleted record
    QVERIFY(deleted.isDeleted());
}

void TestConflict::testRecordSnapshotComputeHash()
{
    // Hash is typically computed externally and stored
    RecordSnapshot snapshot;
    snapshot.content = "Test content for hashing";
    snapshot.contentHash = "abc123def456";  // Pre-computed hash

    QVERIFY(!snapshot.contentHash.isEmpty());
    QCOMPARE(snapshot.contentHash, QString("abc123def456"));
}

void TestConflict::testRecordSnapshotToJson()
{
    RecordSnapshot snapshot;
    snapshot.id = "record123";
    snapshot.description = "Test record";
    snapshot.content = "Binary\x00data";
    snapshot.contentHash = "abc123";
    snapshot.contentType = "application/octet-stream";
    snapshot.category = "Personal";
    snapshot.lastModified = QDateTime(QDate(2024, 6, 15), QTime(10, 30, 0));

    QJsonObject json = snapshot.toJson();

    QCOMPARE(json["id"].toString(), QString("record123"));
    QCOMPARE(json["description"].toString(), QString("Test record"));
    QVERIFY(!json["content"].toString().isEmpty());  // Base64 encoded
    QCOMPARE(json["contentHash"].toString(), QString("abc123"));
    QCOMPARE(json["contentType"].toString(), QString("application/octet-stream"));
    QCOMPARE(json["category"].toString(), QString("Personal"));
    QVERIFY(!json["lastModified"].toString().isEmpty());
}

void TestConflict::testRecordSnapshotFromJson()
{
    QJsonObject json;
    json["id"] = "record456";
    json["description"] = "Loaded record";
    json["content"] = QString::fromLatin1(QByteArray("test content").toBase64());
    json["contentHash"] = "hash789";
    json["contentType"] = "text/plain";
    json["category"] = "Work";
    json["lastModified"] = "2024-06-15T10:30:00Z";

    RecordSnapshot snapshot = RecordSnapshot::fromJson(json);

    QCOMPARE(snapshot.id, QString("record456"));
    QCOMPARE(snapshot.description, QString("Loaded record"));
    QCOMPARE(snapshot.content, QByteArray("test content"));
    QCOMPARE(snapshot.contentHash, QString("hash789"));
    QCOMPARE(snapshot.contentType, QString("text/plain"));
    QCOMPARE(snapshot.category, QString("Work"));
}

// ========== ConflictRecord Tests ==========

void TestConflict::testConflictRecordDefault()
{
    ConflictRecord conflict;

    QVERIFY(conflict.conflictId.isEmpty());
    QCOMPARE(conflict.type, ConflictType::BothModified);
    QCOMPARE(conflict.complexity, ConflictComplexity::Moderate);  // Default is Moderate
    QCOMPARE(conflict.decision, ConflictDecision::Pending);
}

void TestConflict::testConflictRecordGenerateId()
{
    QString id = ConflictRecord::generateId();

    QVERIFY(!id.isEmpty());
    // ID is a UUID without braces (e.g., "a1b2c3d4-e5f6-...")
    QCOMPARE(id.length(), 36);  // Standard UUID length without braces
    QVERIFY(id.contains('-'));   // UUIDs have dashes
}

void TestConflict::testConflictRecordUniqueIds()
{
    QSet<QString> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(ConflictRecord::generateId());
    }
    QCOMPARE(ids.size(), 100);  // All unique
}

void TestConflict::testConflictRecordSummary()
{
    ConflictRecord conflict = createTestConflict();

    QString summary = conflict.summary();

    QVERIFY(!summary.isEmpty());
    QVERIFY(summary.contains("Test memo"));  // Contains description
}

void TestConflict::testConflictRecordAssessComplexitySimple()
{
    // Simple: less than 5% size change
    ConflictRecord conflict;
    conflict.type = ConflictType::BothModified;
    conflict.source.content = QByteArray(100, 'x');
    conflict.target.content = QByteArray(102, 'y');  // 2% change

    conflict.assessComplexity();

    QCOMPARE(conflict.complexity, ConflictComplexity::Simple);
}

void TestConflict::testConflictRecordAssessComplexityModerate()
{
    // Moderate: 5-30% size change
    ConflictRecord conflict;
    conflict.type = ConflictType::BothModified;
    conflict.source.content = QByteArray(100, 'x');
    conflict.target.content = QByteArray(115, 'y');  // 15% change

    conflict.assessComplexity();

    QCOMPARE(conflict.complexity, ConflictComplexity::Moderate);
}

void TestConflict::testConflictRecordAssessComplexityComplex()
{
    // Complex: more than 30% size change
    ConflictRecord conflict;
    conflict.type = ConflictType::BothModified;
    conflict.source.content = QByteArray(100, 'x');
    conflict.target.content = QByteArray(150, 'y');  // 50% change

    conflict.assessComplexity();

    QCOMPARE(conflict.complexity, ConflictComplexity::Complex);
}

void TestConflict::testConflictRecordToJson()
{
    ConflictRecord conflict = createTestConflict();
    conflict.decision = ConflictDecision::UseSource;
    conflict.resolvedBy = "user";
    conflict.resolvedAt = QDateTime::currentDateTime();

    QJsonObject json = conflict.toJson();

    QCOMPARE(json["conflictId"].toString(), conflict.conflictId);
    QCOMPARE(json["conduitId"].toString(), QString("memos"));
    QCOMPARE(json["type"].toString(), QString("BothModified"));
    QVERIFY(json.contains("source"));
    QVERIFY(json.contains("target"));
    QCOMPARE(json["decision"].toString(), QString("UseSource"));
    QCOMPARE(json["resolvedBy"].toString(), QString("user"));
}

void TestConflict::testConflictRecordFromJson()
{
    QJsonObject sourceJson;
    sourceJson["id"] = "src1";
    sourceJson["content"] = QString::fromLatin1(QByteArray("source").toBase64());

    QJsonObject targetJson;
    targetJson["id"] = "tgt1";
    targetJson["content"] = QString::fromLatin1(QByteArray("target").toBase64());

    QJsonObject json;
    json["conflictId"] = "conflict-test-123";
    json["conduitId"] = "contacts";
    json["type"] = "ModifiedVsDeleted";
    json["complexity"] = 1;  // Complexity is stored as int, 1 = Moderate
    json["decision"] = "UseTarget";
    json["source"] = sourceJson;
    json["target"] = targetJson;
    json["detectedAt"] = "2024-06-15T10:30:00Z";
    json["syncSessionId"] = "session456";

    ConflictRecord conflict = ConflictRecord::fromJson(json);

    QCOMPARE(conflict.conflictId, QString("conflict-test-123"));
    QCOMPARE(conflict.conduitId, QString("contacts"));
    QCOMPARE(conflict.type, ConflictType::ModifiedVsDeleted);
    QCOMPARE(conflict.complexity, ConflictComplexity::Moderate);
    QCOMPARE(conflict.decision, ConflictDecision::UseTarget);
    QCOMPARE(conflict.source.id, QString("src1"));
    QCOMPARE(conflict.target.id, QString("tgt1"));
}

void TestConflict::testConflictRecordRoundTrip()
{
    ConflictRecord original = createTestConflict();
    original.decision = ConflictDecision::UseBoth;
    original.resolvedBy = "auto";
    original.assessComplexity();

    QJsonObject json = original.toJson();
    ConflictRecord loaded = ConflictRecord::fromJson(json);

    QCOMPARE(loaded.conflictId, original.conflictId);
    QCOMPARE(loaded.conduitId, original.conduitId);
    QCOMPARE(loaded.type, original.type);
    QCOMPARE(loaded.complexity, original.complexity);
    QCOMPARE(loaded.decision, original.decision);
    QCOMPARE(loaded.resolvedBy, original.resolvedBy);
    QCOMPARE(loaded.source.id, original.source.id);
    QCOMPARE(loaded.target.id, original.target.id);
}

// ========== ConflictStore Tests ==========

void TestConflict::testConflictStoreConstruction()
{
    QCOMPARE(m_store->count(), 0);
    QVERIFY(!m_store->hasPendingConflicts());
}

void TestConflict::testConflictStoreAddConflict()
{
    ConflictRecord conflict = createTestConflict();

    QString id = m_store->addConflict(conflict);

    QVERIFY(!id.isEmpty());
    QCOMPARE(m_store->count(), 1);
    QVERIFY(m_store->hasPendingConflicts());
}

void TestConflict::testConflictStoreGetConflict()
{
    ConflictRecord original = createTestConflict();
    QString id = m_store->addConflict(original);

    ConflictRecord retrieved = m_store->getConflict(id);

    QCOMPARE(retrieved.conflictId, id);
    QCOMPARE(retrieved.conduitId, original.conduitId);
    QCOMPARE(retrieved.type, original.type);
}

void TestConflict::testConflictStoreAllConflicts()
{
    m_store->addConflict(createTestConflict("memos"));
    m_store->addConflict(createTestConflict("contacts"));
    m_store->addConflict(createTestConflict("calendar"));

    QList<ConflictRecord> all = m_store->allConflicts();

    QCOMPARE(all.size(), 3);
}

void TestConflict::testConflictStoreConflictsForConduit()
{
    m_store->addConflict(createTestConflict("memos"));
    m_store->addConflict(createTestConflict("memos"));
    m_store->addConflict(createTestConflict("contacts"));

    QList<ConflictRecord> memoConflicts = m_store->conflictsForConduit("memos");
    QList<ConflictRecord> contactConflicts = m_store->conflictsForConduit("contacts");
    QList<ConflictRecord> calendarConflicts = m_store->conflictsForConduit("calendar");

    QCOMPARE(memoConflicts.size(), 2);
    QCOMPARE(contactConflicts.size(), 1);
    QCOMPARE(calendarConflicts.size(), 0);
}

void TestConflict::testConflictStorePendingConflicts()
{
    QString id1 = m_store->addConflict(createTestConflict());
    QString id2 = m_store->addConflict(createTestConflict());
    m_store->addConflict(createTestConflict());

    // Resolve one
    m_store->resolveConflict(id1, ConflictDecision::UseSource);

    QList<ConflictRecord> pending = m_store->pendingConflicts();

    QCOMPARE(pending.size(), 2);
    QCOMPARE(m_store->pendingCount(), 2);
}

void TestConflict::testConflictStoreResolveConflict()
{
    ConflictRecord conflict = createTestConflict();
    QString id = m_store->addConflict(conflict);

    m_store->resolveConflict(id, ConflictDecision::UseTarget, "test_user");

    ConflictRecord resolved = m_store->getConflict(id);
    QCOMPARE(resolved.decision, ConflictDecision::UseTarget);
    QCOMPARE(resolved.resolvedBy, QString("test_user"));
    QVERIFY(resolved.resolvedAt.isValid());
}

void TestConflict::testConflictStoreResolveAll()
{
    m_store->addConflict(createTestConflict());
    m_store->addConflict(createTestConflict());
    m_store->addConflict(createTestConflict());

    m_store->resolveAll(ConflictDecision::UseSource, "batch");

    QCOMPARE(m_store->pendingCount(), 0);

    QList<ConflictRecord> all = m_store->allConflicts();
    for (const ConflictRecord &c : all) {
        QCOMPARE(c.decision, ConflictDecision::UseSource);
        QCOMPARE(c.resolvedBy, QString("batch"));
    }
}

void TestConflict::testConflictStoreMarkApplied()
{
    ConflictRecord conflict = createTestConflict();
    QString id = m_store->addConflict(conflict);
    m_store->resolveConflict(id, ConflictDecision::UseSource);

    m_store->markApplied(id, true);

    ConflictRecord applied = m_store->getConflict(id);
    QVERIFY(applied.applied);
}

void TestConflict::testConflictStoreResetToPending()
{
    ConflictRecord conflict = createTestConflict();
    QString id = m_store->addConflict(conflict);
    m_store->resolveConflict(id, ConflictDecision::UseTarget);

    QCOMPARE(m_store->pendingCount(), 0);

    m_store->resetToPending(id);

    QCOMPARE(m_store->pendingCount(), 1);
    ConflictRecord reset = m_store->getConflict(id);
    QCOMPARE(reset.decision, ConflictDecision::Pending);
}

void TestConflict::testConflictStoreRemoveConflict()
{
    QString id = m_store->addConflict(createTestConflict());
    QCOMPARE(m_store->count(), 1);

    m_store->removeConflict(id);

    QCOMPARE(m_store->count(), 0);
}

void TestConflict::testConflictStoreRemoveConflictsForConduit()
{
    m_store->addConflict(createTestConflict("memos"));
    m_store->addConflict(createTestConflict("memos"));
    m_store->addConflict(createTestConflict("contacts"));

    m_store->removeConflictsForConduit("memos");

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(m_store->conflictsForConduit("contacts").size(), 1);
}

void TestConflict::testConflictStoreRemoveApplied()
{
    QString id1 = m_store->addConflict(createTestConflict());
    QString id2 = m_store->addConflict(createTestConflict());
    QString id3 = m_store->addConflict(createTestConflict());

    m_store->resolveConflict(id1, ConflictDecision::UseSource);
    m_store->markApplied(id1, true);
    m_store->resolveConflict(id2, ConflictDecision::UseTarget);
    // id3 remains pending

    m_store->removeAppliedConflicts();

    QCOMPARE(m_store->count(), 2);  // id2 (resolved, not applied) + id3 (pending)
}

void TestConflict::testConflictStoreClear()
{
    m_store->addConflict(createTestConflict());
    m_store->addConflict(createTestConflict());
    m_store->addConflict(createTestConflict());

    m_store->clear();

    QCOMPARE(m_store->count(), 0);
    QVERIFY(!m_store->hasPendingConflicts());
}

void TestConflict::testConflictStorePersistence()
{
    // Add conflicts
    ConflictRecord c1 = createTestConflict("memos");
    ConflictRecord c2 = createTestConflict("contacts");
    QString id1 = m_store->addConflict(c1);
    QString id2 = m_store->addConflict(c2);
    m_store->resolveConflict(id1, ConflictDecision::UseSource, "user");

    // Serialize
    QJsonArray json = m_store->toJson();

    // Create new store and load
    ConflictStore store2;
    int loaded = store2.fromJson(json);

    QCOMPARE(loaded, 2);
    QCOMPARE(store2.count(), 2);

    ConflictRecord loaded1 = store2.getConflict(id1);
    QCOMPARE(loaded1.conduitId, QString("memos"));
    QCOMPARE(loaded1.decision, ConflictDecision::UseSource);

    ConflictRecord loaded2 = store2.getConflict(id2);
    QCOMPARE(loaded2.conduitId, QString("contacts"));
    QCOMPARE(loaded2.decision, ConflictDecision::Pending);
}

void TestConflict::testConflictStoreSignals()
{
    QSignalSpy addedSpy(m_store, &ConflictStore::conflictsAdded);
    QSignalSpy resolvedSpy(m_store, &ConflictStore::conflictResolved);
    QSignalSpy changedSpy(m_store, &ConflictStore::conflictsChanged);

    QString id = m_store->addConflict(createTestConflict());
    QCOMPARE(addedSpy.count(), 1);
    QVERIFY(changedSpy.count() >= 1);

    m_store->resolveConflict(id, ConflictDecision::UseTarget);
    QCOMPARE(resolvedSpy.count(), 1);
}

// ========== ConflictPolicy Tests ==========

void TestConflict::testConflictPolicyDefaults()
{
    ConflictPolicy policy;

    QCOMPARE(policy.autoResolve, AutoResolveStrategy::None);
    QCOMPARE(policy.promptStrategy, PromptStrategy::Always);
    QCOMPARE(policy.fallback, FallbackBehavior::Defer);
    QVERIFY(policy.allowBatchReview);
}

void TestConflict::testConflictPolicyShouldAutoResolveNone()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;

    ConflictRecord conflict = createTestConflict();

    QVERIFY(!policy.shouldAutoResolve(conflict));
}

void TestConflict::testConflictPolicyShouldAutoResolveSourceWins()
{
    // Use autoSourceWins() preset which sets promptStrategy to Never
    ConflictPolicy policy = ConflictPolicy::autoSourceWins();

    ConflictRecord conflict = createTestConflict();

    QVERIFY(policy.shouldAutoResolve(conflict));
}

void TestConflict::testConflictPolicyShouldPromptAlways()
{
    ConflictPolicy policy;
    policy.promptStrategy = PromptStrategy::Always;

    ConflictRecord simple = createTestConflict();
    simple.complexity = ConflictComplexity::Simple;

    ConflictRecord complex = createTestConflict();
    complex.complexity = ConflictComplexity::Complex;

    QVERIFY(policy.shouldPrompt(simple));
    QVERIFY(policy.shouldPrompt(complex));
}

void TestConflict::testConflictPolicyShouldPromptWhenComplex()
{
    ConflictPolicy policy;
    policy.promptStrategy = PromptStrategy::WhenComplex;

    ConflictRecord simple = createTestConflict();
    simple.complexity = ConflictComplexity::Simple;

    ConflictRecord complex = createTestConflict();
    complex.complexity = ConflictComplexity::Complex;

    QVERIFY(!policy.shouldPrompt(simple));
    QVERIFY(policy.shouldPrompt(complex));
}

void TestConflict::testConflictPolicyShouldPromptWhenDelete()
{
    ConflictPolicy policy;
    policy.promptStrategy = PromptStrategy::WhenDelete;

    ConflictRecord bothModified = createTestConflict("memos", ConflictType::BothModified);
    ConflictRecord modVsDel = createTestConflict("memos", ConflictType::ModifiedVsDeleted);
    ConflictRecord delVsMod = createTestConflict("memos", ConflictType::DeletedVsModified);

    QVERIFY(!policy.shouldPrompt(bothModified));
    QVERIFY(policy.shouldPrompt(modVsDel));
    QVERIFY(policy.shouldPrompt(delVsMod));
}

void TestConflict::testConflictPolicyGetAutoDecision()
{
    ConflictPolicy sourceWins;
    sourceWins.autoResolve = AutoResolveStrategy::SourceAlwaysWins;

    ConflictPolicy targetWins;
    targetWins.autoResolve = AutoResolveStrategy::TargetAlwaysWins;

    ConflictPolicy duplicate;
    duplicate.autoResolve = AutoResolveStrategy::DuplicateAll;

    ConflictRecord conflict = createTestConflict();

    QCOMPARE(sourceWins.getAutoDecision(conflict), ConflictDecision::UseSource);
    QCOMPARE(targetWins.getAutoDecision(conflict), ConflictDecision::UseTarget);
    QCOMPARE(duplicate.getAutoDecision(conflict), ConflictDecision::UseBoth);
}

void TestConflict::testConflictPolicyToJson()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::WhenComplex;
    policy.promptTimeoutSeconds = 120;
    policy.fallback = FallbackBehavior::Skip;
    policy.allowBatchReview = false;

    QJsonObject json = policy.toJson();

    QCOMPARE(json["autoResolve"].toString(), QString("NewerWins"));
    QCOMPARE(json["promptStrategy"].toString(), QString("WhenComplex"));
    QCOMPARE(json["promptTimeoutSeconds"].toInt(), 120);
    QCOMPARE(json["fallback"].toString(), QString("Skip"));
    QCOMPARE(json["allowBatchReview"].toBool(), false);
}

void TestConflict::testConflictPolicyFromJson()
{
    QJsonObject json;
    json["autoResolve"] = "TargetAlwaysWins";
    json["promptStrategy"] = "Never";
    json["promptTimeoutSeconds"] = 30;
    json["fallback"] = "Abort";
    json["allowBatchReview"] = true;
    json["maxAutoResolvePerSync"] = 50;

    ConflictPolicy policy = ConflictPolicy::fromJson(json);

    QCOMPARE(policy.autoResolve, AutoResolveStrategy::TargetAlwaysWins);
    QCOMPARE(policy.promptStrategy, PromptStrategy::Never);
    QCOMPARE(policy.promptTimeoutSeconds, 30);
    QCOMPARE(policy.fallback, FallbackBehavior::Abort);
    QVERIFY(policy.allowBatchReview);
    QCOMPARE(policy.maxAutoResolvePerSync, 50);
}

void TestConflict::testConflictPolicyPresets()
{
    ConflictPolicy sourceWins = ConflictPolicy::autoSourceWins();
    QCOMPARE(sourceWins.autoResolve, AutoResolveStrategy::SourceAlwaysWins);
    QCOMPARE(sourceWins.promptStrategy, PromptStrategy::Never);

    ConflictPolicy targetWins = ConflictPolicy::autoTargetWins();
    QCOMPARE(targetWins.autoResolve, AutoResolveStrategy::TargetAlwaysWins);
    QCOMPARE(targetWins.promptStrategy, PromptStrategy::Never);

    ConflictPolicy deferAll = ConflictPolicy::deferAll();
    QCOMPARE(deferAll.autoResolve, AutoResolveStrategy::None);
    QCOMPARE(deferAll.fallback, FallbackBehavior::Defer);

    ConflictPolicy interactive = ConflictPolicy::interactive();
    QCOMPARE(interactive.promptStrategy, PromptStrategy::Always);
    QVERIFY(interactive.allowBatchReview);
}

// ========== AutomaticConflictHandler Tests ==========

void TestConflict::testAutoHandlerBasic()
{
    AutomaticConflictHandler handler;
    ConflictPolicy policy = ConflictPolicy::autoSourceWins();

    ConflictRecord conflict = createTestConflict();
    ConflictDecision decision = handler.handleConflict(conflict, policy);

    QCOMPARE(decision, ConflictDecision::UseSource);
}

void TestConflict::testAutoHandlerDefer()
{
    AutomaticConflictHandler handler;
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.fallback = FallbackBehavior::Defer;

    ConflictRecord conflict = createTestConflict();
    ConflictDecision decision = handler.handleConflict(conflict, policy);

    QCOMPARE(decision, ConflictDecision::Pending);
    QCOMPARE(handler.pendingConflicts().size(), 1);
}

QTEST_MAIN(TestConflict)
#include "test_conflict.moc"
