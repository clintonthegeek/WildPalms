// Substrate A1 acceptance: a fifth conduit participates everywhere a stock
// conduit does, with ZERO WildPalms source changes beyond registration.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "../wildpalms_qtest_main.h"

#include "runtime/palmruntime.h"
#include "runtime/routemapping.h"
#include "plugins/pimplugin.h"
#include <kalburator/types/synctypes.h>
#include <kalburator/sync/syncbackendbase.h>   // complete type: createPalmBackend returns unique_ptr<SyncBackendBase>

using namespace WildPalms::Runtime;

namespace {
class FakeDocumentConduit : public WildPalms::Plugins::PimPlugin {
public:
    QString conduitId() const override { return QStringLiteral("document"); }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{QStringLiteral("document")}; }
    QString primaryDbName() const override { return QStringLiteral("DocumentDB"); }
    QString conduitDisplayName() const override { return QStringLiteral("Documents"); }
    bool supportsCategories() const override { return false; }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
};
} // namespace

class TstFifthConduit : public QObject {
    Q_OBJECT
private slots:
    void fifthConduitIsEnumerated();
    void routesTranslateForFifthConduit();
};

void TstFifthConduit::fifthConduitIsEnumerated()
{
    QTemporaryDir dir;
    PalmRuntime rt(dir.path() + "/state");
    const int stockCount = rt.conduits().size();
    rt.appendConduitForTest(std::make_unique<FakeDocumentConduit>());
    QCOMPARE(rt.conduits().size(), stockCount + 1);
    QVERIFY(rt.isPalmConduitBackendId(QStringLiteral("document")));
}

void TstFifthConduit::routesTranslateForFifthConduit()
{
    FakeDocumentConduit doc;
    Kalburator::Sync::SyncMapping m;
    m.id = QStringLiteral("doc-route");
    m.sourceBackend  = QStringLiteral("document");
    m.sourceCalendar = QString();                 // direct (no categories)
    m.targetBackend  = QStringLiteral("acc:col");
    m.targetCalendar = QStringLiteral("col");
    m.enabled = true;
    const auto t = translateRouteSpec(m, { &doc });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->domain, QStringLiteral("document"));
    QCOMPARE(t.spec->hubCollectionId, QStringLiteral("document"));
}

WILDPALMS_QTEST_MAIN(TstFifthConduit)
#include "tst_fifth_conduit.moc"
