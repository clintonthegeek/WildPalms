#include "targetpickerrow.h"
#include "wizardstate.h"

#include "plugins/pimplugin.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

QString TargetPickerRow::pluginId() const
{
    return m_conduit ? m_conduit->conduitId() : QString();
}

TargetPickerRow::TargetPickerRow(const WildPalms::Plugins::PimPlugin *conduit,
                                 WizardState *state,
                                 QWidget *parent)
    : QWidget(parent)
    , m_conduit(conduit)
    , m_state(state)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout();
    auto *label = new QLabel(
        m_conduit ? m_conduit->conduitDisplayName() : QString(), this);
    label->setMinimumWidth(120);
    m_combo = new QComboBox(this);
    top->addWidget(label);
    top->addWidget(m_combo, /*stretch=*/1);
    outer->addLayout(top);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("hint"));
    m_hint->setIndent(124);
    m_hint->setVisible(false);
    outer->addWidget(m_hint);

    rebuild();
    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TargetPickerRow::onCurrentIndexChanged);
}

void TargetPickerRow::rebuild()
{
    if (!m_combo || !m_state || !m_conduit) return;
    const QString pid = m_conduit->conduitId();

    QSignalBlocker block(m_combo);
    m_combo->clear();

    // Index 0, always present: local files.
    m_combo->addItem(tr("Local files (default)"),
                     QVariant::fromValue(QStringList{QString(), QString()}));

    bool anyConnectedAccount = false;
    for (const auto &acc : m_state->accounts) {
        if (!acc.connected) continue;
        anyConnectedAccount = true;
        const QString accName = acc.config.displayName.isEmpty()
            ? acc.id : acc.config.displayName;
        for (const auto &c : acc.collections) {
            if (!m_conduit->matchesCollection(c)) continue;
            QString label = QStringLiteral("%1 ▸ %2").arg(accName, c.name);
            if (c.readOnly) label += tr(" (read-only)");
            m_combo->addItem(label,
                             QVariant::fromValue(QStringList{acc.id, c.id}));
            if (c.readOnly) {
                // Palm→remote writes need a writable target; list it so the
                // user sees it exists, but make it unselectable.
                auto *model = qobject_cast<QStandardItemModel*>(m_combo->model());
                if (model)
                    if (auto *it = model->item(m_combo->count() - 1))
                        it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            }
        }
    }

    // Restore the selection from state; reset stale bindings.
    int current = 0;
    int mi = -1;
    for (int i = 0; i < m_state->mappings.size(); ++i)
        if (m_state->mappings[i].pluginId == pid) { mi = i; break; }
    if (mi >= 0 && m_state->mappings[mi].kind == TargetKind::Account) {
        for (int i = 1; i < m_combo->count(); ++i) {
            const auto data = m_combo->itemData(i).toStringList();
            if (data.value(0) == m_state->mappings[mi].accountRef &&
                data.value(1) == m_state->mappings[mi].collectionId) {
                current = i;
                break;
            }
        }
        if (current == 0) {
            // Bound target no longer exists (account removed or edited).
            m_state->mappings[mi].kind = TargetKind::RawFiles;
            m_state->mappings[mi].accountRef.clear();
            m_state->mappings[mi].collectionId.clear();
        }
    }
    m_combo->setCurrentIndex(current);

    // Shakedown F5: the hint used to be suppressed exactly when it mattered
    // — accounts that failed to connect leave zero collections, so "no
    // matching collections" never showed. Distinguish the two silences.
    const bool hasAccounts = !m_state->accounts.isEmpty();
    if (anyConnectedAccount && m_combo->count() == 1) {
        m_hint->setText(tr("No matching collections on your accounts."));
        m_hint->setVisible(true);
    } else if (hasAccounts && !anyConnectedAccount) {
        m_hint->setText(tr("Accounts have not connected yet — only local "
                           "files are available until they do."));
        m_hint->setVisible(true);
    } else {
        m_hint->setVisible(false);
    }
}

void TargetPickerRow::onCurrentIndexChanged(int idx)
{
    if (idx < 0 || !m_combo) return;
    const auto data = m_combo->itemData(idx).toStringList();
    emit bindingSelected(data.value(0), data.value(1));
}

}  // namespace WildPalms::Wizard
