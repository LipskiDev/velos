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

struct PipelineLayoutDesc {
  const DescriptorSetLayoutHandle *descriptorSetLayouts = nullptr;
  u32 descriptorSetLayoutCount = 0;
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

  BlendFactor srcColor = BlendFactor::SrcAlpha;
  BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
  BlendOp colorOp = BlendOp::Add;

  BlendFactor srcAlpha = BlendFactor::One;
  BlendFactor dstAlpha = BlendFactor::OneMinusSrcAlpha;
  BlendOp alphaOp = BlendOp::Add;
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

struct ComputePipelineDesc {
  ShaderHandle computeShader;

  PipelineLayoutDesc layout{};

  const char *debugName = nullptr;
};

}; // namespace Velos::RHI
