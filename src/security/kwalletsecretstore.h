#ifndef WILDPALMS_KWALLETSECRETSTORE_H
#define WILDPALMS_KWALLETSECRETSTORE_H

#include <QString>

#include <kalburator/sync/secretstore.h>

namespace KWallet {
class Wallet;
}

namespace WildPalms {

/// Persistent host adapter for libkalburator secret references. KWallet owns
/// encryption and unlock policy; profile configuration stores only references.
class KWalletSecretStore final : public Kalburator::Sync::SecretStore
{
public:
    explicit KWalletSecretStore(QString walletName = QStringLiteral("kdewallet"),
                                QString folderName = QStringLiteral("WildPalms"));
    ~KWalletSecretStore() override;

    QString put(const QString &secret) override;
    QString get(const QString &reference) const override;
    bool remove(const QString &reference) override;

private:
    bool ensureWallet() const;

    QString m_walletName;
    QString m_folderName;
    mutable KWallet::Wallet *m_wallet = nullptr;
};

} // namespace WildPalms

#endif
