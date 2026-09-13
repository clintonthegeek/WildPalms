#include <QtTest>
#include <QTemporaryDir>
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrevisionstore.h"
#include "palm/sync/palmchangedetection.h"
#include "palmruntime.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

namespace {
class FakeCD : public WildPalms::PalmSync::PalmChangeDetection {
public:
    QString rev;
protected:
    QString currentDbRevision() const override { return rev; }
};
}

class TestPalmChangeDetection : public QObject {
    Q_OBJECT
private slots:
    void mockRevision_emptyForUnknownDb();
    void mockRevision_bumpsOnWrite();
    void palmBackendForwardsRevision();
    void revisionStore_persistsAcrossInstances();
    void mixin_usesStoreAndHook();
    void mixin_noStoreIsSafe();
};

void TestPalmChangeDetection::mockRevision_emptyForUnknownDb()
{
    MockPalmDatabaseAccess dev;
    QCOMPARE(dev.databaseRevision("NoSuchDB"), QString());
}

void TestPalmChangeDetection::mockRevision_bumpsOnWrite()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("DatebookDB");
    const QString r0 = dev.databaseRevision("DatebookDB");
    PalmRecord rec;                       // default record is fine for the bump
    dev.createRecord("DatebookDB", rec);
    const QString r1 = dev.databaseRevision("DatebookDB");
    QVERIFY(!r1.isEmpty());
    QVERIFY(r0 != r1);
}

void TestPalmChangeDetection::palmBackendForwardsRevision()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("AddressDB");
    WildPalms::PalmSync::PalmBackend backend(&dev);
    PalmRecord rec;
    dev.createRecord("AddressDB", rec);
    QCOMPARE(backend.databaseRevision("AddressDB"), dev.databaseRevision("AddressDB"));
    QVERIFY(!backend.databaseRevision("AddressDB").isEmpty());
}

void TestPalmChangeDetection::revisionStore_persistsAcrossInstances()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/palm-revisions.ini";
    {
        WildPalms::PalmSync::PalmRevisionStore s(path);
        QVERIFY(s.token("palm:calendar").isEmpty());
        s.setToken("palm:calendar", "42");
    }
    WildPalms::PalmSync::PalmRevisionStore s2(path);  // fresh instance, same file
    QCOMPARE(s2.token("palm:calendar"), QString("42"));
}

void TestPalmChangeDetection::mixin_usesStoreAndHook()
{
    QTemporaryDir dir;
    WildPalms::PalmSync::PalmRevisionStore store(dir.path() + "/r.ini");
    FakeCD cd;
    cd.setPalmRevisionStore(&store);
    cd.rev = "7";

    QCOMPARE(cd.collectionRevision("palm:calendar"), QString("7"));   // live hook
    QVERIFY(cd.cachedCollectionRevision("palm:calendar").isEmpty());  // nothing cached
    cd.primeRevisionCache({{"palm:calendar", "7"}});
    QCOMPARE(cd.cachedCollectionRevision("palm:calendar"), QString("7"));
}

void TestPalmChangeDetection::mixin_noStoreIsSafe()
{
    FakeCD cd;                       // no store injected
    cd.rev = "3";
    QCOMPARE(cd.collectionRevision("x"), QString("3"));
    QVERIFY(cd.cachedCollectionRevision("x").isEmpty());
    cd.primeRevisionCache({{"x", "3"}});           // no-op, must not crash
    QVERIFY(cd.cachedCollectionRevision("x").isEmpty());
}

QTEST_MAIN(TestPalmChangeDetection)
#include "tst_palm_change_detection.moc"
