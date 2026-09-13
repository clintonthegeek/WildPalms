#ifndef WILDPALMS_RUNTIME_SYNCCONFIGSTORE_WP_H
#define WILDPALMS_RUNTIME_SYNCCONFIGSTORE_WP_H

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <kalburator/types/isyncconfigstore.h>
#include <kalburator/types/logicalcalendar.h>
#include <kalburator/types/synctypes.h>

class QSettings;

namespace WildPalms::Runtime {

class SyncConfigStore_WP : public Kalburator::Sync::ISyncConfigStore
{
public:
    explicit SyncConfigStore_WP(QSettings *settings);
    ~SyncConfigStore_WP() override;

    void addLogicalCalendar(const Kalburator::Sync::LogicalCalendar &logCal) override;
    void updateLogicalCalendar(const Kalburator::Sync::LogicalCalendar &logCal) override;
    void removeLogicalCalendar(const QString &logicalCalendarId) override;
    Kalburator::Sync::LogicalCalendar logicalCalendar(const QString &logicalCalendarId) const override;

    QVariantMap backendConfig(const QString &backendId) const override;
    void setBackendConfig(const QString &backendId, const QVariantMap &config);

    bool hasSyncMappings() const override;
    QList<Kalburator::Sync::SyncMapping> syncMappings() const override;
    void setSyncMappings(const QList<Kalburator::Sync::SyncMapping> &mappings);

    void save() override;

    int saveCount() const;

private:
    void loadFromSettings();

    QSettings *m_settings;
    QHash<QString, Kalburator::Sync::LogicalCalendar> m_logicalCalendars;
    QHash<QString, QVariantMap> m_backendConfigs;
    QList<Kalburator::Sync::SyncMapping> m_mappings;
    int m_saveCount = 0;
};

} // namespace WildPalms::Runtime

#endif
