#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "plugins/todos/hubtodoreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

using WildPalms::TodoPlugin::HubTodoReader;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

const Shape kTestShape{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id = QStringLiteral("palm:todo");
    info.name = QStringLiteral("ToDos");
    info.type = QStringLiteral("todo");
    hub->createCollection(info, kTestShape);
    return hub;
}

BackendRecord makeRecord(const QString &id, const QByteArray &bytes)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("todo");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

} // namespace

class TstHubTodoReader : public QObject
{
    Q_OBJECT
private slots:
    void listsSeededIds();
    void returnsSeededBytesVerbatim();
    void missingIdYieldsEmptyBytes();
    void emptyCollectionYieldsEmptyList();
};

void TstHubTodoReader::listsSeededIds()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    hub->createRecord(QStringLiteral("palm:todo"),
                      makeRecord(QStringLiteral("t-1"), "BEGIN:VTODO\nEND:VTODO\n"));
    hub->createRecord(QStringLiteral("palm:todo"),
                      makeRecord(QStringLiteral("t-2"), "BEGIN:VTODO\nEND:VTODO\n"));

    HubTodoReader reader(hub.get(), QStringLiteral("palm:todo"));
    const QStringList ids = reader.listRecordIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("t-1")));
    QVERIFY(ids.contains(QStringLiteral("t-2")));
}

void TstHubTodoReader::returnsSeededBytesVerbatim()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    const QByteArray payload = "BEGIN:VTODO\nSUMMARY:Hello\nEND:VTODO\n";
    hub->createRecord(QStringLiteral("palm:todo"),
                      makeRecord(QStringLiteral("t-1"), payload));

    HubTodoReader reader(hub.get(), QStringLiteral("palm:todo"));
    QCOMPARE(reader.recordBytes(QStringLiteral("t-1")), payload);
}

void TstHubTodoReader::missingIdYieldsEmptyBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubTodoReader reader(hub.get(), QStringLiteral("palm:todo"));
    QVERIFY(reader.recordBytes(QStringLiteral("no-such-id")).isEmpty());
}

void TstHubTodoReader::emptyCollectionYieldsEmptyList()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubTodoReader reader(hub.get(), QStringLiteral("palm:todo"));
    QVERIFY(reader.listRecordIds().isEmpty());
}

QTEST_MAIN(TstHubTodoReader)
#include "tst_hubtodoreader.moc"
