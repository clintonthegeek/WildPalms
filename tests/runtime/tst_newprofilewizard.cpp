// tests/runtime/tst_newprofilewizard.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QLineEdit>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/newprofilewizard.h"
#include "app/wizard/wizardstate.h"
#include "runtime/profileregistry.h"

#include <KSharedConfig>
#include <kalburator/sync/backendregistry.h>

using WildPalms::Wizard::NewProfileWizard;
using WildPalms::Wizard::TargetKind;
using WildPalms::Runtime::ProfileRegistry;
using Kalburator::Sync::BackendRegistry;

class TstNewProfileWizard : public QObject {
    Q_OBJECT
private slots:
    void allLocalFlowReturnsAllRawFiles();
    void pageOrderIsNameAccountsBindingsReview();
    void cancelDoesNotPopulateResult();
};

namespace {
std::unique_ptr<ProfileRegistry> makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(dir.path() + QStringLiteral("/wprc"));
    auto r = std::make_unique<ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/wp-root"));
    return r;
}
} // namespace

void TstNewProfileWizard::allLocalFlowReturnsAllRawFiles()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    BackendRegistry backendReg;

    NewProfileWizard w(reg.get(), &backendReg);

    // Fill name on page 1.
    w.setPage(NewProfileWizard::NamePageId, w.page(NewProfileWizard::NamePageId));
    auto *namePage = w.page(NewProfileWizard::NamePageId);
    QVERIFY(namePage);
    auto *edit = namePage->findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("Test"));

    // Initialize all pages by walking them.
    namePage->validatePage();
    QCOMPARE(w.state()->profileName, QStringLiteral("Test"));

    // All-local default: the four mappings are RawFiles already.
    for (const auto &m : w.state()->mappings)
        QCOMPARE(m.kind, TargetKind::RawFiles);
}

void TstNewProfileWizard::pageOrderIsNameAccountsBindingsReview()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    BackendRegistry backendReg;

    NewProfileWizard w(reg.get(), &backendReg);
    QVERIFY(w.page(NewProfileWizard::AccountsPageId));
    QVERIFY(w.page(NewProfileWizard::TargetPickerPageId));
    // Strictly sequential: QWizard default ordering, no nextId() overrides.
    QCOMPARE(w.page(NewProfileWizard::NamePageId)->nextId(),
             int(NewProfileWizard::AccountsPageId));
    QCOMPARE(w.page(NewProfileWizard::AccountsPageId)->nextId(),
             int(NewProfileWizard::TargetPickerPageId));
    QCOMPARE(w.page(NewProfileWizard::TargetPickerPageId)->nextId(),
             int(NewProfileWizard::ReviewPageId));
    QCOMPARE(w.page(NewProfileWizard::ReviewPageId)->nextId(), -1);
}

void TstNewProfileWizard::cancelDoesNotPopulateResult()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    BackendRegistry backendReg;

    NewProfileWizard w(reg.get(), &backendReg);
    // Wizard not exec'd; result() should still return the default-init state.
    const auto r = w.result();
    QVERIFY(r.state.profileName.isEmpty());
    QCOMPARE(r.state.mappings.size(), 4);
    for (const auto &m : r.state.mappings)
        QCOMPARE(m.kind, TargetKind::RawFiles);
}

WILDPALMS_QTEST_MAIN(TstNewProfileWizard)
#include "tst_newprofilewizard.moc"
