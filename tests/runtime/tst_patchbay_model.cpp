// tests/runtime/tst_patchbay_model.cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>

#include "../../src/app/patchbay/patchbaymodel.h"
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

const NodeDesc *nodeById(const QList<NodeDesc> &nodes, const QString &id)
{
    for (const auto &n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const BandDesc *bandByDomain(const NodeDesc &n, const QString &domain)
{
    for (const auto &b : n.bands)
        if (b.domain == domain) return &b;
    return nullptr;
}

} // namespace

class TstPatchbayModel : public QObject {
    Q_OBJECT
private slots:
    // Task 6
    void hubHasBandPerConduit();
    void hubBandPortsOrderedWholeThenCategoriesThenAdd();
    void hubCategoriesUnionDesiredAndRows();
    void palmNodeBandsAndSlots();
    void disconnectedDeviceGhostsPalmNode();
    // Task 7
    void remoteNodePerProvider();
    void portPerCollectionDomainPairing();
    void busyProviderShowsSubtitleAndNoPorts();
    void missingAccountSynthesizesGhostNode();
    // Task 8
    void wireFromWholeDomainRow();
    void wireFromCategoryRow();
    void wireStates();
    void strandsSolidGhostAndNoFreeSlot();
    // Task 9
    void addMappingCreatesRowAndWire();
    void addMappingRejectsDuplicateAndMismatch();
    void removeAndUpdateMapping();
    void addRemoveCategory();
};

void TstPatchbayModel::hubHasBandPerConduit()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *hub = nodeById(m.nodes(), "hub");
    QVERIFY(hub);
    QCOMPARE(hub->kind, NodeKind::Hub);
    QCOMPARE(hub->bands.size(), 4);          // descriptor-driven
    QVERIFY(bandByDomain(*hub, "calendar"));
    QVERIFY(bandByDomain(*hub, "note"));     // memo's domain is "note"
}

void TstPatchbayModel::hubBandPortsOrderedWholeThenCategoriesThenAdd()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    QVERIFY(band);
    QVERIFY(band->ports.size() >= 4);        // All + Work + Personal + add
    QCOMPARE(band->ports.first().kind, PortKind::WholeDomain);
    QCOMPARE(band->ports.first().id, QStringLiteral("dom:calendar"));
    QCOMPARE(band->ports[1].id, QStringLiteral("cat:calendar/Work"));
    QCOMPARE(band->ports.last().kind, PortKind::AddCategory);
}

void TstPatchbayModel::hubCategoriesUnionDesiredAndRows()
{
    auto in = baseInputs();
    // a row referencing a category NOT in desiredCategories must still get a port
    in.mappings.append(row("m1", "calendar", "palm:calendar/name:Conferences",
                           "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    bool found = false;
    for (const auto &p : band->ports)
        if (p.id == QStringLiteral("cat:calendar/Conferences")) found = true;
    QVERIFY(found);
}

void TstPatchbayModel::palmNodeBandsAndSlots()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QCOMPARE(palm->kind, NodeKind::Palm);
    QVERIFY(!palm->ghosted);
    QCOMPARE(palm->title, QStringLiteral("Palm m515"));
    const auto *db = bandByDomain(*palm, "calendar");
    QVERIFY(db);
    // ports: slot 0 Unfiled, slot 1 Work, slot 2 Personal (empty slots skipped)
    QCOMPARE(db->ports.size(), 3);
    QCOMPARE(db->ports[1].id, QStringLiteral("slot:DatebookDB/1"));
    QCOMPARE(db->ports[1].label, QStringLiteral("Work"));
}

void TstPatchbayModel::disconnectedDeviceGhostsPalmNode()
{
    auto in = baseInputs();
    in.deviceConnected = false;
    PatchbayModel m;
    m.setInputs(in);
    const auto *palm = nodeById(m.nodes(), "palm");
    QVERIFY(palm);
    QVERIFY(palm->ghosted);                  // node never vanishes (spec §5.1)
    QVERIFY(!palm->bands.isEmpty());         // snapshot still rendered
}

void TstPatchbayModel::remoteNodePerProvider()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QVERIFY(nc);
    QCOMPARE(nc->kind, NodeKind::Remote);
    QCOMPARE(nc->title, QStringLiteral("Nextcloud"));
    QCOMPARE(nc->bands.size(), 1);
    QCOMPARE(nc->bands.first().ports.size(), 1);
    QCOMPARE(nc->bands.first().ports.first().id,
             QStringLiteral("col:cal1|calendar"));
}

void TstPatchbayModel::portPerCollectionDomainPairing()
{
    auto in = baseInputs();
    // a mixed VEVENT+VTODO collection matches BOTH calendar and todo conduits
    CollectionInfo mixed;
    mixed.id = "cal2"; mixed.name = "Mixed"; mixed.type = "calendar";
    in.conduits[0].matchesCollection =
        [](const CollectionInfo &c) { return c.type == "calendar"; };
    in.conduits[3].matchesCollection =
        [](const CollectionInfo &c) { return c.id == "cal2"; };   // VTODO-capable
    in.providers[0].collections << mixed;
    PatchbayModel m;
    m.setInputs(in);
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QStringList ids;
    for (const auto &p : nc->bands.first().ports) ids << p.id;
    QVERIFY(ids.contains("col:cal2|calendar"));
    QVERIFY(ids.contains("col:cal2|todo"));     // spec §5.3
}

void TstPatchbayModel::busyProviderShowsSubtitleAndNoPorts()
{
    auto in = baseInputs();
    in.providers[0].busyText = QStringLiteral("Connecting…");
    PatchbayModel m;
    m.setInputs(in);
    const auto *nc = nodeById(m.nodes(), "remote:acc-1");
    QCOMPARE(nc->subtitle, QStringLiteral("Connecting…"));
    QVERIFY(nc->bands.first().ports.isEmpty());
}

void TstPatchbayModel::missingAccountSynthesizesGhostNode()
{
    auto in = baseInputs();
    in.mappings.append(row("m9", "calendar", "", "gone-uuid:calX", "calX"));
    PatchbayModel m;
    m.setInputs(in);
    const auto *ghost = nodeById(m.nodes(), "ghost:gone-uuid");
    QVERIFY(ghost);                              // never silently drop (spec §10)
    QCOMPARE(ghost->kind, NodeKind::GhostRemote);
    QVERIFY(ghost->ghosted);
    QCOMPARE(ghost->bands.first().ports.first().id,
             QStringLiteral("col:calX|calendar"));
}

void TstPatchbayModel::wireFromWholeDomainRow()
{
    auto in = baseInputs();
    in.mappings.append(row("m1", "calendar", "", "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    QCOMPARE(m.wires().size(), 1);
    const WireDesc w = m.wires().first();
    QCOMPARE(w.mappingId, QStringLiteral("m1"));
    QCOMPARE(w.sourcePortId, QStringLiteral("dom:calendar"));
    QCOMPARE(w.targetNodeId, QStringLiteral("remote:acc-1"));
    QCOMPARE(w.targetPortId, QStringLiteral("col:cal1|calendar"));
    QCOMPARE(w.state, WireState::TwoWay);
}

void TstPatchbayModel::wireFromCategoryRow()
{
    auto in = baseInputs();
    in.mappings.append(row("m2", "calendar", "palm:calendar/name:Work",
                           "acc-1:cal1", "cal1"));
    PatchbayModel m;
    m.setInputs(in);
    QCOMPARE(m.wires().first().sourcePortId,
             QStringLiteral("cat:calendar/Work"));
}

void TstPatchbayModel::wireStates()
{
    auto in = baseInputs();
    in.mappings.append(row("up",   "calendar", "", "acc-1:cal1", "cal1", "OneWayUpload"));
    in.mappings.append(row("off",  "calendar", "", "acc-1:cal1", "cal1", "TwoWay", false));
    in.mappings.append(row("bad",  "calendar", "", "acc-1:cal1", "cal1"));
    in.mappings.append(row("gone", "calendar", "", "nope:calX",  "calX"));
    in.routeStatuses.insert("bad", RouteStatus::NotARoute);
    PatchbayModel m;
    m.setInputs(in);
    QHash<QString, WireState> got;
    for (const auto &w : m.wires()) got[w.mappingId] = w.state;
    QCOMPARE(got["up"],   WireState::OneWayUpload);
    QCOMPARE(got["off"],  WireState::Disabled);     // enabled=false wins over NotARoute
    QCOMPARE(got["bad"],  WireState::Broken);
    QCOMPARE(got["gone"], WireState::Broken);       // ghost target
    // broken wires carry the ✗ bead glyph
    for (const auto &w : m.wires())
        if (w.state == WireState::Broken) QCOMPARE(w.beadGlyph, QStringLiteral("✗"));
}

void TstPatchbayModel::strandsSolidGhostAndNoFreeSlot()
{
    auto in = baseInputs();
    // "Work" IS in the DatebookDB snapshot (slot 1) → Solid strand.
    // "Offsite" is desired but NOT in the snapshot → Ghost strand (waiting).
    // "Stuffed" desired, and a row referencing it reports NoFreeSlot → no
    // strand, port flagged.
    in.desiredCategories["DatebookDB"] = {"Work", "Offsite", "Stuffed"};
    in.mappings.append(row("ns", "calendar", "palm:calendar/name:Stuffed",
                           "acc-1:cal1", "cal1"));
    in.routeStatuses.insert("ns", RouteStatus::NoFreeSlot);
    PatchbayModel m;
    m.setInputs(in);

    QHash<QString, StrandDesc> byHubPort;
    for (const auto &s : m.strands()) byHubPort[s.hubPortId] = s;

    // whole-domain strand per conduit band that has a snapshot
    QVERIFY(byHubPort.contains("dom:calendar"));
    QVERIFY(byHubPort["dom:calendar"].wholeDomain);

    QCOMPARE(byHubPort["cat:calendar/Work"].state, StrandState::Solid);
    QCOMPARE(byHubPort["cat:calendar/Work"].palmPortId,
             QStringLiteral("slot:DatebookDB/1"));

    QCOMPARE(byHubPort["cat:calendar/Offsite"].state, StrandState::Ghost);
    QCOMPARE(byHubPort["cat:calendar/Offsite"].palmPortId,
             QStringLiteral("db:DatebookDB"));

    QVERIFY(!byHubPort.contains("cat:calendar/Stuffed"));
    const auto *band = bandByDomain(*nodeById(m.nodes(), "hub"), "calendar");
    for (const auto &p : band->ports)
        if (p.id == "cat:calendar/Stuffed") QVERIFY(p.noFreeSlot);
}

void TstPatchbayModel::addMappingCreatesRowAndWire()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QSignalSpy spy(&m, &PatchbayModel::mappingsChanged);

    const QString id = m.addMapping("cat:calendar/Work", "acc-1", "cal1");
    QVERIFY(!id.isEmpty());
    QCOMPARE(spy.count(), 1);
    const QJsonObject r = m.mappingById(id);
    QCOMPARE(r["sourceBackend"].toString(), QStringLiteral("calendar"));
    QCOMPARE(r["sourceCalendar"].toString(),
             QStringLiteral("palm:calendar/name:Work"));
    QCOMPARE(r["targetBackend"].toString(), QStringLiteral("acc-1:cal1"));
    QCOMPARE(r["targetCalendar"].toString(), QStringLiteral("cal1"));
    QCOMPARE(r["mode"].toString(), QStringLiteral("TwoWay"));
    QCOMPARE(r["conflictPolicy"].toString(), QStringLiteral("LastWriteWins"));
    QVERIFY(r["enabled"].toBool());
    QCOMPARE(m.wires().size(), 1);
}

void TstPatchbayModel::addMappingRejectsDuplicateAndMismatch()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QVERIFY(!m.addMapping("dom:calendar", "acc-1", "cal1").isEmpty());
    // exact duplicate
    QVERIFY(m.addMapping("dom:calendar", "acc-1", "cal1").isEmpty());
    // domain mismatch: contacts conduit does not match a calendar collection
    QVERIFY(m.addMapping("dom:contacts", "acc-1", "cal1").isEmpty());
    // unknown provider/collection
    QVERIFY(m.addMapping("dom:calendar", "nope", "cal1").isEmpty());
    QCOMPARE(m.wires().size(), 1);
}

void TstPatchbayModel::removeAndUpdateMapping()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    const QString id = m.addMapping("dom:calendar", "acc-1", "cal1");

    QJsonObject changes;
    changes["mode"] = "OneWayUpload";
    changes["enabled"] = true;
    QVERIFY(m.updateMapping(id, changes));
    QCOMPARE(m.wires().first().state, WireState::OneWayUpload);

    QVERIFY(m.removeMapping(id));
    QVERIFY(m.wires().isEmpty());
    QVERIFY(!m.removeMapping(id));   // already gone
}

void TstPatchbayModel::addRemoveCategory()
{
    PatchbayModel m;
    m.setInputs(baseInputs());
    QSignalSpy spy(&m, &PatchbayModel::desiredCategoriesChanged);

    QVERIFY(m.addCategory("calendar", "Offsite"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("DatebookDB"));
    QVERIFY(spy.first().at(1).toStringList().contains("Offsite"));
    QVERIFY(!m.addCategory("calendar", "offsite"));   // case-insensitive dup
    QVERIFY(!m.addCategory("calendar", ""));

    // removeCategory refuses while a row references it
    const QString id = m.addMapping("cat:calendar/Offsite", "acc-1", "cal1");
    QVERIFY(!m.removeCategory("calendar", "Offsite"));
    QVERIFY(m.removeMapping(id));
    QVERIFY(m.removeCategory("calendar", "Offsite"));
}

WILDPALMS_QTEST_MAIN(TstPatchbayModel)
#include "tst_patchbay_model.moc"
