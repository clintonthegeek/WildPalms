#include "palmruntimeadapters.h"

#include <kalburator/sync/syncbackendbase.h>
#include <kalburator/universal/genericsqlitebackend.h>
#include <QDir>

namespace WildPalms::Runtime {

PalmBackendFactory::PalmBackendFactory(QString id, Creator creator)
    : m_id(std::move(id)), m_creator(std::move(creator))
{}

QString PalmBackendFactory::factoryId() const { return m_id; }

bool PalmBackendFactory::validate(QString &errorMessage) const
{
    if (m_creator) {
        errorMessage.clear();
        return true;
    }
    errorMessage = QStringLiteral("Palm backend factory has no creator: ") + m_id;
    return false;
}

std::unique_ptr<Kalburator::Runtime::BackendEndpoint>
PalmBackendFactory::createEndpoint(
    const Kalburator::Runtime::BackendMaterialization &request, QString &errorMessage) const
{
    if (!m_creator) {
        errorMessage = QStringLiteral("Palm backend factory has no creator: ") + m_id;
        return {};
    }
    auto backend = m_creator();
    if (!backend) {
        errorMessage = QStringLiteral("Palm backend creation failed: ") + m_id;
        return {};
    }
    errorMessage.clear();
    std::unique_ptr<QObject> object(backend.release());
    return std::make_unique<Kalburator::Runtime::BackendEndpoint>(std::move(object));
}

HubBackendFactory::HubBackendFactory(QString defaultPath)
    : m_defaultPath(std::move(defaultPath))
{}

QString HubBackendFactory::factoryId() const { return QStringLiteral("hub"); }

bool HubBackendFactory::validate(QString &errorMessage) const
{
    if (!m_defaultPath.isEmpty()) {
        errorMessage.clear();
        return true;
    }
    errorMessage = QStringLiteral("hub backend factory has no storage path");
    return false;
}

std::unique_ptr<Kalburator::Runtime::BackendEndpoint>
HubBackendFactory::createEndpoint(
    const Kalburator::Runtime::BackendMaterialization &request,
    QString &errorMessage) const
{
    QString path = request.factoryInput.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) path = m_defaultPath;
    if (path.isEmpty()) {
        errorMessage = QStringLiteral("hub backend has no storage path");
        return {};
    }
    auto backend = std::make_unique<Kalburator::Sinks::GenericSqliteBackend>(path);
    errorMessage.clear();
    std::unique_ptr<QObject> object(backend.release());
    return std::make_unique<Kalburator::Runtime::BackendEndpoint>(std::move(object));
}

PalmExternalResourceLease::PalmExternalResourceLease(
    Callback prepare, Callback execute, Callback flush, VoidCallback cancel,
    std::function<void(const Kalburator::Runtime::RunResult &)> finish)
    : m_prepare(std::move(prepare))
    , m_execute(std::move(execute))
    , m_flush(std::move(flush))
    , m_cancel(std::move(cancel))
    , m_finish(std::move(finish))
{}

bool PalmExternalResourceLease::prepare(QString &errorMessage)
{ return m_prepare ? m_prepare(errorMessage) : true; }

bool PalmExternalResourceLease::executePhase(const QString &, QString &errorMessage)
{
    const bool ok = m_execute ? m_execute(errorMessage) : true;
    if (!ok && m_lossHandler)
        m_lossHandler();
    return ok;
}

bool PalmExternalResourceLease::flush(QString &errorMessage)
{ return m_flush ? m_flush(errorMessage) : true; }

void PalmExternalResourceLease::setLossHandler(std::function<void()> handler)
{ m_lossHandler = std::move(handler); }

void PalmExternalResourceLease::cancel()
{ if (m_cancel) m_cancel(); }

void PalmExternalResourceLease::finish(const Kalburator::Runtime::RunResult &result)
{ if (m_finish) m_finish(result); }

} // namespace WildPalms::Runtime
