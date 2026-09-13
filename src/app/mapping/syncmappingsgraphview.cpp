#include "syncmappingsgraphview.h"

#include "palmdbnode.h"
#include "providernode.h"
#include "mappingedge.h"

#include <QApplication>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QUuid>

#include <array>
#include <cmath>
#include <utility>

#include <kalburator/types/synctypes.h>   // syncMappingToJson, syncMappingFromJson

namespace WildPalms::AppMapping {

namespace {
constexpr qreal kLeftColumnX  = 30.0;
constexpr qreal kRightColumnX = 320.0;
constexpr qreal kRowGap       = 30.0;
} // namespace

SyncMappingGraphView::SyncMappingGraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::NoDrag);
    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &SyncMappingGraphView::onSceneSelectionChanged);
}

SyncMappingGraphView::~SyncMappingGraphView() = default;

QString SyncMappingGraphView::palmDomainForDb(const QString &dbName)
{
    if (dbName == QLatin1String("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QLatin1String("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QLatin1String("MemoDB"))     return QStringLiteral("memos");
    if (dbName == QLatin1String("ToDoDB"))     return QStringLiteral("todos");
    return QStringLiteral("unknown");
}

QString SyncMappingGraphView::palmBackendIdForDb(const QString &dbName)
{
    if (dbName == QLatin1String("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QLatin1String("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QLatin1String("MemoDB"))     return QStringLiteral("memo");
    if (dbName == QLatin1String("ToDoDB"))     return QStringLiteral("todo");
    return {};
}

QString SyncMappingGraphView::palmRouteDomainForDb(const QString &dbName)
{
    // The HUB/route domain (matches PimPlugin::domain()). Distinct from the
    // plural collection-type domain in palmDomainForDb(). Fixes the latent bug
    // where this writer emitted "palm:contact/N" / "palm:memo/N" while the
    // route translator expected "palm:contacts/…" / "palm:note/…", so
    // graph-created contact/memo routes never translated.
    if (dbName == QLatin1String("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QLatin1String("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QLatin1String("MemoDB"))     return QStringLiteral("note");
    if (dbName == QLatin1String("ToDoDB"))     return QStringLiteral("todo");
    return {};
}

QString SyncMappingGraphView::palmCollectionIdForSlot(const QString &dbName, int slot) const
{
    // Substrate A3: names-first form "palm:<routeDomain>/name:<categoryName>".
    // Slot 0 (Unfiled) and unnamed slots map to an empty sourceCalendar — a
    // whole-domain Direct route — so the persisted row never carries a stale
    // slot index.
    const QString domain = palmRouteDomainForDb(dbName);
    if (slot <= 0 || domain.isEmpty())
        return {};
    const QStringList names = m_snapshot.value(dbName);
    const QString name = (slot < names.size()) ? names.at(slot) : QString();
    if (name.isEmpty())
        return {};
    return QStringLiteral("palm:%1/name:%2").arg(domain, name);
}

void SyncMappingGraphView::setSnapshot(const QHash<QString, QStringList> &snapshot)
{
    m_snapshot = snapshot;
}

void SyncMappingGraphView::setProviders(const QList<ProviderEntry> &providers)
{
    m_providers = providers;
}

void SyncMappingGraphView::setMappings(const QJsonArray &mappings)
{
    // Stored on edges after rebuild(); kept here only so rebuild() can
    // re-create edges from it.
    m_scene->setProperty("pending-mappings", mappings);
}

QJsonArray SyncMappingGraphView::mappings() const
{
    QJsonArray out;
    for (auto *edge : m_edges)
        out.append(edge->mappingJson());
    return out;
}

void SyncMappingGraphView::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (auto *e : m_edges)
        e->setFlag(QGraphicsItem::ItemIsSelectable, !readOnly);
}

void SyncMappingGraphView::clearScene()
{
    m_scene->clear();
    m_palmNodes.clear();
    m_providerNodes.clear();
    m_edges.clear();
}

void SyncMappingGraphView::rebuild()
{
    const QJsonArray pendingMappings =
        m_scene->property("pending-mappings").toJsonArray();
    clearScene();

    static const std::array<std::pair<const char*, const char*>, 4> kDbs = {{
        {"DatebookDB", "Calendar — DatebookDB"},
        {"AddressDB",  "Contacts — AddressDB"},
        {"MemoDB",     "Memos — MemoDB"},
        {"ToDoDB",     "Todos — ToDoDB"},
    }};

    for (const auto &p : kDbs) {
        const QString db = QString::fromLatin1(p.first);
        const QStringList slotNames = m_snapshot.value(db);
        QStringList sixteen = slotNames;
        if (sixteen.size() != 16) {
            sixteen = QStringList();
            for (int i = 0; i < 16; ++i) sixteen << QString();
        }
        auto *node = new PalmDbNode(db, QString::fromUtf8(p.second),
                                    palmDomainForDb(db), sixteen);
        m_scene->addItem(node);
        m_palmNodes.append(node);
    }

    for (const auto &prov : m_providers) {
        auto *node = new ProviderNode(prov.providerId, prov.displayName,
                                       prov.collections, prov.busyText);
        m_scene->addItem(node);
        m_providerNodes.append(node);
    }

    layoutBipartite();

    // Recreate edges from pending mappings.
    for (const auto v : pendingMappings) {
        const QJsonObject obj = v.toObject();
        const QString sourceBackend = obj.value(QStringLiteral("sourceBackend")).toString();
        const QString sourceCalendar = obj.value(QStringLiteral("sourceCalendar")).toString();
        const QString targetCollection = obj.value(QStringLiteral("targetCalendar")).toString();
        // targetBackend is "<providerId>:<collectionId>"; recover the bare
        // providerId for the node lookup (providerId is a colon-free UUID, so
        // section on the first ':' is safe — and a legacy bare-id mapping with
        // no colon still resolves to itself).
        const QString targetProviderId =
            obj.value(QStringLiteral("targetBackend")).toString().section(QLatin1Char(':'), 0, 0);

        // Substrate A3: reconstruct from the names-first form
        // "palm:<routeDomain>/name:<categoryName>". Locate the Palm DB by
        // sourceBackend, then resolve the category name to its slot index in
        // the live snapshot for edge positioning. A named route whose category
        // is absent from the snapshot (device not seen yet) attaches to the DB
        // header (slot 0, pending). Empty/whole-domain rows render no edge.
        QString dbName;
        int slot = -1;
        for (const auto &p : kDbs) {
            const QString db = QString::fromLatin1(p.first);
            if (palmBackendIdForDb(db) != sourceBackend) continue;
            const QString prefix = QStringLiteral("palm:")
                + palmRouteDomainForDb(db) + QStringLiteral("/name:");
            if (sourceCalendar.startsWith(prefix)) {
                const QString name = sourceCalendar.mid(prefix.size());
                const QStringList names = m_snapshot.value(db);
                slot = 0;   // pending: attach to the DB header until reconciled
                for (int i = 1; i < names.size(); ++i)
                    if (names.at(i) == name) { slot = i; break; }
                dbName = db;
            }
            break;
        }
        if (dbName.isEmpty() || slot < 0) continue;

        PalmDbNode *src = nodeForDb(dbName);
        ProviderNode *tgt = nodeForProvider(targetProviderId);
        if (!src || !tgt) continue;

        auto *edge = new MappingEdge(src, slot, tgt, targetCollection, obj);
        m_scene->addItem(edge);
        m_edges.append(edge);
    }

    setSceneRect(m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50));
}

void SyncMappingGraphView::layoutBipartite()
{
    qreal y = 10.0;
    for (auto *n : m_palmNodes) {
        n->setPos(kLeftColumnX, y);
        y += n->boundingRect().height() + kRowGap;
    }
    y = 10.0;
    for (auto *n : m_providerNodes) {
        n->setPos(kRightColumnX, y);
        y += n->boundingRect().height() + kRowGap;
    }
}

PalmDbNode *SyncMappingGraphView::nodeForDb(const QString &dbName) const
{
    for (auto *n : m_palmNodes)
        if (n->dbName() == dbName) return n;
    return nullptr;
}

ProviderNode *SyncMappingGraphView::nodeForProvider(const QString &providerId) const
{
    for (auto *n : m_providerNodes)
        if (n->providerId() == providerId) return n;
    return nullptr;
}

bool SyncMappingGraphView::isCompatible(const QString &dbName,
                                        const QString &providerId,
                                        const QString &collectionId) const
{
    const QString palmDomain = palmDomainForDb(dbName);
    auto *prov = nodeForProvider(providerId);
    if (!prov) return false;
    const QString collDomain = prov->collectionDomain(collectionId);
    if (collDomain == QLatin1String("unknown")) return true;
    return collDomain == palmDomain;
}

bool SyncMappingGraphView::isDuplicate(const QString &dbName, int slot,
                                       const QString &providerId,
                                       const QString &collectionId) const
{
    const QString srcBackend = palmBackendIdForDb(dbName);
    const QString srcCalendar = palmCollectionIdForSlot(dbName, slot);
    for (auto *e : m_edges) {
        const QJsonObject j = e->mappingJson();
        if (j.value(QStringLiteral("sourceBackend")).toString()  == srcBackend &&
            j.value(QStringLiteral("sourceCalendar")).toString() == srcCalendar &&
            j.value(QStringLiteral("targetBackend")).toString()  ==
                QStringLiteral("%1:%2").arg(providerId, collectionId) &&
            j.value(QStringLiteral("targetCalendar")).toString() == collectionId) {
            return true;
        }
    }
    return false;
}

QJsonObject SyncMappingGraphView::defaultMappingJson(
    const QString &dbName, int slot,
    const QString &providerId, const QString &collectionId) const
{
    Kalburator::Sync::SyncMapping m;
    m.id              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.sourceBackend   = palmBackendIdForDb(dbName);
    m.sourceCalendar  = palmCollectionIdForSlot(dbName, slot);
    // The engine resolves the target by the backend-instance id that
    // ProviderManager registers each collection under: "<providerId>:<collectionId>"
    // (see libkalburator providermanager.cpp). Storing the bare providerId here
    // made SyncEngine::dispatchSync fail backendById() and silently abort the
    // mapping ("backend not found") — no Palm read, no remote fetch.
    m.targetBackend   = QStringLiteral("%1:%2").arg(providerId, collectionId);
    m.targetCalendar  = collectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.conflictPolicy  = Kalburator::Sync::ConflictResolution::AskUser;
    m.lossPolicy      = Kalburator::Sync::WhenLossWouldOccur::Warn;
    m.enabled         = true;
    return Kalburator::Sync::syncMappingToJson(m);
}

bool SyncMappingGraphView::addMappingForTest(
    const QString &dbName, int slot,
    const QString &providerId, const QString &collectionId)
{
    if (m_readOnly) return false;
    if (!nodeForDb(dbName) || !nodeForProvider(providerId)) return false;
    if (!isCompatible(dbName, providerId, collectionId)) return false;
    if (isDuplicate(dbName, slot, providerId, collectionId)) return false;

    const QJsonObject json = defaultMappingJson(dbName, slot, providerId, collectionId);

    auto *src = nodeForDb(dbName);
    auto *tgt = nodeForProvider(providerId);
    auto *edge = new MappingEdge(src, slot, tgt, collectionId, json);
    m_scene->addItem(edge);
    m_edges.append(edge);

    Q_EMIT mappingsChanged(mappings());
    return true;
}

bool SyncMappingGraphView::removeMappingForTest(const QString &mappingId)
{
    if (m_readOnly) return false;
    for (int i = 0; i < m_edges.size(); ++i) {
        if (m_edges.at(i)->mappingId() == mappingId) {
            m_scene->removeItem(m_edges.at(i));
            delete m_edges.takeAt(i);
            Q_EMIT mappingsChanged(mappings());
            return true;
        }
    }
    return false;
}

QList<int> SyncMappingGraphView::activeSlotsForTest(const QString &dbName) const
{
    if (auto *n = nodeForDb(dbName)) return n->activeSlots();
    return {};
}

void SyncMappingGraphView::onSceneSelectionChanged()
{
    QString sel;
    for (auto *e : m_edges) {
        if (e->isSelected()) {
            sel = e->mappingId();
            e->setVisual(MappingEdge::Visual::Selected);
        } else {
            const bool enabled = e->mappingJson()
                .value(QStringLiteral("enabled")).toBool();
            e->setVisual(enabled ? MappingEdge::Visual::Default
                                 : MappingEdge::Visual::Disabled);
        }
    }
    Q_EMIT edgeSelected(sel);
}

// Mouse + key event handlers implement drag-to-connect: press on a
// Palm port anchor begins a drag, move tracks a rubber-band bezier,
// release on a compatible provider anchor creates a SyncMapping
// (validated by isCompatible + isDuplicate). The same code path is
// exercised by tests via beginDragForTest/endDragOnProviderForTest.

PalmDbNode *SyncMappingGraphView::palmAnchorAtScenePos(
    const QPointF &scenePos, int *outSlot) const
{
    if (outSlot) *outSlot = -1;
    for (auto *node : m_palmNodes) {
        const int slot = node->slotAtScenePos(scenePos);
        if (slot >= 0) {
            if (outSlot) *outSlot = slot;
            return node;
        }
    }
    return nullptr;
}

ProviderNode *SyncMappingGraphView::providerAnchorAtScenePos(
    const QPointF &scenePos, QString *outCollectionId) const
{
    if (outCollectionId) outCollectionId->clear();
    for (auto *node : m_providerNodes) {
        const QString c = node->collectionAtScenePos(scenePos);
        if (!c.isEmpty()) {
            if (outCollectionId) *outCollectionId = c;
            return node;
        }
    }
    return nullptr;
}

void SyncMappingGraphView::beginDrag(PalmDbNode *src, int slot,
                                     const QPointF &scenePos)
{
    cancelDrag();
    if (!src || slot < 0 || m_readOnly) return;

    m_dragSourceNode = src;
    m_dragSourceSlot = slot;

    m_dragPathItem = new QGraphicsPathItem();
    QPen pen(QApplication::palette().color(QPalette::Highlight));
    pen.setWidthF(2.0);
    pen.setStyle(Qt::DashLine);
    m_dragPathItem->setPen(pen);
    m_dragPathItem->setZValue(10.0);   // above edges + nodes
    m_scene->addItem(m_dragPathItem);
    updateDrag(scenePos);
}

void SyncMappingGraphView::updateDrag(const QPointF &scenePos)
{
    if (!m_dragSourceNode || !m_dragPathItem) return;
    const QPointF a = m_dragSourceNode->slotAnchorScenePos(m_dragSourceSlot);
    if (a.isNull()) return;
    const qreal dx = std::abs(scenePos.x() - a.x());
    const qreal c  = std::max<qreal>(40.0, dx * 0.5);
    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(a.x() + c, a.y(),
                 scenePos.x() - c, scenePos.y(),
                 scenePos.x(),     scenePos.y());
    m_dragPathItem->setPath(path);
}

bool SyncMappingGraphView::endDrag(const QPointF &scenePos)
{
    if (!m_dragSourceNode) { cancelDrag(); return false; }

    const QString dbName = m_dragSourceNode->dbName();
    const int     slot   = m_dragSourceSlot;

    QString collectionId;
    ProviderNode *tgt = providerAnchorAtScenePos(scenePos, &collectionId);
    cancelDrag();
    if (!tgt) return false;

    return addMappingForTest(dbName, slot, tgt->providerId(), collectionId);
}

void SyncMappingGraphView::cancelDrag()
{
    if (m_dragPathItem) {
        m_scene->removeItem(m_dragPathItem);
        delete m_dragPathItem;
        m_dragPathItem = nullptr;
    }
    m_dragSourceNode = nullptr;
    m_dragSourceSlot = -1;
}

void SyncMappingGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_readOnly) {
        const QPointF scenePos = mapToScene(event->pos());
        int slot = -1;
        if (auto *node = palmAnchorAtScenePos(scenePos, &slot)) {
            beginDrag(node, slot, scenePos);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void SyncMappingGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragSourceNode) {
        updateDrag(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void SyncMappingGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragSourceNode) {
        endDrag(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SyncMappingGraphView::keyPressEvent(QKeyEvent *event)
{
    if (!m_readOnly &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        QString selectedId;
        for (auto *e : m_edges) {
            if (e->isSelected()) { selectedId = e->mappingId(); break; }
        }
        if (!selectedId.isEmpty()) {
            removeMappingForTest(selectedId);
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

} // namespace WildPalms::AppMapping
