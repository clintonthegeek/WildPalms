#include "accountcontroller.h"

#include "palmruntime.h"
#include "../profile.h"

// libkalburator includes — bare names (libkalburator headers are on the
// include path; matches the existing palmruntime.cpp include style).
#include <kalburator/sync/providermanager.h>
#include <kalburator/sync/iprovider.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/types/collectioninfo.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QFutureWatcher>

#include <algorithm>

namespace WildPalms::Runtime {

using Kalburator::Sync::ProviderManager;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;

AccountController::AccountController(const QString &syncFolderPath,
                                     BackendRegistry *registry,
                                     Profile *profile,
                                     PalmRuntime *palmRuntime,
                                     QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_profile(profile)
    , m_palmRuntime(palmRuntime)
{
    Q_UNUSED(syncFolderPath)
    Q_ASSERT(m_registry);
    Q_ASSERT(m_profile);
    Q_ASSERT(m_palmRuntime);

    m_runtimeProviders = m_palmRuntime->hasCollectionRuntime();
    if (m_runtimeProviders)
        loadAndConnect();
    else
        m_providerManager = std::make_unique<ProviderManager>(registry, this);

    if (m_runtimeProviders)
        return;

    QObject::connect(m_providerManager.get(), &ProviderManager::providersChanged,
            this, &AccountController::providersChanged);
    QObject::connect(m_providerManager.get(),
            &ProviderManager::providerStateChanged,
            this, [this](const QString &providerId,
                         Kalburator::Sync::ProviderConnectionState state) {
        ConnectionState s = ConnectionState::Disconnected;
        switch (state) {
            case Kalburator::Sync::ProviderConnectionState::Disconnected:
                s = ConnectionState::Disconnected; break;
            case Kalburator::Sync::ProviderConnectionState::Connecting:
                s = ConnectionState::Connecting; break;
            case Kalburator::Sync::ProviderConnectionState::Connected:
                s = ConnectionState::Connected; break;
            case Kalburator::Sync::ProviderConnectionState::Error:
                s = ConnectionState::Error; break;
        }
        m_states.insert(providerId, s);
        Q_EMIT connectStateChanged(providerId, s);
    });

    loadAndConnect();
}

AccountController::~AccountController()
{
    // ~ProviderManager() calls disconnectAll(), whose providers emit state
    // changes that ProviderManager re-emits as providerStateChanged. By then
    // m_states (declared after m_providerManager, destroyed before it) is
    // already gone, and context-based disconnection only happens later in
    // ~QObject. Sever the connections before member destruction begins.
    disconnect(m_providerManager.get(), nullptr, this, nullptr);
}

void AccountController::loadAndConnect() {
    if (m_runtimeProviders) {
        auto future = m_palmRuntime->connectProviders();
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
            watcher->deleteLater();
            Q_EMIT providersChanged();
            Q_EMIT accountsReady();
        });
        watcher->setFuture(future);
        return;
    }
    for (const auto &cfg : m_profile->accounts()) {
        BackendContribution *contribution = m_registry->contributionFor(cfg.type);
        if (!contribution) continue;
        auto provider = contribution->createProvider(this);
        if (!provider) continue;
        provider->load(cfg);
        m_states.insert(cfg.id, ConnectionState::Connecting);
        m_providerManager->addProvider(std::move(provider));
    }
    QFuture<void> future = m_providerManager->connectAll();

    // Re-apply disabled provider states so mappings start in the right state.
    for (const auto &cfg : m_profile->accounts()) {
        if (!cfg.enabled)
            setProviderEnabled(cfg.id, false);
    }

    // Emit accountsReady() once all providers have finished connecting (or if
    // there are none). The watcher posts the signal on the GUI thread so it is
    // safe to use directly from slots that interact with the UI.
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        Q_EMIT accountsReady();
    });
    watcher->setFuture(future);
}

bool AccountController::providerEnabled(const QString &id) const {
    for (const auto &bc : m_profile->accounts()) {
        if (bc.id == id) return bc.enabled;
    }
    return true; // unknown → assume enabled
}

void AccountController::setProviderEnabled(const QString &providerId, bool enabled) {
    if (!m_profile) return;
    // Update the BackendConfiguration enabled flag
    auto accts = m_profile->accounts();
    auto it = std::find_if(accts.begin(), accts.end(),
        [&](const auto &bc){ return bc.id == providerId; });
    if (it == accts.end()) return;
    it->enabled = enabled;
    m_profile->saveAccount(*it);

    if (m_runtimeProviders) {
        QString error;
        if (!m_palmRuntime->updateProvider(*it, error)) {
            qWarning() << "[AccountController] provider update failed:" << error;
            return;
        }
    }

    // Fan out to all mappings referencing this provider
    QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = providerId + QLatin1Char(':');
    bool changed = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject row = arr.at(i).toObject();
        const QString src = row.value(QStringLiteral("sourceBackend")).toString();
        const QString tgt = row.value(QStringLiteral("targetBackend")).toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) {
            row[QStringLiteral("enabled")] = enabled;
            arr.replace(i, row);
            changed = true;
        }
    }
    if (changed) {
        m_profile->setSyncMappingsJson(arr);
        m_profile->save();
    }
    Q_EMIT providerEnabledChanged(providerId, enabled);
}

QString AccountController::addProvider(const QString &kind,
                                       const BackendConfiguration &config) {
    if (m_palmRuntime->isRunning()) return QString();

    if (m_runtimeProviders) {
        BackendConfiguration cfg = config;
        if (cfg.id.isEmpty())
            cfg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        cfg.type = kind;
        QString error;
        if (!m_palmRuntime->addProvider(cfg, error))
            return {};
        m_profile->saveAccount(cfg);
        m_profile->save();
        m_palmRuntime->connectProviders();
        Q_EMIT providersChanged();
        return cfg.id;
    }

    BackendContribution *contribution = m_registry->contributionFor(kind);
    if (!contribution) return QString();

    BackendConfiguration cfg = config;
    if (cfg.id.isEmpty()) {
        cfg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    cfg.type = kind;

    auto provider = contribution->createProvider(this);
    if (!provider) return QString();
    provider->load(cfg);

    const QString id = cfg.id;
    m_profile->saveAccount(cfg);
    m_profile->save();

    m_providerManager->addProvider(std::move(provider));
    m_states.insert(id, ConnectionState::Connecting);

    // Kick off async connect for just-this-one (connectAll connects all,
    // including the just-added one — ProviderManager handles idempotence).
    m_providerManager->connectAll();

    return id;
}

bool AccountController::removeProvider(const QString &providerId) {
    if (m_palmRuntime->isRunning()) return false;
    if (m_runtimeProviders) {
        const auto summaries = providerSummaries();
        const auto found = std::find_if(summaries.cbegin(), summaries.cend(),
                                        [&providerId](const auto &p) { return p.id == providerId; });
        if (found == summaries.cend()) return false;
    } else if (!m_providerManager->providerById(providerId)) {
        return false;
    }

    // Cascade-delete mappings.
    const QList<int> indices = mappingIndicesFor(providerId);
    if (!indices.isEmpty()) {
        QJsonArray arr = m_profile->syncMappingsJson();
        // Remove from highest index to lowest so positions stay valid.
        QList<int> sorted = indices;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int idx : sorted) arr.removeAt(idx);
        m_profile->setSyncMappingsJson(arr);
        m_profile->save();
        Q_EMIT mappingsChanged();
    }

    if (m_runtimeProviders) {
        QString error;
        if (!m_palmRuntime->removeProvider(providerId, error))
            return false;
    } else {
        m_providerManager->removeProvider(providerId);
    }
    m_states.remove(providerId);
    m_lastErrors.remove(providerId);

    m_profile->removeAccount(providerId);
    m_profile->save();
    return true;
}

QList<IProvider*> AccountController::providers() const {
    if (m_runtimeProviders)
        return {};
    return m_providerManager->providers();
}

QList<AccountController::ProviderSummary> AccountController::providerSummaries() const
{
    if (!m_runtimeProviders) {
        QList<ProviderSummary> out;
        for (auto *provider : m_providerManager->providers()) {
            ProviderSummary summary;
            summary.id = provider->id();
            summary.kind = provider->kind();
            summary.displayName = provider->displayName();
            summary.collections = provider->collections();
            summary.state = stateFor(summary.id);
            summary.errorMessage = errorFor(summary.id);
            out.append(std::move(summary));
        }
        return out;
    }
    QList<ProviderSummary> out;
    for (const auto &provider : m_palmRuntime->providerSnapshots()) {
        ProviderSummary summary;
        summary.id = provider.id;
        summary.kind = provider.kind;
        summary.displayName = provider.displayName;
        summary.collections = provider.collections;
        summary.errorMessage = provider.errorMessage;
        switch (provider.state) {
        case Kalburator::Sync::ProviderConnectionState::Connecting:
            summary.state = ConnectionState::Connecting; break;
        case Kalburator::Sync::ProviderConnectionState::Connected:
            summary.state = ConnectionState::Connected; break;
        case Kalburator::Sync::ProviderConnectionState::Error:
            summary.state = ConnectionState::Error; break;
        default:
            summary.state = ConnectionState::Disconnected; break;
        }
        out.append(std::move(summary));
    }
    return out;
}

QList<CollectionInfo> AccountController::collectionsFor(const QString &id) const {
    if (m_runtimeProviders) {
        for (const auto &provider : providerSummaries())
            if (provider.id == id) return provider.collections;
        return {};
    }
    if (auto *p = m_providerManager->providerById(id)) return p->collections();
    return {};
}

AccountController::ConnectionState
AccountController::stateFor(const QString &id) const {
    if (m_runtimeProviders) {
        for (const auto &provider : providerSummaries())
            if (provider.id == id) return provider.state;
        return ConnectionState::Disconnected;
    }
    return m_states.value(id, ConnectionState::Disconnected);
}

QString AccountController::errorFor(const QString &id) const {
    if (m_runtimeProviders) {
        for (const auto &provider : providerSummaries())
            if (provider.id == id) return provider.errorMessage;
        return {};
    }
    return m_lastErrors.value(id);
}

int AccountController::mappingCountFor(const QString &id) const {
    return mappingIndicesFor(id).size();
}

QStringList AccountController::mappingDescriptionsFor(const QString &id,
                                                     int max) const {
    QStringList out;
    const QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = id + QStringLiteral(":");
    for (int i = 0; i < arr.size() && out.size() < max; ++i) {
        const QJsonObject row = arr.at(i).toObject();
        const QString src = row.value("sourceBackend").toString();
        const QString tgt = row.value("targetBackend").toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) {
            const QString sCol = row.value("sourceCalendar").toString();
            const QString tCol = row.value("targetCalendar").toString();
            out.append(QStringLiteral("%1/%2 \xe2\x86\x92 %3/%4").arg(src, sCol, tgt, tCol));
        }
    }
    return out;
}

ProviderManager *AccountController::providerManager() const {
    return m_providerManager.get();
}

BackendRegistry *AccountController::backendRegistry() const {
    return m_registry;
}

void AccountController::appendMappings(const QJsonArray &rows) {
    if (rows.isEmpty()) return;
    QJsonArray arr = m_profile->syncMappingsJson();
    for (const auto &v : rows) arr.append(v);
    m_profile->setSyncMappingsJson(arr);
    m_profile->save();
    Q_EMIT mappingsChanged();
    if (!m_palmRuntime->isRunning()) {
        m_palmRuntime->reloadMappings(arr);
    }
}

void AccountController::setMappingEnabled(const QString &mappingId, bool enabled) {
    if (!m_profile) return;
    QJsonArray arr = m_profile->syncMappingsJson();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject row = arr.at(i).toObject();
        if (row.value(QStringLiteral("id")).toString() == mappingId) {
            row[QStringLiteral("enabled")] = enabled;
            arr.replace(i, row);
            m_profile->setSyncMappingsJson(arr);
            m_profile->save();
            Q_EMIT mappingEnabledChanged(mappingId, enabled);
            return;
        }
    }
}

QList<int> AccountController::mappingIndicesFor(const QString &id) const {
    QList<int> out;
    const QJsonArray arr = m_profile->syncMappingsJson();
    const QString prefix = id + QStringLiteral(":");
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject row = arr.at(i).toObject();
        const QString src = row.value(QStringLiteral("sourceBackend")).toString();
        const QString tgt = row.value(QStringLiteral("targetBackend")).toString();
        if (src.startsWith(prefix) || tgt.startsWith(prefix)) out.append(i);
    }
    return out;
}

}  // namespace WildPalms::Runtime
