#ifndef WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
#define WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H

#include <kalburator/conflict/conflictpolicy.h>

#include "palmbackendconfig.h"
#include "palmrecord.h"

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
}

namespace WildPalms::PalmConflict {

/**
 * @brief Palm-aware implementation of
 *        `Kalburator::Conflict::ConflictHandler`.
 *
 * Resolution algorithm:
 *
 *   1. Delegate to `ConflictPolicy` for the base decision (mirrors
 *      `AutomaticConflictHandler`): if `shouldAutoResolve` is true, take
 *      `getAutoDecision`; otherwise honour `fallback` (Defer / Skip /
 *      UseDefault / Abort).
 *   2. Apply Palm overlays, which may **override** the base decision:
 *        a. **Archive safety** — never delete an archived record on the
 *           Palm side; flip `DeleteBoth` / delete-directional decisions
 *           to preserve the archived side.
 *        b. **Secret protection** — in `BothModified` where exactly one
 *           side is secret and the base decision is `UseBoth`
 *           (`DuplicateAll`), prefer the secret side to avoid leaking
 *           the record into a non-secret duplicate.
 *        c. **Category tie-break** — in `BothModified` with a
 *           `NewerWins` / `OlderWins` base that collapses on equal
 *           timestamps, prefer the side with a non-zero category.
 *
 * Overlays only fire when the ID decodes as a Palm record (via
 * `PalmBackend::decodeRecordId`) and `IPalmDatabaseAccess` returns a
 * record with the matching attributes. Non-Palm conflicts fall through
 * unchanged — the handler is safe to register as the registry's default
 * even in mixed-backend scenarios.
 *
 * Lifetime: does NOT own the `IPalmDatabaseAccess` or `PalmBackendConfig`.
 * Caller retains ownership; both must outlive the handler.
 */
class PalmConflictHandler
    : public Kalburator::Conflict::ConflictHandler
{
public:
    PalmConflictHandler(WildPalms::PalmSync::IPalmDatabaseAccess *device,
                        const PalmBackendConfig *config);
    ~PalmConflictHandler() override = default;

    Kalburator::Conflict::ConflictDecision handleConflict(
        Kalburator::Conflict::ConflictRecord &conflict,
        const Kalburator::Conflict::ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }
    bool shouldKeepConnectionAlive() const override;
    QList<Kalburator::Conflict::ConflictRecord> pendingConflicts()
        const override { return m_pending; }

    void onSyncStart() override;
    void onSyncEnd(bool hadConflicts, bool allResolved) override;

    /// Test hook: accessor for last overlay applied (empty if base decision
    /// stood). Values: "", "archive", "secret", "category".
    const QString &lastOverlay() const { return m_lastOverlay; }

private:
    // Helper: returns nullopt if the id doesn't decode or the device
    // has no matching record. Used by overlays.
    std::optional<WildPalms::PalmSync::PalmRecord>
        lookupPalmRecord(const QString &encodedId) const;

    // Overlay application — each returns the (possibly adjusted) decision.
    Kalburator::Conflict::ConflictDecision applyOverlays(
        Kalburator::Conflict::ConflictRecord &conflict,
        const Kalburator::Conflict::ConflictPolicy &policy,
        Kalburator::Conflict::ConflictDecision baseDecision);

    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    const PalmBackendConfig *m_config = nullptr;
    QList<Kalburator::Conflict::ConflictRecord> m_pending;
    QString m_lastOverlay;
};

} // namespace WildPalms::PalmConflict

#endif // WILDPALMS_CONFLICT_PALMCONFLICTHANDLER_H
