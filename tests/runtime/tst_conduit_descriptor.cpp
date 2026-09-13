// Sub-project A (config substrate): the conduit descriptor contract.
// Absorbs tst_domainfilter's matching cases (domainfilter.cpp is deleted in
// Task 5) and pins the descriptor defaults a third-party conduit relies on.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "plugins/pimplugin.h"
#include <kalburator/types/collectioninfo.h>
#include <kalburator/sync/syncbackendbase.h>   // complete type: TestConduit::createPalmBackend returns unique_ptr<SyncBackendBase>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Plugins::PimPlugin;

namespace {
// Minimal conduit: only the pure virtuals, defaults everywhere else.
class TestConduit : public PimPlugin {
public:
    explicit TestConduit(QString domain) : m_domain(std::move(domain)) {}
    QString conduitId() const override { return m_domain; }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{m_domain}; }
    QString primaryDbName() const override { return QStringLiteral("TestDB"); }
    QString conduitDisplayName() const override { return QStringLiteral("Test"); }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
private:
    QString m_domain;
};
} // namespace

class TstConduitDescriptor : public QObject {
    Q_OBJECT
private slots:
    void calendarMatching();
    void todoMatching();
    void contactsMatching();
    void noteMatching();
    void genericDomainFallback();
    void tasksOnlyCalendarExcludedFromCalendar();
    void descriptorDefaults();
};

void TstConduitDescriptor::calendarMatching()
{
    TestConduit c(QStringLiteral("calendar"));
    CollectionInfo byType;
    byType.type = QStringLiteral("calendar");
    QVERIFY(c.matchesCollection(byType));                 // empty contentTypes -> type fallback
    CollectionInfo byContent;
    byContent.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(c.matchesCollection(byContent));
    CollectionInfo contacts;
    contacts.type = QStringLiteral("contacts");
    QVERIFY(!c.matchesCollection(contacts));
}

void TstConduitDescriptor::todoMatching()
{
    TestConduit c(QStringLiteral("todo"));
    CollectionInfo vtodo;
    vtodo.type = QStringLiteral("calendar");
    vtodo.contentTypes = { QStringLiteral("VEVENT"), QStringLiteral("VTODO") };
    QVERIFY(c.matchesCollection(vtodo));
    CollectionInfo todos;
    todos.type = QStringLiteral("todos");
    QVERIFY(c.matchesCollection(todos));
    CollectionInfo eventsOnly;
    eventsOnly.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(!c.matchesCollection(eventsOnly));
}

void TstConduitDescriptor::contactsMatching()
{
    TestConduit c(QStringLiteral("contacts"));
    CollectionInfo byType;  byType.type = QStringLiteral("contacts");
    QVERIFY(c.matchesCollection(byType));
    CollectionInfo byCard;  byCard.contentTypes = { QStringLiteral("VCARD") };
    QVERIFY(c.matchesCollection(byCard));
}

void TstConduitDescriptor::noteMatching()
{
    TestConduit c(QStringLiteral("note"));
    CollectionInfo memos;  memos.type = QStringLiteral("memos");
    QVERIFY(c.matchesCollection(memos));
    CollectionInfo cal;    cal.type = QStringLiteral("calendar");
    QVERIFY(!c.matchesCollection(cal));
}

void TstConduitDescriptor::genericDomainFallback()
{
    // A third-party "document" conduit matches collections typed "document"
    // without any WildPalms source change.
    TestConduit c(QStringLiteral("document"));
    CollectionInfo doc;  doc.type = QStringLiteral("document");
    QVERIFY(c.matchesCollection(doc));
    CollectionInfo cal;  cal.type = QStringLiteral("calendar");
    QVERIFY(!c.matchesCollection(cal));
}

void TstConduitDescriptor::tasksOnlyCalendarExcludedFromCalendar()
{
    // DAV providers type everything "calendar"; contentTypes are authoritative.
    TestConduit c(QStringLiteral("calendar"));
    CollectionInfo tasksOnly;
    tasksOnly.type = QStringLiteral("calendar");
    tasksOnly.contentTypes = { QStringLiteral("VTODO") };
    QVERIFY(!c.matchesCollection(tasksOnly));
}

void TstConduitDescriptor::descriptorDefaults()
{
    TestConduit c(QStringLiteral("document"));
    QCOMPARE(c.claimedDatabases(), QStringList{ QStringLiteral("TestDB") });
    QVERIFY(c.supportsCategories());
    QVERIFY(c.categoryStore() == nullptr);
    QCOMPARE(c.categorySlotNames(), QStringList{});   // no store -> empty, no crash
    QVERIFY(c.createConflictHandler() == nullptr);
    QVERIFY(!c.hasMainView());
    QVERIFY(c.createMainView(nullptr) == nullptr);
}

WILDPALMS_QTEST_MAIN(TstConduitDescriptor)
#include "tst_conduit_descriptor.moc"
