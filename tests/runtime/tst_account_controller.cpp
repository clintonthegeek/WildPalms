#include <vector>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

#include "../wildpalms_qtest_main.h"

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <kalburator/sync/iprovider.h>
#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/sync/providermanager.h>

/// Minimal provider whose disconnect() emits connectionStateChanged(false)
/// while connected — same contract as CalDavProvider::disconnect(). Used to
/// reproduce the teardown-order crash: ~ProviderManager() calls
/// disconnectAll() during AccountController member destruction, after
/// m_states is already gone.
class TeardownEmittingProvider : public Kalburator::Sync::IProvider {
    Q_OBJECT
public:
    QString id() const override { return QStringLiteral("teardown-fake"); }
    QString kind() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Teardown Fake"); }
    void load(const Kalburator::Sync::BackendConfiguration &) override {}
    Kalburator::Sync::BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        m_connected = true;
        emit connectionStateChanged(true);
        return QtFuture::makeReadyValueFuture(true);
    }
    void disconnect() override {
        if (!m_connected) return;
        m_connected = false;
        emit connectionStateChanged(false);
    }
    bool isConnected() const override { return m_connected; }
    QList<Kalburator::Sync::CollectionInfo> collections() const override { return {}; }
    std::vector<Kalburator::Sync::ProviderBackendSpec> createBackends() override
    { return {}; }
private:
    bool m_connected = false;
};

class TstAccountController : public QObject {
    Q_OBJECT
private slots:
    void constructs_and_destructs_cleanly();
    void loadFromProfile_reads_existing_accounts();
    void addProvider_returns_uuid_and_persists_to_profile();
    void addProvider_refused_for_unsupported_kind();
    void removeProvider_drops_from_list_and_profile();
    void loadFromProfile_handlesUnreachableServer();
    void removeProvider_cascadesMappings();
    void mappingDescriptionsFor_returns_first_N();
    void ac_lifetime_matches_profile_switch();
    void appendMappings_writes_and_persists();
    void destruction_does_not_deliver_provider_signals();
};

void TstAccountController::constructs_and_destructs_cleanly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Profile profile(dir.path());
    profile.initialize();

    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    using AC = WildPalms::Runtime::AccountController;
    AC ac(dir.path(),
          &rt.backendRegistry(),
          &profile,
          &rt);

    QCOMPARE(ac.providers().size(), 0);
    QCOMPARE(ac.mappingCountFor("nonexistent"), 0);
}

void TstAccountController::loadFromProfile_reads_existing_accounts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Seed Profile with one CardDAV account before constructing AC.
    {
        Profile p(dir.path());
        p.initialize();
        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = QStringLiteral("test-uuid-1");
        cfg.type = QStringLiteral("multiproto-dav");
        cfg.displayName = QStringLiteral("Personal DAV");
        cfg.connectionParams[QStringLiteral("url")] =
            QStringLiteral("https://nonresolvable.example/");
        cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");
        p.saveAccount(cfg);
        QVERIFY(p.save());
    }

    Profile profile(dir.path());
    QVERIFY(profile.load());
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(ac.providers().first()->id(),
             QStringLiteral("test-uuid-1"));
    QCOMPARE(ac.providers().first()->kind(),
             QStringLiteral("multiproto-dav"));
    QCOMPARE(ac.providers().first()->displayName(),
             QStringLiteral("Personal DAV"));
}

void TstAccountController::addProvider_returns_uuid_and_persists_to_profile()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.id = QStringLiteral("manual-uuid");
    cfg.displayName = QStringLiteral("TestServer");
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://nonresolvable.example/");
    cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");

    QString uuid = ac.addProvider(QStringLiteral("multiproto-dav"), cfg);
    QVERIFY(!uuid.isEmpty());
    QCOMPARE(uuid, QStringLiteral("manual-uuid"));
    QCOMPARE(ac.providers().size(), 1);

    // Verify account was persisted to Profile, not a sidecar.
    const auto accts = profile.accounts();
    QCOMPARE(accts.size(), 1);
    QCOMPARE(accts.first().id, QStringLiteral("manual-uuid"));
    QCOMPARE(accts.first().type, QStringLiteral("multiproto-dav"));
}

void TstAccountController::addProvider_refused_for_unsupported_kind()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    QString uuid = ac.addProvider(QStringLiteral("unsupported-kind"), cfg);
    QVERIFY(uuid.isEmpty());
    QCOMPARE(ac.providers().size(), 0);
}

void TstAccountController::removeProvider_drops_from_list_and_profile()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    QString uuid = ac.addProvider(QStringLiteral("multiproto-dav"), cfg);
    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(profile.accounts().size(), 1);

    QVERIFY(ac.removeProvider(uuid));
    QCOMPARE(ac.providers().size(), 0);

    // Account must be gone from Profile, not just from the in-memory list.
    QCOMPARE(profile.accounts().size(), 0);
}

void TstAccountController::loadFromProfile_handlesUnreachableServer()
{
    QTemporaryDir dir;

    // Seed Profile with an unreachable CardDAV account.
    {
        Profile p(dir.path());
        p.initialize();
        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = QStringLiteral("dead-uuid");
        cfg.type = QStringLiteral("multiproto-dav");
        cfg.connectionParams[QStringLiteral("url")] =
            QStringLiteral("https://this-server-does-not-resolve.invalid/");
        cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("x");
        p.saveAccount(cfg);
        QVERIFY(p.save());
    }

    Profile profile(dir.path());
    QVERIFY(profile.load());
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QCOMPARE(ac.providers().size(), 1);
    QCOMPARE(ac.providers().first()->id(),
             QStringLiteral("dead-uuid"));
}

void TstAccountController::removeProvider_cascadesMappings()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    const QString uuid = ac.addProvider(QStringLiteral("multiproto-dav"), cfg);

    // Seed three mappings: 2 reference the provider, 1 doesn't.
    QJsonArray maps;
    maps.append(QJsonObject{
        {"id", "m1"}, {"sourceBackend", "palm:contact/0"},
        {"targetBackend", uuid + ":addrbook-1"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    maps.append(QJsonObject{
        {"id", "m2"}, {"sourceBackend", uuid + ":addrbook-2"},
        {"targetBackend", "palm:contact/1"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    maps.append(QJsonObject{
        {"id", "m3"}, {"sourceBackend", "palm:calendar/0"},
        {"targetBackend", "rawfiles-cal"},
        {"sourceCalendar", ""}, {"targetCalendar", ""}});
    profile.setSyncMappingsJson(maps);
    profile.save();

    QCOMPARE(ac.mappingCountFor(uuid), 2);

    QSignalSpy spy(&ac, &WildPalms::Runtime::AccountController::mappingsChanged);
    QVERIFY(ac.removeProvider(uuid));
    QCOMPARE(spy.count(), 1);

    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().toObject().value("id").toString(),
             QStringLiteral("m3"));
}

void TstAccountController::mappingDescriptionsFor_returns_first_N()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://x/");
    const QString uuid = ac.addProvider(QStringLiteral("multiproto-dav"), cfg);

    QJsonArray maps;
    for (int i = 0; i < 5; ++i) {
        maps.append(QJsonObject{
            {"id", QString("m%1").arg(i)},
            {"sourceBackend", "palm:contact/0"},
            {"targetBackend", uuid + QString(":book-%1").arg(i)},
            {"sourceCalendar", ""}, {"targetCalendar", ""}});
    }
    profile.setSyncMappingsJson(maps);

    auto descs = ac.mappingDescriptionsFor(uuid, 3);
    QCOMPARE(descs.size(), 3);
    QVERIFY(descs.first().startsWith("palm:contact/0"));
}

void TstAccountController::ac_lifetime_matches_profile_switch()
{
    // Simulates the loadProfile teardown order: AC first (releases borrowed
    // registry), then PalmRuntime, then Profile. Inverted order would
    // crash (registry destroyed while AC's ProviderManager still holds it).
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    auto rt = std::make_unique<WildPalms::Runtime::PalmRuntime>(
        dir.path() + "/state");

    auto ac = std::make_unique<WildPalms::Runtime::AccountController>(
        dir.path(),
        &rt->backendRegistry(),
        &profile,
        rt.get());

    ac.reset();
    rt.reset();
    QVERIFY(true);  // Reaching here means no use-after-free.
}

void TstAccountController::appendMappings_writes_and_persists()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QJsonArray rows;
    rows.append(QJsonObject{
        {"id", "new-1"},
        {"sourceBackend", "palm:contact/0"},
        {"targetBackend", "fake-uuid:abc"},
        {"sourceCalendar", ""}, {"targetCalendar", "abc"},
        {"mode", "TwoWay"}, {"conflictPolicy", "AskUser"},
        {"enabled", true}});

    QSignalSpy spy(&ac, &WildPalms::Runtime::AccountController::mappingsChanged);
    ac.appendMappings(rows);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(profile.syncMappingsJson().size(), 1);
}

void TstAccountController::destruction_does_not_deliver_provider_signals()
{
    // Regression: destroying an AccountController holding a CONNECTED provider
    // segfaulted. ~ProviderManager() (a member) calls disconnectAll(), the
    // provider emits connectionStateChanged(false), ProviderManager re-emits
    // providerStateChanged, and the constructor lambda wrote into m_states —
    // which, declared after m_providerManager, was already destroyed.
    // Observable symptom short of the crash: connectStateChanged fires from a
    // half-destroyed AccountController. It must not.
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    auto ac = std::make_unique<WildPalms::Runtime::AccountController>(
        dir.path(), &rt.backendRegistry(), &profile, &rt);

    auto fake = std::make_unique<TeardownEmittingProvider>();
    auto *raw = fake.get();
    ac->providerManager()->addProvider(std::move(fake));
    raw->connect();  // now connected; teardown will emit

    QSignalSpy spy(ac.get(), &WildPalms::Runtime::AccountController::connectStateChanged);
    ac.reset();
    QCOMPARE(spy.count(), 0);
}

WILDPALMS_QTEST_MAIN(TstAccountController)
#include "tst_account_controller.moc"
