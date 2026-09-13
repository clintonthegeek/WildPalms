#include <vector>
// tests/runtime/tst_accountssetuppage.cpp
#include <QtTest/QtTest>
#include <QLabel>
#include <QPromise>
#include <QPushButton>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/accountssetuppage.h"
#include "app/wizard/wizardstate.h"

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/sync/iprovider.h>
#include <kalburator/types/collectioninfo.h>

using WildPalms::Wizard::AccountsSetupPage;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::TargetKind;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;

namespace {

class StubProvider : public IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr) : IProvider(parent) {}
    void setConnectResult(bool ok) { m_connectResult = ok; }
    void setCollections(const QList<CollectionInfo> &c) { m_collections = c; }

    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; p.start();
        p.addResult(m_connectResult); p.finish();
        m_connected = m_connectResult;
        return p.future();
    }
    void disconnect() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return m_collections; }
    std::vector<Kalburator::Sync::ProviderBackendSpec> createBackends() override { return {}; }

private:
    bool m_connectResult = true;
    bool m_connected = false;
    QList<CollectionInfo> m_collections;
};

class StubContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent = nullptr) const override {
        auto p = std::make_unique<StubProvider>(parent);
        p->setConnectResult(s_nextConnectResult);
        p->setCollections(s_nextCollections);
        return p;
    }
    static bool                  s_nextConnectResult;
    static QList<CollectionInfo> s_nextCollections;
};
bool                  StubContribution::s_nextConnectResult = true;
QList<CollectionInfo> StubContribution::s_nextCollections;

WizardState seedState() {
    WizardState s;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m;
        m.pluginId = pid;
        s.mappings.append(m);
    }
    return s;
}

BackendConfiguration stubConfig(const QString &name) {
    BackendConfiguration cfg;
    cfg.type        = QStringLiteral("stub");
    cfg.displayName = name;
    return cfg;
}

} // namespace

class TstAccountsSetupPage : public QObject {
    Q_OBJECT
private slots:
    void init();
    void addConnectsAndStoresCollections();
    void failedConnectKeepsAccountAndDoesNotBlock();
    void removeResetsMappingsReferencingAccount();
    void editClearsChosenCollectionsButKeepsAccountRef();

private:
    BackendRegistry m_registry;
};

void TstAccountsSetupPage::init()
{
    m_registry.unregisterContribution(QStringLiteral("stub"));
    m_registry.registerContribution(std::make_shared<StubContribution>());
    StubContribution::s_nextConnectResult = true;
    StubContribution::s_nextCollections.clear();
}

void TstAccountsSetupPage::addConnectsAndStoresCollections()
{
    CollectionInfo ci;
    ci.id = QStringLiteral("personal"); ci.name = QStringLiteral("Personal");
    ci.type = QStringLiteral("calendar");
    StubContribution::s_nextCollections = { ci };

    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();

    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("My Stub")));
    QVERIFY(!id.isEmpty());
    QTest::qWait(50);   // let the QFutureWatcher resolve

    QCOMPARE(s.accounts.size(), 1);
    QCOMPARE(s.accounts.first().id, id);
    QCOMPARE(s.accounts.first().config.id, id);   // on-disk id == wizard id
    QVERIFY(s.accounts.first().connected);
    QCOMPARE(s.accounts.first().collections.size(), 1);
    QVERIFY(page.isComplete());
}

void TstAccountsSetupPage::failedConnectKeepsAccountAndDoesNotBlock()
{
    StubContribution::s_nextConnectResult = false;

    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    page.addAccountFromConfig(QStringLiteral("stub"),
                              stubConfig(QStringLiteral("Broken")));
    QTest::qWait(50);

    QCOMPARE(s.accounts.size(), 1);
    QVERIFY(!s.accounts.first().connected);
    QVERIFY(page.isComplete());   // failures never block Next (spec §7)
}

void TstAccountsSetupPage::removeResetsMappingsReferencingAccount()
{
    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("Doomed")));
    QTest::qWait(50);

    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = id;
    s.mappings[0].collectionId = QStringLiteral("personal");

    page.removeAccount(id);
    QCOMPARE(s.accounts.size(), 0);
    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    QVERIFY(s.mappings[0].accountRef.isEmpty());
    QVERIFY(s.mappings[0].collectionId.isEmpty());
}

void TstAccountsSetupPage::editClearsChosenCollectionsButKeepsAccountRef()
{
    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("Original")));
    QTest::qWait(50);

    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = id;
    s.mappings[0].collectionId = QStringLiteral("personal");

    page.editAccountConfig(id, QStringLiteral("stub"),
                           stubConfig(QStringLiteral("Renamed")));
    QTest::qWait(50);

    QCOMPARE(s.accounts.size(), 1);
    QCOMPARE(s.accounts.first().id, id);   // id stable across edits
    QCOMPARE(s.accounts.first().config.displayName, QStringLiteral("Renamed"));
    QCOMPARE(s.mappings[0].accountRef, id);          // binding survives...
    QVERIFY(s.mappings[0].collectionId.isEmpty());   // ...but must be re-picked
}

WILDPALMS_QTEST_MAIN(TstAccountsSetupPage)
#include "tst_accountssetuppage.moc"
