#include "accountformwidget.h"

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/backendcontribution.h>
#include <kalburator/sync/iprovider.h>
#include <kalburator/sync/iproviderconfigwidget.h>
#include <kalburator/typesupport/backendconfiguration.h>

#include <QComboBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::App::Accounts {

using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendConfiguration;

AccountFormWidget::AccountFormWidget(BackendRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_registry(registry)
{
    buildUi();
}

AccountFormWidget::~AccountFormWidget() = default;

void AccountFormWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);

    m_kindCombo  = new QComboBox(this);
    m_configStack = new QStackedWidget(this);

    for (auto *contribution : m_registry->contributions()) {
        QString label = contribution->backendType();
        if (label == QStringLiteral("multiproto-dav"))
            label = tr("DAV server (calendar + contacts)");
        else if (label == QStringLiteral("akonadi"))
            label = tr("Akonadi (local)");
        else if (label == QStringLiteral("local-folder"))
            label = tr("Local folder");
        else {
            label[0] = label[0].toUpper();
        }
        m_kindCombo->addItem(label, contribution->backendType());

        auto provider = contribution->createProvider(this);
        QWidget *cfg = provider ? provider->createConfigWidget(m_configStack) : nullptr;
        if (!cfg) cfg = new QWidget(m_configStack);
        m_configStack->addWidget(cfg);
        m_providers.push_back(std::move(provider));
    }

    outer->addWidget(m_kindCombo);
    outer->addWidget(m_configStack);

    m_testButton  = new QPushButton(tr("Test Connection"), this);
    m_statusLabel = new QLabel(this);
    auto *testRow = new QHBoxLayout();
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_statusLabel, 1);
    outer->addLayout(testRow);

    connect(m_kindCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AccountFormWidget::onKindChanged);
    connect(m_testButton, &QPushButton::clicked,
            this, &AccountFormWidget::onTestConnection);

    const bool hasItems = m_kindCombo->count() > 0;
    m_testButton->setEnabled(hasItems);

    if (hasItems)
        onKindChanged(0);
}

QString AccountFormWidget::selectedKind() const {
    return m_kindCombo->currentData().toString();
}

void AccountFormWidget::onKindChanged(int index) {
    m_configStack->setCurrentIndex(index);
    m_statusLabel->clear();
}

void AccountFormWidget::onTestConnection() {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return;
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return;

    // Bridge: widget -> provider, before connect(). Without this the provider
    // is empty and connect() fails its "no server URL" guard immediately.
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->currentWidget()))
        p->load(cw->configuration());

    m_statusLabel->setText(tr("Testing..."));

    // Capture the provider's error() reason so the label can say more than
    // "Failed". Torn down once the test resolves.
    m_lastTestError.clear();
    QObject::disconnect(m_errorConn);
    m_errorConn = connect(p, &IProvider::error, this,
                          [this](const QString &msg) { m_lastTestError = msg; });

    auto fut = p->connect();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, p]() {
        const bool ok = w->result();
        QObject::disconnect(m_errorConn);
        if (ok) {
            QString msg = tr("Connected");
            const QString warn = p->lastWarning();  // partial-protocol failure
            if (!warn.isEmpty()) msg += tr(" — %1").arg(warn);
            m_statusLabel->setText(msg);
        } else {
            m_statusLabel->setText(m_lastTestError.isEmpty()
                ? tr("Failed") : tr("Failed: %1").arg(m_lastTestError));
        }
        p->disconnect();
        w->deleteLater();
    });
    w->setFuture(fut);
}

BackendConfiguration AccountFormWidget::configuration() const {
    const int idx = m_kindCombo->currentIndex();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_providers.size()) return {};
    IProvider *p = m_providers.at(static_cast<std::size_t>(idx)).get();
    if (!p) return {};
    // Bridge: widget -> provider before serializing, so Save persists the
    // user's typed URL/credentials rather than an empty provider.
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->currentWidget()))
        p->load(cw->configuration());
    return p->save();
}

void AccountFormWidget::setConfiguration(const BackendConfiguration &cfg) {
    const int idx = m_kindCombo->findData(cfg.type);
    if (idx < 0) return;
    m_kindCombo->setCurrentIndex(idx);
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->widget(idx)))
        cw->setConfiguration(cfg);
}

bool AccountFormWidget::isValid() const {
    const auto cfg = configuration();
    return cfg.isValid() && !cfg.displayName.isEmpty();
}

}  // namespace WildPalms::App::Accounts
