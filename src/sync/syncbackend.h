#ifndef SYNCBACKEND_H
#define SYNCBACKEND_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QVariant>
#include <kalburator/types/collectioninfo.h>

namespace Sync {

using CollectionInfo = Kalburator::Sync::CollectionInfo;

/**
 * @brief Abstract record for backend storage
 *
 * Represents a single record in a backend (file, CalDAV item, etc.)
 * Backends convert between this and their native format.
 */
class BackendRecord
{
public:
    virtual ~BackendRecord() = default;

    QString id;             ///< Unique identifier (file path, UID, etc.)
    QString type;           ///< Record type: "memo", "contact", "event", "todo"
    QString displayName;    ///< Human-readable name for filenames/display
    QByteArray data;        ///< Raw data content
    QString contentHash;    ///< Hash of data for change detection
    QDateTime lastModified;
    bool isDeleted = false;

    /**
     * @brief Get a displayable description of this record
     */
    virtual QString description() const {
        if (!displayName.isEmpty()) return displayName;
        return id;
    }
};

/**
 * @brief Abstract interface for sync backends
 *
 * A backend is responsible for storing and retrieving data in a specific
 * format/location. Examples:
 *   - LocalFileBackend: Files on disk (iCal, vCard, Markdown)
 *   - CalDAVBackend: CalDAV/CardDAV servers
 *   - AkonadiBackend: KDE Akonadi datastore
 *
 * Designed for compatibility with PlanStanLite's backend system.
 */
class SyncBackend : public QObject
{
    Q_OBJECT

public:
    explicit SyncBackend(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~SyncBackend() = default;

    // ========== Backend Identity ==========

    /**
     * @brief Unique identifier for this backend type
     *
     * Examples: "local-file", "caldav", "akonadi"
     */
    virtual QString backendId() const = 0;

    /**
     * @brief Human-readable name for display
     */
    virtual QString displayName() const = 0;

    /**
     * @brief Check if backend is available and configured
     */
    virtual bool isAvailable() const = 0;

    // ========== Collection Management ==========

    /**
     * @brief List available collections (folders, calendars, etc.)
     */
    virtual QList<CollectionInfo> availableCollections() = 0;

    /**
     * @brief Get info about a specific collection
     */
    virtual CollectionInfo collectionInfo(const QString &collectionId) = 0;

    /**
     * @brief Create a new collection
     * @return Collection ID on success, empty on failure
     */
    virtual QString createCollection(const CollectionInfo &info) = 0;

    // ========== Record Operations ==========

    /**
     * @brief Load all records from a collection
     * @param collectionId Which collection to load
     * @return List of records (caller takes ownership)
     */
    virtual QList<BackendRecord*> loadRecords(const QString &collectionId) = 0;

    /**
     * @brief Load a single record by ID
     * @return Record or nullptr if not found (caller takes ownership)
     */
    virtual BackendRecord* loadRecord(const QString &recordId) = 0;

    /**
     * @brief Create a new record
     * @param collectionId Which collection to add to
     * @param record Record to create
     * @return Assigned record ID on success, empty on failure
     */
    virtual QString createRecord(const QString &collectionId,
                                  const BackendRecord &record) = 0;

    /**
     * @brief Update an existing record
     * @param record Record with updated data (id must be set)
     * @return true on success
     */
    virtual bool updateRecord(const BackendRecord &record) = 0;

    /**
     * @brief Delete a record
     * @param recordId Record to delete
     * @return true on success
     */
    virtual bool deleteRecord(const QString &recordId) = 0;

    // ========== Change Detection ==========

    /**
     * @brief Get records modified since a given time
     *
     * For efficient sync - only fetch changed items.
     */
    virtual QList<BackendRecord*> modifiedSince(const QString &collectionId,
                                                 const QDateTime &since) = 0;

    /**
     * @brief Get list of deleted record IDs since last sync
     *
     * Not all backends support this (file-based can't track deletions).
     * Return empty list if unsupported.
     */
    virtual QStringList deletedSince(const QString &collectionId,
                                      const QDateTime &since) = 0;

    /**
     * @brief Check if backend tracks deletions
     */
    virtual bool supportsDeleteTracking() const { return false; }

    // ========== Batch Operations ==========

    /**
     * @brief Begin a batch operation (for atomic commits)
     *
     * Not all backends support this. Default implementation does nothing.
     */
    virtual void beginBatch() {}

    /**
     * @brief Commit batch operations
     * @return true on success
     */
    virtual bool commitBatch() { return true; }

    /**
     * @brief Rollback batch operations
     */
    virtual void rollbackBatch() {}

    /**
     * @brief Check if backend supports batch/transaction operations
     */
    virtual bool supportsBatch() const { return false; }

signals:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
};

} // namespace Sync

#endif // SYNCBACKEND_H
