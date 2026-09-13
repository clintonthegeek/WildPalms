#ifndef WILDPALMS_RUNTIME_CONFLICTPRESENTER_WP_H
#define WILDPALMS_RUNTIME_CONFLICTPRESENTER_WP_H

#include <kalburator/calendar/iconflictpresenter.h>

namespace WildPalms::Runtime {

// Phase D placeholder — counts refresh calls. Phase F wires the real
// conflict review widget.
class ConflictPresenter_WP : public Kalburator::Sync::IConflictPresenter
{
public:
    ConflictPresenter_WP() = default;
    ~ConflictPresenter_WP() override = default;

    void refreshConflicts() override;

    int refreshCount() const { return m_refreshCount; }

private:
    int m_refreshCount = 0;
};

} // namespace WildPalms::Runtime

#endif
