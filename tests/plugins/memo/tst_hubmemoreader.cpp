#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "plugins/memo/hubmemoreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

using WildPalms::Memo::HubMemoReader;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

const Shape kTestShape{ DomainId{"note"}, EncodingId{"markdown"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id = QStringLiteral("palm:memo");
    info.name = QStringLiteral("Memos");
    info.type = QStringLiteral("note");
    hub->createCollection(info, kTestShape);
    return hub;
}

BackendRecord makeRecord(const QString &id, const QByteArray &bytes)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

} // namespace

class TstHubMemoReader : public QObject
{
    Q_OBJECT
private slots:
    void listsSeededIds();
    void returnsSeededBytesVerbatim();
    void missingIdYieldsEmptyBytes();
    void emptyCollectionYieldsEmptyList();
};

void TstHubMemoReader::listsSeededIds()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    hub->createRecord(QStringLiteral("palm:memo"),
                      makeRecord(QStringLiteral("m-1"), "# title\n\nbody\n"));
    hub->createRecord(QStringLiteral("palm:memo"),
                      makeRecord(QStringLiteral("m-2"), "# title\n\nbody\n"));

    HubMemoReader reader(hub.get(), QStringLiteral("palm:memo"));
    const QStringList ids = reader.listRecordIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("m-1")));
    QVERIFY(ids.contains(QStringLiteral("m-2")));
}

void TstHubMemoReader::returnsSeededBytesVerbatim()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    const QByteArray payload = "# Hello\n\nworld\n";
    hub->createRecord(QStringLiteral("palm:memo"),
                      makeRecord(QStringLiteral("m-1"), payload));

    HubMemoReader reader(hub.get(), QStringLiteral("palm:memo"));
    QCOMPARE(reader.recordBytes(QStringLiteral("m-1")), payload);
}

void TstHubMemoReader::missingIdYieldsEmptyBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubMemoReader reader(hub.get(), QStringLiteral("palm:memo"));
    QVERIFY(reader.recordBytes(QStringLiteral("no-such-id")).isEmpty());
}

void TstHubMemoReader::emptyCollectionYieldsEmptyList()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubMemoReader reader(hub.get(), QStringLiteral("palm:memo"));
    QVERIFY(reader.listRecordIds().isEmpty());
}

QTEST_MAIN(TstHubMemoReader)
#include "tst_hubmemoreader.moc"
