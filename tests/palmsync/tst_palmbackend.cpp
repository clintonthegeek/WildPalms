#include <QCryptographicHash>
#include <QtTest/QtTest>

#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmrecord.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

static PalmRecord makePalm(std::uint32_t id, const QByteArray &payload)
{
    PalmRecord r;
    r.recordId = id;
    r.data = payload;
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

class TestPalmBackend : public QObject
{
    Q_OBJECT
private slots:
    void identity();
    void collectionIdRoundTrip();
    void recordIdRoundTrip();
    void decodeCollectionIdRejectsRecordIds();

    void availableCollectionsReflectsDevice();
    void collectionInfoReturnsEmptyForUnknown();
    void createCollectionDelegatesToDevice();

    void loadRecordsExposesBackendRecords();
    void loadRecordFindsById();
    void createRecordAssignsId();
    void updateRecordWritesBack();
    void deleteRecordRemovesFromDevice();
    void contentHashIsSha256OfData();

    void modifiedSinceFiltersByTimestamp();
    void deletedSincePropagatesEncodedIds();
    void supportsDeleteTrackingFollowsDevice();
    void readAppBlockForwardsToDevice();

    void loadPalmRecordsCachesAcrossCalls();
    void cacheInvalidatedOnMutation();
};

/// MockPalmDatabaseAccess that counts readAllRecords() calls per database.
/// Used by the caching tests below to assert PalmBackend doesn't hammer
/// the device on repeat reads.
class CountingMockPalmDatabaseAccess : public MockPalmDatabaseAccess
{
public:
    mutable QHash<QString, int> readAllCounts;

    QList<PalmRecord> readAllRecords(const QString &dbName) const override
    {
        ++readAllCounts[dbName];
        return MockPalmDatabaseAccess::readAllRecords(dbName);
    }
};

void TestPalmBackend::identity()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    QCOMPARE(backend.backendId(), QStringLiteral("palm"));
    QCOMPARE(backend.displayName(), QStringLiteral("Palm OS Device"));
    QVERIFY(backend.isAvailable());

    PalmBackend detached(nullptr);
    QVERIFY(!detached.isAvailable());
}

void TestPalmBackend::collectionIdRoundTrip()
{
    QCOMPARE(PalmBackend::encodeCollectionId(QStringLiteral("MemoDB")),
             QStringLiteral("palm:memo"));
    QCOMPARE(PalmBackend::encodeCollectionId(QStringLiteral("DatebookDB")),
             QStringLiteral("palm:datebook"));

    QString db;
    QVERIFY(PalmBackend::decodeCollectionId(QStringLiteral("palm:memo"), &db));
    QCOMPARE(db, QStringLiteral("MemoDB"));

    QVERIFY(PalmBackend::decodeCollectionId(QStringLiteral("palm:datebook"), &db));
    QCOMPARE(db, QStringLiteral("DatebookDB"));

    QVERIFY(!PalmBackend::decodeCollectionId(QStringLiteral("notpalm:memo"), &db));
    QVERIFY(!PalmBackend::decodeCollectionId(QStringLiteral("palm:"), &db));
}

void TestPalmBackend::recordIdRoundTrip()
{
    const auto encoded = PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), 42);
    QCOMPARE(encoded, QStringLiteral("palm:memo:42"));

    QString db;
    std::uint32_t id = 0;
    QVERIFY(PalmBackend::decodeRecordId(encoded, &db, &id));
    QCOMPARE(db, QStringLiteral("MemoDB"));
    QCOMPARE(id, 42u);

    QVERIFY(!PalmBackend::decodeRecordId(QStringLiteral("palm:memo"), &db, &id));
    QVERIFY(!PalmBackend::decodeRecordId(
        QStringLiteral("palm:memo:notanumber"), &db, &id));
}

void TestPalmBackend::decodeCollectionIdRejectsRecordIds()
{
    QString db;
    QVERIFY(!PalmBackend::decodeCollectionId(
        QStringLiteral("palm:memo:42"), &db));
}

void TestPalmBackend::availableCollectionsReflectsDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    dev.createDatabase(QStringLiteral("DatebookDB"));

    PalmBackend backend(&dev);
    const auto cols = backend.availableCollections();
    QStringList ids;
    for (const auto &c : cols) ids.append(c.id);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, QStringList()
             << QStringLiteral("palm:datebook")
             << QStringLiteral("palm:memo"));
}

void TestPalmBackend::collectionInfoReturnsEmptyForUnknown()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);
    const auto info = backend.collectionInfo(
        QStringLiteral("palm:nonexistent"));
    QCOMPARE(info.id, QString());
}

void TestPalmBackend::createCollectionDelegatesToDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    Kalburator::Sync::CollectionInfo info;
    info.id = QStringLiteral("palm:memo");
    info.type = QStringLiteral("memos");
    const auto created = backend.createCollection(info);
    QCOMPARE(created, QStringLiteral("palm:memo"));
    QVERIFY(dev.hasDatabase(QStringLiteral("MemoDB")));
}

void TestPalmBackend::loadRecordsExposesBackendRecords()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto id1 = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("a")));
    const auto id2 = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("bb")));

    PalmBackend backend(&dev);
    const auto records = backend.loadRecords(QStringLiteral("palm:memo"));
    QCOMPARE(records.size(), 2);

    QStringList ids;
    for (const auto &r : records) ids.append(r.id);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids,
             QStringList()
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id1)
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id2));
}

void TestPalmBackend::loadRecordFindsById()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto id = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("hello")));

    PalmBackend backend(&dev);
    const auto got = backend.loadRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), id));
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("hello"));
}

void TestPalmBackend::createRecordAssignsId()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend backend(&dev);

    Kalburator::Sync::BackendRecord rec;
    rec.data = QByteArrayLiteral("new");
    rec.type = QStringLiteral("memos");
    rec.contentHash = QStringLiteral("ignored-replaced-by-backend");

    const auto id = backend.createRecord(QStringLiteral("palm:memo"), rec);
    QVERIFY(!id.isEmpty());
    QVERIFY(id.startsWith(QStringLiteral("palm:memo:")));

    // Round-trip: loading it back should match.
    const auto got = backend.loadRecord(id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("new"));
}

void TestPalmBackend::updateRecordWritesBack()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("orig")));

    PalmBackend backend(&dev);

    Kalburator::Sync::BackendRecord rec;
    rec.id = PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId);
    rec.data = QByteArrayLiteral("updated");
    QVERIFY(backend.updateRecord(rec));

    const auto got = backend.loadRecord(rec.id);
    QVERIFY(got.has_value());
    QCOMPARE(got->data, QByteArrayLiteral("updated"));
}

void TestPalmBackend::deleteRecordRemovesFromDevice()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("gone")));

    PalmBackend backend(&dev);
    QVERIFY(backend.deleteRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId)));
    QVERIFY(!dev.readRecord(QStringLiteral("MemoDB"), devId).has_value());
}

void TestPalmBackend::contentHashIsSha256OfData()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));
    const auto devId = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("abc")));

    PalmBackend backend(&dev);
    const auto got = backend.loadRecord(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), devId));
    QVERIFY(got.has_value());

    const auto expected = QCryptographicHash::hash(
        QByteArrayLiteral("abc"), QCryptographicHash::Sha256).toHex();
    QCOMPARE(got->contentHash, QString::fromLatin1(expected));
}

void TestPalmBackend::modifiedSinceFiltersByTimestamp()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmRecord oldRec;
    oldRec.data = QByteArrayLiteral("old");
    oldRec.lastModified = QDateTime::fromString(
        QStringLiteral("2020-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    dev.createRecord(QStringLiteral("MemoDB"), oldRec);

    PalmRecord freshRec;
    freshRec.data = QByteArrayLiteral("fresh");
    dev.createRecord(QStringLiteral("MemoDB"), freshRec);

    PalmBackend backend(&dev);
    const auto cutoff = QDateTime::fromString(
        QStringLiteral("2024-01-01T00:00:00Z"), Qt::ISODate).toUTC();
    const auto modified = backend.modifiedSince(
        QStringLiteral("palm:memo"), cutoff);

    QCOMPARE(modified.size(), 1);
    QCOMPARE(modified.first().data, QByteArrayLiteral("fresh"));
}

void TestPalmBackend::deletedSincePropagatesEncodedIds()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    const auto idA = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("a")));
    const auto idB = dev.createRecord(
        QStringLiteral("MemoDB"), makePalm(0, QByteArrayLiteral("b")));

    const auto before = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), idA));
    QVERIFY(dev.deleteRecord(QStringLiteral("MemoDB"), idB));

    PalmBackend backend(&dev);
    const auto deleted = backend.deletedSince(
        QStringLiteral("palm:memo"), before);
    QStringList sorted = deleted;
    std::sort(sorted.begin(), sorted.end());
    QCOMPARE(sorted, QStringList()
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), idA)
             << PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), idB));
}

void TestPalmBackend::supportsDeleteTrackingFollowsDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);
    QVERIFY(backend.supportsDeleteTracking()); // mock returns true
}

void TestPalmBackend::readAppBlockForwardsToDevice()
{
    MockPalmDatabaseAccess dev;
    PalmBackend backend(&dev);

    // Empty when unset.
    QCOMPARE(backend.readAppBlock(QStringLiteral("DatebookDB")), QByteArray());

    const QByteArray bytes("\x10\x20mock-appinfo", 14);
    dev.setAppBlock(QStringLiteral("DatebookDB"), bytes);
    QCOMPARE(backend.readAppBlock(QStringLiteral("DatebookDB")), bytes);
}

void TestPalmBackend::loadPalmRecordsCachesAcrossCalls()
{
    // E.16 follow-up: when a plugin walks N virtual sub-collections that
    // all sit behind the same Palm DB (e.g. contacts/0..3 → AddressDB),
    // PalmBackend must read the device once, not N times.
    CountingMockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("AddressDB"));
    dev.createRecord(QStringLiteral("AddressDB"),
                     makePalm(0, QByteArrayLiteral("alice")));
    dev.createRecord(QStringLiteral("AddressDB"),
                     makePalm(0, QByteArrayLiteral("bob")));

    PalmBackend backend(&dev);

    // Four reads (one per virtual contact slot) → one device read.
    for (int i = 0; i < 4; ++i) {
        const auto recs = backend.loadPalmRecords(QStringLiteral("AddressDB"));
        QCOMPARE(recs.size(), 2);
    }
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("AddressDB")), 1);

    // The IBlobBackend loadRecords overload shares the same cache.
    const auto blobRecs = backend.loadRecords(QStringLiteral("palm:address"));
    QCOMPARE(blobRecs.size(), 2);
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("AddressDB")), 1);

    // invalidateCache() forces a fresh read.
    backend.invalidateCache(QStringLiteral("AddressDB"));
    (void)backend.loadPalmRecords(QStringLiteral("AddressDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("AddressDB")), 2);
}

void TestPalmBackend::cacheInvalidatedOnMutation()
{
    // Each mutating call drops the cache for its database so the next
    // read sees the new state.
    CountingMockPalmDatabaseAccess dev;
    dev.createDatabase(QStringLiteral("MemoDB"));

    PalmBackend backend(&dev);
    (void)backend.loadPalmRecords(QStringLiteral("MemoDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("MemoDB")), 1);

    // createPalmRecord — cache must drop.
    const auto newId = backend.createPalmRecord(
        QStringLiteral("MemoDB"),
        makePalm(0, QByteArrayLiteral("first")));
    QVERIFY(newId != 0);
    (void)backend.loadPalmRecords(QStringLiteral("MemoDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("MemoDB")), 2);

    // updatePalmRecord — cache must drop.
    PalmRecord updated = makePalm(newId, QByteArrayLiteral("updated"));
    QVERIFY(backend.updatePalmRecord(QStringLiteral("MemoDB"), updated));
    (void)backend.loadPalmRecords(QStringLiteral("MemoDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("MemoDB")), 3);

    // deletePalmRecord — cache must drop.
    QVERIFY(backend.deletePalmRecord(QStringLiteral("MemoDB"), newId));
    (void)backend.loadPalmRecords(QStringLiteral("MemoDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("MemoDB")), 4);

    // Mutations on one DB must not invalidate other DBs.
    dev.createDatabase(QStringLiteral("AddressDB"));
    (void)backend.loadPalmRecords(QStringLiteral("AddressDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("AddressDB")), 1);
    backend.createPalmRecord(QStringLiteral("MemoDB"),
                             makePalm(0, QByteArrayLiteral("x")));
    (void)backend.loadPalmRecords(QStringLiteral("AddressDB"));
    QCOMPARE(dev.readAllCounts.value(QStringLiteral("AddressDB")), 1);
}

QTEST_MAIN(TestPalmBackend)
#include "tst_palmbackend.moc"
