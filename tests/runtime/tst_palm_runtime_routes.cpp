#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "plugins/calendar/calendarbackendplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include <kalburator/sync/backendregistry.h>
#include <kalburator/types/synctypes.h>

class TstPalmRuntimeRoutes : public QObject { Q_OBJECT
private slots:
    void filteredRoute_yieldsRouteMappingInAdditionToPerDomain()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        // Seed a persisted slot-mapped user mapping.
        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u1");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/name:Work");
        m[QStringLiteral("targetBackend")]   = QStringLiteral("caldav-uuid");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("WorkCal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        // Populate the calendar plugin's CategoryMappingStore with slot 3 = "Work".
        // (In normal operation AppInfo parsing does this at createPalmBackend time;
        // here we set it directly via the plugin's accessor.)
        using WildPalms::CalendarPlugin::CalendarBackendPlugin;
        CalendarBackendPlugin *cal = nullptr;
        for (const auto &p : runtime.palmPlugins())
            if (auto *c = dynamic_cast<CalendarBackendPlugin*>(p.get())) cal = c;
        QVERIFY(cal);
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));

        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        const auto mappings = runtime.palmMappings();
        // 4 per-domain (Palm<->hub) + 1 per-route (view<->WorkCal) = 5.
        QCOMPARE(mappings.size(), 5);

        // Find the route mapping.
        int routeIdx = -1;
        for (int i = 0; i < mappings.size(); ++i)
            if (mappings[i].id.contains(QStringLiteral("wp-route-u1"))) routeIdx = i;
        QVERIFY(routeIdx >= 0);

        const auto &r = mappings[routeIdx];
        QCOMPARE(r.sourceBackend,  QStringLiteral("wp-route-u1"));
        QCOMPARE(r.sourceCalendar, QStringLiteral("route-Work"));
        QCOMPARE(r.targetBackend,  QStringLiteral("caldav-uuid"));
        QCOMPARE(r.targetCalendar, QStringLiteral("WorkCal"));

        // The wp-route-u1 backend must be registered in the runtime registry.
        QVERIFY(runtime.backendRegistry().backendInstance(
                    QStringLiteral("wp-route-u1")) != nullptr);
    }

    void wildcardRoute_bindsHubDirectly()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u2");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QString();   // wildcard
        m[QStringLiteral("targetBackend")]   = QStringLiteral("caldav-uuid");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("Personal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        const auto mappings = runtime.palmMappings();
        // 4 per-domain + 1 wildcard route = 5.
        QCOMPARE(mappings.size(), 5);

        int routeIdx = -1;
        for (int i = 0; i < mappings.size(); ++i)
            if (mappings[i].id.contains(QStringLiteral("wp-route-u2"))) routeIdx = i;
        QVERIFY(routeIdx >= 0);
        const auto &r = mappings[routeIdx];
        // Direct: Primary is wp-hub itself, not a wp-route-* wrapper.
        QCOMPARE(r.sourceBackend,  QStringLiteral("wp-hub"));
        QCOMPARE(r.sourceCalendar, QStringLiteral("calendar"));
        QCOMPARE(r.targetBackend,  QStringLiteral("caldav-uuid"));
        QCOMPARE(r.targetCalendar, QStringLiteral("Personal"));

        // No wp-route-u2 backend gets registered for a direct route.
        QVERIFY(runtime.backendRegistry().backendInstance(
                    QStringLiteral("wp-route-u2")) == nullptr);
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeRoutes)
#include "tst_palm_runtime_routes.moc"
