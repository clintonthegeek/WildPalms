#include "syncconfigstore_wp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>

#include <kalburator/typesupport/logicalcalendarjson.h>

namespace {

constexpr auto kLogicalCalendarsGroup = "fullsync/logicalCalendars";
constexpr auto kBackendConfigsGroup   = "fullsync/backendConfigs";
constexpr auto kSyncMappingsKey       = "fullsync/syncMappings";

} // namespace

namespace WildPalms::Runtime {

SyncConfigStore_WP::SyncConfigStore_WP(QSettings *settings)
    : m_settings(settings)
{
    loadFromSettings();
}

SyncConfigStore_WP::~SyncConfigStore_WP() = default;

void SyncConfigStore_WP::loadFromSettings()
{
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup(QString::fromLatin1(kLogicalCalendarsGroup));
    const QStringList logicalKeys = m_settings->childKeys();
    for (const QString &key : logicalKeys) {
        const QByteArray raw = m_settings->value(key).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject()) {
            auto lc = Kalburator::Sync::logicalCalendarFromJson(doc.object());
            if (!lc.id.isEmpty()) {
                m_logicalCalendars.insert(lc.id, lc);
            }
        }
    }
    m_settings->endGroup();

    m_settings->beginGroup(QString::fromLatin1(kBackendConfigsGroup));
    const QStringList backendKeys = m_settings->childKeys();
    for (const QString &key : backendKeys) {
        m_backendConfigs.insert(key, m_settings->value(key).toMap());
    }
    m_settings->endGroup();

    const QByteArray mappingsRaw = m_settings->value(QString::fromLatin1(kSyncMappingsKey)).toByteArray();
    const QJsonDocument mappingsDoc = QJsonDocument::fromJson(mappingsRaw);
    if (mappingsDoc.isArray()) {
        const QJsonArray arr = mappingsDoc.array();
        m_mappings.reserve(arr.size());
        for (const auto &val : arr) {
            if (val.isObject()) {
                m_mappings.append(Kalburator::Sync::syncMappingFromJson(val.toObject()));
            }
        }
    }
}

void SyncConfigStore_WP::addLogicalCalendar(const Kalburator::Sync::LogicalCalendar &logCal)
{
    if (logCal.id.isEmpty()) {
        return;
    }
    m_logicalCalendars.insert(logCal.id, logCal);
}

void SyncConfigStore_WP::updateLogicalCalendar(const Kalburator::Sync::LogicalCalendar &logCal)
{
    if (logCal.id.isEmpty()) {
        return;
    }
    m_logicalCalendars.insert(logCal.id, logCal);
}

void SyncConfigStore_WP::removeLogicalCalendar(const QString &logicalCalendarId)
{
    m_logicalCalendars.remove(logicalCalendarId);
}

Kalburator::Sync::LogicalCalendar SyncConfigStore_WP::logicalCalendar(const QString &logicalCalendarId) const
{
    return m_logicalCalendars.value(logicalCalendarId);
}

QVariantMap SyncConfigStore_WP::backendConfig(const QString &backendId) const
{
    return m_backendConfigs.value(backendId);
}

void SyncConfigStore_WP::setBackendConfig(const QString &backendId, const QVariantMap &config)
{
    if (backendId.isEmpty()) {
        return;
    }
    m_backendConfigs.insert(backendId, config);
}

bool SyncConfigStore_WP::hasSyncMappings() const
{
    return !m_mappings.isEmpty();
}

QList<Kalburator::Sync::SyncMapping> SyncConfigStore_WP::syncMappings() const
{
    return m_mappings;
}

void SyncConfigStore_WP::setSyncMappings(const QList<Kalburator::Sync::SyncMapping> &mappings)
{
    m_mappings = mappings;
}

void SyncConfigStore_WP::save()
{
    ++m_saveCount;
    if (!m_settings) {
        return;
    }

    m_settings->beginGroup(QString::fromLatin1(kLogicalCalendarsGroup));
    m_settings->remove(QString());
    for (auto it = m_logicalCalendars.cbegin(); it != m_logicalCalendars.cend(); ++it) {
        const QJsonDocument doc(Kalburator::Sync::logicalCalendarToJson(it.value()));
        m_settings->setValue(it.key(), doc.toJson(QJsonDocument::Compact));
    }
    m_settings->endGroup();

    m_settings->beginGroup(QString::fromLatin1(kBackendConfigsGroup));
    m_settings->remove(QString());
    for (auto it = m_backendConfigs.cbegin(); it != m_backendConfigs.cend(); ++it) {
        m_settings->setValue(it.key(), it.value());
    }
    m_settings->endGroup();

    QJsonArray mappingsArr;
    for (const auto &m : m_mappings) {
        mappingsArr.append(Kalburator::Sync::syncMappingToJson(m));
    }
    m_settings->setValue(QString::fromLatin1(kSyncMappingsKey),
                         QJsonDocument(mappingsArr).toJson(QJsonDocument::Compact));
    m_settings->sync();
}

int SyncConfigStore_WP::saveCount() const
{
    return m_saveCount;
}

} // namespace WildPalms::Runtime
