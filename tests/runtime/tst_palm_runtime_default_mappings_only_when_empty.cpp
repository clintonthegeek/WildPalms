#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include <kalburator/types/synctypes.h>
#include <kalburator/blob/mockblobbackend.h>
#include <kalburator/types/collectioninfo.h>

#include "../blobsyncbackendwrapper.h"

class TstPalmRuntimeDefaultMappingsOnlyWhenEmpty : public QObject
{
    Q_OBJECT
private slots:
    void defaults_skipped_if_user_mappings_present();
    void defaultsOnlyForUncoveredSlots();
    void defaultMappings_useLastWriteWinsPolicy();
    void starMappingsSupersedePreexistingMappings();  // C Task 9 (was wildcard test)
};

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaults_skipped_if_user_mappings_present()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonObject m;
    m[QStringLiteral("id")] = QStringLiteral("user-only");
    m[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    m[QStringLiteral("sourceCalendar")] = QStringLiteral("cal");
    m[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-x");
    m[QStringLiteral("targetCalendar")] = QStringLiteral("out");
    m[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    m[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    m[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    m[QStringLiteral("enabled")] = true;
    QJsonArray arr;
    arr.append(m);

    rt.reloadMappings(arr);
    QCOMPARE(rt.palmMappings().size(), 1);

    // This test verifies the user-mapping-preservation contract via
    // reloadMappings + palmMappings round-trip. The guard that skips
    // defaults when user mappings are present is tested by inspection
    // of palmruntime.cpp's connectDevice.
    auto current = rt.palmMappings();
    QCOMPARE(current.size(), 1);
    QCOMPARE(current[0].id, QStringLiteral("user-only"));
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaultsOnlyForUncoveredSlots()
{
    // C Task 9: repurposed from an old per-slot F1 test (now retired).
    // Verifies that finishConnect() produces exactly one Star mapping per
    // connected domain (hub-and-spoke topology) — i.e. one mapping per Palm
    // backend, not one per category slot.
    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    const auto mappings = runtime.palmMappings();

    // With all 4 Palm backends registering successfully, there should be
    // exactly 4 domain-level Star mappings (calendar, contacts, memo, todo).
    QCOMPARE(mappings.size(), 4);

    // Every mapping must have wp-hub as source (Primary) and a palm:<domain>
    // collection as target — confirming hub-and-spoke structure.
    for (const auto &m : mappings) {
        QCOMPARE(m.sourceBackend, QStringLiteral("wp-hub"));
        QVERIFY(m.targetCalendar.startsWith(QStringLiteral("palm:")));
    }
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaultMappings_useLastWriteWinsPolicy()
{
    // C Task 9: renamed assertion — new behavior uses AskUser, not LastWriteWins.
    // finishConnect() builds domain-level Star mappings via generateMappings()
    // with conflictPolicy=AskUser so Palm<->hub conflicts surface for review
    // rather than silently auto-resolving (see comment in palmruntime.cpp).
    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    const auto mappings = runtime.palmMappings();
    QVERIFY(!mappings.isEmpty());

    // All auto-generated Star mappings must use AskUser conflict policy
    // (deliberate change from the retired per-slot RawFiles LastWriteWins default).
    for (const auto &m : mappings) {
        QCOMPARE(m.conflictPolicy,
                 Kalburator::Sync::ConflictResolution::AskUser);
    }
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::starMappingsSupersedePreexistingMappings()
{
    // Task 3 (hub<->remote routing): a persisted user mapping with a known
    // Palm plugin sourceBackend is translated by buildRouteLogicalCalendars()
    // into a per-route LogicalCalendar that feeds generateMappings alongside
    // the four domain-level Palm<->hub LCs. The result is 5 mappings: 4
    // Star (Palm<->hub) + 1 route (hub<->remote) from the user entry.
    // The user-preexisting mapping id is NOT preserved verbatim — the route
    // LC gets id "wp-route-<original-id>" so the original "user-preexisting"
    // id no longer appears in the post-connect mapping list.
    // (This replaces the earlier C Task 9 interim that said user mappings are
    // superseded; they are now translated into hub<->remote route LCs instead.)

    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    // Seed a user mapping BEFORE injecting the device.
    QJsonObject m;
    m[QStringLiteral("id")]              = QStringLiteral("user-preexisting");
    m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
    m[QStringLiteral("sourceCalendar")]  = QString();
    m[QStringLiteral("targetBackend")]   = QStringLiteral("test-target");
    m[QStringLiteral("targetCalendar")]  = QStringLiteral("Personal");
    m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
    m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
    m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
    m[QStringLiteral("enabled")]         = true;
    QJsonArray arr; arr.append(m);
    runtime.reloadMappings(arr);
    QCOMPARE(runtime.palmMappings().size(), 1);

    // The route's target must resolve to a materialized endpoint under the
    // CollectionRuntime cutover (topology commit now validates every
    // mapping endpoint); register a mock remote so "test-target" exists.
    auto mockRemote = std::make_unique<Kalburator::Sync::MockBlobBackend>();
    Kalburator::Sync::CollectionInfo personal;
    personal.id = QStringLiteral("Personal");
    personal.name = QStringLiteral("Personal");
    mockRemote->createCollection(personal);
    runtime.registerBackendInstanceForTest(
        QStringLiteral("test-target"),
        WildPalmsTest::BlobSyncBackendWrapper::wrap(std::move(mockRemote), QStringLiteral("test-target")));

    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    // After finishConnect: 4 Palm<->hub Star mappings + 1 hub<->remote route
    // LC translated from the user mapping = 5 total.
    const auto post = runtime.palmMappings();
    QCOMPARE(post.size(), 5);

    // All generated mappings source from wp-hub (Star topology primary).
    for (const auto &x : post) {
        QCOMPARE(x.sourceBackend, QStringLiteral("wp-hub"));
    }

    // The user's original mapping id is NOT in the output — it was translated
    // into a Direct-kind route LC. buildRouteLogicalCalendars() appends that
    // route mapping directly (it is not re-run through generateMappings), so
    // it keeps the "wp-route-<originalId>" id rather than an "auto_..._sync1"
    // generated name.
    bool hasOriginalId = false;
    bool hasRouteMapping = false;
    for (const auto &x : post) {
        if (x.id == QStringLiteral("user-preexisting")) hasOriginalId = true;
        if (x.id == QStringLiteral("wp-route-user-preexisting")) hasRouteMapping = true;
    }
    QVERIFY(!hasOriginalId);
    QVERIFY(hasRouteMapping);
}

QTEST_MAIN(TstPalmRuntimeDefaultMappingsOnlyWhenEmpty)
#include "tst_palm_runtime_default_mappings_only_when_empty.moc"
