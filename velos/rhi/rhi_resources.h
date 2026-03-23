#pragma once

#include "../core/types.h"
#include "rhi_handles.h"
#include "rhi_types.h"
#include "shader/shader_compiler.h"

namespace Velos::RHI {
struct BufferDesc {
  u64 size = 0;
  BufferUsage usage = BufferUsage::None;
  MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
  const char *debugName = nullptr;
};

struct TextureDesc {
  u32 width = 0;
  u32 height = 0;
  u32 mipLevels = 1;
  Format format = Format::Undefined;
  TextureUsage usage = TextureUsage::None;
  const char *debugName = nullptr;
};

struct SamplerDesc {
  const char *debugName = nullptr;
};

struct ShaderDesc {
  ShaderStage stage = ShaderStage::None;
  const void *bytecode = nullptr;
  u64 bytecodeSize = 0;
  const char *entryPoint = "main";
  ShaderReflectionData reflection;
  const char *debugName = nullptr;
};

struct BufferUpdateDesc {
  BufferHandle buffer{};
  u64 offset = 0;
  const void *data = nullptr;
  u64 size = 0;
};

struct TextureUploadDesc {
  TextureHandle texture{};
  const void *data = nullptr;
  u64 dataSize = 0;
  u32 width = 0;
  u32 height = 0;
  u32 mipLevel = 0;
};

}; // namespace Velos::RHI
