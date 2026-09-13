#include <QtTest/QtTest>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "plugins/calendar/calendarconflicthandler.h"

#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/conflict/palmbackendconfig.h"

#include <kalburator/conflict/conflictrecord.h>
#include <kalburator/conflict/conflictpolicy.h>

using KCalendarCore::Alarm;
using KCalendarCore::Event;
using KCalendarCore::ICalFormat;
using KCalendarCore::MemoryCalendar;
using Kalburator::Conflict::ConflictDecision;
using Kalburator::Conflict::ConflictPolicy;
using Kalburator::Conflict::ConflictRecord;
using Kalburator::Conflict::ConflictType;
using Kalburator::Conflict::RecordSnapshot;
using WildPalms::CalendarPlugin::CalendarConflictHandler;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

namespace {

QByteArray serialiseEvent(const Event::Ptr &event)
{
    auto cal = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::utc()));
    cal->addEvent(event);
    return ICalFormat().toString(cal).toUtf8();
}

Event::Ptr baseEvent()
{
    Event::Ptr e(new Event);
    e->setUid(QStringLiteral("conflict-uid"));
    e->setSummary(QStringLiteral("Meeting"));
    e->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0)));
    e->setDtEnd  (QDateTime(QDate(2026, 5, 1), QTime(10, 0)));
    return e;
}

Event::Ptr withAlarm(int minutesBefore)
{
    auto e = baseEvent();
    auto alarm = e->newAlarm();
    alarm->setType(Alarm::Display);
    alarm->setStartOffset(KCalendarCore::Duration(-minutesBefore * 60));
    return e;
}

Event::Ptr withExdate(const QDate &exdate)
{
    auto e = baseEvent();
    auto rrule = e->recurrence();
    rrule->setDaily(1);
    rrule->setDuration(10);
    e->recurrence()->addExDateTime(QDateTime(exdate, QTime(9, 0)));
    return e;
}

ConflictRecord makeConflict(const QByteArray &sourceIcs,
                            const QByteArray &targetIcs)
{
    ConflictRecord cr;
    cr.conflictId = QStringLiteral("c1");
    cr.type = ConflictType::BothModified;
    cr.source.id = QStringLiteral("palm:DatebookDB:1");
    cr.target.id = QStringLiteral("palm:DatebookDB:1");
    cr.source.content = sourceIcs;
    cr.target.content = targetIcs;
    cr.source.contentType = QStringLiteral("text/calendar");
    cr.target.contentType = QStringLiteral("text/calendar");
    cr.source.lastModified = QDateTime::currentDateTimeUtc();
    cr.target.lastModified = QDateTime::currentDateTimeUtc();
    return cr;
}

} // namespace

class TestCalendarConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void alarmOnlyDiffMergesAlarms();
    void exdateOnlyDiffMergesExdates();
    void tzOnlyDiffPrefersFloating();
    void alarmPlusAttendeeDiffDelegates();
    void unrelatedDiffsDelegateToPalm();
    void undecodableContentDelegatesToPalm();
};

void TestCalendarConflictHandler::alarmOnlyDiffMergesAlarms()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = withAlarm(15);
    auto tgt = withAlarm(60);
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("alarm"));
    QVERIFY(!cr.mergedContent.isEmpty());
    // Both alarm offsets present in merged ICS.
    QVERIFY(cr.mergedContent.contains("PT15M") || cr.mergedContent.contains("-PT15M"));
    QVERIFY(cr.mergedContent.contains("PT1H")  || cr.mergedContent.contains("-PT1H"));
}

void TestCalendarConflictHandler::exdateOnlyDiffMergesExdates()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = withExdate(QDate(2026, 5, 5));
    auto tgt = withExdate(QDate(2026, 5, 7));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("exdate"));
    QVERIFY(cr.mergedContent.contains("20260505"));
    QVERIFY(cr.mergedContent.contains("20260507"));
}

void TestCalendarConflictHandler::tzOnlyDiffPrefersFloating()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = baseEvent();   // floating local time
    auto tgt = baseEvent();
    tgt->setDtStart(QDateTime(QDate(2026, 5, 1), QTime(9, 0),
                              QTimeZone("America/New_York")));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    auto decision = h.handleConflict(cr, policy);
    QCOMPARE(decision, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("tz"));
    // Merged content should reflect the floating side (no TZID).
    QVERIFY(!cr.mergedContent.contains("TZID="));
}

void TestCalendarConflictHandler::alarmPlusAttendeeDiffDelegates()
{
    // Pins the fix for the EventDiff field-coverage gap: when alarms
    // differ AND attendees differ, the handler must NOT misclassify
    // as "only alarms differ" and silently overwrite attendees.
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = withAlarm(15);
    auto tgt = withAlarm(60);
    KCalendarCore::Attendee alice(QStringLiteral("Alice"),
                                  QStringLiteral("alice@example.com"));
    tgt->addAttendee(alice);

    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));
    ConflictPolicy policy;
    h.handleConflict(cr, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestCalendarConflictHandler::unrelatedDiffsDelegateToPalm()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto src = baseEvent();
    auto tgt = baseEvent();
    tgt->setSummary(QStringLiteral("Meeting (rescheduled)"));
    auto cr = makeConflict(serialiseEvent(src), serialiseEvent(tgt));

    ConflictPolicy policy;
    h.handleConflict(cr, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestCalendarConflictHandler::undecodableContentDelegatesToPalm()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    CalendarConflictHandler h(&dev, &cfg);

    auto cr = makeConflict(QByteArray("not-ics"), QByteArray("also-not"));
    ConflictPolicy policy;
    h.handleConflict(cr, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

QTEST_MAIN(TestCalendarConflictHandler)
#include "tst_calendarconflicthandler.moc"
