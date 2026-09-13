#ifndef CONFLICTREVIEWWIDGET_H
#define CONFLICTREVIEWWIDGET_H

/**
 * @file conflictreviewwidget.h
 * @brief Widget for reviewing and resolving multiple conflicts
 *
 * Provides a list-based interface for batch conflict resolution.
 * Users can review conflicts at their leisure and apply resolutions
 * in a subsequent sync.
 */

#include <QWidget>
// K.8b T13: core/isyncconduit.h deleted; ConduitLookupFn now lives as a
// void*-returning stub in conflictdialog.h. T14 replaces it with a
// Kalburator::Plugin lookup.
#include "conflictdialog.h"

// Kalburator types used in this header (ConflictStore)
#include <kalburator/conflict/conflictstore.h>

class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QLabel;
class QPushButton;
class QSplitter;
class QComboBox;
class QGroupBox;

/**
 * @brief Widget for batch conflict review
 */
class ConflictReviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ConflictReviewWidget(Kalburator::Conflict::ConflictStore *store,
                                  QWidget *parent = nullptr);
    ~ConflictReviewWidget() override;

    /**
     * @brief Set conduit lookup function for rich conflict display
     */
    void setConduitLookup(ConduitLookupFn fn) { m_conduitLookup = std::move(fn); }

    /**
     * @brief Refresh the conflict list
     */
    void refresh();

    /**
     * @brief Get count of pending conflicts
     */
    int pendingCount() const;

    /**
     * @brief Get count of resolved (but unapplied) conflicts
     */
    int resolvedCount() const;

signals:
    /**
     * @brief Emitted when user wants to apply resolutions via sync
     */
    void applyResolutionsRequested();

    /**
     * @brief Emitted when conflicts change
     */
    void conflictsChanged();

private slots:
    void onConflictSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void onUseSource();
    void onUseTarget();
    void onUseBoth();
    void onSkip();
    void onResetToPending();
    void onResolveAllSource();
    void onResolveAllTarget();
    void onClearResolved();
    void onFilterChanged(int index);

private:
    void setupUI();
    void updateConflictList();
    void displayConflict(const Kalburator::Conflict::ConflictRecord &conflict);
    void updateButtons();
    void resolveCurrentConflict(Kalburator::Conflict::ConflictDecision decision);
    QString decisionToString(Kalburator::Conflict::ConflictDecision decision) const;
    QIcon decisionToIcon(Kalburator::Conflict::ConflictDecision decision) const;

    Kalburator::Conflict::ConflictStore *m_store;
    ConduitLookupFn m_conduitLookup;
    QString m_currentConflictId;

    // Filter
    QComboBox *m_filterCombo;

    // Conflict list
    QListWidget *m_conflictList;

    // Details panel
    QLabel *m_summaryLabel;
    QGroupBox *m_sourceGroup;
    QGroupBox *m_targetGroup;
    QLabel *m_sourceInfoLabel;
    QLabel *m_targetInfoLabel;
    QTextEdit *m_sourceText;
    QTextEdit *m_targetText;

    // Resolution status
    QLabel *m_statusLabel;

    // Action buttons
    QPushButton *m_useSourceBtn;
    QPushButton *m_useTargetBtn;
    QPushButton *m_useBothBtn;
    QPushButton *m_skipBtn;
    QPushButton *m_resetBtn;

    // Batch buttons
    QPushButton *m_resolveAllSourceBtn;
    QPushButton *m_resolveAllTargetBtn;
    QPushButton *m_clearResolvedBtn;
    QPushButton *m_applyBtn;
};

#endif // CONFLICTREVIEWWIDGET_H
