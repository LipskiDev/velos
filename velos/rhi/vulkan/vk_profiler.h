#pragma once

#include "core/profiling.h"
#include <volk.h>

#if defined(TRACY_ENABLE)
#include <tracy/TracyVulkan.hpp>

#define VL_PROFILE_GPU_CONTEXT(physicalDevice, device, queue, cmdBuf)          \
  TracyVkContext(physicalDevice, device, queue, cmdBuf)

#define VL_PROFILE_GPU_DESTROY(ctx) TracyVkDestroy(ctx)

#define VL_PROFILE_GPU_ZONE(ctx, cmdBuf, name) TracyVkZone(ctx, cmdBuf, name)

#define VL_PROFILE_GPU_COLLECT(ctx, cmdBuf) TracyVkCollect(ctx, cmdBuf)

#else

#define VL_PROFILE_GPU_CONTEXT(physicalDevice, device, queue, cmdBuf)
#define VL_PROFILE_GPU_DESTROY(ctx)
#define VL_PROFILE_GPU_ZONE(ctx, cmdBuf, name)
#define VL_PROFILE_GPU_COLLECT(ctx, cmdBuf)

#endif
