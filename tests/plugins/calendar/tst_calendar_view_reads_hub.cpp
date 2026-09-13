// Sub-project D Task 6: CalendarView is reader-driven.
//
// Seeds 2 VEVENT records into a GenericSqliteBackend "palm:calendar"
// collection, constructs a HubCalendarReader over it, instantiates
// CalendarView, and verifies the event list is populated.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QListWidget>
#include <QCalendarWidget>
#include <memory>

#include "plugins/calendar/calendarview.h"
#include "plugins/calendar/hubcalendarreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

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
    info.id   = QStringLiteral("palm:calendar");
    info.name = QStringLiteral("Calendar");
    info.type = QStringLiteral("calendar");
    hub->createCollection(info, kTestShape);
    return hub;
}

void seedEvent(GenericSqliteBackend *hub, const QString &id, const QString &uid)
{
    // VCALENDAR wrapper is required for KCalendarCore::ICalFormat::fromString
    // to parse a VEVENT.
    const QByteArray bytes = QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//WildPalms//TEST//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:%1\r\n"
        "DTSTART:20260528T120000Z\r\n"
        "DTEND:20260528T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid).toUtf8();

    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("event");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    hub->createRecord(QStringLiteral("palm:calendar"), r);
}

} // namespace

class TstCalendarViewReadsHub : public QObject
{
    Q_OBJECT
private slots:
    void loadsEventsFromReader();
};

void TstCalendarViewReadsHub::loadsEventsFromReader()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    seedEvent(hub.get(), QStringLiteral("e-1"), QStringLiteral("evt-1"));
    seedEvent(hub.get(), QStringLiteral("e-2"), QStringLiteral("evt-2"));

    WildPalms::CalendarPlugin::HubCalendarReader reader(
        hub.get(), QStringLiteral("palm:calendar"));
    CalendarView view;
    view.setHubReader(&reader);
    // Select the event date so updateEventList populates the list.
    view.loadFromPath(tmp.path());

    auto *calendarWidget = view.findChild<QCalendarWidget*>();
    QVERIFY(calendarWidget);
    calendarWidget->setSelectedDate(QDate(2026, 5, 28));

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    QVERIFY(list->count() > 0);
}

QTEST_MAIN(TstCalendarViewReadsHub)
#include "tst_calendar_view_reads_hub.moc"
