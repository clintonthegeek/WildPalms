#include "palmbackend.h"

#include <QCryptographicHash>

#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include "ipalmdatabaseaccess.h"
#include "palmrecord.h"

namespace WildPalms::PalmSync {

namespace {
constexpr const char kCollectionPrefix[] = "palm:";

QString collectionTypeForDb(const QString &dbName)
{
    if (dbName == QStringLiteral("MemoDB"))     return QStringLiteral("memos");
    if (dbName == QStringLiteral("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QStringLiteral("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QStringLiteral("ToDoDB"))     return QStringLiteral("todos");
    return QStringLiteral("binary");
}

Kalburator::Sync::CollectionInfo makeCollectionInfo(const QString &dbName)
{
    Kalburator::Sync::CollectionInfo info;
    info.id   = PalmBackend::encodeCollectionId(dbName);
    info.name = dbName;
    info.type = collectionTypeForDb(dbName);
    return info;
}

QString hashForData(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

Kalburator::Sync::BackendRecord palmToBackend(const QString &dbName,
                                              const PalmRecord &pr)
{
    Kalburator::Sync::BackendRecord br;
    br.id = PalmBackend::encodeRecordId(dbName, pr.recordId);
    br.type = collectionTypeForDb(dbName);
    br.data = pr.data;
    br.contentHash = hashForData(pr.data);
    br.lastModified = pr.lastModified;
    br.isDeleted = pr.isDeleted();
    return br;
}

PalmRecord backendToPalm(const Kalburator::Sync::BackendRecord &br,
                         std::uint32_t existingId = 0)
{
    PalmRecord pr;
    pr.recordId = existingId;
    pr.data = br.data;
    pr.lastModified = br.lastModified.isValid()
        ? br.lastModified
        : QDateTime::currentDateTimeUtc();
    return pr;
}

} // namespace

PalmBackend::PalmBackend(IPalmDatabaseAccess *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
}

PalmBackend::~PalmBackend() = default;

QString PalmBackend::backendId() const
{
    return QStringLiteral("palm");
}

QString PalmBackend::displayName() const
{
    return QStringLiteral("Palm OS Device");
}

bool PalmBackend::isAvailable() const
{
    return m_device != nullptr;
}

// --- ID encoding ---

QString PalmBackend::encodeCollectionId(const QString &dbName)
{
    // "DatebookDB" -> "palm:datebook", "MemoDB" -> "palm:memo" etc.
    QString bare = dbName;
    if (bare.endsWith(QStringLiteral("DB"))) {
        bare.chop(2);
    }
    return QStringLiteral("%1%2").arg(QString::fromLatin1(kCollectionPrefix),
                                      bare.toLower());
}

bool PalmBackend::decodeCollectionId(const QString &collectionId,
                                     QString *dbNameOut)
{
    const auto prefix = QString::fromLatin1(kCollectionPrefix);
    if (!collectionId.startsWith(prefix)) return false;
    const auto bare = collectionId.mid(prefix.size());
    if (bare.isEmpty()) return false;
    if (bare.contains(QLatin1Char(':'))) return false; // that's a record id
    if (dbNameOut) {
        // Round-trip mapping: "memo" -> "MemoDB". Capitalise the first
        // letter and append "DB".
        QString titled = bare;
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    return true;
}

QString PalmBackend::encodeRecordId(const QString &dbName,
                                    std::uint32_t recordId)
{
    return QStringLiteral("%1:%2").arg(encodeCollectionId(dbName))
                                  .arg(recordId);
}

bool PalmBackend::decodeRecordId(const QString &encoded,
                                 QString *dbNameOut,
                                 std::uint32_t *recordIdOut)
{
    // Format: "palm:<bare>:<numeric>"
    const auto parts = encoded.split(QLatin1Char(':'));
    if (parts.size() != 3) return false;
    if (parts[0] != QStringLiteral("palm")) return false;
    bool ok = false;
    const auto numeric = parts[2].toUInt(&ok);
    if (!ok) return false;
    if (dbNameOut) {
        QString titled = parts[1];
        titled[0] = titled[0].toUpper();
        *dbNameOut = titled + QStringLiteral("DB");
    }
    if (recordIdOut) *recordIdOut = numeric;
    return true;
}

// --- Empty stubs; filled in by Tasks 5-7 ---

QList<Kalburator::Sync::CollectionInfo> PalmBackend::availableCollections()
{
    if (!m_device) return {};
    QList<Kalburator::Sync::CollectionInfo> out;
    for (const auto &dbName : m_device->availableDatabases()) {
        out.append(makeCollectionInfo(dbName));
    }
    return out;
}

Kalburator::Sync::CollectionInfo PalmBackend::collectionInfo(
    const QString &collectionId)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device || !m_device->hasDatabase(dbName)) return {};
    return makeCollectionInfo(dbName);
}

QString PalmBackend::createCollection(
    const Kalburator::Sync::CollectionInfo &info)
{
    QString dbName;
    if (!decodeCollectionId(info.id, &dbName)) return {};
    if (!m_device) return {};
    if (!m_device->createDatabase(dbName)) return {};
    return info.id;
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::loadRecords(
    const QString &collectionId)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    // Reuse the per-database cache so this overload doesn't fight the
    // category-aware loadPalmRecords() for cache slots.
    const auto records = loadPalmRecords(dbName);
    QList<Kalburator::Sync::BackendRecord> out;
    out.reserve(records.size());
    for (const auto &pr : records) {
        out.append(palmToBackend(dbName, pr));
    }
    return out;
}

std::optional<Kalburator::Sync::BackendRecord> PalmBackend::loadRecord(
    const QString &recordId)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(recordId, &dbName, &numericId)) return std::nullopt;
    if (!m_device) return std::nullopt;

    const auto palm = m_device->readRecord(dbName, numericId);
    if (!palm.has_value()) return std::nullopt;
    return palmToBackend(dbName, *palm);
}

QString PalmBackend::createRecord(
    const QString &collectionId,
    const Kalburator::Sync::BackendRecord &record)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    PalmRecord pr = backendToPalm(record);
    const auto newId = m_device->createRecord(dbName, pr);
    if (newId == 0) return {};
    m_palmRecordsCache.remove(dbName);
    return encodeRecordId(dbName, newId);
}

bool PalmBackend::updateRecord(const Kalburator::Sync::BackendRecord &record)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(record.id, &dbName, &numericId)) return false;
    if (!m_device) return false;

    PalmRecord pr = backendToPalm(record, numericId);
    const bool ok = m_device->updateRecord(dbName, pr);
    if (ok) m_palmRecordsCache.remove(dbName);
    return ok;
}

bool PalmBackend::deleteRecord(const QString &recordId)
{
    QString dbName;
    std::uint32_t numericId = 0;
    if (!decodeRecordId(recordId, &dbName, &numericId)) return false;
    if (!m_device) return false;
    const bool ok = m_device->deleteRecord(dbName, numericId);
    if (ok) m_palmRecordsCache.remove(dbName);
    return ok;
}

QList<Kalburator::Sync::BackendRecord> PalmBackend::modifiedSince(
    const QString &collectionId, const QDateTime &since)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    QList<Kalburator::Sync::BackendRecord> out;
    for (const auto &pr : m_device->recordsModifiedSince(dbName, since)) {
        out.append(palmToBackend(dbName, pr));
    }
    return out;
}

QStringList PalmBackend::deletedSince(const QString &collectionId,
                                      const QDateTime &since)
{
    QString dbName;
    if (!decodeCollectionId(collectionId, &dbName)) return {};
    if (!m_device) return {};

    QStringList out;
    for (auto id : m_device->recordsDeletedSince(dbName, since)) {
        out.append(encodeRecordId(dbName, id));
    }
    return out;
}

bool PalmBackend::supportsDeleteTracking() const
{
    return m_device && m_device->supportsDeleteTracking();
}

// --- Palm-level record access (category-aware) ---

QList<PalmRecord> PalmBackend::loadPalmRecords(const QString &dbName)
{
    if (!m_device) return {};
    const auto it = m_palmRecordsCache.constFind(dbName);
    if (it != m_palmRecordsCache.constEnd()) return it.value();
    auto records = m_device->readAllRecords(dbName);
    m_palmRecordsCache.insert(dbName, records);
    return records;
}

std::optional<PalmRecord> PalmBackend::loadPalmRecord(const QString &dbName,
                                                       std::uint32_t recordId)
{
    if (!m_device) return std::nullopt;
    return m_device->readRecord(dbName, recordId);
}

std::uint32_t PalmBackend::createPalmRecord(const QString &dbName,
                                             const PalmRecord &record)
{
    if (!m_device) return 0;
    const auto newId = m_device->createRecord(dbName, record);
    if (newId != 0) m_palmRecordsCache.remove(dbName);
    return newId;
}

bool PalmBackend::updatePalmRecord(const QString &dbName, const PalmRecord &record)
{
    if (!m_device) return false;
    const bool ok = m_device->updateRecord(dbName, record);
    if (ok) m_palmRecordsCache.remove(dbName);
    return ok;
}

bool PalmBackend::deletePalmRecord(const QString &dbName, std::uint32_t recordId)
{
    if (!m_device) return false;
    const bool ok = m_device->deleteRecord(dbName, recordId);
    if (ok) m_palmRecordsCache.remove(dbName);
    return ok;
}

QByteArray PalmBackend::readAppBlock(const QString &dbName) const
{
    return m_device ? m_device->readAppBlock(dbName) : QByteArray();
}

QString PalmBackend::databaseRevision(const QString &dbName) const
{
    return m_device ? m_device->databaseRevision(dbName) : QString();
}

bool PalmBackend::wipePalmDatabase(const QString &dbName)
{
    if (!m_device) return false;
    const bool deleted = m_device->deleteDatabase(dbName);
    const bool recreated = m_device->createDatabase(dbName);
    invalidateCache(dbName);
    return deleted && recreated;
}

void PalmBackend::invalidateCache(const QString &dbName)
{
    if (dbName.isEmpty()) m_palmRecordsCache.clear();
    else                  m_palmRecordsCache.remove(dbName);
}

} // namespace WildPalms::PalmSync
