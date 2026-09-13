#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "plugins/contacts/hubcontactsreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

using WildPalms::ContactsPlugin::HubContactsReader;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

const Shape kTestShape{ DomainId{"contacts"}, EncodingId{"vcard4"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id = QStringLiteral("palm:contacts");
    info.name = QStringLiteral("Contacts");
    info.type = QStringLiteral("contacts");
    hub->createCollection(info, kTestShape);
    return hub;
}

BackendRecord makeRecord(const QString &id, const QByteArray &bytes)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("contact");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

} // namespace

class TstHubContactsReader : public QObject
{
    Q_OBJECT
private slots:
    void listsSeededIds();
    void returnsSeededBytesVerbatim();
    void missingIdYieldsEmptyBytes();
    void emptyCollectionYieldsEmptyList();
};

void TstHubContactsReader::listsSeededIds()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    hub->createRecord(QStringLiteral("palm:contacts"),
                      makeRecord(QStringLiteral("c-1"),
                                 "BEGIN:VCARD\nVERSION:4.0\nFN:Test\nEND:VCARD\n"));
    hub->createRecord(QStringLiteral("palm:contacts"),
                      makeRecord(QStringLiteral("c-2"),
                                 "BEGIN:VCARD\nVERSION:4.0\nFN:Test\nEND:VCARD\n"));

    HubContactsReader reader(hub.get(), QStringLiteral("palm:contacts"));
    const QStringList ids = reader.listRecordIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("c-1")));
    QVERIFY(ids.contains(QStringLiteral("c-2")));
}

void TstHubContactsReader::returnsSeededBytesVerbatim()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    const QByteArray payload = "BEGIN:VCARD\nVERSION:4.0\nFN:Hello\nEND:VCARD\n";
    hub->createRecord(QStringLiteral("palm:contacts"),
                      makeRecord(QStringLiteral("c-1"), payload));

    HubContactsReader reader(hub.get(), QStringLiteral("palm:contacts"));
    QCOMPARE(reader.recordBytes(QStringLiteral("c-1")), payload);
}

void TstHubContactsReader::missingIdYieldsEmptyBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubContactsReader reader(hub.get(), QStringLiteral("palm:contacts"));
    QVERIFY(reader.recordBytes(QStringLiteral("no-such-id")).isEmpty());
}

void TstHubContactsReader::emptyCollectionYieldsEmptyList()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubContactsReader reader(hub.get(), QStringLiteral("palm:contacts"));
    QVERIFY(reader.listRecordIds().isEmpty());
}

QTEST_MAIN(TstHubContactsReader)
#include "tst_hubcontactsreader.moc"
