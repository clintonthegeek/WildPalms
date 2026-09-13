// tests/runtime/tst_targetpickerpage.cpp
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/targetpickerpage.h"
#include "app/wizard/targetpickerrow.h"
#include "app/wizard/wizardstate.h"
#include "runtime/conduitcatalog.h"
#include "plugins/pimplugin.h"   // complete type: vector<unique_ptr<PimPlugin>> destructor

#include <kalburator/types/collectioninfo.h>

using WildPalms::Wizard::TargetPickerPage;
using WildPalms::Wizard::TargetPickerRow;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::WizardAccount;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::TargetKind;
using Kalburator::Sync::CollectionInfo;

namespace {

WizardState seedState() {
    WizardState s;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m;
        m.pluginId = pid;
        m.kind     = TargetKind::RawFiles;
        s.mappings.append(m);
    }
    return s;
}

CollectionInfo col(const QString &id, const QString &name,
                   const QString &type, bool readOnly = false) {
    CollectionInfo c;
    c.id = id; c.name = name; c.type = type; c.readOnly = readOnly;
    return c;
}

// One connected account: a writable calendar, a read-only calendar,
// and a todos collection. No contacts, no memos.
WizardState stateWithConnectedAccount() {
    auto s = seedState();
    WizardAccount acc;
    acc.id   = QStringLiteral("acc-1");
    acc.kind = QStringLiteral("multiproto-dav");
    acc.config.displayName = QStringLiteral("Fastmail");
    acc.connected = true;
    acc.collections = {
        col(QStringLiteral("cal-1"),  QStringLiteral("Personal"), QStringLiteral("calendar")),
        col(QStringLiteral("cal-ro"), QStringLiteral("Holidays"), QStringLiteral("calendar"), true),
        col(QStringLiteral("todo-1"), QStringLiteral("Tasks"),    QStringLiteral("todos")),
    };
    s.accounts.append(acc);
    return s;
}

QComboBox *comboFor(TargetPickerPage &page, const QString &pluginId) {
    for (auto *r : page.findChildren<TargetPickerRow*>())
        if (r->pluginId() == pluginId)
            return r->findChild<QComboBox*>();
    return nullptr;
}

} // namespace

class TstTargetPickerPage : public QObject {
    Q_OBJECT
private slots:
    void populatesDomainFilteredBindings();
    void readOnlyCollectionsAreNotSelectable();
    void selectingBindingWritesMapping();
    void localFilesResetsMapping();
    void staleBindingResetsToLocalOnRebuild();
    void hintShownWhenAccountsHaveNoMatchingCollections();
    void hintShownWhenAccountsNotYetConnected();
};

void TstTargetPickerPage::populatesDomainFilteredBindings()
{
    auto s = stateWithConnectedAccount();
    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QVERIFY(cal);
    QCOMPARE(cal->count(), 3);   // Local files + Personal + Holidays(ro)
    QCOMPARE(cal->itemText(1), QStringLiteral("Fastmail ▸ Personal"));

    auto *todo = comboFor(page, QStringLiteral("todo"));
    QVERIFY(todo);
    QCOMPARE(todo->count(), 2);  // Local files + Tasks

    auto *contacts = comboFor(page, QStringLiteral("contacts"));
    QVERIFY(contacts);
    QCOMPARE(contacts->count(), 1);  // Local files only

    auto *memo = comboFor(page, QStringLiteral("memo"));
    QVERIFY(memo);
    QCOMPARE(memo->count(), 1);
    QVERIFY(memo->isEnabled());      // no more hardcoded memo disable
}

void TstTargetPickerPage::readOnlyCollectionsAreNotSelectable()
{
    auto s = stateWithConnectedAccount();
    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QVERIFY(cal);
    auto *model = qobject_cast<QStandardItemModel*>(cal->model());
    QVERIFY(model);
    QVERIFY(cal->itemText(2).contains(QStringLiteral("read-only")));
    QVERIFY(!(model->item(2)->flags() & Qt::ItemIsEnabled));
    QVERIFY(model->item(1)->flags() & Qt::ItemIsEnabled);
}

void TstTargetPickerPage::selectingBindingWritesMapping()
{
    auto s = stateWithConnectedAccount();
    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    cal->setCurrentIndex(1);   // Fastmail ▸ Personal

    QCOMPARE(s.mappings[0].kind, TargetKind::Account);
    QCOMPARE(s.mappings[0].accountRef, QStringLiteral("acc-1"));
    QCOMPARE(s.mappings[0].collectionId, QStringLiteral("cal-1"));
}

void TstTargetPickerPage::localFilesResetsMapping()
{
    auto s = stateWithConnectedAccount();
    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = QStringLiteral("acc-1");
    s.mappings[0].collectionId = QStringLiteral("cal-1");

    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QCOMPARE(cal->currentIndex(), 1);   // selection restored from state
    cal->setCurrentIndex(0);            // back to Local files

    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    QVERIFY(s.mappings[0].accountRef.isEmpty());
    QVERIFY(s.mappings[0].collectionId.isEmpty());
}

void TstTargetPickerPage::staleBindingResetsToLocalOnRebuild()
{
    auto s = stateWithConnectedAccount();
    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = QStringLiteral("gone-account");
    s.mappings[0].collectionId = QStringLiteral("gone-col");

    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QCOMPARE(cal->currentIndex(), 0);
}

void TstTargetPickerPage::hintShownWhenAccountsHaveNoMatchingCollections()
{
    auto s = stateWithConnectedAccount();
    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    TargetPickerRow *contactsRow = nullptr;
    for (auto *r : page.findChildren<TargetPickerRow*>())
        if (r->pluginId() == QStringLiteral("contacts")) { contactsRow = r; break; }
    QVERIFY(contactsRow);
    // Account is connected but has no contacts collections -> hint visible flag.
    auto *hint = contactsRow->findChild<QLabel*>(QStringLiteral("hint"));
    QVERIFY(hint);
    QVERIFY(!hint->isHidden());
}

// Shakedown F5: an account that has NOT connected used to suppress every
// hint (zero collections ⇒ "no matching" never fired). The row must say
// why only local files are offered.
void TstTargetPickerPage::hintShownWhenAccountsNotYetConnected()
{
    auto s = seedState();
    WizardAccount acc;
    acc.id   = QStringLiteral("acc-1");
    acc.kind = QStringLiteral("multiproto-dav");
    acc.config.displayName = QStringLiteral("Fastmail");
    acc.connected = false;          // connect failed / still in progress
    s.accounts.append(acc);

    auto conduits = WildPalms::Runtime::createStockConduits();
    TargetPickerPage page(&s, &conduits);
    page.initializePage();

    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        TargetPickerRow *row = nullptr;
        for (auto *r : page.findChildren<TargetPickerRow*>())
            if (r->pluginId() == pid) { row = r; break; }
        QVERIFY(row);
        auto *hint = row->findChild<QLabel*>(QStringLiteral("hint"));
        QVERIFY(hint);
        QVERIFY2(!hint->isHidden(),
                 qPrintable(QStringLiteral("hint hidden for %1").arg(pid)));
        QVERIFY(hint->text().contains(QStringLiteral("not connected"),
                                      Qt::CaseInsensitive)
                || hint->text().contains(QStringLiteral("connecting"),
                                         Qt::CaseInsensitive));
    }
}

WILDPALMS_QTEST_MAIN(TstTargetPickerPage)
#include "tst_targetpickerpage.moc"
