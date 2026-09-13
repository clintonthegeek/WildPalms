#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "plugins/calendar/hubcalendarreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

using WildPalms::CalendarPlugin::HubCalendarReader;
using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

const Shape kTestShape{ DomainId{"calendar"}, EncodingId{"ical"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id = QStringLiteral("palm:calendar");
    info.name = QStringLiteral("Calendar");
    info.type = QStringLiteral("calendar");
    hub->createCollection(info, kTestShape);
    return hub;
}

BackendRecord makeRecord(const QString &id, const QByteArray &bytes)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("event");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

} // namespace

class TstHubCalendarReader : public QObject
{
    Q_OBJECT
private slots:
    void listsSeededIds();
    void returnsSeededBytesVerbatim();
    void missingIdYieldsEmptyBytes();
    void emptyCollectionYieldsEmptyList();
};

void TstHubCalendarReader::listsSeededIds()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-1"), "BEGIN:VEVENT\nEND:VEVENT\n"));
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-2"), "BEGIN:VEVENT\nEND:VEVENT\n"));

    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    const QStringList ids = reader.listRecordIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("evt-1")));
    QVERIFY(ids.contains(QStringLiteral("evt-2")));
}

void TstHubCalendarReader::returnsSeededBytesVerbatim()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    const QByteArray payload = "BEGIN:VEVENT\nSUMMARY:Hello\nEND:VEVENT\n";
    hub->createRecord(QStringLiteral("palm:calendar"),
                      makeRecord(QStringLiteral("evt-1"), payload));

    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QCOMPARE(reader.recordBytes(QStringLiteral("evt-1")), payload);
}

void TstHubCalendarReader::missingIdYieldsEmptyBytes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QVERIFY(reader.recordBytes(QStringLiteral("no-such-id")).isEmpty());
}

void TstHubCalendarReader::emptyCollectionYieldsEmptyList()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    auto hub = makeHub(dir);
    HubCalendarReader reader(hub.get(), QStringLiteral("palm:calendar"));
    QVERIFY(reader.listRecordIds().isEmpty());
}

QTEST_MAIN(TstHubCalendarReader)
#include "tst_hubcalendarreader.moc"
