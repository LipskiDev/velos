#pragma once

#include "rhi/rhi_types.h"
#include <volk.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace Velos::RHI {
inline void VK_CHECK(VkResult result, const char *message) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(message);
  }
}

inline VkFormat ToVkFormat(Format format) {
  switch (format) {
  case Format::RGBA8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case Format::BGRA8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case Format::RGBA16_FLOAT:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case Format::R32_FLOAT:
    return VK_FORMAT_R32_SFLOAT;
  case Format::RG32_FLOAT:
    return VK_FORMAT_R32G32_SFLOAT;
  case Format::RGB32_FLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case Format::RGBA32_FLOAT:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case Format::D32_FLOAT:
    return VK_FORMAT_D32_SFLOAT;
  case Format::D24_UNORM_S8_UINT:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

inline VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return VK_SHADER_STAGE_VERTEX_BIT;

  case ShaderStage::Fragment:
    return VK_SHADER_STAGE_FRAGMENT_BIT;

  case ShaderStage::Compute:
    return VK_SHADER_STAGE_COMPUTE_BIT;

  default:
    throw std::runtime_error("Unsupported ShaderStage");
  }
}

inline VkPrimitiveTopology ToVkPrimitiveTopology(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::TriangleList:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case PrimitiveTopology::TriangleStrip:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case PrimitiveTopology::LineList:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  default:
    throw std::runtime_error("Unsupported primitive topology");
  }
}
} // namespace Velos::RHI
