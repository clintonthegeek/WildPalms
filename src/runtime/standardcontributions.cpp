#include "standardcontributions.h"

#include <kalburator/sync/backendregistry.h>
#include <kalburator/sync/multiprotocoldavbackendcontribution.h>
#ifdef HAVE_AKONADI
#include <kalburator/sync/akonadibackendcontribution.h>
#endif

#include "localfoldercontribution.h"

#include <memory>

namespace WildPalms::Runtime {

void registerStandardContributions(Kalburator::Sync::BackendRegistry *registry)
{
    if (!registry) return;
    // One DAV account = one set of credentials = both protocols. The
    // multi-protocol provider degrades per leg (a CalDAV-only or
    // CardDAV-only server connects with a warning), so the single-protocol
    // CalDav/CardDav contributions are deliberately NOT registered — they
    // would only re-split credentials across two accounts.
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::MultiProtocolDavBackendContribution>());
#ifdef HAVE_AKONADI
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::AkonadiBackendContribution>());
#endif
    // Substrate A2: credential-less local-folder source — the first proof that
    // local sources are providers in the same registry as DAV/Akonadi.
    registry->registerContribution(
        std::make_shared<WildPalms::Runtime::LocalFolderContribution>());
}

}  // namespace WildPalms::Runtime
