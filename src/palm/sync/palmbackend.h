#ifndef WILDPALMS_SYNC_PALMBACKEND_H
#define WILDPALMS_SYNC_PALMBACKEND_H

#include <cstdint>
#include <optional>

#include <QHash>
#include <QList>
#include <QObject>

#include <kalburator/blob/iblobbackend.h>
#include "palmrecord.h"

namespace WildPalms::PalmSync {

class IPalmDatabaseAccess;

/**
 * @brief Kalburator::Sync::IBlobBackend implementation backed by a
 *        Palm device abstraction.
 *
 * One instance is intended to be owned by the application runtime and
 * shared across plugins. Each Palm database is surfaced as a
 * CollectionInfo whose id is "palm:<dbname>" (lowercase, no "DB"
 * suffix; e.g. "palm:memo" for MemoDB). Record IDs are encoded as
 * "palm:<dbname>:<numericId>".
 *
 * Does not own the IPalmDatabaseAccess; caller is responsible for
 * keeping it alive for the backend's lifetime.
 */
class PalmBackend : public QObject, public Kalburator::Sync::IBlobBackend {
    Q_OBJECT
public:
    explicit PalmBackend(IPalmDatabaseAccess *device,
                         QObject *parent = nullptr);
    ~PalmBackend() override;

    // --- Identity ---
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(
        const QString &collectionId) override;
    QString createCollection(
        const Kalburator::Sync::CollectionInfo &info) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(
        const QString &collectionId,
        const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(
        const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override;

    // Palm records are committed individually by the device protocol; the
    // generic batch capability is intentionally unsupported.
    void beginBatch() override {}
    bool commitBatch() override { return false; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // --- ID encoding (exposed for tests and for callers that need to
    //     round-trip between PalmRecord and BackendRecord IDs).   ---
    static QString  encodeRecordId(const QString &dbName,
                                   std::uint32_t recordId);
    static bool     decodeRecordId(const QString &encoded,
                                   QString *dbNameOut,
                                   std::uint32_t *recordIdOut);
    static QString  encodeCollectionId(const QString &dbName);
    static bool     decodeCollectionId(const QString &collectionId,
                                       QString *dbNameOut);

    // --- Palm-level record access (category-aware) ---
    // These bypass BackendRecord (which has no properties map) so that
    // adapters can read/write the category slot directly.
    QList<PalmRecord> loadPalmRecords(const QString &dbName);
    std::optional<PalmRecord> loadPalmRecord(const QString &dbName,
                                             std::uint32_t recordId);
    std::uint32_t createPalmRecord(const QString &dbName,
                                   const PalmRecord &record);
    bool updatePalmRecord(const QString &dbName, const PalmRecord &record);
    /// Category-aware delete that takes the database name explicitly,
    /// avoiding the asymmetric round-trip in decodeRecordId for db names
    /// with internal capitals (e.g. "ToDoDB" -> "palm:todo:N" -> "TodoDB").
    bool deletePalmRecord(const QString &dbName, std::uint32_t recordId);

    /// AppInfo-block accessor. Forwards to IPalmDatabaseAccess; returns
    /// empty QByteArray on missing database or read failure. Used by
    /// plugins (CalendarBackendPlugin) to populate per-database
    /// CategoryMappingStore at session start.
    QByteArray readAppBlock(const QString &dbName) const;

    /// Forwards the device's cheap per-DB change token (for ChangeDetection).
    QString databaseRevision(const QString &dbName) const;

    /// Drop+recreate a Palm-side database. The clobber-sync flow uses
    /// this on the plugin's classic DB name (e.g. "DatebookDB") to
    /// produce an empty target before re-pushing hub data. Idempotent:
    /// if the DB doesn't exist, recreates it; if it does, removes all
    /// records (via IPalmDatabaseAccess::deleteDatabase) and recreates.
    /// Invalidates the per-DB record cache on success.
    /// Returns true on success; false on any failure of the underlying
    /// delete or create call (in which case device state is
    /// indeterminate — caller should treat the mapping as failed).
    bool wipePalmDatabase(const QString &dbName);

    /// Drop cached records for one database (or all if dbName is empty).
    /// Mutators call this automatically for the affected dbName; callers
    /// that bypass this backend to change device state should call it
    /// manually so the next loadPalmRecords sees the truth.
    void invalidateCache(const QString &dbName = QString());

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);

private:
    IPalmDatabaseAccess *m_device = nullptr;

    // Per-database record cache. Mutating ops (create/update/delete) wipe
    // the entry for their database. Cuts N×readAllRecords down to 1×
    // when a plugin walks N virtual sub-collections (palm:contact/0..3,
    // palm:todo/0..3) backed by the same Palm database.
    mutable QHash<QString, QList<PalmRecord>> m_palmRecordsCache;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_SYNC_PALMBACKEND_H
