#ifndef WILDPALMS_PALMSYNC_PALMCHANGEDETECTION_H
#define WILDPALMS_PALMSYNC_PALMCHANGEDETECTION_H

#include <kalburator/sync/changedetection.h>          // Kalburator::Sync::ChangeDetection
#include "palmrevisionstore.h"
#include <QMap>
#include <QString>

namespace WildPalms::PalmSync {

/// Shared base implementing the lib's collection-level ChangeDetection for a
/// Palm backend that wraps exactly ONE physical database. Concrete backends
/// add this as a base and implement currentDbRevision() (their single DB's
/// modnum, via m_palmBackend->databaseRevision(DatabaseName)). PalmRuntime
/// injects the per-profile store via setPalmRevisionStore().
class PalmChangeDetection : public Kalburator::Sync::ChangeDetection {
public:
    void setPalmRevisionStore(PalmRevisionStore *store) { m_revStore = store; }

    QString collectionRevision(const QString &collectionId) override
    {
        Q_UNUSED(collectionId);          // one DB per backend; id-agnostic
        return currentDbRevision();
    }

    QString cachedCollectionRevision(const QString &collectionId) const override
    {
        return m_revStore ? m_revStore->token(collectionId) : QString();
    }

    // Not part of the lib interface (removed upstream, sync-hardening H3):
    // WP-side helper persisting tokens into the per-profile store.
    void primeRevisionCache(const QMap<QString, QString> &cache)
    {
        if (!m_revStore) return;
        for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
            m_revStore->setToken(it.key(), it.value());
    }

protected:
    /// The wrapped DB's current modnum token (empty if unavailable).
    virtual QString currentDbRevision() const = 0;

private:
    PalmRevisionStore *m_revStore = nullptr;   // not owned
};

} // namespace WildPalms::PalmSync
#endif
