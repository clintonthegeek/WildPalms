#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include "runtime/palmruntime.h"
#include <kalburator/types/synctypes.h>

class TstPalmRuntimeReloadMappings : public QObject
{
    Q_OBJECT
private slots:
    void replaces_mapping_list();
    void empty_array_clears_mappings();
};

static QJsonObject mapping(const QString &id,
                           const QString &source,
                           const QString &target)
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    o[QStringLiteral("sourceCalendar")] = source;
    o[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-cal");
    o[QStringLiteral("targetCalendar")] = target;
    o[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    o[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    o[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    o[QStringLiteral("enabled")] = true;
    return o;
}

void TstPalmRuntimeReloadMappings::replaces_mapping_list()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonArray arr;
    arr.append(mapping(QStringLiteral("user-1"), QStringLiteral("cal-A"), QStringLiteral("cal-A-out")));
    arr.append(mapping(QStringLiteral("user-2"), QStringLiteral("cal-B"), QStringLiteral("cal-B-out")));

    rt.reloadMappings(arr);

    auto m = rt.palmMappings();
    QCOMPARE(m.size(), 2);
    QCOMPARE(m[0].id, QStringLiteral("user-1"));
    QCOMPARE(m[1].id, QStringLiteral("user-2"));
}

void TstPalmRuntimeReloadMappings::empty_array_clears_mappings()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonArray arr;
    arr.append(mapping(QStringLiteral("user-1"), QStringLiteral("cal-A"), QStringLiteral("cal-A-out")));
    rt.reloadMappings(arr);
    QCOMPARE(rt.palmMappings().size(), 1);

    rt.reloadMappings(QJsonArray{});
    QCOMPARE(rt.palmMappings().size(), 0);
}

QTEST_MAIN(TstPalmRuntimeReloadMappings)
#include "tst_palm_runtime_reload_mappings.moc"
