#ifndef WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H
#define WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H

#include <QObject>
#include <QString>

#include <kalburator/engine/imassdeleteguard.h>

class QWidget;

namespace WildPalms::Runtime {

/// IMassDeleteGuard that surfaces a QMessageBox::question on the GUI
/// thread. SyncEngine calls confirmMassDelete from a worker thread; the
/// presenter marshals via Qt::BlockingQueuedConnection to its parent
/// window before showing the prompt, so the dialog lands on the right
/// thread and the worker waits for the user's answer.
class MassDeleteGuardPresenter
    : public QObject
    , public Kalburator::Conflict::IMassDeleteGuard
{
    Q_OBJECT
public:
    explicit MassDeleteGuardPresenter(QWidget *parent);
    ~MassDeleteGuardPresenter() override;

    /// IMassDeleteGuard
    bool confirmMassDelete(const QString &mappingId,
                           const QString &targetBackendId,
                           int proposedDeletes,
                           int baselineCount) override;

protected:
    /// Test seam: invoked on the GUI thread. Production override pops
    /// a QMessageBox; tests override to return a preset value.
    virtual bool promptUser(const QString &mappingId,
                            const QString &targetBackendId,
                            int proposedDeletes,
                            int baselineCount);

private:
    QWidget *m_parentWidget;
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H
