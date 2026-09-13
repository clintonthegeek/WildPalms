#ifndef WILDPALMS_RUNTIME_PALMRUNTIMEADAPTERS_H
#define WILDPALMS_RUNTIME_PALMRUNTIMEADAPTERS_H

#include <functional>
#include <memory>

#include <kalburator/runtime/collectionruntime.h>

namespace Kalburator::Sync { class SyncBackendBase; }

namespace WildPalms::Runtime {

/// Adapts a consumer-owned backend creator to the runtime's opaque endpoint
/// factory. The runtime owns the returned endpoint after topology commit.
class PalmBackendFactory final : public Kalburator::Runtime::BackendFactory {
public:
    using Creator = std::function<std::unique_ptr<Kalburator::Sync::SyncBackendBase>()>;

    PalmBackendFactory(QString id, Creator creator);
    QString factoryId() const override;
    bool validate(QString &errorMessage) const override;
    std::unique_ptr<Kalburator::Runtime::BackendEndpoint> createEndpoint(
        const Kalburator::Runtime::BackendMaterialization &request,
        QString &errorMessage) const override;

private:
    QString m_id;
    Creator m_creator;
};

class HubBackendFactory final : public Kalburator::Runtime::BackendFactory {
public:
    explicit HubBackendFactory(QString defaultPath);
    QString factoryId() const override;
    bool validate(QString &errorMessage) const override;
    std::unique_ptr<Kalburator::Runtime::BackendEndpoint> createEndpoint(
        const Kalburator::Runtime::BackendMaterialization &request,
        QString &errorMessage) const override;

private:
    QString m_defaultPath;
};

/// Generic external-resource adapter for the Palm link. The callbacks keep
/// Palm-specific device types out of libkalburator's public runtime contract.
class PalmExternalResourceLease final
    : public Kalburator::Runtime::ExternalResourceLease {
public:
    using Callback = std::function<bool(QString &)>;
    using VoidCallback = std::function<void()>;

    PalmExternalResourceLease(Callback prepare, Callback execute,
                              Callback flush, VoidCallback cancel,
                              std::function<void(const Kalburator::Runtime::RunResult &)> finish);
    bool prepare(QString &errorMessage) override;
    bool executePhase(const QString &phase, QString &errorMessage) override;
    bool flush(QString &errorMessage) override;
    void setLossHandler(std::function<void()> handler) override;
    void cancel() override;
    void finish(const Kalburator::Runtime::RunResult &result) override;

private:
    Callback m_prepare;
    Callback m_execute;
    Callback m_flush;
    VoidCallback m_cancel;
    VoidCallback m_lossHandler;
    std::function<void(const Kalburator::Runtime::RunResult &)> m_finish;
};

} // namespace WildPalms::Runtime

#endif
