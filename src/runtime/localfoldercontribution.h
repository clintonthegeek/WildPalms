#ifndef WILDPALMS_RUNTIME_LOCALFOLDERCONTRIBUTION_H
#define WILDPALMS_RUNTIME_LOCALFOLDERCONTRIBUTION_H

#include <kalburator/sync/backendcontribution.h>
#include "localfolderprovider.h"

namespace WildPalms::Runtime {

/// Substrate A2: registers the credential-less local-folder provider into the
/// same BackendRegistry as DAV/Akonadi — everything is a provider.
class LocalFolderContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("local-folder"); }
    QString displayName() const override { return QStringLiteral("Local folder"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
        createProvider(QObject *parent) const override
    { return std::make_unique<LocalFolderProvider>(parent); }
};

} // namespace WildPalms::Runtime
#endif
