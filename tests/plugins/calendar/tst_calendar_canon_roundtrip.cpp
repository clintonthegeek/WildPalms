#include <QTest>

// WildPalms calendar plugin
#include "calendardomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/calendar/datebookcodec.h"      // DatebookCodec, X-WP-PALM-* property names
#include "palm/calendar/categorymappingstore.h"

// libkalburator shape graph + calendar canon (via Kalburator::Sync)
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/shape/transformationregistry.h>
#include <kalburator/shape/shape.h>
#include <kalburator/shape/pipeline.h>
#include <kalburator/shape/lossprofile.h>
#include <kalburator/calendar/calendardomaindefinition.h>
#include <kalburator/calendar/calendarstockshapes.h>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

using namespace Kalburator::Shape;
using WildPalms::CalendarPlugin::CalendarPalmShapes;
using WildPalms::PalmSync::PalmRecord;

namespace {

ShapeRegistries makeCalendarRegistries(
    const WildPalms::PalmCalendar::CategoryMappingStore *cats = nullptr)
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Calendar::CalendarDomainDefinition def;
    const auto spine = def.canonicalSpine();        // [ (ical, cat), (canon, cat) ]
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

    Kalburator::Calendar::CalendarStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (calendar, palm) + palm<->ical edges. Not part of stock; the
    // palm->ical->canon path only compiles once these are registered.
    CalendarPalmShapes wpShapes{cats};
    for (const auto &[shape, cat] : wpShapes.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : wpShapes.edges())
        reg.registerEdge(edge);

    return regs;
}

// Build a (calendar, palm) record's wire bytes carrying known identity + a
// recurring event, via DatebookCodec::encode on an Event we construct.
//
// DatebookCodec::encode reads the record ID from the Event's customProperty
// ("KCalendarCore", "X-WP-PALM-RECORDID"), so we must set it that way.
QByteArray makePalmRecordBytes(quint8 slot, quint32 recordId,
                               const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
    event->setSummary(summary);
    event->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2026, 6, 1), QTime(10, 0), QTimeZone::utc()));
    event->recurrence()->setDaily(1);
    // DatebookCodec uses customProperty("KCalendarCore", RecordIdProperty) to
    // recover the record ID on encode — use the same API the codec uses.
    event->setCustomProperty(
        "KCalendarCore",
        QByteArray(WildPalms::PalmCalendar::DatebookCodec::RecordIdProperty),
        QString::number(recordId));

    PalmRecord pr = WildPalms::PalmCalendar::DatebookCodec::encode(event, slot);
    return pr.toWireBytes();
}

} // namespace

class TestCalendarCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentityAndRecurrence()
    {
        const auto regs = makeCalendarRegistries();
        const Shape palm { DomainId{"calendar"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"calendar"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 2, /*recordId*/ 77,
                                QStringLiteral("Standup"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(), "no palm->canon pipeline (palm->ical->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        // Decode the round-tripped palm wire back to an Event and check identity.
        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        const auto decoded = WildPalms::PalmCalendar::DatebookCodec::decode(pr2);
        QVERIFY2(decoded.isValid(), "round-tripped palm record did not decode");
        QCOMPARE(decoded.event->summary(), QStringLiteral("Standup"));
        QVERIFY2(decoded.event->recurs(), "daily recurrence lost across canon round-trip");

        // Record ID is stashed in the Event's customProperty by the codec.
        const QString rid = decoded.event->customProperty(
            "KCalendarCore",
            QByteArray(WildPalms::PalmCalendar::DatebookCodec::RecordIdProperty));
        QCOMPARE(rid, QStringLiteral("77"));
    }

    void palmCategorySlotCarriedAsCanonCategory()
    {
        WildPalms::PalmCalendar::CategoryMappingStore cats;
        QVERIFY(cats.setSlotName(QStringLiteral("DatebookDB"), 3,
                                 QStringLiteral("Work")));

        const auto regs = makeCalendarRegistries(&cats);
        const Shape palm { DomainId{"calendar"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"calendar"}, EncodingId{"canon"} };
        const Shape ical { DomainId{"calendar"}, EncodingId{"ical"}  };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 3, /*recordId*/ 88,
                                QStringLiteral("Sprint planning"));

        // palm -> ical: the slot-3 name "Work" should land in CATEGORIES.
        const auto toIcal = regs.transformation.compile(palm, ical);
        QVERIFY2(toIcal.has_value(), "no palm->ical pipeline");
        const QByteArray icalBytes = toIcal->apply(palmBytes);
        QVERIFY2(icalBytes.contains("CATEGORIES:Work"),
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
        const auto regs = makeCalendarRegistries();
        const Shape palm { DomainId{"calendar"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"calendar"}, EncodingId{"canon"} };

        QVERIFY2(regs.transformation.inspect(palm, canon).isLossless(),
                 "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm cannot hold most calendar fields)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("attendees")}),
                 LossKind::Dropped);
        // C: `categories` is NO LONGER dropped — the Palm category slot now
        // round-trips via the canonical CATEGORIES field. Guard the invariant.
        QVERIFY2(!down.affected.contains(PropertyId{QStringLiteral("categories")}),
                 "categories must no longer be in the canon->palm loss profile");
    }
};

QTEST_GUILESS_MAIN(TestCalendarCanonRoundtrip)
#include "tst_calendar_canon_roundtrip.moc"
