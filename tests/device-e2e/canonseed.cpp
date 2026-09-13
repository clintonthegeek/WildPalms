#include "canonseed.h"

#include <QJsonArray>
#include <QJsonObject>

#include <kalburator/shape/canonenvelope.h>

namespace WildPalms {
namespace DeviceE2E {

QByteArray buildCanonCalendarEvent(const CanonCalendarEventSpec &spec)
{
    namespace CE = Kalburator::Shape::CanonEnvelope;

    QJsonObject ev;
    ev.insert(QStringLiteral("summary"), spec.summary);
    ev.insert(QStringLiteral("description"), spec.description);

    QJsonObject start;
    start.insert(QStringLiteral("dateTime"), spec.start.toUTC().toString(Qt::ISODate));
    start.insert(QStringLiteral("floating"), false);
    QJsonObject end;
    end.insert(QStringLiteral("dateTime"), spec.end.toUTC().toString(Qt::ISODate));
    end.insert(QStringLiteral("floating"), false);
    ev.insert(QStringLiteral("start"), start);
    ev.insert(QStringLiteral("end"), end);
    ev.insert(QStringLiteral("allDay"), spec.allDay);

    if (spec.withAlarm) {
        QJsonObject alarm;
        alarm.insert(QStringLiteral("type"), 1); // Display
        alarm.insert(QStringLiteral("offset"), spec.alarmOffsetSeconds);
        alarm.insert(QStringLiteral("text"), QStringLiteral("Reminder"));
        QJsonArray alarms;
        alarms.append(alarm);
        ev.insert(QStringLiteral("alarms"), alarms);
    }

    CE::stampEnvelope(ev, QStringLiteral("calendar"), spec.uid);
    return CE::serialize(ev);
}

} // namespace DeviceE2E
} // namespace WildPalms
