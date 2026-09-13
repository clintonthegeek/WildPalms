#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include "../wildpalms_qtest_main.h"

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <kalburator/typesupport/backendconfiguration.h>

using Kalburator::Sync::BackendConfiguration;

class TstProviderEnableFanout : public QObject {
    Q_OBJECT
private slots:
    void setProviderEnabled_false_disablesAllMappings();
    void setProviderEnabled_true_reEnablesMappings();
    void providerEnabled_getter_reflectsState();
};

void TstProviderEnableFanout::setProviderEnabled_false_disablesAllMappings()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    BackendConfiguration bc;
    bc.id = QStringLiteral("p1");
    bc.type = QStringLiteral("caldav");
    bc.enabled = true;
    profile.saveAccount(bc);

    // Two mappings referencing p1, one not
    QJsonArray maps;
    maps.append(QJsonObject{{"id","m1"},{"sourceBackend","p1:cal-A"},
        {"targetBackend","palm:cal/0"},{"enabled",true}});
    maps.append(QJsonObject{{"id","m2"},{"sourceBackend","palm:cal/1"},
        {"targetBackend","p1:cal-B"},{"enabled",true}});
    maps.append(QJsonObject{{"id","m3"},{"sourceBackend","rawfiles:x"},
        {"targetBackend","palm:cal/2"},{"enabled",true}});
    profile.setSyncMappingsJson(maps);

    ac.setProviderEnabled(QStringLiteral("p1"), false);

    // Provider disabled
    QCOMPARE(ac.providerEnabled(QStringLiteral("p1")), false);

    // m1 and m2 disabled, m3 unchanged
    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.size(), 3);
    for (const auto &v : after) {
        const QJsonObject row = v.toObject();
        const QString id = row.value("id").toString();
        if (id == "m3") {
            QCOMPARE(row.value("enabled").toBool(), true);
        } else {
            QCOMPARE(row.value("enabled").toBool(), false);
        }
    }
}

void TstProviderEnableFanout::setProviderEnabled_true_reEnablesMappings()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    BackendConfiguration bc;
    bc.id = QStringLiteral("p1");
    bc.type = QStringLiteral("caldav");
    bc.enabled = false;
    profile.saveAccount(bc);

    QJsonArray maps;
    maps.append(QJsonObject{{"id","m"},{"sourceBackend","p1:x"},
        {"targetBackend","palm:c/0"},{"enabled",false}});
    profile.setSyncMappingsJson(maps);

    ac.setProviderEnabled(QStringLiteral("p1"), true);

    QCOMPARE(ac.providerEnabled(QStringLiteral("p1")), true);
    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.first().toObject().value("enabled").toBool(), true);
}

void TstProviderEnableFanout::providerEnabled_getter_reflectsState()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    BackendConfiguration bc;
    bc.id = QStringLiteral("p2");
    bc.type = QStringLiteral("caldav");
    bc.enabled = true;
    profile.saveAccount(bc);

    QCOMPARE(ac.providerEnabled(QStringLiteral("p2")), true);
    QCOMPARE(ac.providerEnabled(QStringLiteral("unknown")), true); // default

    ac.setProviderEnabled(QStringLiteral("p2"), false);
    QCOMPARE(ac.providerEnabled(QStringLiteral("p2")), false);
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProviderEnableFanout)
#include "tst_provider_enable_fanout.moc"
