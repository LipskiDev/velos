#pragma once

#include "rhi/rhi_pipeline.h"
#include "rhi/rhi_types.h"
#include <volk.h>

#include <vk_mem_alloc.h>

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

inline VkFormat ToVkVertexFormat(VertexFormat format) {
  switch (format) {
  case VertexFormat::Float32:
    return VK_FORMAT_R32_SFLOAT;
  case VertexFormat::Float32x2:
    return VK_FORMAT_R32G32_SFLOAT;
  case VertexFormat::Float32x3:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case VertexFormat::Float32x4:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  default:
    throw std::runtime_error("Unsupported VertexFormat");
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

inline VkBufferUsageFlags ToVkBufferUsageFlags(BufferUsage usage) {
  VkBufferUsageFlags flags = 0;

  if (HasFlag(usage, BufferUsage::Vertex)) {
    flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (HasFlag(usage, BufferUsage::Index)) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (HasFlag(usage, BufferUsage::Uniform)) {
    flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (HasFlag(usage, BufferUsage::Storage)) {
    flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if (HasFlag(usage, BufferUsage::TransferSrc)) {
    flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if (HasFlag(usage, BufferUsage::TransferDst)) {
    flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  return flags;
}

inline VmaAllocationCreateInfo ToVmaAllocationCreateInfo(MemoryUsage usage) {
  VmaAllocationCreateInfo allocInfo{};

  switch (usage) {
  case MemoryUsage::GPUOnly:
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    break;

  case MemoryUsage::CPUToGPU:
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    break;

  case MemoryUsage::GPUToCPU:
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    break;

  default:
    throw std::runtime_error("Unsupported MemoryUsage");
  }

  return allocInfo;
}

inline VkMemoryPropertyFlags ToVkMemoryPropertyFlags(MemoryUsage usage) {
  switch (usage) {
  case MemoryUsage::GPUOnly:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  case MemoryUsage::CPUToGPU:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  case MemoryUsage::GPUToCPU:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }

  throw std::runtime_error("Unsupported MemoryUsage");
}

inline VkVertexInputRate ToVkInputRate(VertexInputRate rate) {
  switch (rate) {
  case VertexInputRate::PerVertex:
    return VK_VERTEX_INPUT_RATE_VERTEX;
  case VertexInputRate::PerInstance:
    return VK_VERTEX_INPUT_RATE_INSTANCE;
  }

  throw std::runtime_error("Unsupported VertexInputRate");
}
} // namespace Velos::RHI
