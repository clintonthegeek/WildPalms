#ifndef WILDPALMS_APP_MAPPING_PROVIDERNODE_H
#define WILDPALMS_APP_MAPPING_PROVIDERNODE_H

#include <QGraphicsItem>
#include <QString>
#include <QList>
#include <QRectF>

#include <kalburator/types/collectioninfo.h>   // Kalburator::Sync::CollectionInfo

namespace WildPalms::AppMapping {

/// QGraphicsItem rendering a single provider (CalDAV/CardDAV/Akonadi/
/// rawfiles) with one port row per collection. Left-side anchors carry
/// the collection id.
///
/// Each collection port row is tagged with its CollectionInfo::type
/// ("calendar" / "contacts" / "memos" / "todos"). The graph view uses
/// the per-row type to enforce domain compatibility during edge
/// creation.
class ProviderNode : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    /// providerId: matches Kalburator::Sync::IProvider::id()
    /// displayName: title bar text (provider's display name)
    /// collections: ordered list; rendered top-to-bottom
    /// busyText: when non-empty, the node renders a single disabled
    ///           placeholder row with this text instead of collection
    ///           rows (e.g. "Connecting…").
    ProviderNode(const QString &providerId,
                 const QString &displayName,
                 const QList<Kalburator::Sync::CollectionInfo> &collections,
                 const QString &busyText = QString(),
                 QGraphicsItem *parent = nullptr);

    QString providerId() const { return m_providerId; }

    /// Domain ("calendar"/"contacts"/"memos"/"todos"/"unknown") for the
    /// collection identified by `collectionId`. "unknown" if absent.
    QString collectionDomain(const QString &collectionId) const;

    /// Scene position of the left-side anchor for the named collection,
    /// or invalid QPointF if absent.
    QPointF collectionAnchorScenePos(const QString &collectionId) const;

    /// Collection id whose left-side anchor sits under scenePos, or
    /// empty string if none.
    QString collectionAtScenePos(const QPointF &scenePos) const;

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QList<Kalburator::Sync::CollectionInfo> collections() const { return m_collections; }

private:
    QRectF anchorLocalRect(int row) const;

    QString                                   m_providerId;
    QString                                   m_displayName;
    QList<Kalburator::Sync::CollectionInfo>   m_collections;
    QString                                   m_busyText;

    static constexpr qreal kNodeWidth     = 200.0;
    static constexpr qreal kHeaderHeight  = 28.0;
    static constexpr qreal kRowHeight     = 22.0;
    static constexpr qreal kAnchorRadius  = 5.0;
};

} // namespace WildPalms::AppMapping

#endif
