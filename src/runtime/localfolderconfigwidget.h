#ifndef WILDPALMS_RUNTIME_LOCALFOLDERCONFIGWIDGET_H
#define WILDPALMS_RUNTIME_LOCALFOLDERCONFIGWIDGET_H

#include <kalburator/sync/iproviderconfigwidget.h>
#include <kalburator/typesupport/backendconfiguration.h>

#include <QWidget>

class QListWidget;
class QPushButton;

namespace WildPalms::Runtime {

/// Shakedown F2: "Local folder" accounts used to be a config dead end —
/// createConfigWidget returned nullptr, so the Add Account dialog accepted
/// an empty account that then failed forever with "No folders configured"
/// and no UI existed to add folder entries.
///
/// This widget edits the provider's (path, domain) entry list and
/// implements Kalburator::Sync::IProviderConfigWidget so AccountFormWidget's
/// generic bridge moves values between widget and provider.
class LocalFolderConfigWidget final : public QWidget,
                                      public Kalburator::Sync::IProviderConfigWidget
{
    Q_OBJECT
public:
    explicit LocalFolderConfigWidget(QWidget *parent = nullptr);

    Kalburator::Sync::BackendConfiguration configuration() const override;
    void setConfiguration(const Kalburator::Sync::BackendConfiguration &cfg) override;

private:
    void onAdd();
    void onRemove();

    QListWidget  *m_rows = nullptr;
    QPushButton  *m_addButton = nullptr;
    QPushButton  *m_removeButton = nullptr;
    // Incoming configuration preserved so configuration() can round-trip
    // id/displayName that this widget does not edit.
    Kalburator::Sync::BackendConfiguration m_baseCfg;
};

}  // namespace WildPalms::Runtime

#endif
