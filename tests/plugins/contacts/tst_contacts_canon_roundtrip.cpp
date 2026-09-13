#include <QTest>

// WildPalms contacts plugin
#include "contactsdomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/contactcodec.h"
#include "palm/calendar/categorymappingstore.h"

// libkalburator shape graph + contacts canon (via Kalburator::Sync)
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/shape/transformationregistry.h>
#include <kalburator/shape/shape.h>
#include <kalburator/shape/pipeline.h>
#include <kalburator/shape/lossprofile.h>
#include <kalburator/contacts/contactsdomaindefinition.h>
#include <kalburator/contacts/contactsstockshapes.h>

using namespace Kalburator::Shape;
using WildPalms::ContactsPlugin::ContactsPalmShapes;
using WildPalms::PalmSync::PalmRecord;

namespace {

ShapeRegistries makeContactsRegistries(
    const WildPalms::PalmCalendar::CategoryMappingStore *cats = nullptr)
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Contacts::ContactsDomainDefinition def;
    const auto spine = def.canonicalSpine();        // [ (vcard4, cat), (canon, cat) ]
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
    // If the spine were empty, compile(palm, canon) below returns nullopt and
    // the test's QVERIFY2 fails cleanly rather than crashing.

    Kalburator::Contacts::ContactsStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // Register the WildPalms palm<->vcard4 edges; not part of libkalburator's
    // stock shapes, so the palm->vcard4->canon path only compiles once these run.
    ContactsPalmShapes wpShapes{cats};
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        reg.registerEdge(edge);

    return regs;
}

QByteArray makePalmRecordBytes(quint8 slot, quint32 recordId, bool secret,
                               const QString &last, const QString &first)
{
    WildPalms::PalmCodecs::Contact c;
    c.lastName  = last;
    c.firstName = first;

    PalmRecord pr;
    pr.data     = WildPalms::PalmCodecs::encodeContact(c);
    pr.category = slot;
    pr.recordId = recordId;
    if (secret)
        pr.attributes |= PalmRecord::AttrSecret;
    return pr.toWireBytes();
}

} // namespace

class TestContactsCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeContactsRegistries();
        const Shape palm { DomainId{"contacts"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"contacts"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 3, /*recordId*/ 42, /*secret*/ true,
                                QStringLiteral("Lovelace"), QStringLiteral("Ada"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(), "no palm->canon pipeline (palm->vcard4->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);

        QCOMPARE(pr2.recordId, std::uint32_t{42});
        QVERIFY2(pr2.isSecret(), "secret bit lost across canon round-trip");

        const auto c2 = WildPalms::PalmCodecs::decodeContact(QByteArrayView(pr2.data));
        QVERIFY2(c2.has_value(), "could not decode round-tripped contact body");
        QCOMPARE(c2->lastName,  QStringLiteral("Lovelace"));
        QCOMPARE(c2->firstName, QStringLiteral("Ada"));
    }

    void palmCategorySlotCarriedAsCanonCategory()
    {
        WildPalms::PalmCalendar::CategoryMappingStore cats;
        QVERIFY(cats.setSlotName(QStringLiteral("AddressDB"), 3,
                                 QStringLiteral("Work")));

        const auto regs = makeContactsRegistries(&cats);
        const Shape palm  { DomainId{"contacts"}, EncodingId{"palm"}   };
        const Shape canon { DomainId{"contacts"}, EncodingId{"canon"}  };
        const Shape vcard4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 3, /*recordId*/ 88, /*secret*/ false,
                                QStringLiteral("Turing"), QStringLiteral("Alan"));

        // palm -> vcard4: the slot-3 name "Work" should land in CATEGORIES.
        const auto toVcard = regs.transformation.compile(palm, vcard4);
        QVERIFY2(toVcard.has_value(), "no palm->vcard4 pipeline");
        const QByteArray vcardBytes = toVcard->apply(palmBytes);
        QVERIFY2(vcardBytes.contains("CATEGORIES:Work"),
                 "palm slot 3 ('Work') did not surface as CATEGORIES:Work in vCard");

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
        const auto regs = makeContactsRegistries();
        const Shape palm { DomainId{"contacts"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"contacts"}, EncodingId{"canon"} };

        const LossProfile up = regs.transformation.inspect(palm, canon);
        QVERIFY2(up.isLossless(), "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(), "canon->palm must report loss (Palm cannot hold most canon fields)");
        // C: `categories` is NO LONGER dropped — the Palm category slot now
        // round-trips via the canonical CATEGORIES field. Guard the invariant.
        QVERIFY2(!down.affected.contains(PropertyId{QStringLiteral("categories")}),
                 "categories must no longer be in the canon->palm loss profile");
    }
};

QTEST_GUILESS_MAIN(TestContactsCanonRoundtrip)
#include "tst_contacts_canon_roundtrip.moc"
