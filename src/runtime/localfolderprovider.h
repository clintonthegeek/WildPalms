#ifndef WILDPALMS_RUNTIME_LOCALFOLDERPROVIDER_H
#define WILDPALMS_RUNTIME_LOCALFOLDERPROVIDER_H

#include <kalburator/sync/iprovider.h>
#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/types/collectioninfo.h>

#include <vector>

namespace WildPalms::Runtime {

/// Credential-less provider (substrate A2): each configured (path, domain)
/// entry is one writable collection. Proves the everything-is-a-provider
/// model for local sources; ICS-feed/document stores follow the pattern.
class LocalFolderProvider : public Kalburator::Sync::IProvider {
    Q_OBJECT
public:
    explicit LocalFolderProvider(QObject *parent = nullptr);

    QString id() const override          { return m_cfg.id; }
    QString kind() const override        { return QStringLiteral("local-folder"); }
    QString displayName() const override { return m_cfg.displayName; }
    void load(const Kalburator::Sync::BackendConfiguration &cfg) override;
    Kalburator::Sync::BackendConfiguration save() const override { return m_cfg; }
    QWidget *createConfigWidget(QWidget *parent) override;
    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QList<Kalburator::Sync::CollectionInfo> collections() const override
    { return m_collections; }
    std::vector<Kalburator::Sync::ProviderBackendSpec> createBackends() override;
    QString lastWarning() const override { return {}; }
    QString lastError() const override { return m_lastError; }

private:
    struct Entry { QString path; QString domain; QString collectionId; };
    Kalburator::Sync::BackendConfiguration m_cfg;
    QList<Entry> m_entries;
    QList<Kalburator::Sync::CollectionInfo> m_collections;
    bool m_connected = false;
    QString m_lastError;
};

} // namespace WildPalms::Runtime
#endif
