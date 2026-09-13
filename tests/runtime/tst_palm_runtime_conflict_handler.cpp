#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "runtime/palmruntime.h"
#include <kalburator/conflict/conflictpolicy.h>
#include <kalburator/conflict/conflicthandlerregistry.h>

namespace KSync = Kalburator::Sync;

class StubKalburatorHandler : public Kalburator::Conflict::ConflictHandler
{
public:
    Kalburator::Conflict::ConflictDecision handleConflict(
        Kalburator::Conflict::ConflictRecord &,
        const Kalburator::Conflict::ConflictPolicy &) override
    {
        return Kalburator::Conflict::ConflictDecision::UseSource;
    }
};

class TstPalmRuntimeConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void install_handler_makes_it_default();
};

void TstPalmRuntimeConflictHandler::install_handler_makes_it_default()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    StubKalburatorHandler handler;
    runtime.setConflictHandler(&handler);

    QVERIFY(runtime.conflictHandlerForTest() == &handler);
}

QTEST_MAIN(TstPalmRuntimeConflictHandler)
#include "tst_palm_runtime_conflict_handler.moc"
