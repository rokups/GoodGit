// Dear ImGui user configuration for ggui (IMGUI_USER_CONFIG).
#pragma once

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_USE_WCHAR32

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
// Test failures are reported by the engine; never trap into a debugger in CI.
#define IM_DEBUG_BREAK() ((void)0)
#include "imgui_te_imconfig.h"
#endif
