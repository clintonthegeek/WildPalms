#include "localfolderprovider.h"
#include "localfolderconfigwidget.h"

#include <kalburator/universal/markdownfilesbackend.h>
#include <kalburator/universal/rawfilesbackend.h>

#include <QDir>
#include <QFutureInterface>

namespace WildPalms::Runtime {

namespace {
// Shakedown F2: the entry domain doubles as the shape domain ("note" →
// Markdown), but CollectionInfo::type must match what PimPlugin::
// matchesCollection expects, which differs for memo/todo.
QString collectionTypeForDomain(const QString &domain)
{
    if (domain == QLatin1String("note"))
        return QStringLiteral("memos");
    if (domain == QLatin1String("todo"))
        return QStringLiteral("todos");
    return domain;
}
}  // namespace

LocalFolderProvider::LocalFolderProvider(QObject *parent)
    : Kalburator::Sync::IProvider(parent) {}

void LocalFolderProvider::load(const Kalburator::Sync::BackendConfiguration &cfg)
{
    m_cfg = cfg;
    m_entries.clear();
    const QVariantList list =
        cfg.connectionParams.value(QStringLiteral("entries")).toList();
    int i = 0;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.path   = m.value(QStringLiteral("path")).toString();
        e.domain = m.value(QStringLiteral("domain")).toString();
        // Stable, path-independent collection id within this provider.
        e.collectionId = QStringLiteral("folder-%1").arg(i++);
        if (!e.path.isEmpty() && !e.domain.isEmpty())
            m_entries.append(e);
    }
}

QFuture<bool> LocalFolderProvider::connect()
{
    // Synchronous: validate paths, build collections, resolve immediately.
    m_lastError.clear();
    m_collections.clear();
    bool ok = !m_entries.isEmpty();
    if (m_entries.isEmpty())
        m_lastError = QStringLiteral("No folders configured");
    for (const auto &e : m_entries) {
        if (!QDir(e.path).exists()) {
            ok = false;
            m_lastError = QStringLiteral("Folder does not exist: %1").arg(e.path);
            break;
        }
        Kalburator::Sync::CollectionInfo ci;
        ci.id       = e.collectionId;
        ci.name     = QDir(e.path).dirName();
        ci.type     = collectionTypeForDomain(e.domain);
        ci.readOnly = false;
        m_collections.append(ci);
    }
    if (!ok) {
        m_collections.clear();
        emit error(m_lastError);
    } else {
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
    }
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(ok);
    fi.reportFinished();
    return fi.future();
}

void LocalFolderProvider::disconnect()
{
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    emit connectionStateChanged(false);
}

std::vector<Kalburator::Sync::ProviderBackendSpec>
LocalFolderProvider::createBackends()
{
    std::vector<Kalburator::Sync::ProviderBackendSpec> out;
    if (!m_connected) return out;
    for (const auto &e : m_entries) {
        // v1 dispatch (substrate spec A2): note -> Markdown; rest -> RawFiles.
        std::unique_ptr<Kalburator::Sinks::RawFilesBackend> backend;
        Kalburator::Shape::Shape shape;
        if (e.domain == QLatin1String("note")) {
            backend = std::make_unique<Kalburator::Sinks::MarkdownFilesBackend>(e.path);
            shape = Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("note")},
                Kalburator::Shape::EncodingId{QStringLiteral("markdown")} };
        } else {
            backend = std::make_unique<Kalburator::Sinks::RawFilesBackend>(e.path);
            shape = Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{e.domain},
                Kalburator::Shape::EncodingId{QStringLiteral("raw")} };
        }
        Kalburator::Sync::CollectionInfo ci = m_collections.at(
            static_cast<int>(out.size()));
        // K.9: universal sinks must re-declare each collection with its
        // shape on every construction.
        backend->createCollection(ci, shape);
        Kalburator::Sync::ProviderBackendSpec spec;
        spec.domainId    = e.collectionId;
        spec.collections = { ci };
        spec.backend     = std::move(backend);
        out.push_back(std::move(spec));
    }
    return out;
}

QWidget *LocalFolderProvider::createConfigWidget(QWidget *parent)
{
    // Shakedown F2: used to return nullptr — "Local folder" was a config
    // dead end (an empty account that failed forever with "No folders
    // configured", and no UI to add folder entries).
    return new LocalFolderConfigWidget(parent);
}

} // namespace WildPalms::Runtime
