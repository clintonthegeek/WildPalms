#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include "../wildpalms_qtest_main.h"

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <kalburator/typesupport/backendconfiguration.h>

class TstMappingEnablePersists : public QObject {
    Q_OBJECT
private slots:
    void setMappingEnabled_false_persists();
    void setMappingEnabled_true_persists();
    void setMappingEnabled_unknownId_noOp();
    void setMappingEnabled_emitsSignal();
};

void TstMappingEnablePersists::setMappingEnabled_false_persists()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    // Seed one mapping
    QJsonArray maps;
    maps.append(QJsonObject{{"id","m1"},{"sourceBackend","palm:c/0"},
        {"targetBackend","rawfiles:x"},{"enabled",true}});
    profile.setSyncMappingsJson(maps);

    ac.setMappingEnabled(QStringLiteral("m1"), false);

    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().toObject().value("enabled").toBool(), false);
}

void TstMappingEnablePersists::setMappingEnabled_true_persists()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QJsonArray maps;
    maps.append(QJsonObject{{"id","m1"},{"sourceBackend","palm:c/0"},
        {"targetBackend","rawfiles:x"},{"enabled",false}});
    profile.setSyncMappingsJson(maps);

    ac.setMappingEnabled(QStringLiteral("m1"), true);

    const QJsonArray after = profile.syncMappingsJson();
    QCOMPARE(after.first().toObject().value("enabled").toBool(), true);
}

void TstMappingEnablePersists::setMappingEnabled_unknownId_noOp()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QJsonArray maps;
    maps.append(QJsonObject{{"id","m1"},{"enabled",true}});
    profile.setSyncMappingsJson(maps);

    ac.setMappingEnabled(QStringLiteral("does-not-exist"), false);

    // Original mapping unaffected
    QCOMPARE(profile.syncMappingsJson().first().toObject()
             .value("enabled").toBool(), true);
}

void TstMappingEnablePersists::setMappingEnabled_emitsSignal()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    QJsonArray maps;
    maps.append(QJsonObject{{"id","m1"},{"enabled",true}});
    profile.setSyncMappingsJson(maps);

    QSignalSpy spy(&ac,
        &WildPalms::Runtime::AccountController::mappingEnabledChanged);
    ac.setMappingEnabled(QStringLiteral("m1"), false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("m1"));
    QCOMPARE(spy.first().at(1).toBool(), false);
}

WILDPALMS_QTEST_GUILESS_MAIN(TstMappingEnablePersists)
#include "tst_mapping_enable_persists.moc"
