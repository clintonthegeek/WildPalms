#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QDateTime>

#include <KConfigGroup>

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/app/wizard/newprofilewizard.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

#include <KConfigGroup>
#include <KSharedConfig>

// Shakedown F1: first-run now routes through runProfileWizard() (the real
// accounts-first wizard), stubbed here via setRunProfileWizardForTest —
// the old bare name-prompt stopgap is gone.
class TstKf6MainWindowStartup : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryRunsWizard_cancelCreatesNothing();
    void emptyRegistryRunsWizard_acceptCreatesAndLoadsProfile();
    void validLastActiveAutoLoads();
    void staleLastActiveAutoLoadsMostRecent();
};

void TstKf6MainWindowStartup::emptyRegistryRunsWizard_cancelCreatesNothing()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);

    KF6MainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    int wizardRuns = 0;
    w.setRunProfileWizardForTest([&wizardRuns]() {
        ++wizardRuns;
        return WildPalms::Wizard::Result{};   // user cancels the wizard
    });

    const QString picked = w.runStartupForTest();

    QCOMPARE(wizardRuns, 1);
    QVERIFY(picked.isEmpty());
    QCOMPARE(w.profileRegistryForTest()->entries().size(), 0);
}

void TstKf6MainWindowStartup::emptyRegistryRunsWizard_acceptCreatesAndLoadsProfile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    KF6MainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    w.setRunProfileWizardForTest([]() {
        WildPalms::Wizard::Result r;
        r.state.profileName = QStringLiteral("FirstRun");
        // All-local profile: RawFiles rows produce no persisted mappings.
        for (const auto &pid : { QStringLiteral("calendar"),
                                  QStringLiteral("contacts"),
                                  QStringLiteral("memo"),
                                  QStringLiteral("todo") }) {
            WildPalms::Wizard::MappingSpec m;
            m.pluginId = pid;
            m.kind     = WildPalms::Wizard::TargetKind::RawFiles;
            r.state.mappings.append(m);
        }
        return r;
    });

    const QString picked = w.runStartupForTest();

    const auto entries = w.profileRegistryForTest()->entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().name, QStringLiteral("FirstRun"));
    QCOMPARE(picked, entries.first().path);
    QCOMPARE(w.currentProfileIdForTest(), entries.first().id);

    // The created profile is NOT hollow: name + sync folder were persisted
    // from the wizard result.
    Profile prof(entries.first().path);
    QVERIFY(prof.load());
    QCOMPARE(prof.name(), QStringLiteral("FirstRun"));
}

void TstKf6MainWindowStartup::validLastActiveAutoLoads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path() + QStringLiteral("/wp-root"));

    const auto entry = reg->registerNew(QStringLiteral("AutoLoad"));
    QVERIFY(entry.isValid());
    reg->setLastActive(entry.id);
    QVERIFY(QDir(entry.path).exists());

    KF6MainWindow w;
    w.setProfileRegistryForTest(std::move(reg));
    int wizardRuns = 0;
    w.setRunProfileWizardForTest([&wizardRuns]() {
        ++wizardRuns;   // must never be reached — a profile exists
        return WildPalms::Wizard::Result{};
    });

    const QString picked = w.runStartupForTest();

    QCOMPARE(wizardRuns, 0);
    QCOMPARE(picked, entry.path);
}

void TstKf6MainWindowStartup::staleLastActiveAutoLoadsMostRecent()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto a = reg->registerNew(QStringLiteral("Alpha"));
    const auto b = reg->registerNew(QStringLiteral("Bravo"));
    // Make Bravo more recent.
    reg->setLastActive(a.id);
    QTest::qSleep(5);
    reg->setLastActive(b.id);
    // Stamp lastActiveId with a bogus value (stale).
    {
        KConfigGroup g(cfg, QStringLiteral("General"));
        g.writeEntry("lastActiveProfileId", QStringLiteral("does-not-exist"));
        cfg->sync();
    }
    // Reload so the registry picks up the on-disk stale id.
    reg = std::make_unique<WildPalms::Runtime::ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());

    KF6MainWindow w;
    int wizardRuns = 0;
    w.setRunProfileWizardForTest([&wizardRuns]() {
        ++wizardRuns;   // must never be reached — a profile exists
        return WildPalms::Wizard::Result{};
    });
    w.setProfileRegistryForTest(std::move(reg));

    const QString picked = w.runStartupForTest();

    QCOMPARE(wizardRuns, 0);
    QCOMPARE(picked, b.path);
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowStartup)
#include "tst_kf6mainwindow_startup.moc"
