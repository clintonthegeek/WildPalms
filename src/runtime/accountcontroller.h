#ifndef WILDPALMS_RUNTIME_ACCOUNTCONTROLLER_H
#define WILDPALMS_RUNTIME_ACCOUNTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QFuture>
#include <QJsonArray>
#include <memory>
#include <kalburator/runtime/collectionruntime.h>

class Profile;
namespace Kalburator::Sync {
    class ProviderManager;
    class BackendRegistry;
    class IProvider;
    struct BackendConfiguration;
    struct CollectionInfo;
}
namespace WildPalms::Runtime {
    class PalmRuntime;
}

namespace WildPalms::Runtime {

/// Profile-scoped owner of provider lifecycle and provider-bound mapping
/// integrity. Constructed in KF6MainWindow::loadProfile() AFTER PalmRuntime;
/// torn down BEFORE PalmRuntime in closeProfile() / loadProfile().
///
/// AccountController does NOT bind anything to anything — bindings (which
/// Palm slot ↔ which backend) live in SyncMappings, edited via MappingEditor.
/// AccountController only manages credentials, connection state, and cascades
/// mapping deletion when an account is removed.
///
/// Persistence: Profile::accounts() (K.8b T11). Provider construction goes
/// through BackendRegistry::contributionFor(kind)->createProvider().
class AccountController : public QObject {
    Q_OBJECT
public:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Error,
    };
    Q_ENUM(ConnectionState)

    struct ProviderSummary {
        QString id;
        QString kind;
        QString displayName;
        QList<Kalburator::Sync::CollectionInfo> collections;
        ConnectionState state = ConnectionState::Disconnected;
        QString errorMessage;
    };

    AccountController(const QString &syncFolderPath,
                      Kalburator::Sync::BackendRegistry *registry,
                      Profile *profile,
                      PalmRuntime *palmRuntime,
                      QObject *parent = nullptr);
    // syncFolderPath kept for API compatibility; not used for persistence.
    ~AccountController() override;

    /// Add a new provider. Persists immediately; connect() runs async.
    /// Refuses if palmRuntime->isRunning(); returns empty string on refusal.
    /// (Stubbed in Task 3; full impl in Task 5.)
    QString addProvider(const QString &kind,
                        const Kalburator::Sync::BackendConfiguration &config);

    /// Remove a provider AND cascade-delete mappings referencing its backends.
    /// Refuses if palmRuntime->isRunning(); returns false on refusal.
    /// (Stubbed in Task 3; full impl in Tasks 5–6.)
    bool removeProvider(const QString &providerId);

    QList<Kalburator::Sync::IProvider*> providers() const;
    QList<ProviderSummary> providerSummaries() const;
    QList<Kalburator::Sync::CollectionInfo>
        collectionsFor(const QString &providerId) const;
    ConnectionState stateFor(const QString &providerId) const;
    QString errorFor(const QString &providerId) const;
    int mappingCountFor(const QString &providerId) const;
    QStringList mappingDescriptionsFor(const QString &providerId, int max) const;

    /// Backing ProviderManager (used by AccountsPage to wire signals and by
    /// the Sync Mappings graph view to look up providers by id).
    Kalburator::Sync::ProviderManager *providerManager() const;

    /// BackendRegistry (used by AddAccountDialog to enumerate contributions).
    Kalburator::Sync::BackendRegistry *backendRegistry() const;

    /// Append rows to Profile::syncMappingsJson and persist. Binds a
    /// provider's collections to Palm slots. The caller decides slot
    /// semantics; AC just persists. (The Sync Mappings graph view is the
    /// current caller; previously MappingPromptDialog.)
    void appendMappings(const QJsonArray &rows);

    bool providerEnabled(const QString &id) const;

public Q_SLOTS:
    void setProviderEnabled(const QString &providerId, bool enabled);
    void setMappingEnabled(const QString &mappingId, bool enabled);

signals:
    void providersChanged();
    void connectStateChanged(QString providerId, ConnectionState state);
    void connectFailed(QString providerId, QString error);
    void mappingsChanged();   // emitted on cascade-delete (Task 6)
    void providerEnabledChanged(QString providerId, bool enabled);
    void mappingEnabledChanged(QString mappingId, bool enabled);
    /// Emitted once after the initial loadAndConnect() completes — i.e. after
    /// all configured providers have either connected or failed. Guaranteed to
    /// fire on the GUI thread. Profiles with no providers fire this immediately.
    void accountsReady();

private:
    void loadAndConnect();
    QList<int> mappingIndicesFor(const QString &providerId) const;

    Kalburator::Sync::BackendRegistry               *m_registry;        // borrowed
    Profile                                         *m_profile;         // borrowed
    PalmRuntime                                     *m_palmRuntime;     // borrowed
    std::unique_ptr<Kalburator::Sync::ProviderManager> m_providerManager;
    bool m_runtimeProviders = false;
    QHash<QString, ConnectionState>                  m_states;
    QHash<QString, QString>                          m_lastErrors;
};

}  // namespace WildPalms::Runtime

#endif
