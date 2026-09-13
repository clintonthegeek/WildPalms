#ifndef WILDPALMS_RUNTIME_CONFLICTRESOLVER_WP_H
#define WILDPALMS_RUNTIME_CONFLICTRESOLVER_WP_H

#include <kalburator/calendar/iconflictresolver.h>
#include <kalburator/types/synctypes.h>

namespace WildPalms::Runtime {

// Phase D placeholder — always returns SourceWins. Phase F replaces
// this with the real WP conflict dialog.
class ConflictResolver_WP : public Kalburator::Sync::IConflictResolver
{
public:
    ConflictResolver_WP() = default;
    ~ConflictResolver_WP() override = default;

    Kalburator::Sync::ConflictResolution resolveConflict(
        const Kalburator::Sync::ConflictInfo &conflict,
        QWidget *parentWidget) override;

    int resolveCount() const { return m_resolveCount; }

private:
    int m_resolveCount = 0;
};

} // namespace WildPalms::Runtime

#endif
