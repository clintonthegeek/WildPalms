#ifndef PROFILE_H
#define PROFILE_H

#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonArray>
#include <QObject>

// K.8b T9: accounts subgroup — needs full type for QList<> member
#include <kalburator/typesupport/backendconfiguration.h>

/**
 * @brief Connection mode for Palm device
 *
 * Controls how the connection is managed between operations.
 */
enum class ConnectionMode
{
    /**
     * Keep connection alive using periodic tickle (dlp_GetSysDateTime).
     * Allows multiple operations without reconnecting.
     * Note: Palm screen shows last operation until disconnect.
     */
    KeepAlive,

    /**
     * Disconnect after each operation completes (traditional HotSync).
     * Palm shows "HotSync Complete" and closes cleanly.
     * Requires pressing HotSync button again for next operation.
     */
    DisconnectAfterSync
};

/**
 * @brief Device fingerprint for identifying a specific Palm device
 *
 * A fingerprint uniquely identifies a Palm device using its USB serial number
 * (most reliable), User ID (a 32-bit value set on first sync), and username.
 * This allows us to detect when the wrong device is connected to a profile.
 */
struct DeviceFingerprint
{
    // --- Identity fields (used for matching) ---
    quint32 userId = 0;
    QString userName;
    QString usbSerialNumber;  // USB descriptor serial (e.g. "L0JG14I11398")

    // --- Informational fields (NOT used for identity matching) ---
    QString modelName;         // from CardInfo.name (e.g. "Palm m515")
    QString manufacturer;      // from CardInfo.manufacturer (e.g. "Palm, Inc.")
    quint32 romVersion = 0;    // from SysInfo.romVersion
    QString productId;         // from SysInfo.prodID
    quint64 romSize = 0;       // ROM bytes
    quint64 ramSize = 0;       // total RAM bytes
    quint64 ramFree = 0;       // free RAM bytes (snapshot at last connect)

    bool isValid() const { return userId != 0 || !userName.isEmpty() || !usbSerialNumber.isEmpty(); }
    bool isEmpty() const { return userId == 0 && userName.isEmpty() && usbSerialNumber.isEmpty(); }

    enum class MatchResult { Match, MismatchKnown, Indeterminate };

    MatchResult compare(const DeviceFingerprint &other) const {
        if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty())
            return usbSerialNumber == other.usbSerialNumber
                ? MatchResult::Match : MatchResult::MismatchKnown;
        if (userId != 0 && other.userId != 0)
            return userId == other.userId
                ? MatchResult::Match : MatchResult::MismatchKnown;
        if (!userName.isEmpty() && !other.userName.isEmpty())
            return userName == other.userName
                ? MatchResult::Match : MatchResult::MismatchKnown;
        return MatchResult::Indeterminate;
    }

    struct ComparisonRow { QString label; QString lhs; QString rhs; };

    static QList<ComparisonRow>
    comparisonRows(const DeviceFingerprint &expected,
                   const DeviceFingerprint &connected) {
        auto orDash = [](const QString &s) {
            return s.isEmpty() ? QStringLiteral("—") : s;
        };
        auto idStr = [](quint32 id) {
            return id == 0 ? QString() : QString::number(id);
        };
        QList<ComparisonRow> out;
        out.append({ QObject::tr("Serial"),
                     orDash(expected.usbSerialNumber),
                     orDash(connected.usbSerialNumber) });
        out.append({ QObject::tr("User"),
                     orDash(expected.userName),
                     orDash(connected.userName) });
        out.append({ QObject::tr("User ID"),
                     orDash(idStr(expected.userId)),
                     orDash(idStr(connected.userId)) });
        out.append({ QObject::tr("Model"),
                     orDash(expected.modelName),
                     orDash(connected.modelName) });
        return out;
    }

    // Match another fingerprint (USB serial takes priority, then userId, then userName)
    bool matches(const DeviceFingerprint &other) const {
        // USB serial number is the most reliable identifier
        if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty()) {
            return usbSerialNumber == other.usbSerialNumber;
        }
        if (userId != 0 && other.userId != 0) {
            return userId == other.userId;
        }
        return !userName.isEmpty() && userName == other.userName;
    }

    // Create a display string for the fingerprint
    // Uses model name as primary label when available
    QString displayString() const {
        if (isEmpty()) return QString();

        // Build identity suffix: "Clinton, ID: 12345"
        QString identity;
        if (!userName.isEmpty() && userId != 0) {
            identity = QString("%1, ID: %2").arg(userName).arg(userId);
        } else if (!userName.isEmpty()) {
            identity = userName;
        } else if (userId != 0) {
            identity = QString("ID: %1").arg(userId);
        }

        // Use model name as primary label if available
        if (!modelName.isEmpty()) {
            if (identity.isEmpty()) return modelName;
            return QString("%1 (%2)").arg(modelName, identity);
        }

        // Fallback: identity-only display
        if (!usbSerialNumber.isEmpty()) {
            if (identity.isEmpty()) return QString("S/N: %1").arg(usbSerialNumber);
            return QString("%1 (S/N: %2)").arg(identity, usbSerialNumber);
        }
        return identity;
    }

    // Format Palm OS version from romVersion field (e.g. "5.2.1")
    QString palmOSVersionString() const {
        if (romVersion == 0) return QString();
        int major = (romVersion >> 16) & 0xFF;
        int minor = (romVersion >> 8) & 0xFF;
        int patch = romVersion & 0xFF;
        if (patch == 0) return QString("%1.%2").arg(major).arg(minor);
        return QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
    }

    // Format a byte count as a human-readable size (e.g. "16 MB")
    static QString formatMemorySize(quint64 bytes) {
        if (bytes == 0) return QString();
        if (bytes >= 1024 * 1024) return QString("%1 MB").arg(bytes / (1024 * 1024));
        if (bytes >= 1024) return QString("%1 KB").arg(bytes / 1024);
        return QString("%1 B").arg(bytes);
    }

    // Check if extended device info (model/memory/OS) is available
    bool hasExtendedInfo() const {
        return !modelName.isEmpty() || romVersion != 0 || ramSize != 0;
    }

};

/**
 * @brief Profile represents a sync profile with its settings
 *
 * Profile settings are stored in the sync folder itself as .wildpalms.conf,
 * making profiles portable - you can move the entire sync folder and the
 * settings travel with it.
 *
 * Each profile corresponds to:
 *   - A specific Palm device (identified by fingerprint)
 *   - A sync folder with memos/, contacts/, calendar/, todos/
 *   - Device-specific connection settings (port, baud rate)
 */
class Profile
{
public:
    /**
     * @brief Create a profile for the given sync folder path
     * @param syncFolderPath Path to the sync folder (e.g., ~/PalmSync)
     */
    explicit Profile(const QString &syncFolderPath = QString());

    // Profile location
    QString syncFolderPath() const { return m_syncFolderPath; }
    void setSyncFolderPath(const QString &path);

    // Profile identity
    QString name() const;
    void setName(const QString &name);

    /// Sticky profile id — matches the on-disk directory basename.
    /// Populated by load() from profile.conf:[profile]/id; falls
    /// back to the directory basename if the key is missing or load
    /// hasn't been called.
    QString id() const;

    /// Default path for a fresh profile with the given id under
    /// ~/.wildpalms/<id>. Static helper used by ProfileRegistry
    /// and tests.
    static QString defaultPathForId(const QString &id);

    /// Schema version of the loaded profile.conf. 1 for F.1a.
    int schemaVersion() const;

    // Check if profile is valid (folder exists and is writable)
    bool isValid() const;

    // Check if profile config file exists
    bool exists() const;

    // ========== Device Settings ==========

    // Device connection settings
    QString devicePath() const;
    void setDevicePath(const QString &path);

    QString baudRate() const;
    void setBaudRate(const QString &rate);

    // Device fingerprint - identifies which Palm this profile is for
    DeviceFingerprint deviceFingerprint() const;
    void setDeviceFingerprint(const DeviceFingerprint &fingerprint);

    // Check if this profile has a registered device
    bool hasRegisteredDevice() const;

    // Connection mode - how to manage connection between operations
    ConnectionMode connectionMode() const;
    void setConnectionMode(ConnectionMode mode);

    // Auto-sync after connection
    bool autoSyncOnConnect() const;
    void setAutoSyncOnConnect(bool enabled);

    // Default sync type for auto-sync
    QString defaultSyncType() const;  // "hotsync" or "fullsync"
    void setDefaultSyncType(const QString &type);

    // Last sync timestamp (overall, not per-conduit)
    QDateTime lastSyncTime() const;
    void setLastSyncTime(const QDateTime &time);

    // ========== Sync Settings ==========

    // Conflict resolution policy (legacy - maps to autoResolve)
    QString conflictPolicy() const;
    void setConflictPolicy(const QString &policy);

    // Auto-resolve strategy: "none", "palm_wins", "pc_wins", "newer_wins", "older_wins", "duplicate"
    QString conflictAutoResolve() const;
    void setConflictAutoResolve(const QString &strategy);

    // Fallback behavior: "defer", "skip", "use_default", "abort"
    QString conflictFallback() const;
    void setConflictFallback(const QString &fallback);

    // Prompt strategy: "always_ask", "first_only", "batch_at_end"
    QString conflictPromptStrategy() const;
    void setConflictPromptStrategy(const QString &strategy);

    // Connection behavior: "keep_alive", "disconnect_and_defer", "timeout_and_defer"
    QString conflictConnectionBehavior() const;
    void setConflictConnectionBehavior(const QString &behavior);

    // Timeout in seconds for interactive conflict resolution
    int conflictTimeoutSeconds() const;
    void setConflictTimeoutSeconds(int seconds);

    // ========== Sync Mappings (G.7 Task 54) ==========

    // Raw JSON form of the SyncMapping list. Callers that need the parsed
    // SyncMapping types (which require Kalburator headers) can use
    // WildPalmsSyncMappingHelper::parseMappings(profile.syncMappingsJson()).
    QJsonArray syncMappingsJson() const;
    void setSyncMappingsJson(const QJsonArray &json);

    // ========== Category slot snapshot persistence (F.3 T1) =========

    /// F.3: Category slot snapshot persistence.
    ///
    /// Returns an empty QStringList (size 0) if no snapshot has been stored
    /// for `dbName`. Otherwise returns exactly 16 entries indexed by slot
    /// (0..15). Slot 0 is always returned as "Unfiled" (forced even if the
    /// stored value is empty). Empty string at any other index means the
    /// slot is unnamed/absent.
    QStringList categorySlotNames(const QString &dbName) const;

    /// Set the category slot snapshot for `dbName`. `names` must have
    /// exactly 16 entries (slot 0..15). Writes to [categories/<dbName>]
    /// in profile.conf and calls QSettings::sync() before returning — does
    /// NOT invoke Profile::save() (categories live outside the in-memory
    /// state Profile::save would write).
    void setCategorySlotNames(const QString &dbName, const QStringList &names);

    /// Substrate A3/A4: the category names the configuration WANTS on the
    /// device for this database (<=15; Unfiled is implicit at slot 0). The
    /// reconciler binds them to slots at device connect, writing AppInfo for
    /// missing ones. Distinct from categorySlotNames() (the last-seen on-device
    /// snapshot). Stored in [desiredCategories/<dbName>] of profile.conf.
    QStringList desiredCategoryNames(const QString &dbName) const;
    void setDesiredCategoryNames(const QString &dbName, const QStringList &names);

    /// Substrate A4: set by the wizard (sub-project B) when a freshly created
    /// profile's first sync must clobber the Palm; cleared after it runs.
    bool initialSyncPending() const;
    void setInitialSyncPending(bool pending);

    // ========== Accounts (K.8b T9: replaces .wildpalms.providers sidecar) =========

    QList<Kalburator::Sync::BackendConfiguration> accounts() const;
    void saveAccount(const Kalburator::Sync::BackendConfiguration &cfg);
    void removeAccount(const QString &id);
    void setAccounts(const QList<Kalburator::Sync::BackendConfiguration> &list);

    // ========== Persistence ==========

    // Load settings from .wildpalms.conf in the sync folder
    bool load();

    // Save settings to .wildpalms.conf in the sync folder
    bool save();

    // Initialize a new profile (create directories and default config)
    bool initialize();

    // Get the path to the state directory
    QString stateDirectoryPath() const;

    // Get the path to the install folder (for .prc/.pdb files to install)
    QString installFolderPath() const;

private:
    QString m_syncFolderPath;
    QString m_name;
    QString m_id;
    int     m_schemaVersion = 1;

    // Device settings
    QString m_devicePath;
    QString m_baudRate;
    DeviceFingerprint m_deviceFingerprint;
    ConnectionMode m_connectionMode = ConnectionMode::KeepAlive;
    bool m_autoSyncOnConnect = false;
    QString m_defaultSyncType = "hotsync";

    // Sync metadata
    QDateTime m_lastSyncTime;

    // Sync settings
    QString m_conflictPolicy;
    QString m_conflictAutoResolve = "none";
    QString m_conflictFallback = "defer";
    QString m_conflictPromptStrategy = "always_ask";
    QString m_conflictConnectionBehavior = "keep_alive";
    int m_conflictTimeoutSeconds = 60;
    QJsonArray m_syncMappingsJson;

    // K.8b T9: accounts (replaces .wildpalms.providers sidecar)
    QList<Kalburator::Sync::BackendConfiguration> m_accounts;

    bool saveProfileConf() const;
    bool saveAccountsConf() const;
    bool saveMappingsConf() const;

    bool loadProfileConf();
    bool loadAccountsConf();
    bool loadMappingsConf();

    QString sanitizeKConfigGroupId(const QString &id) const;

    // Default values
    static const QString DEFAULT_CONFLICT_POLICY;
    static const QString DEFAULT_DEVICE_PATH;
    static const QString DEFAULT_BAUD_RATE;

};

#endif // PROFILE_H
