#pragma once

#include "../core/types.h"

namespace Velos::RHI {

static constexpr u32 kInvalidHandle = 0;

template <typename Tag> struct Handle {
  u32 id = kInvalidHandle;

  constexpr bool IsValid() const { return id != kInvalidHandle; }

  constexpr explicit operator bool() const { return IsValid(); }
};

struct BufferTag {};
struct ImageTag {};
struct ImageViewTag {};
struct SamplerTag {};
struct ShaderTag {};
struct PipelineTag {};
struct CommandListTag {};
struct SwapchainTag {};
struct DescriptorSetLayoutTag {};
struct DescriptorPoolTag {};
struct DescriptorSetTag {};

using BufferHandle = Handle<BufferTag>;
using SamplerHandle = Handle<SamplerTag>;
using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using CommandListHandle = Handle<CommandListTag>;
using SwapchainHandle = Handle<SwapchainTag>;
using ImageHandle = Handle<ImageTag>;
using ImageViewHandle = Handle<ImageViewTag>;
using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>;
using DescriptorPoolHandle = Handle<DescriptorPoolTag>;
using DescriptorSetHandle = Handle<DescriptorSetTag>;

} // namespace Velos::RHI
