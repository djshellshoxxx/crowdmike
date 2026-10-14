#include "EditorContext.h"
#include "UI/Theme.h"

namespace crowdmike::ui {
const char* pageName(PageId id) noexcept {
    static const char* const names[] = { "Live", "Inputs", "Groups", "FX", "Matrix", "Record", "Playback", "Scenes", "Settings", "Diagnostics" };
    return names[static_cast<int>(id)];
}
const Palette& Page::palette() noexcept { return crowdmike::ui::palette(); }
}
