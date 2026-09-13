#include <QtTest/QtTest>

#include <kalburator/conflict/conflicthandlerregistry.h>
#include <kalburator/conflict/conflictpolicy.h>

#include "mockpalmdatabaseaccess.h"
#include "palmbackendconfig.h"
#include "palmconflicthandler.h"

using Kalburator::Conflict::ConflictHandler;
using Kalburator::Conflict::ConflictHandlerRegistry;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::PalmConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;

class TestPalmConflictHandlerRegistration : public QObject
{
    Q_OBJECT
private slots:
    void registersUnderPalmBackendId();
    void unregistrationClearsLookup();
    void defaultHandlerServesMissingBackendIds();
};

void TestPalmConflictHandlerRegistration::registersUnderPalmBackendId()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm"), &handler);

    QVERIFY(registry.hasHandler(QStringLiteral("palm")));
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(&handler));
}

void TestPalmConflictHandlerRegistration::unregistrationClearsLookup()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.registerHandler(QStringLiteral("palm"), &handler);
    registry.unregisterHandler(QStringLiteral("palm"));

    QVERIFY(!registry.hasHandler(QStringLiteral("palm")));
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(nullptr));
}

void TestPalmConflictHandlerRegistration::defaultHandlerServesMissingBackendIds()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler defaultHandler(&dev, &cfg);

    ConflictHandlerRegistry registry;
    registry.setDefaultHandler(&defaultHandler);

    // No "palm" registered; default takes over.
    QCOMPARE(registry.handlerFor(QStringLiteral("palm")),
             static_cast<ConflictHandler *>(&defaultHandler));
    QCOMPARE(registry.handlerFor(QStringLiteral("anything")),
             static_cast<ConflictHandler *>(&defaultHandler));
}

QTEST_MAIN(TestPalmConflictHandlerRegistration)
#include "tst_palmconflicthandler_registration.moc"
