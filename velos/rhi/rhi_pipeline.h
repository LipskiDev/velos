#pragma once

#include <span>
#include <vector>

#include "../core/types.h"
#include "rhi_handles.h"
#include "rhi_types.h"

namespace Velos::RHI {
enum class VertexInputRate {
  PerVertex,
  PerInstance,
};

struct VertexAttributeDesc {
  u32 location = 0;
  u32 binding = 0;
  VertexFormat format = VertexFormat::Float32;
  u32 offset = 0;
};

struct VertexBufferLayoutDesc {
  u32 stride = 0;
  VertexInputRate inputRate = VertexInputRate::PerVertex;
  std::vector<VertexAttributeDesc> attributes;
};

struct PushConstantRangeDesc {
  ShaderStage stages = ShaderStage::None;
  u32 offset = 0;
  u32 size = 0;
};

enum class DescriptorType {
  UniformBuffer,
  StorageBuffer,
  SampledTexture,
  Sampler,
  StorageTexture,
};

struct BindingDesc {
  u32 binding = 0;
  DescriptorType type = DescriptorType::UniformBuffer;
  u32 count = 1;
  ShaderStage stages = ShaderStage::None;
};

struct PipelineLayoutDesc {
  std::span<const BindingDesc> bindings;
  std::span<const PushConstantRangeDesc> pushConstants;
};

struct RasterStateDesc {
  bool cullBackFaces = true;
  bool frontFaceCCW = true;
  bool wireframe = false;
};

struct DepthStateDesc {
  bool depthTestEnable = false;
  bool depthWriteEnable = false;
  Format depthFormat = Format::Undefined;
};

struct BlendStateDesc {
  bool enable = false;
};

struct GraphicsPipelineDesc {
  ShaderHandle vertexShader{};
  ShaderHandle fragmentShader{};

  std::vector<VertexBufferLayoutDesc> vertexLayouts;

  PipelineLayoutDesc layout{};

  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  RasterStateDesc raster{};
  DepthStateDesc depth{};
  BlendStateDesc blend{};

  Format colorFormat = Format::Undefined;
  const char *debugName = nullptr;
};

}; // namespace Velos::RHI
