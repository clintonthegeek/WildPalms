#include <QtTest/QtTest>

#include <cstring>

#include <pi-appinfo.h>

#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/contacts/palmcontactsbackend.h"
#include "plugins/contacts/contactsconflicthandler.h"

#include <kalburator/shape/shapecontribution.h>  // O7: complete type for shapeContributions() peerShapes()

#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "runtime/palmdeviceaccess.h"

// K.8b T13: core/ibackendplugin_v2.h deleted along with the V2 plugin ABI.

// Complete-type includes for libkalburator pointers (delete on a forward
// decl is UB; ConflictHandler is needed for dynamic_cast).
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/conflict/conflictpolicy.h>
#include <kalburator/conflict/conflictrecord.h>

// K.7: registry assertions for constructor-time domain extension
// registration. ContactsDomainPlugin removed; use registerStockPlugins.
#include <kalburator/shape/domainregistry.h>
#include <kalburator/shape/domainoperationsregistry.h>
#include <kalburator/shape/transformationregistry.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>

using WildPalms::ContactsPlugin::ContactsBackendPlugin;
using WildPalms::ContactsPlugin::PalmContactsBackend;
using WildPalms::ContactsPlugin::ContactsConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::Runtime::PalmDeviceAccess;

namespace {

// Build an AddressDB AppInfo block with the named slots populated.
// Names must fit in pi-appinfo's 16-byte name field (15 chars + NUL).
QByteArray buildAddressDbAppInfo(const QStringList &slotNames)
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const int n = static_cast<int>(std::min<qsizetype>(slotNames.size(), 16));
    for (int i = 0; i < n; ++i) {
        const QByteArray utf = slotNames[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

} // namespace

class TestContactsBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void cleanup();
    void pluginIdentity();
    void createBackends_returnsBlobOnly();
    void createBackends_populatesCategoryStoreFromAppInfo();
    void createConflictHandler_requiresPriorCreateBackends();
    void createConflictHandler_returnsContactsConflictHandler();
    void enrichConflictSnapshot_extractsFnFromVcard();
    void formatConflictRecordHtml_includesTitleAndPre();
    void constructorRegistersPalmShape();
};

void TestContactsBackendPlugin::cleanup()
{
    // O7: nothing to reset. The plugin no longer mutates any process-global
    // registry at construction (shape registration moved to shapeContributions(),
    // applied by PluginManager into an injected ShapeRegistries). The ambient
    // ::instance() registries were deleted in libkalburator v0.57.
}

void TestContactsBackendPlugin::pluginIdentity()
{
    ContactsBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("contacts"));
    QCOMPARE(p.version(),  QStringLiteral("2.0"));
    auto claims = p.claimedDatabases();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims[0], QStringLiteral("AddressDB"));
    // Sub-project D: contacts joined the V2 sidebar.
    QVERIFY(p.hasMainView());
}

void TestContactsBackendPlugin::createBackends_returnsBlobOnly()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);
}

void TestContactsBackendPlugin::createBackends_populatesCategoryStoreFromAppInfo()
{
    // Slots 0 ("Unfiled"), 1 ("Family"), 5 ("Customers") populated;
    // intermediate slots (2..4) blank — must NOT show as collections.
    QStringList names;
    for (int i = 0; i < 16; ++i) names << QString();
    names[0] = QStringLiteral("Unfiled");
    names[1] = QStringLiteral("Family");
    names[5] = QStringLiteral("Customers");

    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    mock->setAppBlock(QStringLiteral("AddressDB"), buildAddressDbAppInfo(names));
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);

    auto *blob = static_cast<PalmContactsBackend *>(blobPtr.get());
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 4);  // palm:contacts (domain) + Unfiled (slot 0) + Family (slot 1) + Customers (slot 5)

    // The domain-level collection must be present (C, Task 9).
    const QStringList ids = [&] {
        QStringList l;
        for (const auto &c : cols) l << c.id;
        return l;
    }();
    QVERIFY(ids.contains(QStringLiteral("palm:contacts")));
}

void TestContactsBackendPlugin::createConflictHandler_requiresPriorCreateBackends()
{
    ContactsBackendPlugin p;
    auto *handler = p.createConflictHandler();
    QVERIFY(handler == nullptr);
}

void TestContactsBackendPlugin::createConflictHandler_returnsContactsConflictHandler()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);   // primes m_device
    Q_UNUSED(blobPtr)

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<ContactsConflictHandler *>(handler) != nullptr);
    delete handler;
}

void TestContactsBackendPlugin::enrichConflictSnapshot_extractsFnFromVcard()
{
    ContactsBackendPlugin p;
    Kalburator::Conflict::RecordSnapshot snap;
    snap.content = QByteArrayLiteral(
        "BEGIN:VCARD\r\n"
        "VERSION:4.0\r\n"
        "FN:Jane Doe\r\n"
        "N:Doe;Jane;;;\r\n"
        "END:VCARD\r\n");

    p.enrichConflictSnapshot(snap, /*isSourceSide=*/false);

    QCOMPARE(snap.metadata.value(QStringLiteral("title")).toString(),
             QStringLiteral("Jane Doe"));
    QCOMPARE(snap.contentType, QStringLiteral("text/vcard"));
}

void TestContactsBackendPlugin::formatConflictRecordHtml_includesTitleAndPre()
{
    ContactsBackendPlugin p;
    Kalburator::Conflict::RecordSnapshot snap;
    snap.content = QByteArrayLiteral("BEGIN:VCARD\nFN:Jane Doe\nEND:VCARD\n");
    snap.metadata[QStringLiteral("title")] = QStringLiteral("Jane Doe");

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<h3>Jane Doe</h3>")));
    QVERIFY(html.contains(QStringLiteral("<pre>")));
}

void TestContactsBackendPlugin::constructorRegistersPalmShape()
{
    // O7: the plugin no longer registers shapes in its ctor; it exposes them
    // via shapeContributions(), which PluginManager applies into the injected
    // ShapeRegistries. Verify the contribution yields the (contacts, palm) peer.
    ContactsBackendPlugin plugin;

    const auto contribs = plugin.shapeContributions();
    QCOMPARE(contribs.size(), 1);

    const Kalburator::Shape::Shape palm{
        Kalburator::Shape::DomainId{"contacts"},
        Kalburator::Shape::EncodingId{"palm"} };

    bool foundPalm = false;
    for (const auto &[shape, cat] : contribs.first()->peerShapes())
        if (shape == palm) foundPalm = true;
    QVERIFY(foundPalm);
}

QTEST_MAIN(TestContactsBackendPlugin)
#include "tst_contactsbackendplugin.moc"
