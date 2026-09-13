#ifndef WILDPALMS_PALM_TODO_PALMTODOBACKEND_H
#define WILDPALMS_PALM_TODO_PALMTODOBACKEND_H

#include <kalburator/sync/syncbackendbase.h>

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmToDo {

/**
 * @brief SyncBackend for the Palm ToDoDB (tasks).
 *
 * Native shape: (todo, palm-todo). Records are stored as raw Palm DLP bytes.
 * G.7 Task 52.
 */
class PalmToDoBackend : public Kalburator::Sync::SyncBackendBase
{
    Q_OBJECT
public:
    static constexpr const char *DatabaseName = "ToDoDB";
    static constexpr const char *CollectionId = "palm:todo";

    explicit PalmToDoBackend(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        const QString &deviceId,
        QObject *parent = nullptr);
    ~PalmToDoBackend() override;

    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;
    QString resourceId() const override;

    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    bool loadRecordsOrError(const QString &collectionId,
                            QList<Kalburator::Sync::BackendRecord> &records,
                            QString &error) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;


private:
    static QString encodeRecordId(std::uint32_t palmId);
    static bool decodeRecordId(const QString &encoded, std::uint32_t *palmIdOut);

    WildPalms::PalmSync::IPalmDatabaseAccess *m_device   = nullptr;
    QString                                    m_deviceId;
};

} // namespace WildPalms::PalmToDo

#endif // WILDPALMS_PALM_TODO_PALMTODOBACKEND_H
