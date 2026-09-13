// M.12 — WildPalms AddAccountDialog baseline smoke test
//
// Pins current AddAccountDialog behaviour (empty registry, single
// contribution, selectedKind) BEFORE the Phase M migration replaces it
// with ProviderConfigDialog.  Each test constructs its own local
// BackendRegistry, so no process-wide singleton cleanup is required.

#include <QObject>
#include <QtTest/QtTest>
#include <QComboBox>

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/multiprotocoldavbackendcontribution.h>
#include <kalburator/typesupport/backendconfiguration.h>

#include "../src/app/accounts/addaccountdialog.h"

using namespace Kalburator::Sync;
using namespace WildPalms::App::Accounts;

class TstAddAccountDialogBaseline : public QObject
{
    Q_OBJECT
private slots:
    void constructsWithEmptyRegistry();
    void comboPopulatedForRegisteredContributions();
    void selectedKindReturnsRegisteredType();
};

void TstAddAccountDialogBaseline::constructsWithEmptyRegistry()
{
    // An empty registry → combo has no items; dialog must still construct
    // without crashing.
    BackendRegistry registry;
    AddAccountDialog dlg(&registry);
    auto *combo = dlg.findChild<QComboBox *>();
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 0);
}

void TstAddAccountDialogBaseline::comboPopulatedForRegisteredContributions()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<MultiProtocolDavBackendContribution>());
    AddAccountDialog dlg(&registry);
    auto *combo = dlg.findChild<QComboBox *>();
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 1);
}

void TstAddAccountDialogBaseline::selectedKindReturnsRegisteredType()
{
    BackendRegistry registry;
    registry.registerContribution(std::make_shared<MultiProtocolDavBackendContribution>());
    AddAccountDialog dlg(&registry);
    QCOMPARE(dlg.selectedKind(), QStringLiteral("multiproto-dav"));
}

QTEST_MAIN(TstAddAccountDialogBaseline)
#include "test_addaccountdialog_baseline.moc"
