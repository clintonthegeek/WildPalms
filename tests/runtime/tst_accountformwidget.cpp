#include <vector>
// tests/runtime/tst_accountformwidget.cpp
#include <QtTest/QtTest>
#include <QComboBox>
#include "../wildpalms_qtest_main.h"

#include "app/accounts/accountformwidget.h"
#include <kalburator/sync/backendregistry.h>
#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/sync/iprovider.h>
#include <kalburator/types/collectioninfo.h>
#include <QPromise>

using WildPalms::App::Accounts::AccountFormWidget;
using Kalburator::Sync::BackendRegistry;

namespace {

class StubProvider : public Kalburator::Sync::IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr)
        : Kalburator::Sync::IProvider(parent) {}
    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    void load(const Kalburator::Sync::BackendConfiguration &) override {}
    Kalburator::Sync::BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; p.start(); p.addResult(true); p.finish();
        return p.future();
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<Kalburator::Sync::CollectionInfo> collections() const override { return {}; }
    std::vector<Kalburator::Sync::ProviderBackendSpec> createBackends() override { return {}; }
};

class StubContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
    createProvider(QObject *parent = nullptr) const override {
        return std::make_unique<StubProvider>(parent);
    }
};

} // namespace

class TstAccountFormWidget : public QObject
{
    Q_OBJECT
private slots:
    void widgetExposesKindCombo();
    void emptyRegistryYieldsEmptySelectedKind();
    void emptyRegistryYieldsInvalidConfiguration();
    void setConfigurationOnEmptyRegistryIsSafeNoOp();
    void setConfigurationSelectsKindByType();
    void kindComboListsOnlyRegisteredContributions();
};

void TstAccountFormWidget::widgetExposesKindCombo()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY2(combo, "AccountFormWidget must own a QComboBox for kind selection");
}

void TstAccountFormWidget::emptyRegistryYieldsEmptySelectedKind()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    QCOMPARE(w.selectedKind(), QString());
}

void TstAccountFormWidget::emptyRegistryYieldsInvalidConfiguration()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    QVERIFY(!w.isValid());
    const auto cfg = w.configuration();
    QVERIFY(!cfg.isValid());
}

void TstAccountFormWidget::setConfigurationOnEmptyRegistryIsSafeNoOp()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    Kalburator::Sync::BackendConfiguration cfg;
    cfg.type = QStringLiteral("caldav");
    w.setConfiguration(cfg);          // must not crash
    QCOMPARE(w.selectedKind(), QString());
}

void TstAccountFormWidget::setConfigurationSelectsKindByType()
{
    BackendRegistry reg;
    // Stub contribution registered under type "stub".
    reg.registerContribution(std::make_shared<StubContribution>());
    AccountFormWidget w(&reg);
    Kalburator::Sync::BackendConfiguration cfg;
    cfg.type = QStringLiteral("stub");
    cfg.displayName = QStringLiteral("Edited");
    w.setConfiguration(cfg);
    QCOMPARE(w.selectedKind(), QStringLiteral("stub"));
}

void TstAccountFormWidget::kindComboListsOnlyRegisteredContributions()
{
    // Spec §8(a): offered kinds are exactly the registered contributions —
    // a build without Akonadi never offers Akonadi.
    BackendRegistry reg;
    reg.registerContribution(std::make_shared<StubContribution>());
    AccountFormWidget w(&reg);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 1);
    QCOMPARE(combo->itemData(0).toString(), QStringLiteral("stub"));
}

WILDPALMS_QTEST_MAIN(TstAccountFormWidget)
#include "tst_accountformwidget.moc"
