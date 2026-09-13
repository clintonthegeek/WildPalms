#include "palmcontactsbackend.h"

#include <QCryptographicHash>

#include <kalburator/types/collectioninfo.h>
#include "ipalmdatabaseaccess.h"
#include "palmrecord.h"

namespace WildPalms::PalmContacts {

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using WildPalms::PalmSync::IPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

PalmContactsBackend::PalmContactsBackend(IPalmDatabaseAccess *device,
                                         const QString &deviceId,
                                         QObject *parent)
    : Kalburator::Sync::SyncBackendBase(parent)
    , m_device(device)
    , m_deviceId(deviceId)
{
}

PalmContactsBackend::~PalmContactsBackend() = default;

QString PalmContactsBackend::backendType() const
{
    return QStringLiteral("palm-contacts");
}

QList<Shape> PalmContactsBackend::nativeShapes() const
{
    return { Shape{ DomainId{QStringLiteral("contacts")},
                    EncodingId{QStringLiteral("palm-address")} } };
}

QString PalmContactsBackend::resourceId() const
{
    return QStringLiteral("palm-device:") + m_deviceId;
}

QString PalmContactsBackend::encodeRecordId(std::uint32_t palmId)
{
    return QStringLiteral("palm:address:%1").arg(palmId);
}

bool PalmContactsBackend::decodeRecordId(const QString &encoded, std::uint32_t *palmIdOut)
{
    static const QString prefix = QStringLiteral("palm:address:");
    if (!encoded.startsWith(prefix)) return false;
    bool ok = false;
    *palmIdOut = encoded.mid(prefix.size()).toUInt(&ok);
    return ok;
}

QList<BackendRecord> PalmContactsBackend::loadRecords(const QString &collectionId)
{
    if (collectionId != QLatin1String(CollectionId) || !m_device)
        return {};

    const auto palmRecords = m_device->readAllRecords(QLatin1String(DatabaseName));
    QList<BackendRecord> result;
    result.reserve(palmRecords.size());
    for (const PalmRecord &pr : palmRecords) {
        if (pr.isDeleted()) continue;
        BackendRecord r;
        r.id           = encodeRecordId(pr.recordId);
        r.type         = QStringLiteral("contact");
        r.data         = pr.data;
        r.contentHash  = QString::fromLatin1(
            QCryptographicHash::hash(pr.data, QCryptographicHash::Sha256).toHex());
        r.lastModified = pr.lastModified;
        result.append(std::move(r));
    }
    return result;
}

bool PalmContactsBackend::loadRecordsOrError(const QString &collectionId,
                                             QList<BackendRecord> &records,
                                             QString &error)
{
    records.clear();
    error.clear();

    if (collectionId != QLatin1String(CollectionId)) {
        error = QStringLiteral("unknown collection: %1").arg(collectionId);
        return false;
    }
    if (!m_device) {
        error = QStringLiteral("backend not ready (no device)");
        return false;
    }
    if (!m_device->isConnected()) {
        error = QStringLiteral("Palm link not connected before reading %1")
                  .arg(QLatin1String(DatabaseName));
        return false;
    }

    const auto palmRecords = m_device->readAllRecords(QLatin1String(DatabaseName));
    if (!m_device->isConnected()) {
        error = QStringLiteral("Palm link lost while reading %1")
                  .arg(QLatin1String(DatabaseName));
        return false;
    }

    records.reserve(palmRecords.size());
    for (const PalmRecord &pr : palmRecords) {
        if (pr.isDeleted()) continue;
        BackendRecord r;
        r.id           = encodeRecordId(pr.recordId);
        r.type         = QStringLiteral("contact");
        r.data         = pr.data;
        r.contentHash  = QString::fromLatin1(
            QCryptographicHash::hash(pr.data, QCryptographicHash::Sha256).toHex());
        r.lastModified = pr.lastModified;
        records.append(std::move(r));
    }
    return true;
}

std::optional<BackendRecord> PalmContactsBackend::loadRecord(const QString &recordId)
{
    if (!m_device) return std::nullopt;
    std::uint32_t palmId = 0;
    if (!decodeRecordId(recordId, &palmId)) return std::nullopt;

    auto pr = m_device->readRecord(QLatin1String(DatabaseName), palmId);
    if (!pr) return std::nullopt;

    BackendRecord r;
    r.id           = recordId;
    r.type         = QStringLiteral("contact");
    r.data         = pr->data;
    r.contentHash  = QString::fromLatin1(
        QCryptographicHash::hash(pr->data, QCryptographicHash::Sha256).toHex());
    r.lastModified = pr->lastModified;
    return r;
}

QString PalmContactsBackend::createRecord(const QString &collectionId,
                                          const BackendRecord &record)
{
    if (collectionId != QLatin1String(CollectionId) || !m_device)
        return {};

    PalmRecord pr;
    pr.recordId   = 0; // device assigns
    pr.data       = record.data;
    pr.attributes = 0;

    const std::uint32_t newId = m_device->createRecord(QLatin1String(DatabaseName), pr);
    return encodeRecordId(newId);
}

bool PalmContactsBackend::updateRecord(const BackendRecord &record)
{
    if (!m_device) return false;
    std::uint32_t palmId = 0;
    if (!decodeRecordId(record.id, &palmId)) return false;

    PalmRecord pr;
    pr.recordId   = palmId;
    pr.data       = record.data;
    pr.attributes = PalmRecord::AttrDirty;
    return m_device->updateRecord(QLatin1String(DatabaseName), pr);
}

bool PalmContactsBackend::deleteRecord(const QString &recordId)
{
    if (!m_device) return false;
    std::uint32_t palmId = 0;
    if (!decodeRecordId(recordId, &palmId)) return false;
    return m_device->deleteRecord(QLatin1String(DatabaseName), palmId);
}

QList<CollectionInfo> PalmContactsBackend::availableCollections()
{
    CollectionInfo info;
    info.id   = QLatin1String(CollectionId);
    info.name = QStringLiteral("Palm Contacts");
    info.type = QStringLiteral("contacts");
    return { info };
}


} // namespace WildPalms::PalmContacts
