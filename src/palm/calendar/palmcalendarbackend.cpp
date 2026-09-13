#include "palmcalendarbackend.h"

#include <QRegularExpression>
#include <kalburator/calendar/syncoperation.h>

#include "categorymappingstore.h"
#include "ipalmdatabaseaccess.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "datebookcodec.h"
#include "palmrecord.h"

namespace WildPalms::PalmCalendar {

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::DeleteOperation;
using Kalburator::Sync::FetchOperation;
using Kalburator::Sync::PushOperation;
using Kalburator::Sync::SyncOperation;
using WildPalms::PalmSync::IPalmDatabaseAccess;

PalmCalendarBackend::PalmCalendarBackend(IPalmDatabaseAccess *device,
                                         CategoryMappingStore *categoryStore,
                                         QObject *parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_device(device)
    , m_categoryStore(categoryStore)
{
}

PalmCalendarBackend::~PalmCalendarBackend() = default;

QString PalmCalendarBackend::backendType() const
{
    return QStringLiteral("palm-calendar");
}

QList<Kalburator::Shape::Shape> PalmCalendarBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("palm-datebook")} } };
}

void PalmCalendarBackend::loadCalendars(const QString &collectionId)
{
    if (collectionId != QLatin1String(CollectionId)) {
        emit loadCalendarsFinished(
            collectionId, false,
            QStringLiteral("not a Palm calendar collection"));
        return;
    }

    // Slot 0 always.
    emit calendarDiscovered(collectionId, calendarIdForSlot(0));

    // Slots 1..15 from the store.
    if (m_categoryStore) {
        for (int slot : m_categoryStore->populatedSlots(
                 QLatin1String(DatabaseName))) {
            emit calendarDiscovered(collectionId, calendarIdForSlot(slot));
        }
    }

    emit loadCalendarsFinished(collectionId, true);
}

int PalmCalendarBackend::slotFromCalendarId(const QString &calendarId)
{
    static const QRegularExpression kRe(QStringLiteral("^palm:calendar/(\\d+)$"));
    const auto m = kRe.match(calendarId);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    if (!ok || n < 0 || n > 15) return -1;
    return n;
}

QString PalmCalendarBackend::calendarIdForSlot(int slot)
{
    return QStringLiteral("%1%2")
        .arg(QLatin1String(CalendarIdPrefix))
        .arg(slot);
}

void PalmCalendarBackend::storeCalendars(
    const QString &, const QList<KCalendarCore::MemoryCalendar *> &)
{
    // Palm calendar slots are implicit (created/renamed via the device's
    // category editor). No storage action at this level.
}

void PalmCalendarBackend::startSync(
    const QString &collectionId,
    KCalendarCore::MemoryCalendar *calendar,
    const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
    const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
    const QMap<QString, QString> &stagedDeletions)
{
    Q_UNUSED(collectionId);
    Q_UNUSED(calendar);
    // Route by each incidence's X-WP-PALM-CATEGORY-SLOT property,
    // else 0.
    auto slotForIncidence = [](const KCalendarCore::Incidence::Ptr &inc) {
        if (!inc) return 0;
        const auto s = inc->customProperty(
            "KCalendarCore",
            QByteArray(DatebookCodec::CategorySlotProperty));
        if (s.isEmpty()) return 0;
        bool ok = false;
        const int n = s.toInt(&ok);
        return (ok && n >= 0 && n <= 15) ? n : 0;
    };

    // Creations + updates both go through pushItems (the codec
    // preserves recordId from the property, so pushItems' own
    // "recordId==0 ? create : update" logic handles both).
    QHash<int, QList<KCalendarCore::Incidence::Ptr>> bySlot;
    for (const auto &inc : stagedCreations) bySlot[slotForIncidence(inc)].append(inc);
    for (const auto &inc : stagedUpdates)   bySlot[slotForIncidence(inc)].append(inc);
    for (auto it = bySlot.constBegin(); it != bySlot.constEnd(); ++it) {
        auto *op = pushItems(calendarIdForSlot(it.key()), it.value());
        if (op) op->deleteLater();
    }

    // Deletions: map<uid, calendarId>. Group by calendarId.
    QHash<QString, QStringList> delByCal;
    for (auto it = stagedDeletions.constBegin();
         it != stagedDeletions.constEnd(); ++it) {
        delByCal[it.value()].append(it.key());
    }
    for (auto it = delByCal.constBegin(); it != delByCal.constEnd(); ++it) {
        auto *op = deleteItems(it.key(), it.value());
        if (op) op->deleteLater();
    }
}

void PalmCalendarBackend::removeItem(const QString &calId,
                                      const QString &itemUid)
{
    auto *op = deleteItems(calId, QStringList{ itemUid });
    if (op) op->deleteLater();
    emit itemRemoved(calId, itemUid);
}

// ========== Blob-level disconnect guard (Layer B) ==========
bool PalmCalendarBackend::loadRecordsOrError(const QString &collectionId,
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
    return true;
}

// ========== Operation API (Task 5) ==========
FetchOperation *PalmCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    op->setState(SyncOperation::Running);

    if (!m_device->isConnected()) {
        const auto err = QStringLiteral("Palm link not connected before reading %1")
                           .arg(QLatin1String(DatabaseName));
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    const auto records = m_device->readAllRecords(QLatin1String(DatabaseName));
    emit fetchStarted(calendarId, records.size());

    if (!m_device->isConnected()) {
        const auto err = QStringLiteral("Palm link lost while reading %1")
                           .arg(QLatin1String(DatabaseName));
        op->fail(err);
        emit fetchFinished(calendarId, false, err);
        return op;
    }

    QList<KCalendarCore::Incidence::Ptr> items;
    for (const auto &rec : records) {
        if (static_cast<int>(rec.category) != slot) {
            continue;
        }
        const auto decoded = DatebookCodec::decode(rec);
        if (!decoded.isValid()) {
            continue;
        }
        items.append(decoded.event);
    }

    // Batch-form streaming (libkalburator E10/v0.90.1): one itemsFetched
    // per fetch pass — the per-item itemFetched signal was deleted.
    if (!items.isEmpty())
        emit itemsFetched(calendarId, items);

    op->setFetchedItems(items);
    op->complete();
    emit fetchFinished(calendarId, true);
    return op;
}

PushOperation *PalmCalendarBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = new PushOperation(calendarId, items, this);

    const int slot = slotFromCalendarId(calendarId);
    if (slot < 0) {
        op->fail(QStringLiteral("invalid calendar id: %1").arg(calendarId));
        return op;
    }

    if (!m_device) {
        op->fail(QStringLiteral("no device"));
        return op;
    }

    // Ensure the database exists. A real device may surface a missing
    // DatebookDB as a hard error; the mock creates lazily.
    m_device->createDatabase(QLatin1String(DatabaseName));

    op->setState(SyncOperation::Running);
    emit writeStarted(calendarId, items.size());

    for (const auto &incidence : items) {
        if (!incidence || incidence->type() != KCalendarCore::IncidenceBase::TypeEvent) {
            op->addFailedUid(incidence ? incidence->uid() : QString());
            continue;
        }
        const auto event = incidence.staticCast<KCalendarCore::Event>();
        auto rec = DatebookCodec::encode(event, slot);
        if (rec.data.isEmpty()) {
            op->addFailedUid(event->uid());
            continue;
        }

        if (rec.recordId == 0) {
            // New record.
            const auto newId = m_device->createRecord(
                QLatin1String(DatabaseName), rec);
            if (newId == 0) {
                op->addFailedUid(event->uid());
                continue;
            }
            // Stash the assigned record ID on the event so callers
            // carrying the Incidence onward see the server-side ID.
            event->setCustomProperty(
                "KCalendarCore",
                QByteArray(DatebookCodec::RecordIdProperty),
                QString::number(newId));
            op->addSucceededUid(event->uid());
        } else {
            if (m_device->updateRecord(QLatin1String(DatabaseName), rec)) {
                op->addSucceededUid(event->uid());
            } else {
                op->addFailedUid(event->uid());
            }
        }
    }

    op->complete();
    return op;
}

DeleteOperation *PalmCalendarBackend::deleteItems(
    const QString &calendarId, const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);

    if (slotFromCalendarId(calendarId) < 0) {
        const auto err = QStringLiteral("invalid calendar id: %1").arg(calendarId);
        op->fail(err);
        return op;
    }

    if (!m_device) {
        const auto err = QStringLiteral("no device");
        op->fail(err);
        return op;
    }

    op->setState(SyncOperation::Running);

    // UIDs from DatebookCodec have the form "palm-datebook-<recordId>".
    static const QRegularExpression kUidRe(
        QStringLiteral("^palm-datebook-(\\d+)$"));

    for (const auto &uid : uids) {
        const auto m = kUidRe.match(uid);
        if (!m.hasMatch()) {
            op->addFailedUid(uid);
            continue;
        }
        bool ok = false;
        const auto recordId = m.captured(1).toUInt(&ok);
        if (!ok) {
            op->addFailedUid(uid);
            continue;
        }
        if (m_device->deleteRecord(QLatin1String(DatabaseName), recordId)) {
            op->addSucceededUid(uid);
        } else {
            op->addFailedUid(uid);
        }
    }

    op->complete();
    return op;
}

} // namespace WildPalms::PalmCalendar
