// tests/runtime/tst_syncmappingsgraphview.cpp
#include <QtTest/QtTest>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>

#include "../../src/app/mapping/syncmappingsgraphview.h"
#include "../wildpalms_qtest_main.h"

#include <kalburator/types/collectioninfo.h>

using WildPalms::AppMapping::SyncMappingGraphView;
using Kalburator::Sync::CollectionInfo;

class TstSyncMappingsGraphView : public QObject {
    Q_OBJECT
private slots:
    void emptyProfileShowsPlaceholders();
    void slotNamesRenderAsPorts();
    void addMappingCreatesEdge();
    void duplicateMappingRejected();
    void domainMismatchRejected();
    void deleteMappingRemovesEdge();
    void readOnlyBlocksMutations();
    void dragConnectCreatesMapping();
    void dragOnIncompatibleTargetCancels();
};

namespace {
QHash<QString, QStringList> exampleSnapshot()
{
    QStringList datebook(16);
    datebook[0] = QStringLiteral("Unfiled");
    datebook[1] = QStringLiteral("Work");
    datebook[2] = QStringLiteral("Personal");

    QStringList contacts(16);
    contacts[0] = QStringLiteral("Unfiled");
    contacts[1] = QStringLiteral("Work");

    return {
        {QStringLiteral("DatebookDB"), datebook},
        {QStringLiteral("AddressDB"),  contacts},
        {QStringLiteral("MemoDB"),     {}},
        {QStringLiteral("ToDoDB"),     {}},
    };
}

QList<CollectionInfo> calCollections()
{
    CollectionInfo c1;
    c1.id = QStringLiteral("caldav:p1:work");
    c1.name = QStringLiteral("Work Calendar");
    c1.type = QStringLiteral("calendar");

    CollectionInfo c2;
    c2.id = QStringLiteral("caldav:p1:personal");
    c2.name = QStringLiteral("Personal Calendar");
    c2.type = QStringLiteral("calendar");
    return {c1, c2};
}

QList<CollectionInfo> contactCollections()
{
    CollectionInfo c1;
    c1.id = QStringLiteral("carddav:p2:contacts");
    c1.name = QStringLiteral("All Contacts");
    c1.type = QStringLiteral("contacts");
    return {c1};
}
} // namespace

void TstSyncMappingsGraphView::emptyProfileShowsPlaceholders()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest({});           // no category names at all
    view.setProvidersForTest({});          // no providers
    view.setMappings(QJsonArray());
    view.rebuild();

    QCOMPARE(view.palmDbNodeCount(), 4);   // four DB nodes always present
    QCOMPARE(view.providerNodeCount(), 0);
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::slotNamesRenderAsPorts()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QCOMPARE(view.activeSlotsForTest(QStringLiteral("DatebookDB")),
             (QList<int>{0, 1, 2}));
    QCOMPARE(view.activeSlotsForTest(QStringLiteral("AddressDB")),
             (QList<int>{0, 1}));
    QCOMPARE(view.activeSlotsForTest(QStringLiteral("MemoDB")),
             QList<int>{});   // empty snapshot
    QCOMPARE(view.providerNodeCount(), 1);
}

void TstSyncMappingsGraphView::addMappingCreatesEdge()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);

    const bool ok = view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work"));

    QVERIFY(ok);
    QCOMPARE(view.edgeCount(), 1);
    QCOMPARE(spy.count(), 1);

    const QJsonArray emitted = spy.takeFirst().at(0).toJsonArray();
    QCOMPARE(emitted.size(), 1);
}

void TstSyncMappingsGraphView::duplicateMappingRejected()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QVERIFY(view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));

    // Duplicate (same source+target) → rejected.
    QVERIFY(!view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));

    QCOMPARE(view.edgeCount(), 1);
}

void TstSyncMappingsGraphView::domainMismatchRejected()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p2"), QStringLiteral("iCloud"), contactCollections()},
    });
    view.rebuild();

    // DatebookDB (calendar) → carddav contacts collection → mismatch.
    const bool ok = view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p2"), QStringLiteral("carddav:p2:contacts"));

    QVERIFY(!ok);
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::deleteMappingRemovesEdge()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QVERIFY(view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));
    QCOMPARE(view.edgeCount(), 1);

    const QString mappingId = view.edgesForTest().first().toObject()
        .value(QStringLiteral("id")).toString();
    QVERIFY(!mappingId.isEmpty());

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);
    QVERIFY(view.removeMappingForTest(mappingId));
    QCOMPARE(view.edgeCount(), 0);
    QCOMPARE(spy.count(), 1);
}

void TstSyncMappingsGraphView::readOnlyBlocksMutations()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();
    view.setReadOnly(true);

    QVERIFY(!view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::dragConnectCreatesMapping()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);

    view.beginDragForTest(QStringLiteral("DatebookDB"), 1);
    QVERIFY(view.isDraggingForTest());

    const bool ok = view.endDragOnProviderForTest(
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work"));
    QVERIFY(ok);
    QVERIFY(!view.isDraggingForTest());
    QCOMPARE(view.edgeCount(), 1);
    QCOMPARE(spy.count(), 1);
}

void TstSyncMappingsGraphView::dragOnIncompatibleTargetCancels()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p2"), QStringLiteral("iCloud"), contactCollections()},
    });
    view.rebuild();

    view.beginDragForTest(QStringLiteral("DatebookDB"), 1);
    QVERIFY(view.isDraggingForTest());

    // DatebookDB (calendar) dropped on a contacts collection → no edge.
    const bool ok = view.endDragOnProviderForTest(
        QStringLiteral("p2"), QStringLiteral("carddav:p2:contacts"));
    QVERIFY(!ok);
    QVERIFY(!view.isDraggingForTest());
    QCOMPARE(view.edgeCount(), 0);
}

WILDPALMS_QTEST_MAIN(TstSyncMappingsGraphView)
#include "tst_syncmappingsgraphview.moc"
