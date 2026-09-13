#include <QTest>

// WildPalms memo plugin
#include "notedomainextension.h"
#include "memomarkdown.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/memocodec.h"
#include "palm/calendar/categorymappingstore.h"

// libkalburator shape graph + note canon (via Kalburator::Sync)
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/shape/transformationregistry.h>
#include <kalburator/shape/shape.h>
#include <kalburator/shape/pipeline.h>
#include <kalburator/shape/lossprofile.h>
#include <kalburator/note/notedomaindefinition.h>
#include <kalburator/note/notestockshapes.h>

using namespace Kalburator::Shape;
using WildPalms::Memo::NotePalmShapes;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::PalmCalendar::CategoryMappingStore;

namespace {

void registerNoteSpineAndStock(ShapeRegistries &regs)
{
    auto &reg = regs.transformation;

    Kalburator::Note::NoteDomainDefinition def;
    const auto spine = def.canonicalSpine();   // [ (note, canon) ]
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

    Kalburator::Note::NoteStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);
}

ShapeRegistries makeNoteRegistries()
{
    ShapeRegistries regs;
    registerNoteSpineAndStock(regs);

    // WildPalms: (note, palm) + palm<->canon edges. Not part of stock; the
    // palm->canon path only compiles once these run.
    NotePalmShapes wpShapes;
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        regs.transformation.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        regs.transformation.registerEdge(edge);

    return regs;
}

ShapeRegistries makeNoteRegistriesWithStore(const CategoryMappingStore *store)
{
    ShapeRegistries regs;
    registerNoteSpineAndStock(regs);

    // WildPalms: (note, palm) + palm<->canon edges, with a CategoryMappingStore
    // so the Palm category slot travels as a name in the YAML frontmatter.
    NotePalmShapes wpShapes(store);
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        regs.transformation.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        regs.transformation.registerEdge(edge);

    return regs;
}

// Build a (note, palm) record's wire bytes carrying a known recordId + slot.
QByteArray makePalmMemoBytes(quint8 slot, quint32 recordId, const QString &text)
{
    WildPalms::PalmCodecs::Memo memo;
    memo.text = text;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = WildPalms::PalmCodecs::encodeMemo(memo);
    return pr.toWireBytes();
}

} // namespace

class TestMemoNoteRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmMemoBytes(/*slot*/ 3, /*recordId*/ 88,
                              QStringLiteral("Buy milk\nand eggs"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->markdown->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 88u);
        QCOMPARE(static_cast<int>(pr2.category), 3);
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr2.data);
        QVERIFY2(decoded.has_value(), "round-tripped palm memo did not decode");
        QCOMPARE(decoded->text, QStringLiteral("Buy milk\nand eggs"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        // palm->canon is NOT isLossless here: libkalburator's markdown<->canon
        // edge declares frontmatter=Reversible (identity rides in providerExtras),
        // and Reversible is a non-empty affected entry. But nothing is DROPPED.
        const LossProfile up = regs.transformation.inspect(palm, canon);
        QVERIFY2(up.droppedProperties().isEmpty(),
                 "palm->canon must drop nothing");
        QCOMPARE(up.affected.value(PropertyId{QStringLiteral("frontmatter")}),
                 LossKind::Reversible);

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm holds plain text only)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("body")}),
                 LossKind::Simplified);
    }

    // Task 8: Palm category slot travels as a NAME in the YAML frontmatter when
    // a CategoryMappingStore is threaded through the stages. On the return path,
    // the name resolves back to the original slot.
    void categoryNameRoundTripViaFrontmatter()
    {
        // Seed MemoDB: slot 3 = "Work".
        CategoryMappingStore store;
        store.setSlotName(QStringLiteral("MemoDB"), 3, QStringLiteral("Work"));

        const auto regs = makeNoteRegistriesWithStore(&store);
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmMemoBytes(/*slot*/ 3, /*recordId*/ 77,
                              QStringLiteral("Work memo"));

        // palm -> canon: the intermediate markdown must contain the NAME not the
        // raw slot number so that cross-domain category routing works.
        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(), "no palm->canon pipeline with store");

        // Intercept the markdown to verify the frontmatter key carries the name.
        // We do this by re-running the palm->markdown half directly via encode().
        // (The full pipeline goes palm->markdown->canon, so we inspect via decode
        //  after running canon->palm to confirm round-trip.)

        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon with store produced empty bytes");

        // canon -> palm: the name in frontmatter must resolve back to slot 3.
        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline with store");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm with store produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 77u);
        QCOMPARE(static_cast<int>(pr2.category), 3);

        // Also verify that the intermediate markdown uses the NAME "Work" as the
        // category: value by directly exercising the encode/decode seam.
        // memomarkdown::encode() with the store writes `category: Work` (not 3).
        {
            using namespace WildPalms::Memo;
            MarkdownMemo mm;
            mm.recordId     = 77;
            mm.categorySlot = 3;
            mm.content.text = QStringLiteral("Work memo");
            const QString md = encode(mm, &store, QStringLiteral("MemoDB"));
            QVERIFY2(md.contains(QStringLiteral("category: Work")),
                     "encode() with store must write category NAME not slot number");
            QVERIFY2(!md.contains(QStringLiteral("category: 3")),
                     "encode() with store must NOT write the numeric slot");

            // decode() with the store resolves the name back to slot 3.
            const MarkdownMemo back = decode(md, &store, QStringLiteral("MemoDB"));
            QCOMPARE(back.categorySlot, 3);
        }
    }
};

QTEST_GUILESS_MAIN(TestMemoNoteRoundtrip)
#include "tst_memo_note_roundtrip.moc"
