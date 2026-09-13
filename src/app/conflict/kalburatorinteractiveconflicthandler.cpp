#include "kalburatorinteractiveconflicthandler.h"
#include <kalburator/conflict/conflictstore.h>
#include "../conflictdialog.h"   // direct include — safe after qsynccore delete
#include <QMetaObject>
#include <QThread>
#include <QTimer>

using ConflictDecision = Kalburator::Conflict::ConflictDecision;
using ConflictRecord   = Kalburator::Conflict::ConflictRecord;
using ConflictPolicy   = Kalburator::Conflict::ConflictPolicy;

KalburatorInteractiveConflictHandler::KalburatorInteractiveConflictHandler(
    Kalburator::Conflict::ConflictStore *store,
    QWidget *parentWidget,
    QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_parentWidget(parentWidget)
{}

ConflictDecision
KalburatorInteractiveConflictHandler::handleConflict(
    ConflictRecord &conflict,
    const ConflictPolicy &policy)
{
    ++m_conflictsHandled;

    if (QThread::currentThread() == thread()) {
        return handleConflictOnGuiThread(conflict, policy);
    }

    ConflictDecision decision = ConflictDecision::Pending;
    // Reference captures are safe: BlockingQueuedConnection blocks this thread
    // until the slot returns, so the captured locals remain alive throughout.
    QMetaObject::invokeMethod(
        this,
        [this, &conflict, &policy, &decision]() {
            decision = handleConflictOnGuiThread(conflict, policy);
        },
        Qt::BlockingQueuedConnection);
    return decision;
}

void KalburatorInteractiveConflictHandler::onSyncStart()
{
    m_localPending.clear();
    m_conflictsHandled = 0;
    m_conflictsDeferred = 0;
}

void KalburatorInteractiveConflictHandler::onSyncEnd(
    bool hadConflicts, bool allResolved)
{
    Q_UNUSED(hadConflicts);
    Q_UNUSED(allResolved);
}

ConflictDecision
KalburatorInteractiveConflictHandler::handleConflictOnGuiThread(
    ConflictRecord &conflict,
    const ConflictPolicy &policy)
{
    if (m_hook) {
        return m_hook(conflict, policy);
    }

    if (!m_parentWidget) {
        m_localPending.append(conflict);
        ++m_conflictsDeferred;
        return ConflictDecision::Pending;
    }

    // Tickle the device link while the user thinks (15 s cadence matches
    // the legacy keep-alive pattern).
    QTimer keepAlive;
    keepAlive.setInterval(15 * 1000);
    connect(&keepAlive, &QTimer::timeout,
            this, &KalburatorInteractiveConflictHandler::keepAliveRequested);
    keepAlive.start();

    ConflictDialog dlg(conflict, policy, nullptr /*conduitLookup*/, m_parentWidget);
    dlg.exec();
    const ConflictDecision decision = dlg.decision();

    keepAlive.stop();

    if (decision == ConflictDecision::Pending) {
        m_localPending.append(conflict);
        ++m_conflictsDeferred;
        return ConflictDecision::Pending;
    }

    return decision;
}
