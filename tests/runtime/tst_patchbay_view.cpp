// tests/runtime/tst_patchbay_view.cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "../../src/app/patchbay/patchbaymodel.h"
#include "../../src/app/patchbay/syncpatchbayview.h"
#include "../../src/app/patchbay/signalpathwire.h"
#include "../wildpalms_qtest_main.h"

#include <kalburator/types/collectioninfo.h>

using namespace WildPalms::AppPatchbay;
using Kalburator::Sync::CollectionInfo;
using WildPalms::Runtime::RouteStatus;

namespace {

QList<ConduitFacts> stockConduits()
{
    auto typeIs = [](const char *t) {
        return [t](const CollectionInfo &c) { return c.type == QLatin1String(t); };
    };
    return {
        { "calendar", "calendar", "DatebookDB", "Calendar", typeIs("calendar") },
        { "contacts", "contacts", "AddressDB",  "Contacts", typeIs("contacts") },
        { "memo",     "note",     "MemoDB",     "Memos",    typeIs("notes")    },
        { "todo",     "todo",     "ToDoDB",     "Todos",    typeIs("todo")     },
    };
}

QStringList snapshot16(std::initializer_list<const char *> names)
{
    QStringList l;
    for (const char *n : names) l << QString::fromUtf8(n);
    while (l.size() < 16) l << QString();
    return l;
}

QJsonObject row(const char *id, const char *srcBackend, const char *srcCal,
                const char *tgtBackend, const char *tgtCal,
                const char *mode = "TwoWay", bool enabled = true)
{
    QJsonObject r;
    r["id"] = id;
    r["sourceBackend"] = srcBackend;
    r["sourceCalendar"] = srcCal;
    r["targetBackend"] = tgtBackend;
    r["targetCalendar"] = tgtCal;
    r["mode"] = mode;
    r["conflictPolicy"] = "LastWriteWins";
    r["enabled"] = enabled;
    return r;
}

PatchbayModel::Inputs baseInputs()
{
    PatchbayModel::Inputs in;
    in.conduits = stockConduits();
    in.slotSnapshot = {
        { "DatebookDB", snapshot16({"Unfiled", "Work", "Personal"}) },
        { "AddressDB",  snapshot16({"Unfiled"}) },
    };
    in.desiredCategories = { { "DatebookDB", {"Work", "Personal"} } };
    CollectionInfo cal;
    cal.id = "cal1"; cal.name = "Team"; cal.type = "calendar";
    PatchbayModel::ProviderEntry nc{ "acc-1", "Nextcloud", QString(), { cal } };
    in.providers = { nc };
    in.deviceConnected = true;
    in.deviceName = "Palm m515";
    return in;
}

} // namespace

class TstPatchbayView : public QObject {
    Q_OBJECT
private slots:
    void rebuildPopulatesScene();
    void columnsAreOrdered();
    void wireCarriesState();
    // Task 13
    void dragConnectCreatesMapping();
    void dragOnPalmTierIgnored();
    void deleteSelectedRemovesMapping();
    // Task 15
    void inlineCategoryEditorCommits();
    // Bug fix: inline editor must be a viewport overlay, not a scene proxy
    // (embedded proxies inherit the view transform — popups zoom — and the
    // tool-routed GraphScene swallows their key events).
    void inlineCategoryEditorIsNotSceneEmbedded();
};

void TstPatchbayView::rebuildPopulatesScene()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    model.setInputs(in);

    SyncPatchbayView view;
    view.setModel(&model);

    QCOMPARE(view.nodeCount(), 3);      // palm + hub + remote:acc-1
    QCOMPARE(view.wireCount(), 1);
    QVERIFY(view.strandCount() >= 4);   // 4 whole-domain strands minimum
    QVERIFY(view.wireItem("m1") != nullptr);
}

void TstPatchbayView::columnsAreOrdered()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    const QPointF palm = view.nodePos("palm");
    const QPointF hub = view.nodePos("hub");
    const QPointF remote = view.nodePos("remote:acc-1");
    QVERIFY(palm.x() < hub.x());
    QVERIFY(hub.x() < remote.x());
}

void TstPatchbayView::wireCarriesState()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1",
                           "OneWayUpload"));
    model.setInputs(in);
    SyncPatchbayView view;
    view.setModel(&model);
    QCOMPARE(view.wireItem("m1")->wireState(), WireState::OneWayUpload);
}

void TstPatchbayView::dragConnectCreatesMapping()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    view.requestEdgeForTest("hub", "cat:calendar/Work@r",
                            "remote:acc-1", "col:cal1|calendar@l");
    QCOMPARE(model.mappings().size(), 1);
    QCOMPARE(view.wireCount(), 1);

    // reverse drag direction also works
    view.requestEdgeForTest("remote:acc-1", "col:cal1|calendar@l",
                            "hub", "dom:calendar@r");
    QCOMPARE(model.mappings().size(), 2);
}

void TstPatchbayView::dragOnPalmTierIgnored()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);
    view.requestEdgeForTest("palm", "slot:DatebookDB/1@r",
                            "hub", "cat:calendar/Work@l");
    QCOMPARE(model.mappings().size(), 0);   // strands are not user-wirable
}

void TstPatchbayView::deleteSelectedRemovesMapping()
{
    PatchbayModel model;
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    model.setInputs(in);
    SyncPatchbayView view;
    view.setModel(&model);

    view.wireItem("m1")->graphicsItem()->setSelected(true);
    view.deleteSelectedWires();
    QCOMPARE(model.mappings().size(), 0);
    QCOMPARE(view.wireCount(), 0);
}

void TstPatchbayView::inlineCategoryEditorCommits()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    view.openCategoryEditorForTest("calendar");
    QVERIFY(view.categoryEditorVisible());
    view.commitCategoryEditorForTest("Offsite");

    bool found = false;
    for (const auto &n : model.nodes()) {
        if (n.id != "hub") continue;
        for (const auto &b : n.bands)
            for (const auto &p : b.ports)
                if (p.id == "cat:calendar/Offsite") found = true;
    }
    QVERIFY(found);
    QVERIFY(!view.categoryEditorVisible());
}

void TstPatchbayView::inlineCategoryEditorIsNotSceneEmbedded()
{
    PatchbayModel model;
    model.setInputs(baseInputs());
    SyncPatchbayView view;
    view.setModel(&model);

    view.openCategoryEditorForTest("calendar");
    QVERIFY(view.categoryEditorVisible());
    // The editor must live on the viewport, NOT inside the GraphScene as a
    // QGraphicsProxyWidget. An embedded proxy inherits the view transform
    // (its context menu zooms with the wheel) and the tool-routed scene
    // never delivers key events to it (can't type).
    QVERIFY(!view.categoryEditorEmbeddedInSceneForTest());
}

WILDPALMS_QTEST_MAIN(TstPatchbayView)
#include "tst_patchbay_view.moc"
