// Phase J Task 11: CardDAV E2E integration test.
// Contacts equivalent of tst_runtime_caldav_e2e — wires
// FakeCardDavServer + CardDavProvider + PalmRuntime through the real
// SyncEngine to verify end-to-end contacts sync.

#include <QtTest/QtTest>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"
#include "fakecarddavserver.h"

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/shape/shape.h>

#include <kalburator/sync/carddavprovider.h>
#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/calendar/syncbackend.h>
#include <kalburator/sync/syncbackendbase.h>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>
// K.8b T7: BlobBackendAdapter deleted; inject via BlobSyncBackendWrapper.
#include "../blobsyncbackendwrapper.h"

using namespace WildPalms::Runtime;
using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

namespace {

bool waitForFutureBool(QFuture<bool> f, int timeoutMs = 5000)
{
    if (f.isFinished()) return true;
    QFutureWatcher<bool> w;
    QSignalSpy doneSpy(&w, &QFutureWatcher<bool>::finished);
    w.setFuture(f);
    if (f.isFinished()) return true;
    return doneSpy.wait(timeoutMs);
}

BackendConfiguration makeCardDavConfig(const QUrl &serverUrl)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("test-carddav");
    cfg.type = QStringLiteral("carddav");
    cfg.displayName = QStringLiteral("Fake CardDAV");
    cfg.connectionParams.insert(QStringLiteral("url"), serverUrl.toString());
    cfg.connectionParams.insert(QStringLiteral("username"), QStringLiteral("testuser"));
    cfg.connectionParams.insert(QStringLiteral("password"), QStringLiteral("testpass"));
    return cfg;
}

QByteArray makeVCard4(const QString &uid, const QString &fn)
{
    return QStringLiteral(
        "BEGIN:VCARD\r\nVERSION:4.0\r\n"
        "UID:%1\r\nFN:%2\r\n"
        "END:VCARD\r\n")
        .arg(uid, fn).toUtf8();
}

const QString kPalmConId  = QStringLiteral("palm:contacts/0");
const QString kPalmBkId   = QStringLiteral("palm-contacts");
const QString kCarddavBkId = QStringLiteral("carddav-personal");
// Use {contacts, vcard4} on the palm wrapper side: matches RemoteContactsBackend's
// native shape so the engine's fast-path applies (no transcoding needed).
const Shape   kConShape   = { DomainId{QStringLiteral("contacts")},
                               EncodingId{QStringLiteral("vcard4")} };

} // namespace

class TstRuntimeCardDavE2E : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // O7: no global seeding — each PalmRuntime loads stock + WP plugins into
        // its own ShapeRegistries (see PalmRuntime::registerPalmPlugins).
    }

    void palm_to_carddav_propagates();
    void carddav_to_palm_propagates();
};

void TstRuntimeCardDavE2E::palm_to_carddav_propagates()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCardDavServer server;
    QVERIFY(server.startListening());

    CardDavProvider provider;
    provider.load(makeCardDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));
    QVERIFY(provider.isConnected());

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString carddavColId = cols.first().id;
    auto specs = provider.createBackends();
    QVERIFY(!specs.empty());
    auto carddavBackendOwned = std::move(specs.front().backend);
    QVERIFY(carddavBackendOwned);
    auto *carddavRaw = carddavBackendOwned.release();
    auto *carddavSync = dynamic_cast<SyncBackendBase *>(carddavRaw);
    QVERIFY(carddavSync);
    runtime.registerBackendInstanceForTest(
        kCarddavBkId, std::unique_ptr<SyncBackendBase>(carddavSync));

    auto palmBlob = std::make_unique<MockBlobBackend>();
    {
        CollectionInfo ci; ci.id = kPalmConId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
        BackendRecord rec;
        rec.id   = QStringLiteral("contact-palm-001");
        rec.data = makeVCard4(QStringLiteral("contact-palm-001@palm"),
                              QStringLiteral("Palm Contact"));
        rec.type = QStringLiteral("text/vcard");
        palmBlob->createRecord(kPalmConId, rec);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlob), kPalmBkId, kConShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("test-p2c");
        m.sourceBackend = kPalmBkId;
        m.targetBackend = kCarddavBkId;
        m.sourceCalendar = kPalmConId;
        m.targetCalendar = carddavColId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);

    // The palm contact should land on the CardDAV server.
    QVERIFY(server.hasContact(QStringLiteral("personal"),
                              QStringLiteral("contact-palm-001@palm")));
}

void TstRuntimeCardDavE2E::carddav_to_palm_propagates()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    PalmRuntime runtime(profileDir.path());

    FakeCardDavServer server;
    QVERIFY(server.startListening());
    server.setSeedRecords(QStringLiteral("personal"),
                         { makeVCard4(QStringLiteral("server-001@carddav"),
                                      QStringLiteral("Server Contact")) });

    CardDavProvider provider;
    provider.load(makeCardDavConfig(server.baseUrl()));
    QVERIFY(waitForFutureBool(provider.connect()));

    auto cols = provider.collections();
    QVERIFY(!cols.isEmpty());
    const QString carddavColId = cols.first().id;
    auto specs = provider.createBackends();
    QVERIFY(!specs.empty());
    auto carddavBackendOwned = std::move(specs.front().backend);
    QVERIFY(carddavBackendOwned);
    auto *carddavRaw = carddavBackendOwned.release();
    auto *carddavSync = dynamic_cast<SyncBackendBase *>(carddavRaw);
    QVERIFY(carddavSync);
    runtime.registerBackendInstanceForTest(
        kCarddavBkId, std::unique_ptr<SyncBackendBase>(carddavSync));

    auto palmBlobOwned = std::make_unique<MockBlobBackend>();
    MockBlobBackend *palmBlob = palmBlobOwned.get();
    {
        CollectionInfo ci; ci.id = kPalmConId; ci.name = QStringLiteral("Unfiled");
        palmBlob->createCollection(ci);
    }
    runtime.registerBackendInstanceForTest(kPalmBkId,
        WildPalmsTest::BlobSyncBackendWrapper::wrap(
            std::move(palmBlobOwned), kPalmBkId, kConShape));

    {
        SyncMapping m;
        m.id = QStringLiteral("test-c2p");
        m.sourceBackend = kCarddavBkId;
        m.targetBackend = kPalmBkId;
        m.sourceCalendar = carddavColId;
        m.targetCalendar = kPalmConId;
        m.mode = SyncMode::TwoWay;
        m.enabled = true;
        runtime.setMappingsForTest({m});
    }

    auto future = runtime.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
    QVERIFY(future.resultAt(0).success);

    // The server contact should land on the palm blob backend.
    QVERIFY(!palmBlob->recordsIn(kPalmConId).isEmpty());
}

WILDPALMS_QTEST_GUILESS_MAIN(TstRuntimeCardDavE2E)
#include "tst_runtime_carddav_e2e.moc"
