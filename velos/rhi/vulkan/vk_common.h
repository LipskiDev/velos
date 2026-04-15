#pragma once

#include "rhi/rhi_pipeline.h"
#include "rhi/rhi_types.h"
#include <volk.h>

#include <vk_mem_alloc.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Velos::RHI {

inline VkResult setObjectDebugName(VkDevice device, VkObjectType type,
                                   uint64_t handle, const char *name) {
  if (!name || !*name || !vkSetDebugUtilsObjectNameEXT) {
    return VK_SUCCESS;
  }

  const VkDebugUtilsObjectNameInfoEXT ni = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = type,
      .objectHandle = handle,
      .pObjectName = name,
  };

  return vkSetDebugUtilsObjectNameEXT(device, &ni);
}

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
  case VertexFormat::UNorm8x4:
    return VK_FORMAT_R8G8B8A8_UNORM;
  default:
    throw std::runtime_error("Unsupported VertexFormat");
  }
}

inline VkShaderStageFlags ToVkShaderStage(ShaderStage stage) {
  VkShaderStageFlags flags = 0;

  if ((stage & ShaderStage::Vertex) == ShaderStage::Vertex) {
    flags |= VK_SHADER_STAGE_VERTEX_BIT;
  }

  if ((stage & ShaderStage::Fragment) == ShaderStage::Fragment) {
    flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  if ((stage & ShaderStage::Compute) == ShaderStage::Compute) {
    flags |= VK_SHADER_STAGE_COMPUTE_BIT;
  }

  if (flags == 0) {
    throw std::runtime_error("Unsupported ShaderStage (empty)");
  }

  return flags;
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
  if (HasFlag(usage, BufferUsage::ShaderDeviceAddress)) {
    flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
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

inline VkImageUsageFlags ToVkImageUsage(ImageUsage usage) {
  VkImageUsageFlags flags = 0;

  if ((usage & ImageUsage::TransferSrc) == ImageUsage::TransferSrc) {
    flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }

  if ((usage & ImageUsage::TransferDst) == ImageUsage::TransferDst) {
    flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  if ((usage & ImageUsage::Sampled) == ImageUsage::Sampled) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }

  if ((usage & ImageUsage::ColorAttachment) == ImageUsage::ColorAttachment) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }

  if ((usage & ImageUsage::DepthStencil) == ImageUsage::DepthStencil) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }

  return flags;
}

inline VkImageAspectFlags ToVkImageAspect(ImageAspect aspect) {
  VkImageAspectFlags flags = 0;

  if ((aspect & ImageAspect::Color) == ImageAspect::Color) {
    flags |= VK_IMAGE_ASPECT_COLOR_BIT;
  }

  if ((aspect & ImageAspect::Depth) == ImageAspect::Depth) {
    flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  if ((aspect & ImageAspect::Stencil) == ImageAspect::Stencil) {
    flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  return flags;
}

inline VkImageLayout ToVkImageLayout(ImageLayout layout) {
  switch (layout) {
  case ImageLayout::Undefined:
    return VK_IMAGE_LAYOUT_UNDEFINED;

  case ImageLayout::ColorAttachment:
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  case ImageLayout::DepthAttachment:
    return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

  case ImageLayout::ShaderReadOnly:
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  case ImageLayout::TransferSrc:
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  case ImageLayout::TransferDst:
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  case ImageLayout::Present:
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }

  throw std::runtime_error("Unsupported ImageLayout");
}

inline VkImageType ToVkImageType(ImageType type) {
  switch (type) {
  case ImageType::Image2D:
    return VK_IMAGE_TYPE_2D;

  case ImageType::Cube:
    // Cubemaps are still 2D images in Vulkan
    return VK_IMAGE_TYPE_2D;

  default:
    throw std::runtime_error("Unknown ImageType");
  }
}

inline VkImageViewType ToVkImageViewType(ImageViewType type) {
  switch (type) {
  case ImageViewType::View2D:
    return VK_IMAGE_VIEW_TYPE_2D;
  case ImageViewType::Cube:
    return VK_IMAGE_VIEW_TYPE_CUBE;
  default:
    throw std::runtime_error("Unknown ImageType");
  }
}

inline bool IsDepthFormat(Format format) {
  switch (format) {
  case Format::D32_FLOAT:
  case Format::D24_UNORM_S8_UINT:
    return true;

  default:
    return false;
  }
}

inline bool HasStencilAspect(Format format) {
  switch (format) {
  case Format::D24_UNORM_S8_UINT:
    return true;

  default:
    return false;
  }
}

inline ImageAspect DefaultImageAspectFromFormat(Format format) {
  switch (format) {
  case Format::RGBA8_UNORM:
  case Format::BGRA8_UNORM:
    return ImageAspect::Color;

  case Format::D32_FLOAT:
    return ImageAspect::Depth;

  case Format::D24_UNORM_S8_UINT:
    return ImageAspect::Depth | ImageAspect::Stencil;

  case Format::Undefined:
  default:
    return ImageAspect::None;
  }
}

inline VkFilter ToVkFilter(Filter filter) {
  switch (filter) {
  case Filter::Nearest:
    return VK_FILTER_NEAREST;
  case Filter::Linear:
    return VK_FILTER_LINEAR;
  default:
    return VK_FILTER_LINEAR;
  }
}

inline VkSamplerAddressMode ToVkSamplerAddressMode(SamplerAddressMode mode) {
  switch (mode) {
  case SamplerAddressMode::Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case SamplerAddressMode::ClampToEdge:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  default:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

inline VkDescriptorType ToVkDescriptorType(DescriptorType type) {
  switch (type) {
  case DescriptorType::UniformBuffer:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case DescriptorType::CombinedImageSampler:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  default:
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
  }
}

inline VkShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) {
  VkShaderStageFlags flags = 0;

  if ((stage & ShaderStage::Vertex) == ShaderStage::Vertex) {
    flags |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if ((stage & ShaderStage::Fragment) == ShaderStage::Fragment) {
    flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  if ((stage & ShaderStage::Compute) == ShaderStage::Compute) {
    flags |= VK_SHADER_STAGE_COMPUTE_BIT;
  }

  return flags;
}

inline VkIndexType ToVkIndexType(IndexType type) {
  switch (type) {
  case IndexType::U16:
    return VK_INDEX_TYPE_UINT16;
  case IndexType::U32:
    return VK_INDEX_TYPE_UINT32;
  default:
    throw std::runtime_error("Unsupported IndexType");
  }
}

inline VkBlendFactor ToVkBlendFactor(BlendFactor factor) {
  switch (factor) {
  case BlendFactor::Zero:
    return VK_BLEND_FACTOR_ZERO;
  case BlendFactor::One:
    return VK_BLEND_FACTOR_ONE;

  case BlendFactor::SrcColor:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case BlendFactor::OneMinusSrcColor:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;

  case BlendFactor::DstColor:
    return VK_BLEND_FACTOR_DST_COLOR;
  case BlendFactor::OneMinusDstColor:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;

  case BlendFactor::SrcAlpha:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case BlendFactor::OneMinusSrcAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

  case BlendFactor::DstAlpha:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case BlendFactor::OneMinusDstAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;

  case BlendFactor::ConstantColor:
    return VK_BLEND_FACTOR_CONSTANT_COLOR;
  case BlendFactor::OneMinusConstantColor:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;

  case BlendFactor::ConstantAlpha:
    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
  case BlendFactor::OneMinusConstantAlpha:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;

  default:
    throw std::runtime_error("Unsupported BlendFactor");
  }
}

inline VkBlendOp ToVkBlendOp(BlendOp op) {
  switch (op) {
  case BlendOp::Add:
    return VK_BLEND_OP_ADD;
  case BlendOp::Subtract:
    return VK_BLEND_OP_SUBTRACT;
  case BlendOp::ReverseSubtract:
    return VK_BLEND_OP_REVERSE_SUBTRACT;
  case BlendOp::Min:
    return VK_BLEND_OP_MIN;
  case BlendOp::Max:
    return VK_BLEND_OP_MAX;

  default:
    throw std::runtime_error("Unsupported BlendOp");
  }
}
} // namespace Velos::RHI
