#include <QtTest/QtTest>

#include "plugins/calendar/calendarbackendplugin.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "runtime/palmdeviceaccess.h"

// Complete-type includes for the libkalburator pointers returned by the
// plugin (delete on a forward-decl is UB; SyncBackend::backendId is
// virtual but needs the full type for the call and unique_ptr destructor).
#include <kalburator/calendar/syncbackend.h>      // Kalburator::Sync::SyncBackend (calendar-typed)
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/conflict/conflictpolicy.h>   // brings in Kalburator::Conflict::ConflictHandler

using WildPalms::CalendarPlugin::CalendarBackendPlugin;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::Runtime::PalmDeviceAccess;

class TestCalendarBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void identityFields();
    void claimsDatebookDB();
    void hasMainView();
    void createPalmBackendReturnsCalendarBackend();
    void createConflictHandlerNonNullAfterCreatePalmBackend();
};

void TestCalendarBackendPlugin::identityFields()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("calendar"));
    QVERIFY(!p.displayName().isEmpty());
    QVERIFY(!p.description().isEmpty());
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestCalendarBackendPlugin::claimsDatebookDB()
{
    CalendarBackendPlugin p;
    QCOMPARE(p.claimedDatabases(), QStringList{QStringLiteral("DatebookDB")});
}

void TestCalendarBackendPlugin::hasMainView()
{
    CalendarBackendPlugin p;
    QVERIFY(p.hasMainView());
    QVERIFY(!p.mainViewName().isEmpty());
}

void TestCalendarBackendPlugin::createPalmBackendReturnsCalendarBackend()
{
    CalendarBackendPlugin p;
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    auto backend = p.createPalmBackend(&dev);
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendId(), QStringLiteral("calendar"));
}

void TestCalendarBackendPlugin::createConflictHandlerNonNullAfterCreatePalmBackend()
{
    CalendarBackendPlugin p;
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    auto backend = p.createPalmBackend(&dev);
    Q_UNUSED(backend)

    auto *h = p.createConflictHandler();
    QVERIFY(h != nullptr);
    delete h;
}

QTEST_MAIN(TestCalendarBackendPlugin)
#include "tst_calendarbackendplugin.moc"
