#include "accountspage.h"
#include "addaccountdialog.h"

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"

#include <kalburator/ui/accountslistwidget.h>
#include <kalburator/typesupport/backendconfiguration.h>

#include <QMessageBox>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using AC = WildPalms::Runtime::AccountController;

AccountsPage::AccountsPage(AC *accounts,
                           WildPalms::Runtime::PalmRuntime *palmRuntime,
                           QWidget *parent)
    : QWidget(parent)
    , m_accounts(accounts)
    , m_palmRuntime(palmRuntime)
{
    buildUi();
    refreshList();
}

void AccountsPage::buildUi() {
    auto *outer = new QVBoxLayout(this);
    m_listWidget = new Kalburator::Ui::AccountsListWidget(this);
    outer->addWidget(m_listWidget);

    QObject::connect(m_listWidget, &Kalburator::Ui::AccountsListWidget::accountAddRequested,
                     this, &AccountsPage::onAddClicked);

    QObject::connect(m_listWidget, &Kalburator::Ui::AccountsListWidget::accountRemoved,
                     this, [this](const QString &id) {
                         const int n = m_accounts->mappingCountFor(id);
                         const QStringList sample = m_accounts->mappingDescriptionsFor(id, 3);
                         QString body = tr("Remove account?");
                         if (n > 0) {
                             body = tr("Remove account? This will delete %1 sync mapping(s):\n\n%2")
                                       .arg(n).arg(sample.join(u'\n'));
                             if (n > sample.size())
                                 body += tr("\n... and %1 more.").arg(n - sample.size());
                         }
                         if (QMessageBox::question(this, tr("Remove Account"), body)
                                 != QMessageBox::Yes)
                             return;
                         if (!m_accounts->removeProvider(id))
                             QMessageBox::warning(this, tr("Remove Account"),
                                                  tr("Couldn't remove (sync may be running)."));
                     });

    QObject::connect(m_listWidget, &Kalburator::Ui::AccountsListWidget::accountEnabledChanged,
                     m_accounts, &AC::setProviderEnabled);

    QObject::connect(m_listWidget, &Kalburator::Ui::AccountsListWidget::accountEditRequested,
                     this, [this](const QString &) {
                         QMessageBox::information(this, tr("Edit Account"),
                             tr("Account editing will be available in a future release."));
                     });

    QObject::connect(m_accounts, &AC::providersChanged,
                     this, &AccountsPage::refreshList);
    QObject::connect(m_accounts, &AC::mappingsChanged,
                     this, &AccountsPage::refreshList);
    if (m_palmRuntime) {
        QObject::connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runStarted,
                         this, &AccountsPage::onPalmRunStarted);
        QObject::connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runFinished,
                         this, &AccountsPage::onPalmRunFinished);
    }
}

void AccountsPage::refreshList() {
    QList<Kalburator::Sync::BackendConfiguration> configs;
    for (const auto &provider : m_accounts->providerSummaries()) {
        Kalburator::Sync::BackendConfiguration cfg;
        cfg.id = provider.id;
        cfg.type = provider.kind;
        cfg.displayName = provider.displayName;
        cfg.enabled = m_accounts->providerEnabled(provider.id);
        configs.append(cfg);
    }
    m_listWidget->setAccounts(configs);
}

void AccountsPage::onAddClicked() {
    AddAccountDialog dlg(m_accounts->backendRegistry(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString uuid = m_accounts->addProvider(
        dlg.selectedKind(), dlg.configuration());
    if (uuid.isEmpty()) {
        QMessageBox::warning(this, tr("Add Account"),
            tr("Couldn't add account (sync may be running, "
               "or the kind isn't supported)."));
        return;
    }
    // The F.3 Sync Mappings graph view (next settings tab) takes over the
    // collection-binding flow that the old MappingPromptDialog handled here.
}

void AccountsPage::onPalmRunStarted()  { m_listWidget->setEnabled(false); }
void AccountsPage::onPalmRunFinished() { m_listWidget->setEnabled(true); }

}  // namespace WildPalms::App::Accounts
