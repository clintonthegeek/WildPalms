#include <QtTest/QtTest>

#include "plugins/contacts/contactsconflicthandler.h"
#include "plugins/contacts/contactsvcardtranscoder.h"

#include "palm/codecs/contactcodec.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"

#include <kalburator/conflict/conflictpolicy.h>
#include <kalburator/conflict/conflictrecord.h>

using WildPalms::ContactsPlugin::ContactsConflictHandler;
using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;
using Kalburator::Conflict::ConflictDecision;
using Kalburator::Conflict::ConflictPolicy;
using Kalburator::Conflict::ConflictRecord;
using Kalburator::Conflict::ConflictType;

namespace {

QByteArray makeContactVcard(const Contact &c,
                            std::uint32_t recordId = 1,
                            int slot = 0,
                            bool secret = false)
{
    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = static_cast<std::uint8_t>(slot);
    pr.data = encodeContact(c);
    if (secret) pr.attributes |= PalmRecord::AttrSecret;
    return WildPalms::ContactsPlugin::encodePalmToVcard(pr, /*cats*/ nullptr, /*dbName*/ {});
}

ConflictRecord makeConflict(const QByteArray &sourceBytes,
                            const QByteArray &targetBytes)
{
    ConflictRecord c;
    c.conflictId = QStringLiteral("c-contact-1");
    c.type = ConflictType::BothModified;
    c.source.id = QStringLiteral("palm-contact:1");
    c.target.id = QStringLiteral("palm-contact:1");
    c.source.content = sourceBytes;
    c.target.content = targetBytes;
    c.source.contentType = QStringLiteral("text/vcard");
    c.target.contentType = QStringLiteral("text/vcard");
    c.source.lastModified = QDateTime(QDate(2026, 4, 25), QTime(10, 0));
    c.target.lastModified = QDateTime(QDate(2026, 4, 25), QTime(11, 0));
    return c;
}

ConflictPolicy defaultPolicy()
{
    ConflictPolicy p;
    return p;
}

} // namespace

class TestContactsConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void mergesPhoneSlotUnion_whenNoSingleFieldsDiffer();
    void mergesCustomSlotUnion();
    void mergesPhoneAndCustom_inOneCall();
    void delegatesWhenLastNameDiffers();
    void delegatesWhenSamePhoneSlotHasDifferentValues();
    void delegatesWhenSecretBitDiffers();
    void delegatesWhenSourceVcardDoesNotDecode();
};

void TestContactsConflictHandler::mergesPhoneSlotUnion_whenNoSingleFieldsDiffer()
{
    // Note: vCard 4.0 has no Palm-style phone-slot index, so the transcoder
    // re-packs phone numbers into the lowest free slots on decode. To
    // exercise the per-slot union the test inputs are arranged so that
    // *after* the vCard round-trip the two sides occupy different slots:
    // source has two phones (-> phone[0] and phone[1]); target has only
    // the shared one (-> phone[0], phone[1] empty). Union merge keeps
    // both, matching what the overlay must do.
    Contact src;
    src.lastName  = QStringLiteral("Doe");
    src.firstName = QStringLiteral("John");
    src.phone[0]  = QStringLiteral("555-0000");
    src.phone[1]  = QStringLiteral("555-2222");
    src.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Mobile") };

    Contact tgt;
    tgt.lastName  = QStringLiteral("Doe");
    tgt.firstName = QStringLiteral("John");
    tgt.phone[0]  = QStringLiteral("555-0000");
    tgt.phoneLabels = { QStringLiteral("Work") };

    auto c = makeConflict(makeContactVcard(src), makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    ConflictDecision d = h.handleConflict(c, defaultPolicy());
    QCOMPARE(d, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("field-union"));

    auto merged = WildPalms::ContactsPlugin::decodeVcardToPalm(c.mergedContent, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(merged.has_value());
    auto mc = decodeContact(QByteArrayView(merged->data));
    QVERIFY(mc.has_value());
    QCOMPARE(mc->phone[0], QStringLiteral("555-0000"));
    QCOMPARE(mc->phone[1], QStringLiteral("555-2222"));
}

void TestContactsConflictHandler::mergesCustomSlotUnion()
{
    Contact src;
    src.lastName  = QStringLiteral("Doe");
    src.firstName = QStringLiteral("Jane");
    src.custom[1] = QStringLiteral("hobby:woodworking");

    Contact tgt;
    tgt.lastName  = QStringLiteral("Doe");
    tgt.firstName = QStringLiteral("Jane");
    tgt.custom[2] = QStringLiteral("nickname:Janie");

    auto c = makeConflict(makeContactVcard(src), makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    ConflictDecision d = h.handleConflict(c, defaultPolicy());
    QCOMPARE(d, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("field-union"));

    auto merged = WildPalms::ContactsPlugin::decodeVcardToPalm(c.mergedContent, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(merged.has_value());
    auto mc = decodeContact(QByteArrayView(merged->data));
    QVERIFY(mc.has_value());
    QCOMPARE(mc->custom[1], QStringLiteral("hobby:woodworking"));
    QCOMPARE(mc->custom[2], QStringLiteral("nickname:Janie"));
}

void TestContactsConflictHandler::mergesPhoneAndCustom_inOneCall()
{
    // Source side adds a second phone on top of a shared one; target
    // side leaves the second-phone slot empty but adds custom[0]. The
    // per-slot union must keep both edits.
    Contact src;
    src.lastName  = QStringLiteral("Smith");
    src.firstName = QStringLiteral("Pat");
    src.phone[0]  = QStringLiteral("555-0000");
    src.phone[1]  = QStringLiteral("555-1111");
    src.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Home") };

    Contact tgt;
    tgt.lastName  = QStringLiteral("Smith");
    tgt.firstName = QStringLiteral("Pat");
    tgt.phone[0]  = QStringLiteral("555-0000");
    tgt.phoneLabels = { QStringLiteral("Work") };
    tgt.custom[0] = QStringLiteral("favourite-colour:teal");

    auto c = makeConflict(makeContactVcard(src), makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    ConflictDecision d = h.handleConflict(c, defaultPolicy());
    QCOMPARE(d, ConflictDecision::Merge);
    QCOMPARE(h.lastOverlay(), QStringLiteral("field-union"));

    auto merged = WildPalms::ContactsPlugin::decodeVcardToPalm(c.mergedContent, /*cats*/ nullptr, /*dbName*/ {});
    QVERIFY(merged.has_value());
    auto mc = decodeContact(QByteArrayView(merged->data));
    QVERIFY(mc.has_value());
    QCOMPARE(mc->phone[0], QStringLiteral("555-0000"));
    QCOMPARE(mc->phone[1], QStringLiteral("555-1111"));
    QCOMPARE(mc->custom[0], QStringLiteral("favourite-colour:teal"));
}

void TestContactsConflictHandler::delegatesWhenLastNameDiffers()
{
    Contact src;
    src.lastName  = QStringLiteral("Doe");
    src.firstName = QStringLiteral("John");

    Contact tgt;
    tgt.lastName  = QStringLiteral("Smith");
    tgt.firstName = QStringLiteral("John");

    auto c = makeConflict(makeContactVcard(src), makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    // SourceAlwaysWins -> PalmConflictHandler returns UseSource.
    const ConflictPolicy policy = ConflictPolicy::autoSourceWins();

    ConflictDecision d = h.handleConflict(c, policy);
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
    QCOMPARE(d, ConflictDecision::UseSource);
}

void TestContactsConflictHandler::delegatesWhenSamePhoneSlotHasDifferentValues()
{
    Contact src;
    src.lastName  = QStringLiteral("Doe");
    src.firstName = QStringLiteral("John");
    src.phone[2]  = QStringLiteral("555-AAAA");
    src.phoneLabels = { QStringLiteral("Mobile") };

    Contact tgt;
    tgt.lastName  = QStringLiteral("Doe");
    tgt.firstName = QStringLiteral("John");
    tgt.phone[2]  = QStringLiteral("555-BBBB");
    tgt.phoneLabels = { QStringLiteral("Mobile") };

    auto c = makeConflict(makeContactVcard(src), makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestContactsConflictHandler::delegatesWhenSecretBitDiffers()
{
    Contact src;
    src.lastName  = QStringLiteral("Doe");
    src.firstName = QStringLiteral("John");

    Contact tgt;
    tgt.lastName  = QStringLiteral("Doe");
    tgt.firstName = QStringLiteral("John");

    auto c = makeConflict(makeContactVcard(src, /*recordId*/1, /*slot*/0, /*secret*/true),
                          makeContactVcard(tgt, /*recordId*/1, /*slot*/0, /*secret*/false));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

void TestContactsConflictHandler::delegatesWhenSourceVcardDoesNotDecode()
{
    Contact tgt;
    tgt.lastName  = QStringLiteral("Doe");
    tgt.firstName = QStringLiteral("John");

    auto c = makeConflict(QByteArray("not-a-vcard-at-all"),
                          makeContactVcard(tgt));

    MockPalmDatabaseAccess device;
    PalmBackendConfig cfg;
    ContactsConflictHandler h(&device, &cfg);

    h.handleConflict(c, defaultPolicy());
    QCOMPARE(h.lastOverlay(), QStringLiteral("delegated"));
}

QTEST_MAIN(TestContactsConflictHandler)
#include "tst_contactsconflicthandler.moc"
