#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QLabel>
#include <QPushButton>

#include "../wildpalms_qtest_main.h"

#include "app/accounts/accountspage.h"
#include "app/accounts/addaccountdialog.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include <kalburator/ui/accountslistwidget.h>
#include <kalburator/typesupport/backendconfiguration.h>

class TstAccountsPage : public QObject {
    Q_OBJECT
private slots:
    void emptyState_addEnabled();
    void afterAdd_listShowsProvider();
    void interlock_disablesAddRemoveDuringRun();
};

void TstAccountsPage::emptyState_addEnabled()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();
    QTest::qWait(50);

    auto *listWidget = page.findChild<Kalburator::Ui::AccountsListWidget*>();
    QVERIFY(listWidget);

    // No account rows — only the "Add account…" button is present.
    auto labels = listWidget->findChildren<QLabel*>();
    QCOMPARE(labels.size(), 0);

    auto *addBtn = listWidget->findChild<QPushButton*>(QStringLiteral("addAccount"));
    QVERIFY(addBtn);
    QVERIFY(addBtn->isEnabled());
}

void TstAccountsPage::afterAdd_listShowsProvider()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);

    Kalburator::Sync::BackendConfiguration cfg;
    cfg.displayName = "Server X";
    cfg.connectionParams["url"] = "https://x/";
    ac.addProvider("multiproto-dav", cfg);

    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();
    QTest::qWait(50);

    auto *listWidget = page.findChild<Kalburator::Ui::AccountsListWidget*>();
    QVERIFY(listWidget);

    // One row present: the label contains "Server X".
    auto labels = listWidget->findChildren<QLabel*>();
    QCOMPARE(labels.size(), 1);
    QVERIFY(labels.at(0)->text().contains(QStringLiteral("Server X")));
}

void TstAccountsPage::interlock_disablesAddRemoveDuringRun()
{
    QTemporaryDir dir;
    Profile profile(dir.path()); profile.initialize();
    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");
    WildPalms::Runtime::AccountController ac(dir.path(),
        &rt.backendRegistry(), &profile, &rt);
    WildPalms::App::Accounts::AccountsPage page(&ac, &rt);
    page.show();

    // Emit runStarted directly via test-emit pattern. If PalmRuntime
    // doesn't allow external signal emission, fall back to checking
    // updateInterlock via setRunningForTest if exposed; otherwise QSKIP.
    emit rt.runStarted("test");
    QTest::qWait(50);

    auto *listWidget = page.findChild<Kalburator::Ui::AccountsListWidget*>();
    QVERIFY(listWidget);
    // Whole widget disabled during sync.
    QVERIFY(!listWidget->isEnabled());

    emit rt.runFinished({});
    QTest::qWait(50);
    QVERIFY(listWidget->isEnabled());
}

WILDPALMS_QTEST_MAIN(TstAccountsPage)
#include "tst_accounts_page.moc"
