#include "localfolderconfigwidget.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace WildPalms::Runtime {

using Kalburator::Sync::BackendConfiguration;

namespace {

struct DomainOption { QString value; QString label; };

// Internal domain values match LocalFolderProvider's Entry::domain
// ("note" selects the Markdown backend; others use RawFiles).
const QVector<DomainOption> &domainOptions()
{
    static const QVector<DomainOption> opts{
        { QStringLiteral("calendar"), QObject::tr("Calendar") },
        { QStringLiteral("contacts"), QObject::tr("Contacts") },
        { QStringLiteral("note"),     QObject::tr("Memos") },
        { QStringLiteral("todo"),     QObject::tr("Tasks") },
    };
    return opts;
}

QString labelForDomain(const QString &value)
{
    for (const auto &o : domainOptions())
        if (o.value == value) return o.label;
    return value;
}

}  // namespace

LocalFolderConfigWidget::LocalFolderConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    outer->addWidget(new QLabel(
        tr("Pick folders to sync. Each folder is one writable collection."),
        this));

    m_rows = new QListWidget(this);
    outer->addWidget(m_rows, /*stretch=*/1);

    auto *buttons = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add folder…"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    outer->addLayout(buttons);

    connect(m_addButton, &QPushButton::clicked,
            this, &LocalFolderConfigWidget::onAdd);
    connect(m_removeButton, &QPushButton::clicked,
            this, &LocalFolderConfigWidget::onRemove);
    connect(m_rows, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeButton->setEnabled(!m_rows->selectedItems().isEmpty());
    });
    m_removeButton->setEnabled(false);
}

void LocalFolderConfigWidget::onAdd()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder to sync"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    QStringList labels;
    for (const auto &o : domainOptions())
        labels << o.label;
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this,
        tr("What lives in this folder?"),
        tr("Sync this folder as:"), labels, /*current=*/0, /*editable=*/false,
        &ok);
    if (!ok || chosen.isEmpty()) return;

    QString domainValue;
    for (const auto &o : domainOptions())
        if (o.label == chosen) domainValue = o.value;

    auto *item = new QListWidgetItem(
        QStringLiteral("%1 — %2").arg(dir, chosen), m_rows);
    item->setData(Qt::UserRole, dir);
    item->setData(Qt::UserRole + 1, domainValue);
}

void LocalFolderConfigWidget::onRemove()
{
    const auto selected = m_rows->selectedItems();
    for (auto *item : selected)
        delete item;
}

BackendConfiguration LocalFolderConfigWidget::configuration() const
{
    BackendConfiguration cfg;
    cfg.type = QStringLiteral("local-folder");
    // Preserve id/displayName from whatever was loaded into the widget —
    // AccountFormWidget routes widget→provider→save(), which would clobber
    // them otherwise.
    if (!m_baseCfg.id.isEmpty())
        cfg.id = m_baseCfg.id;
    if (!m_baseCfg.displayName.isEmpty()) {
        cfg.displayName = m_baseCfg.displayName;
    } else {
        // A sensible default: first folder's name, else generic.
        if (m_rows->count() > 0)
            cfg.displayName = QDir(m_rows->item(0)->data(Qt::UserRole)
                                       .toString()).dirName();
        if (cfg.displayName.isEmpty())
            cfg.displayName = QStringLiteral("Local folders");
    }

    QVariantList entries;
    for (int i = 0; i < m_rows->count(); ++i) {
        const auto *item = m_rows->item(i);
        QVariantMap e;
        e.insert(QStringLiteral("path"), item->data(Qt::UserRole));
        e.insert(QStringLiteral("domain"), item->data(Qt::UserRole + 1));
        entries.append(e);
    }
    cfg.connectionParams.insert(QStringLiteral("entries"), entries);
    return cfg;
}

void LocalFolderConfigWidget::setConfiguration(const BackendConfiguration &cfg)
{
    m_baseCfg = cfg;
    m_rows->clear();
    const QVariantList list =
        cfg.connectionParams.value(QStringLiteral("entries")).toList();
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        const QString path = m.value(QStringLiteral("path")).toString();
        const QString domain = m.value(QStringLiteral("domain")).toString();
        if (path.isEmpty() && domain.isEmpty()) continue;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(path, labelForDomain(domain)),
            m_rows);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, domain);
    }
}

}  // namespace WildPalms::Runtime
