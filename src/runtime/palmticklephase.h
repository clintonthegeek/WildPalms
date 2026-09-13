#ifndef WILDPALMS_RUNTIME_PALMTICKLEPHASE_H
#define WILDPALMS_RUNTIME_PALMTICKLEPHASE_H

// P2: Extracted so the unit test (tst_tickle_phase.cpp) can include
// this header without pulling in the full palmruntime.cpp translation unit.

#include <kalburator/engine/syncengine.h>  // Kalburator::Engine::SyncPhase

namespace WildPalms::Runtime {

/// Returns true if the keep-alive tickle should be paused for the given
/// sync phase, given whether Palm is the source and/or target of the current
/// mapping. Pausing is only needed during phases that issue DLP calls to the
/// device; network/CalDAV fetch phases keep the tickle alive so the Palm
/// doesn't think the connection dropped.
///
/// Pure function — no side effects.
inline bool shouldPauseTickle(Kalburator::Engine::SyncEngine::SyncPhase phase,
                               bool palmIsSource, bool palmIsTarget)
{
    using Phase = Kalburator::Engine::SyncEngine::SyncPhase;
    switch (phase) {
    case Phase::FetchingSource: return palmIsSource;
    case Phase::FetchingTarget: return palmIsTarget;
    case Phase::Processing:     return true;   // conservative: apply phase touches Palm
    case Phase::Idle:
    case Phase::Complete:       return false;
    }
    return false;
}

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_PALMTICKLEPHASE_H
