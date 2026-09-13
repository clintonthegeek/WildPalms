// K.8b T7: test-only wrapper that presents an IBlobBackend as a SyncBackend.
//
// BlobBackendAdapter was deleted from production code (palmruntime.cpp) in T7.
// Tests that inject a MockBlobBackend via the old registerBlobBackendForTest()
// now use registerBackendInstanceForTest(id, BlobSyncBackendWrapper::wrap(...)).
//
// Usage:
//   auto owned = std::make_unique<MockBlobBackend>();
//   auto *raw  = owned.get();   // keep for post-sync assertions
//   runtime.registerBackendInstanceForTest(id,
//       BlobSyncBackendWrapper::wrap(std::move(owned)));
//
// With explicit shape (for cross-domain mappings like contacts/vcard4):
//   runtime.registerBackendInstanceForTest(id,
//       BlobSyncBackendWrapper::wrap(std::move(owned), myShape));

#pragma once

#include <memory>
#include <optional>

#include <kalburator/calendar/syncbackend.h>
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/shape/shape.h>

namespace WildPalmsTest {

// Note: no Q_OBJECT — this header is included in multiple TUs and the
// wrapper needs no signals/slots of its own.
class BlobSyncBackendWrapper final : public Kalburator::Sync::SyncBackend
{
public:
    // ── factory ────────────────────────────────────────────────────────────
    /// Default shape: (blob, raw) — matches the identity pipeline that
    /// dispatchBlobSync compiles for blob-domain backends.
    static std::unique_ptr<BlobSyncBackendWrapper>
    wrap(std::unique_ptr<Kalburator::Sync::IBlobBackend> blob,
         const QString &id,
         QObject *parent = nullptr)
    {
        const Kalburator::Shape::Shape defaultShape{
            Kalburator::Shape::DomainId{QStringLiteral("blob")},
            Kalburator::Shape::EncodingId{QStringLiteral("raw")}
        };
        return std::unique_ptr<BlobSyncBackendWrapper>(
            new BlobSyncBackendWrapper(std::move(blob), id, defaultShape, parent));
    }

    /// Explicit shape — for cross-domain mappings (e.g. contacts/vcard4).
    static std::unique_ptr<BlobSyncBackendWrapper>
    wrap(std::unique_ptr<Kalburator::Sync::IBlobBackend> blob,
         const QString &id,
         const Kalburator::Shape::Shape &shape,
         QObject *parent = nullptr)
    {
        return std::unique_ptr<BlobSyncBackendWrapper>(
            new BlobSyncBackendWrapper(std::move(blob), id, shape, parent));
    }

    // ── SyncBackend identity ────────────────────────────────────────────────
    QString backendType() const override { return m_id; }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {m_shape}; }

    // ── IBlobBackend identity ───────────────────────────────────────────────
    QString backendId()   const override { return m_id; }
    QString displayName() const override { return m_blob->displayName(); }
    bool    isAvailable() const override { return m_blob->isAvailable(); }

    // ── IBlobBackend collections ────────────────────────────────────────────
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override
        { return m_blob->availableCollections(); }
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &id) override
        { return m_blob->collectionInfo(id); }
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override
        { return m_blob->createCollection(info); }

    // ── IBlobBackend records ────────────────────────────────────────────────
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &colId) override
        { return m_blob->loadRecords(colId); }
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &id) override
        { return m_blob->loadRecord(id); }
    QString createRecord(const QString &colId,
                         const Kalburator::Sync::BackendRecord &rec) override
        { return m_blob->createRecord(colId, rec); }
    bool updateRecord(const Kalburator::Sync::BackendRecord &rec) override
        { return m_blob->updateRecord(rec); }
    bool deleteRecord(const QString &id) override
        { return m_blob->deleteRecord(id); }

    // ── IBlobBackend change detection ───────────────────────────────────────
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &colId, const QDateTime &dt) override
        { return m_blob->modifiedSince(colId, dt); }
    QStringList deletedSince(const QString &colId, const QDateTime &dt) override
        { return m_blob->deletedSince(colId, dt); }
    bool supportsDeleteTracking() const override
        { return m_blob->supportsDeleteTracking(); }

private:
    BlobSyncBackendWrapper(std::unique_ptr<Kalburator::Sync::IBlobBackend> blob,
                           const QString &id,
                           const Kalburator::Shape::Shape &shape,
                           QObject *parent)
        : Kalburator::Sync::SyncBackend(parent)
        , m_blob(std::move(blob))
        , m_id(id)
        , m_shape(shape)
    {}

    std::unique_ptr<Kalburator::Sync::IBlobBackend> m_blob;
    QString m_id;
    Kalburator::Shape::Shape m_shape;
};

} // namespace WildPalmsTest
