#include "accountssetuppage.h"
#include "wizardstate.h"

#include "app/accounts/addaccountdialog.h"   // WildPalmsAppAccounts (PUBLIC dep of this lib)

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/sync/iprovider.h>

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

namespace {
QString kindFriendly(const QString &kind) {
    if (kind == QStringLiteral("multiproto-dav")) return QStringLiteral("DAV");
    if (kind == QStringLiteral("akonadi"))        return QObject::tr("Akonadi (local)");
    return kind.toUpper();
}
} // namespace

AccountsSetupPage::AccountsSetupPage(Kalburator::Sync::BackendRegistry *registry,
                                     WizardState *state,
                                     QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Accounts"));
    setSubTitle(tr("Add the accounts this profile syncs with. Skip this "
                   "page to keep everything in local files."));

    auto *outer = new QVBoxLayout(this);
    auto *listHost = new QWidget(this);
    m_listLayout = new QVBoxLayout(listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(listHost);

    m_addButton = new QPushButton(tr("Add Account…"), this);
    m_addButton->setObjectName(QStringLiteral("addAccount"));
    connect(m_addButton, &QPushButton::clicked,
            this, &AccountsSetupPage::onAddClicked);
    outer->addWidget(m_addButton, 0, Qt::AlignLeft);
    outer->addStretch(1);
}

AccountsSetupPage::~AccountsSetupPage() = default;

void AccountsSetupPage::initializePage()
{
    rebuildList();
}

bool AccountsSetupPage::isComplete() const
{
    return m_inFlightIds.isEmpty();
}

int AccountsSetupPage::accountIndex(const QString &id) const
{
    for (int i = 0; i < m_state->accounts.size(); ++i)
        if (m_state->accounts[i].id == id) return i;
    return -1;
}

QString AccountsSetupPage::addAccountFromConfig(
    const QString &kind, const Kalburator::Sync::BackendConfiguration &cfg)
{
    WizardAccount acc;
    acc.id     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    acc.kind   = kind;
    acc.config = cfg;
    acc.config.id = acc.id;   // on-disk account id == wizard id (F.1c §10.2)
    if (acc.config.type.isEmpty()) acc.config.type = kind;
    m_state->accounts.append(acc);
    connectAccount(acc.id);
    return acc.id;
}

void AccountsSetupPage::editAccountConfig(
    const QString &id, const QString &kind,
    const Kalburator::Sync::BackendConfiguration &cfg)
{
    const int i = accountIndex(id);
    if (i < 0 || m_inFlightIds.contains(id)) return;
    auto &acc = m_state->accounts[i];
    acc.kind   = kind;
    acc.config = cfg;
    acc.config.id = id;       // id is stable across edits
    if (acc.config.type.isEmpty()) acc.config.type = kind;
    acc.connected = false;
    acc.error.clear();
    acc.collections.clear();
    // Collections may have changed; bindings on this account must re-pick.
    for (auto &m : m_state->mappings)
        if (m.kind == TargetKind::Account && m.accountRef == id)
            m.collectionId.clear();
    m_providers.erase(id.toStdString());
    connectAccount(id);
}

void AccountsSetupPage::removeAccount(const QString &id)
{
    if (m_inFlightIds.contains(id)) return;
    const int i = accountIndex(id);
    if (i < 0) return;
    m_providers.erase(id.toStdString());
    m_state->accounts.removeAt(i);
    for (auto &m : m_state->mappings) {
        if (m.accountRef == id) {
            m.kind = TargetKind::RawFiles;
            m.accountRef.clear();
            m.collectionId.clear();
        }
    }
    rebuildList();
}

void AccountsSetupPage::connectAccount(const QString &id)
{
    const int i = accountIndex(id);
    if (i < 0) return;
    auto &acc = m_state->accounts[i];

    Kalburator::Sync::BackendContribution *contribution =
        m_registry ? m_registry->contributionFor(acc.kind) : nullptr;
    std::unique_ptr<Kalburator::Sync::IProvider> provider =
        contribution ? contribution->createProvider(this) : nullptr;
    if (!provider) {
        acc.connected = false;
        acc.error = tr("No provider for account kind: %1").arg(acc.kind);
        rebuildList();
        return;
    }
    provider->load(acc.config);

    auto *p = provider.get();
    const std::string skey = id.toStdString();
    m_providers.insert_or_assign(skey, std::move(provider));
    m_lastError.remove(id);
    QObject::connect(p, &Kalburator::Sync::IProvider::error, this,
                     [this, id](const QString &msg) { m_lastError[id] = msg; });

    m_inFlightIds.insert(id);
    emit completeChanged();

    auto *w = new QFutureWatcher<bool>(this);
    QObject::connect(w, &QFutureWatcher<bool>::finished, this, [this, w, id]() {
        const bool ok = w->future().resultCount() > 0 && w->result();
        const int i = accountIndex(id);
        if (i >= 0) {
            auto &acc = m_state->accounts[i];
            acc.connected = ok;
            acc.error = ok ? QString()
                           : m_lastError.value(id, tr("Couldn't connect."));
            if (ok) {
                auto it = m_providers.find(id.toStdString());
                acc.collections = (it != m_providers.end() && it->second)
                    ? it->second->collections()
                    : QList<Kalburator::Sync::CollectionInfo>{};
            } else {
                acc.collections = {};
            }
        }
        m_inFlightIds.remove(id);
        rebuildList();
        emit completeChanged();
        w->deleteLater();
    });
    w->setFuture(p->connect());
    rebuildList();
}

void AccountsSetupPage::rebuildList()
{
    while (auto *item = m_listLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto &acc : m_state->accounts) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);

        const QString name = acc.config.displayName.isEmpty()
            ? tr("Unnamed account") : acc.config.displayName;
        QString status;
        if (m_inFlightIds.contains(acc.id))
            status = tr("Connecting…");
        else if (acc.connected)
            status = tr("Connected — %n collection(s)", "",
                        int(acc.collections.size()));
        else
            status = acc.error.isEmpty()
                ? tr("Not connected") : tr("Failed: %1").arg(acc.error);

        auto *label = new QLabel(QStringLiteral("%1 (%2) — %3")
                                     .arg(name, kindFriendly(acc.kind), status),
                                 row);
        h->addWidget(label, 1);

        auto *edit = new QPushButton(tr("Edit"), row);
        edit->setObjectName(QStringLiteral("edit:%1").arg(acc.id));
        edit->setEnabled(!m_inFlightIds.contains(acc.id));
        connect(edit, &QPushButton::clicked, this,
                [this, id = acc.id]() { onEditClicked(id); });
        h->addWidget(edit);

        auto *remove = new QPushButton(tr("Remove"), row);
        remove->setObjectName(QStringLiteral("remove:%1").arg(acc.id));
        remove->setEnabled(!m_inFlightIds.contains(acc.id));
        connect(remove, &QPushButton::clicked, this,
                [this, id = acc.id]() { removeAccount(id); });
        h->addWidget(remove);

        m_listLayout->addWidget(row);
    }
}

void AccountsSetupPage::onAddClicked()
{
    WildPalms::App::Accounts::AddAccountDialog dlg(m_registry, this);
    if (dlg.exec() != QDialog::Accepted) return;
    addAccountFromConfig(dlg.selectedKind(), dlg.configuration());
}

void AccountsSetupPage::onEditClicked(const QString &id)
{
    const int i = accountIndex(id);
    if (i < 0) return;
    WildPalms::App::Accounts::AddAccountDialog dlg(m_registry, this);
    dlg.setWindowTitle(tr("Edit Account"));
    dlg.setConfiguration(m_state->accounts[i].config);
    if (dlg.exec() != QDialog::Accepted) return;
    editAccountConfig(id, dlg.selectedKind(), dlg.configuration());
}

}  // namespace WildPalms::Wizard
