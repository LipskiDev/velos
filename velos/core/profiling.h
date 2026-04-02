#pragma once

#if defined(TRACY_ENABLE)
#define VL_PROFILING 1
#include <tracy/Tracy.hpp>
#else
#define VL_PROFILING 0
#endif

#if VL_PROFILING
#define VL_PROFILE_ZONE() ZoneScoped
#define VL_PROFILE_ZONE_N(name) ZoneScopedN(name)
#define VL_PROFILE_FRAME() FrameMark
#else
#define VL_PROFILE_ZONE()
#define VL_PROFILE_ZONE_N(name)
#define VL_PROFILE_FRAME()
#endif
