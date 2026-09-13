#include <QTest>
#include <QFuture>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/types/synctypes.h>
#include "palm/kpilotlink.h"
#include "palm/pilotrecord.h"
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>
// K.8b T7: BlobBackendAdapter deleted; inject via BlobSyncBackendWrapper.
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

static SyncMapping makeTwoWayMapping(const QString &srcBackend, const QString &srcCol,
                                     const QString &tgtBackend, const QString &tgtCol)
{
    SyncMapping m;
    m.id             = QStringLiteral("test-mapping");
    m.sourceBackend  = srcBackend;
    m.targetBackend  = tgtBackend;
    m.sourceCalendar = srcCol;
    m.targetCalendar = tgtCol;
    m.mode           = SyncMode::TwoWay;
    m.enabled        = true;
    return m;
}

// Minimal KPilotLink mock for backup/restore tests.
// retrieveDatabase() writes a small marker file to destPath.
// installFile() records the file paths it was called with.
class MockKPilotLink : public KPilotLink {
    Q_OBJECT
public:
    explicit MockKPilotLink(QObject *parent = nullptr) : KPilotLink(parent) {}

    QStringList dbNames;        // preset list returned by listDatabases()
    QStringList installedFiles; // filled in by installFile()

    // Connection management
    bool openConnection() override { return true; }
    void closeConnection() override {}
    LinkStatus status() const override { return AcceptedDevice; }

    // User / system info — not exercised by backup/restore
    bool readUserInfo(struct PilotUser &) override { return true; }
    bool writeUserInfo(const struct PilotUser &) override { return true; }
    bool readSysInfo(struct SysInfo &) override { return true; }
    bool readStorageInfo(int, struct CardInfo &) override { return true; }

    // Database / record ops — not exercised by backup/restore
    int openDatabase(const QString &, bool) override { return 0; }
    bool closeDatabase(int) override { return true; }
    QStringList listDatabases() override { return dbNames; }
    QList<PilotRecord*> readAllRecords(int) override { return {}; }
    PilotRecord* readRecordByIndex(int, int) override { return nullptr; }
    PilotRecord* readRecordById(int, int) override { return nullptr; }
    bool writeRecord(int, PilotRecord *) override { return true; }
    bool deleteRecord(int, int) override { return true; }
    QList<PilotRecord*> readModifiedRecords(int) override { return {}; }
    bool resetDBIndex(int) override { return true; }
    bool readAppBlock(int, unsigned char *, size_t *) override { return true; }
    bool writeAppBlock(int, const unsigned char *, size_t) override { return true; }
    bool beginSync() override { return true; }
    bool endSync() override { return true; }
    bool isConnected() const override { return true; }
    bool cleanUpDatabase(int) override { return true; }
    bool resetSyncFlags(int) override { return true; }

    // Raw file transfer — these are what backup/restore actually call
    bool retrieveDatabase(const QString &dbName, const QString &destPath) override {
        QFile f(destPath);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(("MOCKPDB:" + dbName).toUtf8());
        return true;
    }
    bool installFile(const QString &filePath) override {
        installedFiles.append(filePath);
        return true;
    }
};

} // namespace

class TestPalmRuntimeModes : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // O7: no global seeding — each PalmRuntime loads stock + WP plugins into
        // its own ShapeRegistries (see PalmRuntime::registerPalmPlugins).
    }

    void fullSync_clearsBaselinesThenCopiesPalmToPC() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *pcRaw = pcBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc")));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        // fullSync should succeed and copy Palm records to PC.
        auto future = runtime.fullSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        const auto written = pcRaw->recordsIn(QStringLiteral("pc-col"));
        QCOMPARE(written.size(), 1);
        QVERIFY(written.contains(QStringLiteral("rec-A")));
    }

    void copyPalmToPC_overwritesPC() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        // Palm has rec-A; PC already has rec-B.
        auto palmBlob = std::make_unique<MockBlobBackend>();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("palm-col");
            ci.name = QStringLiteral("Palm");
            palmBlob->createCollection(ci);
            palmBlob->createRecord(QStringLiteral("palm-col"),
                makeRecord(QStringLiteral("rec-A"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-A\r\nSUMMARY:A\r\n"
                               "DTSTART:20260501T090000Z\r\nDTEND:20260501T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("palm"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(palmBlob), QStringLiteral("palm")));

        auto pcBlob = std::make_unique<MockBlobBackend>();
        MockBlobBackend *pcRaw = pcBlob.get();
        {
            CollectionInfo ci;
            ci.id   = QStringLiteral("pc-col");
            ci.name = QStringLiteral("PC");
            pcBlob->createCollection(ci);
            pcBlob->createRecord(QStringLiteral("pc-col"),
                makeRecord(QStringLiteral("rec-B"),
                    QByteArray("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                               "BEGIN:VEVENT\r\nUID:rec-B\r\nSUMMARY:B\r\n"
                               "DTSTART:20260502T090000Z\r\nDTEND:20260502T100000Z\r\n"
                               "END:VEVENT\r\nEND:VCALENDAR\r\n")));
        }
        runtime.registerBackendInstanceForTest(QStringLiteral("pc"),
            WildPalmsTest::BlobSyncBackendWrapper::wrap(
                std::move(pcBlob), QStringLiteral("pc")));

        runtime.setMappingsForTest(
            {makeTwoWayMapping(QStringLiteral("palm"), QStringLiteral("palm-col"),
                               QStringLiteral("pc"),   QStringLiteral("pc-col"))});

        auto future = runtime.copyPalmToPC();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // PC should now have Palm's record, not its own.
        const auto pcFinal = pcRaw->recordsIn(QStringLiteral("pc-col"));
        QVERIFY(pcFinal.contains(QStringLiteral("rec-A")));
        QVERIFY(!pcFinal.contains(QStringLiteral("rec-B")));
    }

    // copyPCToPalm_overwritesPalm: deleted. The copyPCToPalm() mode was
    // subsumed by clobberSync() (which adds a pre-wipe phase). The replacement
    // semantic test is tst_palm_runtime_clobber_sync.cpp.


    void backup_dumpsAllDatabasesAsFiles() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        PalmRuntime runtime(profileDir.path());

        MockKPilotLink link;
        link.dbNames = {QStringLiteral("DatebookDB"), QStringLiteral("MemoDB")};
        // K.8b T15: inject link via PalmDeviceAccess seam; setLinkForTest on
        // PalmRuntime removed — facade owns the link now.
        auto device = std::make_unique<PalmDeviceAccess>();
        device->setLinkForTest(&link);
        runtime.setDeviceAccessForTest(std::move(device));

        auto future = runtime.backup();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // backup dir should contain one .pdb file per database
        const QString backupDir = QDir(profileDir.path()).filePath(QStringLiteral("backup"));
        QVERIFY(QFileInfo(backupDir + QStringLiteral("/DatebookDB.pdb")).exists());
        QVERIFY(QFileInfo(backupDir + QStringLiteral("/MemoDB.pdb")).exists());
    }

    void restore_installsAllPdbFilesFromBackupDir() {
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());

        // Pre-populate backup dir with two fake .pdb files.
        const QString backupDir = QDir(profileDir.path()).filePath(QStringLiteral("backup"));
        QDir().mkpath(backupDir);
        const QStringList files = {
            backupDir + QStringLiteral("/DatebookDB.pdb"),
            backupDir + QStringLiteral("/MemoDB.pdb"),
        };
        for (const QString &f : files) {
            QFile file(f);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("FAKEPDB");
        }

        PalmRuntime runtime(profileDir.path());

        MockKPilotLink link;
        // K.8b T15: inject link via PalmDeviceAccess seam.
        auto device = std::make_unique<PalmDeviceAccess>();
        device->setLinkForTest(&link);
        runtime.setDeviceAccessForTest(std::move(device));

        auto future = runtime.restore();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        QVERIFY(future.resultAt(0).success);

        // installFile should have been called for both files
        QCOMPARE(link.installedFiles.size(), 2);
        const QSet<QString> installed(link.installedFiles.cbegin(), link.installedFiles.cend());
        QVERIFY(installed.contains(files[0]));
        QVERIFY(installed.contains(files[1]));
    }

    void backend_registry_accessor_returns_owned_registry() {
        QString tmpProfile = QDir(QDir::tempPath()).filePath(QStringLiteral("wp-test-profile"));
        QDir(tmpProfile).removeRecursively();
        QDir().mkpath(tmpProfile);

        WildPalms::Runtime::PalmRuntime rt(tmpProfile);
        Kalburator::Sync::BackendRegistry &reg = rt.backendRegistry();

        // C: the canonical hub ("wp-hub") is registered at construction; Palm
        // backends are registered later at finishConnect (device connect), so
        // before connecting the only instance is the hub. Calling
        // registeredInstanceIds() exercises the borrowed reference; a
        // use-after-free would crash here rather than passing.
        QCOMPARE(reg.registeredInstanceIds().size(), 1);
        QVERIFY(reg.registeredInstanceIds().contains(QStringLiteral("wp-hub")));
    }
};

QTEST_GUILESS_MAIN(TestPalmRuntimeModes)
#include "tst_palm_runtime_modes.moc"
