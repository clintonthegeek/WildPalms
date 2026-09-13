#include <QTest>

// WildPalms todo plugin
#include "tododomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/todocodec.h"          // Todo POD, encodeTodo/decodeTodo
#include "palm/calendar/categorymappingstore.h"

// libkalburator shape graph + todo canon (via Kalburator::Sync)
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/shape/transformationregistry.h>
#include <kalburator/shape/shape.h>
#include <kalburator/shape/pipeline.h>
#include <kalburator/shape/lossprofile.h>
#include <kalburator/todo/tododomaindefinition.h>
#include <kalburator/todo/todostockshapes.h>

using namespace Kalburator::Shape;
using WildPalms::TodoPlugin::TodoPalmShapes;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;

namespace {

ShapeRegistries makeTodoRegistries(
    const WildPalms::PalmCalendar::CategoryMappingStore *cats = nullptr)
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Todo::TodoDomainDefinition def;
    const auto spine = def.canonicalSpine();
    if (!spine.isEmpty()) {
        const auto &[rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(def.domain(), rootShape);
        for (int i = 1; i < spine.size(); ++i) {
            const auto &[s, cat] = spine.at(i);
            reg.registerShape(s, cat);
            reg.appendCanonicalVersion(def.domain(), s);
        }
    }

    Kalburator::Todo::TodoStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (todo, palm) + palm<->ical-vtodo edges. Not part of stock; the
    // palm->ical-vtodo->canon path only compiles once these run.
    TodoPalmShapes wpShapes{cats};
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        reg.registerEdge(edge);

    return regs;
}

// Build a (todo, palm) record's wire bytes carrying a known recordId + content.
// The recordId identity stamp (X-WP-PALM-RECORDID) is applied by the transcoder
// (encodePalmToIcs reads it from PalmRecord::recordId), so we set it here.
QByteArray makePalmTodoBytes(quint8 slot, quint32 recordId,
                             const QString &description)
{
    Todo t;
    t.description = description;
    t.priority    = 1;        // highest
    t.isComplete  = false;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = encodeTodo(t);
    return pr.toWireBytes();
}

} // namespace

class TestTodoCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmTodoBytes(/*slot*/ 2, /*recordId*/ 77,
                              QStringLiteral("Buy milk"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->ical-vtodo->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        // Decode the round-tripped palm wire and check summary + recordId survive.
        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 77u);
        const auto decoded = decodeTodo(QByteArrayView(pr2.data));
        QVERIFY2(decoded.has_value(), "round-tripped palm todo did not decode");
        QCOMPARE(decoded->description, QStringLiteral("Buy milk"));
    }

    void palmCategorySlotCarriedAsCanonCategory()
    {
        WildPalms::PalmCalendar::CategoryMappingStore cats;
        QVERIFY(cats.setSlotName(QStringLiteral("ToDoDB"), 3,
                                 QStringLiteral("Work")));

        const auto regs = makeTodoRegistries(&cats);
        const Shape palm  { DomainId{"todo"}, EncodingId{"palm"}      };
        const Shape canon { DomainId{"todo"}, EncodingId{"canon"}     };
        const Shape vtodo { DomainId{"todo"}, EncodingId{"ical-vtodo"} };

        const QByteArray palmBytes =
            makePalmTodoBytes(/*slot*/ 3, /*recordId*/ 88,
                              QStringLiteral("Prepare report"));

        // palm -> ical-vtodo: slot-3 name "Work" should land in CATEGORIES.
        const auto toVtodo = regs.transformation.compile(palm, vtodo);
        QVERIFY2(toVtodo.has_value(), "no palm->ical-vtodo pipeline");
        const QByteArray vtodoBytes = toVtodo->apply(palmBytes);
        QVERIFY2(vtodoBytes.contains("CATEGORIES:Work"),
                 "palm slot 3 ('Work') did not surface as CATEGORIES:Work");

        // palm -> canon: canon path also compiles and is non-empty.
        const auto toCanon = regs.transformation.compile(palm, canon);
        QVERIFY2(toCanon.has_value(), "no palm->canon pipeline");
        const QByteArray canonBytes = toCanon->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        // canon -> palm: CATEGORIES "Work" should map back to slot 3.
        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(static_cast<int>(pr2.category), 3);
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        QVERIFY2(regs.transformation.inspect(palm, canon).isLossless(),
                 "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm cannot hold most todo fields)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Dropped);
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Degraded);
        // C: `categories` is NO LONGER dropped — the Palm category slot now
        // round-trips via the canonical CATEGORIES field. Guard the invariant.
        QVERIFY2(!down.affected.contains(PropertyId{QStringLiteral("categories")}),
                 "categories must no longer be in the canon->palm loss profile");
    }
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
