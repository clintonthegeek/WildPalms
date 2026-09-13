#include "kwalletsecretstore.h"

#include <kwallet.h>
#include <QUuid>
#include <utility>

namespace WildPalms {

KWalletSecretStore::KWalletSecretStore(QString walletName, QString folderName)
    : m_walletName(std::move(walletName))
    , m_folderName(std::move(folderName))
{
}

KWalletSecretStore::~KWalletSecretStore()
{
    delete m_wallet;
}

bool KWalletSecretStore::ensureWallet() const
{
    if (m_wallet && m_wallet->isOpen())
        return true;
    delete m_wallet;
    m_wallet = KWallet::Wallet::openWallet(m_walletName, 0, KWallet::Wallet::Synchronous);
    if (!m_wallet || !m_wallet->isOpen())
        return false;
    if (!m_wallet->hasFolder(m_folderName) && !m_wallet->createFolder(m_folderName))
        return false;
    return m_wallet->setFolder(m_folderName);
}

QString KWalletSecretStore::put(const QString &secret)
{
    if (secret.isEmpty() || !ensureWallet())
        return {};
    const QString reference = QStringLiteral("secret:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return m_wallet->writePassword(reference, secret) == 0 ? reference : QString();
}

QString KWalletSecretStore::get(const QString &reference) const
{
    if (reference.isEmpty() || !ensureWallet())
        return {};
    QString secret;
    return m_wallet->readPassword(reference, secret) == 0 ? secret : QString();
}

bool KWalletSecretStore::remove(const QString &reference)
{
    return !reference.isEmpty() && ensureWallet()
        && m_wallet->removeEntry(reference) == 0;
}

} // namespace WildPalms
