// Substrate A3: names-first route translation + per-route status.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "runtime/routemapping.h"
#include "plugins/pimplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include <kalburator/types/synctypes.h>
#include <kalburator/sync/syncbackendbase.h>   // complete type: StubConduit returns unique_ptr<SyncBackendBase>

using namespace WildPalms::Runtime;
using Kalburator::Sync::SyncMapping;

namespace {
class StubConduit : public WildPalms::Plugins::PimPlugin {
public:
    StubConduit(QString id, QString domain, QString db)
        : m_id(std::move(id)), m_domain(std::move(domain)), m_db(std::move(db)) {}
    QString conduitId() const override { return m_id; }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{m_domain}; }
    QString primaryDbName() const override { return m_db; }
    QString conduitDisplayName() const override { return m_id; }
    WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const override
    { return &m_store; }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
    mutable WildPalms::PalmCalendar::CategoryMappingStore m_store;
private:
    QString m_id, m_domain, m_db;
};

SyncMapping row(const QString &src, const QString &srcCal)
{
    SyncMapping m;
    m.id = QStringLiteral("m1");
    m.sourceBackend = src;
    m.sourceCalendar = srcCal;
    m.targetBackend = QStringLiteral("acc:col");
    m.targetCalendar = QStringLiteral("col");
    m.enabled = true;
    return m;
}
} // namespace

class TstRouteMapping : public QObject {
    Q_OBJECT
private slots:
    void directRouteIsActive();
    void namedRouteResolvesAgainstStore();
    void namedRouteWaitsBeforeDevice();
    void namedRouteReportsNoFreeSlot();
    void unknownConduitIsNotARoute();
    void disabledRowIsNotARoute();
};

void TstRouteMapping::directRouteIsActive()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(row(QStringLiteral("calendar"), QString()),
                                      { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->kind, RouteSpec::Kind::Direct);
    QCOMPARE(t.spec->domain, QStringLiteral("calendar"));
}

void TstRouteMapping::namedRouteResolvesAgainstStore()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    cal.m_store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->kind, RouteSpec::Kind::Filtered);
    QCOMPARE(t.spec->categoryName, QStringLiteral("Work"));
}

void TstRouteMapping::namedRouteWaitsBeforeDevice()
{
    // Store has never been populated (no slot names at all): the route still
    // produces a spec (hub<->remote filtering works by name), with status
    // WaitingForDevice instead of today's silent drop.
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::WaitingForDevice);
    QCOMPARE(t.spec->categoryName, QStringLiteral("Work"));
}

void TstRouteMapping::namedRouteReportsNoFreeSlot()
{
    // Store populated (device seen) but the name is absent: the reconciler
    // could not place it (table full) — surface NoFreeSlot, still produce
    // the spec so hub<->remote continues to flow.
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    for (int i = 1; i <= 15; ++i)
        cal.m_store.setSlotName(QStringLiteral("DatebookDB"), i,
                                QStringLiteral("Cat%1").arg(i));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::NoFreeSlot);
}

void TstRouteMapping::unknownConduitIsNotARoute()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(row(QStringLiteral("plucker"), QString()),
                                      { &cal });
    QVERIFY(!t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::NotARoute);
}

void TstRouteMapping::disabledRowIsNotARoute()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    auto m = row(QStringLiteral("calendar"), QString());
    m.enabled = false;
    const auto t = translateRouteSpec(m, { &cal });
    QVERIFY(!t.spec.has_value());
}

WILDPALMS_QTEST_MAIN(TstRouteMapping)
#include "tst_routemapping.moc"
