#include "addaccountdialog.h"
#include "accountformwidget.h"

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/typesupport/backendconfiguration.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendConfiguration;

AddAccountDialog::AddAccountDialog(BackendRegistry *registry, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Account"));
    setModal(true);

    auto *outer = new QVBoxLayout(this);

    m_form = new AccountFormWidget(registry, this);
    outer->addWidget(m_form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    // Preserve existing behavior: disable OK when no contributions registered.
    if (registry && registry->contributions().empty())
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}

AddAccountDialog::~AddAccountDialog() = default;

QString AddAccountDialog::selectedKind() const {
    return m_form ? m_form->selectedKind() : QString();
}

BackendConfiguration AddAccountDialog::configuration() const {
    return m_form ? m_form->configuration() : BackendConfiguration{};
}

void AddAccountDialog::setConfiguration(const BackendConfiguration &cfg) {
    if (m_form) m_form->setConfiguration(cfg);
}

}  // namespace WildPalms::App::Accounts
