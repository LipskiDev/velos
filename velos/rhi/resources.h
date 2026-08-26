#pragma once

#include "../core/types.h"
#include "handles.h"
#include "types.h"
#include "shader/shader_compiler.h"

namespace Velos::RHI {
struct BufferDesc {
  u64 size = 0;
  BufferUsage usage = BufferUsage::None;
  MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
  const void *initialData = nullptr;
  const char *debugName = nullptr;
};

struct QueryPoolDesc {
  u32 queryCount = 0;
  const char *debugName = nullptr;
};

struct ImageDesc {
  u32 width = 1;
  u32 height = 1;
  u32 depth = 1;

  u32 mipLevels = 1;
  u32 arrayLayers = 1;

  Format format = Format::Undefined;
  ImageType type = ImageType::Image2D;
  ImageUsage usage = ImageUsage::None;

  const char *debugName = nullptr;
};

struct SwapchainDesc {
  void *windowHandle = nullptr;
  u32 width = 0;
  u32 height = 0;
  Format format = Format::BGRA8_UNORM;
  u32 bufferCount = 2;
  bool vsync = true;
  const char *debugName = nullptr;
};

struct ImageViewDesc {
  ImageHandle image;
  Format format = Format::Undefined;
  ImageViewType type = ImageViewType::View2D;
  ImageAspect aspect = ImageAspect::Color;

  u32 baseMipLevel = 0;
  u32 mipLevelCount = 1;
  u32 baseArrayLayer = 0;
  u32 arrayLayerCount = 1;

  const char *debugName = nullptr;
};

struct SamplerDesc {
  Filter minFilter = Filter::Linear;
  Filter magFilter = Filter::Linear;
  SamplerAddressMode addressU = SamplerAddressMode::Repeat;
  SamplerAddressMode addressV = SamplerAddressMode::Repeat;
  SamplerAddressMode addressW = SamplerAddressMode::Repeat;
  bool enableAnisotropy = false;
  float maxAnisotropy = 1.0f;
  float minLod = 0.0f;
  float maxLod = 0.0f;

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

struct BufferCopyRegion {
  u64 srcOffset;
  u64 dstOffset;
  u64 size;
};

struct BufferImageCopyRegion {
  u64 bufferOffset = 0;

  u32 bufferRowLength = 0;
  u32 bufferImageHeight = 0;

  u32 mipLevel = 0;
  u32 baseArrayLayer = 0;
  u32 layerCount = 1;

  Offset3D imageOffset = {0, 0, 0};
  Extent3D imageExtent = {1, 1, 1};

  ImageAspect aspect = ImageAspect::Color;
};

enum class BindingFlags : u32 {
	None = 0,
	PartiallyBound = 1 << 0,
	UpdateAfterBind = 1 << 1,
	VariableCount = 1 << 2,
};

enum class BindingPoolFlags : u32 {
	None = 0,
	UpdateAfterBind = 1 << 0
};

struct BindingDesc {
  u32 binding = 0;
  BindingType type = BindingType::UniformBuffer;
  u32 count = 1;
  ShaderStage visibility = ShaderStage::Vertex;
  BindingFlags flags = BindingFlags::None;
};

struct BindingSetAllocationDesc {
	BindingPoolHandle pool;
	BindingLayoutHandle layout;
	u32 variableBindingCount = 0;
	const char* debugName = nullptr;
};

struct BindingLayoutDesc {
  const BindingDesc *bindings = nullptr;
  u32 bindingCount = 0;
  const char *debugName = nullptr;
};

struct BindingPoolDesc {
  const BindingPoolSize *poolSizes = nullptr;
  u32 poolSizeCount = 0;
  u32 maxSets = 0;
  BindingPoolFlags flags = BindingPoolFlags::None;
  const char *debugName = nullptr;
};

struct BindingBufferInfo {
  BufferHandle buffer;
  u64 offset = 0;
  u64 range = 0;
};

struct BindingImageInfo {
  SamplerHandle sampler;
  ImageViewHandle imageView;
  ImageLayout imageLayout = ImageLayout::ShaderReadOnly;
};

struct BindingWriteDesc {
  BindingSetHandle dstSet;
  u32 binding = 0;
  u32 arrayElement = 0;
  BindingType type = BindingType::UniformBuffer;

  const BindingBufferInfo *bufferInfo = nullptr;
  const BindingImageInfo *imageInfo = nullptr;
  u32 descriptorCount = 1;
};

struct GeneratedPipelineLayout {
	std::vector<RHI::BindingLayoutHandle> setLayouts;
	std::vector<RHI::BindingLayoutHandle> ownedSetLayouts;
};

}; // namespace Velos::RHI
