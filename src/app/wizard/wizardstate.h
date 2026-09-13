#ifndef WILDPALMS_APP_WIZARD_WIZARDSTATE_H
#define WILDPALMS_APP_WIZARD_WIZARDSTATE_H

#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/types/collectioninfo.h>

#include <QList>
#include <QString>

namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,
    Account,          // bound to a WizardAccount's collection
};

/// An account created on the wizard's Accounts page. The page owns the
/// transient IProvider; discovery results are value-copied here so later
/// pages (Bindings, Review) need no live provider.
struct WizardAccount {
    QString id;       // wizard-local UUID; reused as the on-disk account id
    QString kind;     // contribution backendType(): "multiproto-dav" | "akonadi" | ...
    Kalburator::Sync::BackendConfiguration config;

    // Discovery results, filled by AccountsSetupPage when connect() resolves.
    bool    connected = false;
    QString error;
    QList<Kalburator::Sync::CollectionInfo> collections;
};

struct MappingSpec {
    QString    pluginId;     // calendar | contacts | memo | todo (matches plugin->pluginId())
    TargetKind kind = TargetKind::RawFiles;
    QString    accountRef;   // WizardAccount.id for Account; empty for RawFiles
    QString    collectionId; // chosen collection for Account; empty for RawFiles
};

struct WizardState {
    QString              profileName;
    QList<WizardAccount> accounts;
    QList<MappingSpec>   mappings;   // exactly four, keyed by pluginId in insertion order

    const WizardAccount *accountById(const QString &id) const {
        for (const auto &a : accounts)
            if (a.id == id) return &a;
        return nullptr;
    }
};

}  // namespace WildPalms::Wizard

#endif
