/**
 * @file test_profile_accounts.cpp
 * @brief Tests for Profile accounts subgroup (K.8b T9)
 *
 * Verifies round-trip persistence of BackendConfiguration entries
 * via the Profile::accounts / saveAccount / removeAccount / setAccounts API.
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "profile.h"
#include <kalburator/typesupport/backendconfiguration.h>

using Kalburator::Sync::BackendConfiguration;

class TestProfileAccounts : public QObject
{
    Q_OBJECT

private slots:
    void roundTripAccount();
    void updateExistingAccount();
    void removeAccount();
    void setAccounts();
};

void TestProfileAccounts::roundTripAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");

    // Create and save
    {
        Profile p(profilePath);
        QVERIFY(p.initialize());

        BackendConfiguration cfg;
        cfg.id = QStringLiteral("test-uuid-1");
        cfg.type = QStringLiteral("caldav");
        cfg.displayName = QStringLiteral("My CalDAV");
        cfg.enabled = true;
        cfg.connectionParams[QStringLiteral("url")] = QStringLiteral("https://example.com/dav");
        cfg.connectionParams[QStringLiteral("username")] = QStringLiteral("alice");

        p.saveAccount(cfg);
        QVERIFY(p.save());
    }

    // Load and verify
    {
        Profile p2(profilePath);
        QVERIFY(p2.load());

        const auto accts = p2.accounts();
        QCOMPARE(accts.size(), 1);
        QCOMPARE(accts.first().id, QStringLiteral("test-uuid-1"));
        QCOMPARE(accts.first().type, QStringLiteral("caldav"));
        QCOMPARE(accts.first().displayName, QStringLiteral("My CalDAV"));
        QVERIFY(accts.first().enabled);
        QCOMPARE(accts.first().connectionParams.value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://example.com/dav"));
        QCOMPARE(accts.first().connectionParams.value(QStringLiteral("username")).toString(),
                 QStringLiteral("alice"));
    }
}

void TestProfileAccounts::updateExistingAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    Profile p(profilePath);
    QVERIFY(p.initialize());

    BackendConfiguration cfg;
    cfg.id = QStringLiteral("acc-1");
    cfg.type = QStringLiteral("caldav");
    cfg.displayName = QStringLiteral("Original");
    p.saveAccount(cfg);
    QCOMPARE(p.accounts().size(), 1);

    // Update via saveAccount with same id
    cfg.displayName = QStringLiteral("Updated");
    p.saveAccount(cfg);
    QCOMPARE(p.accounts().size(), 1); // not duplicated
    QCOMPARE(p.accounts().first().displayName, QStringLiteral("Updated"));
}

void TestProfileAccounts::removeAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    Profile p(profilePath);
    QVERIFY(p.initialize());

    BackendConfiguration a, b;
    a.id = QStringLiteral("id-a");
    a.type = QStringLiteral("caldav");
    b.id = QStringLiteral("id-b");
    b.type = QStringLiteral("caldav");

    p.saveAccount(a);
    p.saveAccount(b);
    QCOMPARE(p.accounts().size(), 2);

    p.removeAccount(QStringLiteral("id-a"));
    QCOMPARE(p.accounts().size(), 1);
    QCOMPARE(p.accounts().first().id, QStringLiteral("id-b"));
}

void TestProfileAccounts::setAccounts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString profilePath = dir.path() + QStringLiteral("/profile");
    Profile p(profilePath);
    QVERIFY(p.initialize());

    BackendConfiguration x;
    x.id = QStringLiteral("x");
    x.type = QStringLiteral("local");
    p.saveAccount(x);
    QCOMPARE(p.accounts().size(), 1);

    QList<BackendConfiguration> newList;
    BackendConfiguration y, z;
    y.id = QStringLiteral("y");
    y.type = QStringLiteral("caldav");
    z.id = QStringLiteral("z");
    z.type = QStringLiteral("carddav");
    newList << y << z;

    p.setAccounts(newList);
    QCOMPARE(p.accounts().size(), 2);
    QCOMPARE(p.accounts().at(0).id, QStringLiteral("y"));
    QCOMPARE(p.accounts().at(1).id, QStringLiteral("z"));
}

QTEST_GUILESS_MAIN(TestProfileAccounts)
#include "test_profile_accounts.moc"
