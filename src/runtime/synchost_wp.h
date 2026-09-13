#ifndef WILDPALMS_RUNTIME_SYNCHOST_WP_H
#define WILDPALMS_RUNTIME_SYNCHOST_WP_H

#include <QHash>
#include <QString>

#include <kalburator/calendar/isynchost.h>

namespace Kalburator::Sync {
class SyncBackend;
}

namespace WildPalms::Runtime {

class SyncConfigStore_WP;

// G.9.a impl of Kalburator::Sync::ISyncHost with narrowed interface.
class SyncHost_WP : public Kalburator::Sync::ISyncHost
{
public:
    explicit SyncHost_WP(SyncConfigStore_WP *configStore);
    ~SyncHost_WP() override;

    void registerBackend(const QString &id, Kalburator::Sync::SyncBackend *backend);

    // Kalburator::Sync::ISyncHost
    Kalburator::Sync::SyncBackend* backendById(const QString &id) override;
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override;
    Kalburator::Sync::ISyncConfigStore* configStore() override;

    // G.9.a — generic record-change notification
    void recordChanged(const QString &mappingId,
                       const QString &recordId,
                       Kalburator::Sync::ISyncHost::ChangeKind kind) override;

    // Test inspection
    int recordChangedCount() const { return m_recordChangedCount; }

private:
    SyncConfigStore_WP *m_configStore;
    QHash<QString, Kalburator::Sync::SyncBackend*> m_backends;

    int m_recordChangedCount = 0;
};

} // namespace WildPalms::Runtime

#endif
