#include "profile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QDateTime>

#include <kalburator/typesupport/backendconfiguration.h>

const QString Profile::DEFAULT_CONFLICT_POLICY = "ask";
const QString Profile::DEFAULT_DEVICE_PATH = "/dev/ttyUSB0";
const QString Profile::DEFAULT_BAUD_RATE = "115200";


Profile::Profile(const QString &syncFolderPath)
    : m_syncFolderPath(syncFolderPath)
    , m_devicePath(DEFAULT_DEVICE_PATH)
    , m_baudRate(DEFAULT_BAUD_RATE)
    , m_conflictPolicy(DEFAULT_CONFLICT_POLICY)
{
    // Try to load existing settings if path is set
    if (!m_syncFolderPath.isEmpty()) {
        load();
    }
}

void Profile::setSyncFolderPath(const QString &path)
{
    m_syncFolderPath = path;
}

QString Profile::name() const
{
    if (!m_name.isEmpty()) {
        return m_name;
    }
    // Default to folder name
    if (!m_syncFolderPath.isEmpty()) {
        return QFileInfo(m_syncFolderPath).fileName();
    }
    return QString();
}

void Profile::setName(const QString &name)
{
    m_name = name;
}

QString Profile::id() const
{
    return m_id;
}

int Profile::schemaVersion() const
{
    return m_schemaVersion;
}

QString Profile::defaultPathForId(const QString &id)
{
    return QDir::homePath() + QStringLiteral("/.wildpalms/") + id;
}

bool Profile::isValid() const
{
    if (m_syncFolderPath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_syncFolderPath);
    return info.exists() && info.isDir() && info.isWritable();
}

bool Profile::exists() const
{
    if (m_syncFolderPath.isEmpty())
        return false;
    return QFile::exists(m_syncFolderPath + QStringLiteral("/profile.conf"));
}

// ========== Device Settings ==========

QString Profile::devicePath() const
{
    return m_devicePath;
}

void Profile::setDevicePath(const QString &path)
{
    m_devicePath = path;
}

QString Profile::baudRate() const
{
    return m_baudRate;
}

void Profile::setBaudRate(const QString &rate)
{
    m_baudRate = rate;
}

DeviceFingerprint Profile::deviceFingerprint() const
{
    return m_deviceFingerprint;
}

void Profile::setDeviceFingerprint(const DeviceFingerprint &fingerprint)
{
    m_deviceFingerprint = fingerprint;
}

bool Profile::hasRegisteredDevice() const
{
    return m_deviceFingerprint.isValid();
}

ConnectionMode Profile::connectionMode() const
{
    return m_connectionMode;
}

void Profile::setConnectionMode(ConnectionMode mode)
{
    m_connectionMode = mode;
}

bool Profile::autoSyncOnConnect() const
{
    return m_autoSyncOnConnect;
}

void Profile::setAutoSyncOnConnect(bool enabled)
{
    m_autoSyncOnConnect = enabled;
}

QString Profile::defaultSyncType() const
{
    return m_defaultSyncType;
}

void Profile::setDefaultSyncType(const QString &type)
{
    m_defaultSyncType = type;
}

QDateTime Profile::lastSyncTime() const
{
    return m_lastSyncTime;
}

void Profile::setLastSyncTime(const QDateTime &time)
{
    m_lastSyncTime = time;
}

// ========== Sync Settings ==========

QString Profile::conflictPolicy() const
{
    return m_conflictPolicy;
}

void Profile::setConflictPolicy(const QString &policy)
{
    m_conflictPolicy = policy;
}

QString Profile::conflictAutoResolve() const
{
    return m_conflictAutoResolve;
}

void Profile::setConflictAutoResolve(const QString &strategy)
{
    m_conflictAutoResolve = strategy;
}

QString Profile::conflictFallback() const
{
    return m_conflictFallback;
}

void Profile::setConflictFallback(const QString &fallback)
{
    m_conflictFallback = fallback;
}

QString Profile::conflictPromptStrategy() const
{
    return m_conflictPromptStrategy;
}

void Profile::setConflictPromptStrategy(const QString &strategy)
{
    m_conflictPromptStrategy = strategy;
}

QString Profile::conflictConnectionBehavior() const
{
    return m_conflictConnectionBehavior;
}

void Profile::setConflictConnectionBehavior(const QString &behavior)
{
    m_conflictConnectionBehavior = behavior;
}

int Profile::conflictTimeoutSeconds() const
{
    return m_conflictTimeoutSeconds;
}

void Profile::setConflictTimeoutSeconds(int seconds)
{
    m_conflictTimeoutSeconds = qBound(15, seconds, 300);
}

bool Profile::load()
{
    if (m_syncFolderPath.isEmpty())
        return false;
    QDir dir(m_syncFolderPath);
    if (!dir.exists())
        return false;

    if (!loadProfileConf())  return false;
    if (!loadAccountsConf()) return false;
    if (!loadMappingsConf()) return false;
    return true;
}

bool Profile::loadProfileConf()
{
    const QString path = m_syncFolderPath + QStringLiteral("/profile.conf");
    if (!QFile::exists(path))
        return false;

    QSettings s(path, QSettings::IniFormat);

    m_schemaVersion = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (m_schemaVersion != 1) {
        qWarning() << "[Profile] unknown schemaVersion" << m_schemaVersion
                   << "in" << path;
        return false;
    }

    m_id = s.value(QStringLiteral("profile/id")).toString();
    if (m_id.isEmpty())
        m_id = QFileInfo(m_syncFolderPath).fileName();
    m_name = s.value(QStringLiteral("profile/name")).toString();

    m_devicePath = s.value(QStringLiteral("device/path"),
                            DEFAULT_DEVICE_PATH).toString();
    m_baudRate = s.value(QStringLiteral("device/baudRate"),
                          DEFAULT_BAUD_RATE).toString();

    const QString modeStr =
        s.value(QStringLiteral("device/connectionMode"),
                QStringLiteral("keepalive")).toString();
    m_connectionMode = (modeStr == QStringLiteral("disconnect"))
        ? ConnectionMode::DisconnectAfterSync
        : ConnectionMode::KeepAlive;

    m_autoSyncOnConnect = s.value(QStringLiteral("device/autoSyncOnConnect"),
                                   false).toBool();
    m_defaultSyncType   = s.value(QStringLiteral("device/defaultSyncType"),
                                   QStringLiteral("hotsync")).toString();

    m_deviceFingerprint.userId = s.value(
        QStringLiteral("device/userId"), 0).toUInt();
    m_deviceFingerprint.userName = s.value(
        QStringLiteral("device/userName")).toString();
    m_deviceFingerprint.usbSerialNumber = s.value(
        QStringLiteral("device/usbSerialNumber")).toString();
    m_deviceFingerprint.modelName = s.value(
        QStringLiteral("device/modelName")).toString();
    m_deviceFingerprint.manufacturer = s.value(
        QStringLiteral("device/manufacturer")).toString();
    m_deviceFingerprint.romVersion = s.value(
        QStringLiteral("device/romVersion"), 0).toUInt();
    m_deviceFingerprint.productId = s.value(
        QStringLiteral("device/productId")).toString();
    m_deviceFingerprint.romSize = s.value(
        QStringLiteral("device/romSize"), 0).toULongLong();
    m_deviceFingerprint.ramSize = s.value(
        QStringLiteral("device/ramSize"), 0).toULongLong();
    m_deviceFingerprint.ramFree = s.value(
        QStringLiteral("device/ramFree"), 0).toULongLong();

    const QString lastSyncStr = s.value(
        QStringLiteral("sync/lastSyncTime")).toString();
    m_lastSyncTime = lastSyncStr.isEmpty()
        ? QDateTime()
        : QDateTime::fromString(lastSyncStr, Qt::ISODate);

    m_conflictPolicy = s.value(QStringLiteral("sync/conflictPolicy"),
                                DEFAULT_CONFLICT_POLICY).toString();
    m_conflictAutoResolve = s.value(
        QStringLiteral("sync/conflictAutoResolve"),
        QStringLiteral("none")).toString();
    m_conflictFallback = s.value(
        QStringLiteral("sync/conflictFallback"),
        QStringLiteral("defer")).toString();
    m_conflictPromptStrategy = s.value(
        QStringLiteral("sync/conflictPromptStrategy"),
        QStringLiteral("always_ask")).toString();
    m_conflictConnectionBehavior = s.value(
        QStringLiteral("sync/conflictConnectionBehavior"),
        QStringLiteral("keep_alive")).toString();
    m_conflictTimeoutSeconds = s.value(
        QStringLiteral("sync/conflictTimeoutSeconds"), 60).toInt();

    return true;
}

bool Profile::loadAccountsConf()
{
    m_accounts.clear();
    const QString path = m_syncFolderPath + QStringLiteral("/accounts.conf");
    if (!QFile::exists(path))
        return true;   // empty accounts is valid.

    QSettings s(path, QSettings::IniFormat);
    const int v = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (v != 1) {
        qWarning() << "[Profile] unknown accounts schemaVersion" << v;
        return false;
    }

    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("account-"))) continue;
        const QString id = g.mid(QStringLiteral("account-").size());
        if (id.isEmpty()) continue;

        Kalburator::Sync::BackendConfiguration bc;
        bc.id          = id;
        bc.type        = s.value(g + QStringLiteral("/type")).toString();
        bc.displayName = s.value(g + QStringLiteral("/displayName")).toString();
        bc.enabled     = s.value(g + QStringLiteral("/enabled"), true).toBool();

        s.beginGroup(g + QStringLiteral("/params"));
        const QStringList paramKeys = s.childKeys();
        for (const QString &k : paramKeys) {
            bc.connectionParams[k] = s.value(k);
        }
        s.endGroup();

        m_accounts.append(bc);
    }
    return true;
}

bool Profile::loadMappingsConf()
{
    m_syncMappingsJson = QJsonArray{};
    const QString path = m_syncFolderPath + QStringLiteral("/mappings.conf");
    if (!QFile::exists(path))
        return true;   // empty mappings is valid.

    QSettings s(path, QSettings::IniFormat);
    const int v = s.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    if (v != 1) {
        qWarning() << "[Profile] unknown mappings schemaVersion" << v;
        return false;
    }

    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(QStringLiteral("mapping-"))) continue;

        QJsonObject obj;
        s.beginGroup(g);
        const QStringList keys = s.childKeys();
        for (const QString &k : keys) {
            const QVariant val = s.value(k);
            if (val.typeId() == QMetaType::Bool) {
                obj[k] = val.toBool();
            } else if (val.typeId() == QMetaType::Int
                       || val.typeId() == QMetaType::Double) {
                obj[k] = val.toDouble();
            } else {
                // Strings may have been JSON-serialised objects/arrays
                // for the embedded case; try to parse, fall back to plain
                // string.
                const QByteArray asUtf8 = val.toString().toUtf8();
                const QJsonDocument doc = QJsonDocument::fromJson(asUtf8);
                if (doc.isObject()) obj[k] = doc.object();
                else if (doc.isArray()) obj[k] = doc.array();
                else obj[k] = val.toString();
            }
        }
        s.endGroup();

        m_syncMappingsJson.append(obj);
    }
    return true;
}

QJsonArray Profile::syncMappingsJson() const { return m_syncMappingsJson; }

void Profile::setSyncMappingsJson(const QJsonArray &json) { m_syncMappingsJson = json; }

// ========== Category slot snapshot persistence (F.3 T1) =========

QStringList Profile::categorySlotNames(const QString &dbName) const
{
    if (dbName.isEmpty() || m_syncFolderPath.isEmpty()) return {};

    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    const QString group = QStringLiteral("categories/") + dbName;
    s.beginGroup(group);
    const QStringList keys = s.childKeys();
    if (keys.isEmpty()) {
        s.endGroup();
        return {};
    }

    QStringList out;
    out.reserve(16);
    for (int slot = 0; slot < 16; ++slot) {
        QString value = s.value(QStringLiteral("slot%1").arg(slot)).toString();
        if (slot == 0 && value.isEmpty())
            value = QStringLiteral("Unfiled");
        out << value;
    }
    s.endGroup();
    return out;
}

void Profile::setCategorySlotNames(const QString &dbName,
                                   const QStringList &names)
{
    if (m_syncFolderPath.isEmpty()) return;
    if (dbName.isEmpty()) {
        qWarning() << "[Profile] setCategorySlotNames: empty dbName";
        return;
    }
    if (names.size() != 16) {
        qWarning() << "[Profile] setCategorySlotNames: expected 16 entries, got"
                   << names.size();
        return;
    }

    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    const QString group = QStringLiteral("categories/") + dbName;
    s.remove(group);                     // clear stale slot keys
    s.beginGroup(group);
    for (int slot = 0; slot < 16; ++slot) {
        const QString &v = names.at(slot);
        if (!v.isEmpty())
            s.setValue(QStringLiteral("slot%1").arg(slot), v);
    }
    s.endGroup();
    s.sync();
}

QStringList Profile::desiredCategoryNames(const QString &dbName) const
{
    if (dbName.isEmpty() || m_syncFolderPath.isEmpty()) return {};

    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    s.beginGroup(QStringLiteral("desiredCategories/") + dbName);
    const QStringList names = s.value(QStringLiteral("names")).toStringList();
    s.endGroup();
    return names;
}

void Profile::setDesiredCategoryNames(const QString &dbName,
                                      const QStringList &names)
{
    if (m_syncFolderPath.isEmpty()) return;
    if (dbName.isEmpty()) {
        qWarning() << "[Profile] setDesiredCategoryNames: empty dbName";
        return;
    }
    // Palm category tables hold 15 user slots (slot 0 is the implicit Unfiled).
    QStringList capped = names;
    while (capped.size() > 15) capped.removeLast();

    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    s.beginGroup(QStringLiteral("desiredCategories/") + dbName);
    s.setValue(QStringLiteral("names"), capped);
    s.endGroup();
    s.sync();
}

bool Profile::initialSyncPending() const
{
    if (m_syncFolderPath.isEmpty()) return false;
    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    return s.value(QStringLiteral("initialSync/pending"), false).toBool();
}

void Profile::setInitialSyncPending(bool pending)
{
    if (m_syncFolderPath.isEmpty()) return;
    QSettings s(m_syncFolderPath + QStringLiteral("/profile.conf"),
                QSettings::IniFormat);
    s.setValue(QStringLiteral("initialSync/pending"), pending);
    s.sync();
}

// ========== Accounts (K.8b T9) ==========

QList<Kalburator::Sync::BackendConfiguration> Profile::accounts() const
{
    return m_accounts;
}

void Profile::saveAccount(const Kalburator::Sync::BackendConfiguration &cfg)
{
    for (auto &existing : m_accounts) {
        if (existing.id == cfg.id) {
            existing = cfg;
            return;
        }
    }
    m_accounts.append(cfg);
}

void Profile::removeAccount(const QString &id)
{
    m_accounts.removeIf([&id](const Kalburator::Sync::BackendConfiguration &c) {
        return c.id == id;
    });
}

void Profile::setAccounts(const QList<Kalburator::Sync::BackendConfiguration> &list)
{
    m_accounts = list;
}

bool Profile::save()
{
    if (m_syncFolderPath.isEmpty())
        return false;

    QDir dir(m_syncFolderPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    if (!saveProfileConf())  return false;
    if (!saveAccountsConf()) return false;
    if (!saveMappingsConf()) return false;
    return true;
}

bool Profile::saveProfileConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/profile.conf");
    QSettings s(path, QSettings::IniFormat);

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    s.setValue(QStringLiteral("profile/id"),
               m_id.isEmpty() ? QFileInfo(m_syncFolderPath).fileName() : m_id);
    s.setValue(QStringLiteral("profile/name"), m_name);

    s.setValue(QStringLiteral("device/path"),    m_devicePath);
    s.setValue(QStringLiteral("device/baudRate"), m_baudRate);
    s.setValue(QStringLiteral("device/connectionMode"),
               m_connectionMode == ConnectionMode::DisconnectAfterSync
                   ? QStringLiteral("disconnect")
                   : QStringLiteral("keepalive"));
    s.setValue(QStringLiteral("device/autoSyncOnConnect"), m_autoSyncOnConnect);
    s.setValue(QStringLiteral("device/defaultSyncType"),    m_defaultSyncType);
    s.setValue(QStringLiteral("device/userId"),           m_deviceFingerprint.userId);
    s.setValue(QStringLiteral("device/userName"),         m_deviceFingerprint.userName);
    s.setValue(QStringLiteral("device/usbSerialNumber"),  m_deviceFingerprint.usbSerialNumber);
    s.setValue(QStringLiteral("device/modelName"),        m_deviceFingerprint.modelName);
    s.setValue(QStringLiteral("device/manufacturer"),     m_deviceFingerprint.manufacturer);
    s.setValue(QStringLiteral("device/romVersion"),       m_deviceFingerprint.romVersion);
    s.setValue(QStringLiteral("device/productId"),        m_deviceFingerprint.productId);
    s.setValue(QStringLiteral("device/romSize"),
               QString::number(m_deviceFingerprint.romSize));
    s.setValue(QStringLiteral("device/ramSize"),
               QString::number(m_deviceFingerprint.ramSize));
    s.setValue(QStringLiteral("device/ramFree"),
               QString::number(m_deviceFingerprint.ramFree));

    if (m_lastSyncTime.isValid())
        s.setValue(QStringLiteral("sync/lastSyncTime"),
                   m_lastSyncTime.toUTC().toString(Qt::ISODate));
    s.setValue(QStringLiteral("sync/conflictPolicy"),          m_conflictPolicy);
    s.setValue(QStringLiteral("sync/conflictAutoResolve"),     m_conflictAutoResolve);
    s.setValue(QStringLiteral("sync/conflictFallback"),        m_conflictFallback);
    s.setValue(QStringLiteral("sync/conflictPromptStrategy"),  m_conflictPromptStrategy);
    s.setValue(QStringLiteral("sync/conflictConnectionBehavior"),
               m_conflictConnectionBehavior);
    s.setValue(QStringLiteral("sync/conflictTimeoutSeconds"),  m_conflictTimeoutSeconds);

    s.sync();
    return s.status() == QSettings::NoError;
}

bool Profile::saveAccountsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/accounts.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();   // F.1a: full rewrite, no merge.

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    for (const auto &a : m_accounts) {
        const QString group = QStringLiteral("account-") + sanitizeKConfigGroupId(a.id);
        s.setValue(group + QStringLiteral("/type"),        a.type);
        s.setValue(group + QStringLiteral("/displayName"), a.displayName);
        s.setValue(group + QStringLiteral("/enabled"),     a.enabled);

        const QString paramsGroup = group + QStringLiteral("/params");
        for (auto it = a.connectionParams.constBegin();
             it != a.connectionParams.constEnd(); ++it) {
            s.setValue(paramsGroup + QStringLiteral("/") + it.key(), it.value());
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}

bool Profile::saveMappingsConf() const
{
    const QString path = m_syncFolderPath + QStringLiteral("/mappings.conf");
    QSettings s(path, QSettings::IniFormat);
    s.clear();

    s.setValue(QStringLiteral("meta/schemaVersion"), m_schemaVersion);

    QSet<QString> usedGroups;
    for (const auto &v : m_syncMappingsJson) {
        if (!v.isObject()) continue;
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;

        QString group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id);
        int n = 2;
        while (usedGroups.contains(group)) {
            group = QStringLiteral("mapping-") + sanitizeKConfigGroupId(id)
                  + QStringLiteral("-") + QString::number(n++);
        }
        usedGroups.insert(group);

        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            const QJsonValue val = it.value();
            if (val.isString())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toString());
            else if (val.isBool())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toBool());
            else if (val.isDouble())
                s.setValue(group + QStringLiteral("/") + it.key(), val.toDouble());
            else {
                const QJsonDocument doc = val.isObject()
                    ? QJsonDocument(val.toObject())
                    : QJsonDocument(val.toArray());
                s.setValue(group + QStringLiteral("/") + it.key(),
                           doc.toJson(QJsonDocument::Compact));
            }
        }
    }

    s.sync();
    return s.status() == QSettings::NoError;
}

QString Profile::sanitizeKConfigGroupId(const QString &id) const
{
    QString out = id;
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.replace(QLatin1Char('['), QLatin1Char('_'));
    out.replace(QLatin1Char(']'), QLatin1Char('_'));
    return out;
}

bool Profile::initialize()
{
    if (m_syncFolderPath.isEmpty()) {
        return false;
    }

    QDir dir(m_syncFolderPath);

    // Create main directory
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return false;
        }
    }

    // Create subdirectories
    dir.mkpath("memos");
    dir.mkpath("contacts");
    dir.mkpath("calendar");
    dir.mkpath("todos");
    dir.mkpath("install");
    dir.mkpath("install/installed");
    dir.mkpath(".state");

    // Save default settings
    return save();
}

QString Profile::stateDirectoryPath() const
{
    if (m_syncFolderPath.isEmpty()) {
        return QString();
    }
    return QDir(m_syncFolderPath).filePath(".state");
}

QString Profile::installFolderPath() const
{
    if (m_syncFolderPath.isEmpty()) {
        return QString();
    }
    return QDir(m_syncFolderPath).filePath("install");
}
