#ifndef WILDPALMS_APP_MAPPING_SYNCMAPPINGSGRAPHVIEW_H
#define WILDPALMS_APP_MAPPING_SYNCMAPPINGSGRAPHVIEW_H

#include <QGraphicsView>
#include <QGraphicsPathItem>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <kalburator/types/collectioninfo.h>

#include "palmdbnode.h"
#include "providernode.h"

class QGraphicsScene;

namespace WildPalms::AppMapping {

class MappingEdge;

class SyncMappingGraphView : public QGraphicsView {
    Q_OBJECT
public:
    /// Provider info supplied by the page wrapper. F.3 doesn't reach
    /// into AccountController directly to keep the graph view easy to
    /// unit-test (the page builds these structs and hands them in).
    struct ProviderEntry {
        QString providerId;
        QString displayName;
        QList<Kalburator::Sync::CollectionInfo> collections;
        QString busyText;   // empty → connected
    };

    explicit SyncMappingGraphView(QWidget *parent = nullptr);
    ~SyncMappingGraphView() override;

    void setSnapshot(const QHash<QString, QStringList> &snapshot);
    void setProviders(const QList<ProviderEntry> &providers);
    void setMappings(const QJsonArray &mappings);
    QJsonArray mappings() const;

    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }

    /// Wipe and re-layout the scene from the current snapshot + providers
    /// + mappings. Call after any of those change.
    void rebuild();

    // ----- Test seams (public for unit tests; production callers also
    // use these — the names just signal that they're directly callable
    // without simulating mouse events) -----
    void setSnapshotForTest(const QHash<QString, QStringList> &snapshot) {
        setSnapshot(snapshot);
    }
    void setProvidersForTest(const QList<ProviderEntry> &providers) {
        setProviders(providers);
    }
    bool addMappingForTest(const QString &dbName, int slot,
                           const QString &providerId,
                           const QString &collectionId);
    bool removeMappingForTest(const QString &mappingId);
    int  edgeCount() const { return m_edges.size(); }
    int  palmDbNodeCount() const { return m_palmNodes.size(); }
    int  providerNodeCount() const { return m_providerNodes.size(); }
    QList<int> activeSlotsForTest(const QString &dbName) const;
    QJsonArray edgesForTest() const { return mappings(); }

signals:
    void mappingsChanged(const QJsonArray &mappings);
    void edgeSelected(const QString &mappingId);   // empty = deselected

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSceneSelectionChanged();

private:
    static QString palmDomainForDb(const QString &dbName);
    static QString palmBackendIdForDb(const QString &dbName);
    /// Substrate A3: hub/route domain for a Palm DB (calendar/contacts/note/
    /// todo). Distinct from palmDomainForDb() (which returns the plural
    /// collection-type domain memos/todos used for provider compatibility).
    static QString palmRouteDomainForDb(const QString &dbName);
    /// Substrate A3: the persisted sourceCalendar for a slot, in names-first
    /// form "palm:<routeDomain>/name:<categoryName>" (resolved from the
    /// current snapshot). Empty for slot 0 / unnamed slots (a Direct route).
    /// Now an instance method — it reads the live category-name snapshot.
    QString palmCollectionIdForSlot(const QString &dbName, int slot) const;

    PalmDbNode  *nodeForDb(const QString &dbName) const;
    ProviderNode *nodeForProvider(const QString &providerId) const;

    /// Validation helpers (also used by drag-and-drop paths).
    bool isCompatible(const QString &dbName, const QString &providerId,
                      const QString &collectionId) const;
    bool isDuplicate(const QString &dbName, int slot,
                     const QString &providerId,
                     const QString &collectionId) const;

    void layoutBipartite();
    void clearScene();
    QJsonObject defaultMappingJson(const QString &dbName, int slot,
                                   const QString &providerId,
                                   const QString &collectionId) const;

    /// Drag-to-connect helpers. `scenePos` is in scene coordinates.
    /// Production mouse handlers translate viewport→scene before
    /// calling these; tests call them directly via the *ForTest seams.
    void beginDrag(PalmDbNode *src, int slot, const QPointF &scenePos);
    void updateDrag(const QPointF &scenePos);
    /// Returns true if the drag landed on a compatible target and a
    /// new mapping was created.
    bool endDrag(const QPointF &scenePos);
    void cancelDrag();

    /// Hit-test helpers — find which node + which port lives at scenePos
    /// (right-side anchor of a PalmDbNode, or left-side anchor of a
    /// ProviderNode). Returns nullptr / -1 / empty on miss.
    PalmDbNode  *palmAnchorAtScenePos(const QPointF &scenePos, int *outSlot) const;
    ProviderNode *providerAnchorAtScenePos(const QPointF &scenePos,
                                            QString *outCollectionId) const;

public:
    // Test seams for the drag interaction. Production callers go through
    // the mouse event handlers.
    void beginDragForTest(const QString &dbName, int slot) {
        if (auto *node = nodeForDb(dbName)) {
            const QPointF p = node->slotAnchorScenePos(slot);
            beginDrag(node, slot, p);
        }
    }
    bool endDragOnProviderForTest(const QString &providerId,
                                  const QString &collectionId) {
        if (auto *node = nodeForProvider(providerId)) {
            const QPointF p = node->collectionAnchorScenePos(collectionId);
            return endDrag(p);
        }
        cancelDrag();
        return false;
    }
    bool isDraggingForTest() const { return m_dragSourceNode != nullptr; }

private:
    QGraphicsScene *m_scene {nullptr};

    QHash<QString, QStringList>     m_snapshot;
    QList<ProviderEntry>            m_providers;

    QList<PalmDbNode*>    m_palmNodes;
    QList<ProviderNode*>  m_providerNodes;
    QList<MappingEdge*>   m_edges;

    bool m_readOnly {false};

    // Drag-to-connect state. Non-null source means a drag is in flight.
    PalmDbNode         *m_dragSourceNode {nullptr};
    int                 m_dragSourceSlot {-1};
    QGraphicsPathItem  *m_dragPathItem   {nullptr};   // rubber-band feedback
};

} // namespace WildPalms::AppMapping

#endif
