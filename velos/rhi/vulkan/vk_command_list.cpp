#include "vlpch.h"
#include "rhi/vulkan/vk_command_list.h"
#include "rhi/rhi_command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/vulkan/vk_common.h"
#include "rhi/vulkan/vk_device.h"

#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace Velos::RHI {

VulkanCommandList::VulkanCommandList(VulkanDevice &device,
                                     VkCommandBuffer commandBuffer)
    : device_(device), commandBuffer_(commandBuffer) {}

void VulkanCommandList::Begin() {
  VK_CHECK(vkResetCommandBuffer(commandBuffer_, 0),
           "Failed to reset Vulkan command buffer");

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  beginInfo.pInheritanceInfo = nullptr;

  VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
           "Failed to begin Vulkan command buffer");
}

void VulkanCommandList::End() {
  VK_CHECK(vkEndCommandBuffer(commandBuffer_),
           "Failed to end Vulkan command buffer");
}

void VulkanCommandList::SetViewport(const Viewport &viewport) {
  VkViewport vkViewport{};
  vkViewport.x = viewport.x;
  vkViewport.y = viewport.y;
  vkViewport.width = viewport.width;
  vkViewport.height = viewport.height;
  vkViewport.minDepth = viewport.minDepth;
  vkViewport.maxDepth = viewport.maxDepth;

  vkCmdSetViewport(commandBuffer_, 0, 1, &vkViewport);
}

void VulkanCommandList::SetScissor(const Rect2D &scissor) {
  VkRect2D vkScissor{};
  vkScissor.offset.x = scissor.offset.x;
  vkScissor.offset.y = scissor.offset.y;
  vkScissor.extent.width = scissor.extent.width;
  vkScissor.extent.height = scissor.extent.height;

  vkCmdSetScissor(commandBuffer_, 0, 1, &vkScissor);
}

void VulkanCommandList::Barrier(const BufferBarrier &) {
  throw std::runtime_error("Buffer barrier not implemented yet");
}

void VulkanCommandList::Barrier(const ImageBarrier &barrier) {
  if (!barrier.image.IsValid()) {
    throw std::runtime_error("Image barrier requires a valid image handle");
  }

  const VulkanImage &srcImage = device_.GetImage(barrier.image);
  VulkanImage &image = const_cast<VulkanImage &>(srcImage);

  VkPipelineStageFlags srcStageMask = 0;
  VkPipelineStageFlags dstStageMask = 0;
  VkAccessFlags srcAccessMask = 0;
  VkAccessFlags dstAccessMask = 0;

  const ImageLayout oldLayout = image.layout;
  const ImageLayout newLayout = barrier.newLayout;

  if (oldLayout == newLayout) {
    return;
  }

  // Determine stage/access masks for supported transitions
  if (oldLayout == ImageLayout::Undefined &&
      newLayout == ImageLayout::ColorAttachment) {
    srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  } else if (oldLayout == ImageLayout::ColorAttachment &&
             newLayout == ImageLayout::Present) {
    srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dstAccessMask = 0;
  } else if (oldLayout == ImageLayout::Undefined &&
             newLayout == ImageLayout::DepthAttachment) {
    srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  } else if (oldLayout == ImageLayout::Undefined &&
             newLayout == ImageLayout::TransferDst) {
    srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  } else if (oldLayout == ImageLayout::TransferDst &&
             newLayout == ImageLayout::ShaderReadOnly) {
    srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  } else if (oldLayout == ImageLayout::Present &&
             newLayout == ImageLayout::ColorAttachment) {
    srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  } else {
    throw std::runtime_error("Unsupported image barrier transition");
  }

  VkImageMemoryBarrier vkBarrier{};
  vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  vkBarrier.srcAccessMask = srcAccessMask;
  vkBarrier.dstAccessMask = dstAccessMask;
  vkBarrier.oldLayout = ToVkImageLayout(oldLayout);
  vkBarrier.newLayout = ToVkImageLayout(newLayout);
  vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  vkBarrier.image = image.image;
  vkBarrier.subresourceRange.aspectMask = ToVkImageAspect(barrier.aspect);
  vkBarrier.subresourceRange.baseMipLevel = 0;
  vkBarrier.subresourceRange.levelCount = image.mipLevels;
  vkBarrier.subresourceRange.baseArrayLayer = 0;
  vkBarrier.subresourceRange.layerCount = image.arrayLayers;

  vkCmdPipelineBarrier(device_.GetCommandBuffer(), srcStageMask, dstStageMask,
                       0, 0, nullptr, 0, nullptr, 1, &vkBarrier);

  image.layout = newLayout;
}

void VulkanCommandList::UpdateBuffer(const BufferUpdateDesc &update) {
  const VulkanBuffer &dst = device_.GetBuffer(update.buffer);

  void *mapped = nullptr;
  VkResult result = vkMapMemory(device_.GetVkDevice(), dst.memory, 0,
                                VK_WHOLE_SIZE, 0, &mapped);
  if (result != VK_SUCCESS || mapped == nullptr) {
    throw std::runtime_error(
        "VulkanCommandList::UpdateBuffer: vkMapMemory failed");
  }

  memcpy(static_cast<std::byte *>(mapped) + update.offset, update.data,
         static_cast<size_t>(update.size));

  if (true) {
    const VkDeviceSize atomSize =
        device_.GetPhysicalDeviceProperties().limits.nonCoherentAtomSize;

    const VkDeviceSize alignedOffset = update.offset & ~(atomSize - 1);
    const VkDeviceSize end = update.offset + update.size;
    const VkDeviceSize alignedEnd = (end + atomSize - 1) & ~(atomSize - 1);

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = dst.memory;
    range.offset = alignedOffset;
    range.size = alignedEnd - alignedOffset;

    result = vkFlushMappedMemoryRanges(device_.GetVkDevice(), 1, &range);
    if (result != VK_SUCCESS) {
      vkUnmapMemory(device_.GetVkDevice(), dst.memory);
      throw std::runtime_error(
          "VulkanCommandList::UpdateBuffer: vkFlushMappedMemoryRanges failed");
    }
  }

  vkUnmapMemory(device_.GetVkDevice(), dst.memory);
}

void VulkanCommandList::BeginRendering(const RenderingInfo &renderingInfo) {
  if (renderingInfo.colorAttachmentCount == 0 ||
      !renderingInfo.colorAttachments) {
    throw std::runtime_error(
        "BeginRendering requires at least one color attachment");
  }

  if (renderingInfo.colorAttachmentCount != 1) {
    throw std::runtime_error(
        "BeginRendering currently supports exactly one color attachment");
  }

  const ColorAttachmentDesc &colorAttachment =
      renderingInfo.colorAttachments[0];
  const VulkanImageView &colorView = device_.GetImageView(colorAttachment.view);

  VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  switch (colorAttachment.loadOp) {
  case LoadOp::Load:
    loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    break;
  case LoadOp::Clear:
    loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    break;
  case LoadOp::DontCare:
    loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    break;
  }

  VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  switch (colorAttachment.storeOp) {
  case StoreOp::Store:
    storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    break;
  case StoreOp::DontCare:
    storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    break;
  }

  VkClearValue clearValue{};
  clearValue.color.float32[0] = colorAttachment.clearValue.r;
  clearValue.color.float32[1] = colorAttachment.clearValue.g;
  clearValue.color.float32[2] = colorAttachment.clearValue.b;
  clearValue.color.float32[3] = colorAttachment.clearValue.a;

  VkRenderingAttachmentInfo colorAttachmentInfo{};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = colorView.view;
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = loadOp;
  colorAttachmentInfo.storeOp = storeOp;
  colorAttachmentInfo.clearValue = clearValue;

  VkRenderingAttachmentInfo depthAttachmentInfo{};
  bool hasDepthAttachment = renderingInfo.depthAttachment != nullptr;

  if (hasDepthAttachment) {
    const DepthAttachmentDesc &depthAttachment = *renderingInfo.depthAttachment;
    const VulkanImageView &depthView =
        device_.GetImageView(depthAttachment.view);

    VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    switch (depthAttachment.loadOp) {
    case LoadOp::Load:
      depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      break;
    case LoadOp::Clear:
      depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      break;
    case LoadOp::DontCare:
      depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      break;
    }

    VkAttachmentStoreOp depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    switch (depthAttachment.storeOp) {
    case StoreOp::Store:
      depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
      break;
    case StoreOp::DontCare:
      depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      break;
    }

    VkClearValue depthClearValue{};
    depthClearValue.depthStencil.depth = depthAttachment.clearDepth;
    depthClearValue.depthStencil.stencil = depthAttachment.clearStencil;

    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depthView.view;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = depthLoadOp;
    depthAttachmentInfo.storeOp = depthStoreOp;
    depthAttachmentInfo.clearValue = depthClearValue;
  }

  VkRenderingInfo vkRenderingInfo{};
  vkRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  vkRenderingInfo.renderArea.offset.x = renderingInfo.renderArea.offset.x;
  vkRenderingInfo.renderArea.offset.y = renderingInfo.renderArea.offset.y;
  vkRenderingInfo.renderArea.extent.width =
      renderingInfo.renderArea.extent.width;
  vkRenderingInfo.renderArea.extent.height =
      renderingInfo.renderArea.extent.height;
  vkRenderingInfo.layerCount = 1;
  vkRenderingInfo.colorAttachmentCount = 1;
  vkRenderingInfo.pColorAttachments = &colorAttachmentInfo;
  vkRenderingInfo.pDepthAttachment =
      hasDepthAttachment ? &depthAttachmentInfo : nullptr;
  vkRenderingInfo.pStencilAttachment = nullptr;

  vkCmdBeginRendering(commandBuffer_, &vkRenderingInfo);
}

void VulkanCommandList::EndRendering() { vkCmdEndRendering(commandBuffer_); }

void VulkanCommandList::BindPipeline(PipelineHandle pipeline) {
  const VulkanPipeline &vkPipeline = device_.GetPipeline(pipeline);

  vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vkPipeline.pipeline);
  boundGraphicsPipeline_ = pipeline;
}

void VulkanCommandList::BindVertexBuffer(u32 firstSlot,
                                         BufferHandle bufferHandle,
                                         u64 offset) {

  const VulkanBuffer &buffer = device_.GetBuffer(bufferHandle);

  if (buffer.buffer == VK_NULL_HANDLE) {
    throw std::runtime_error("BindVertexBuffer: invalid Vulkan buffer");
  }

  if (!HasFlag(buffer.usage, BufferUsage::Vertex)) {
    throw std::runtime_error(
        "BindVertexBuffer: buffer was not created with BufferUsage::Vertex");
  }

  if (offset >= buffer.size) {
    throw std::runtime_error("BindVertexBuffer: offset is out of bounds");
  }

  VkBuffer vkBuffer = buffer.buffer;
  VkDeviceSize vkOffset = static_cast<VkDeviceSize>(offset);

  vkCmdBindVertexBuffers(commandBuffer_, firstSlot, 1, &vkBuffer, &vkOffset);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer,
                                        IndexType indexType, u64 offset) {
  if (!buffer.IsValid()) {
    throw std::runtime_error("BindIndexBuffer: requires a valid buffer handle");
  }

  const VulkanBuffer &vkBuffer = device_.GetBuffer(buffer);

  vkCmdBindIndexBuffer(commandBuffer_, vkBuffer.buffer,
                       static_cast<VkDeviceSize>(offset),
                       ToVkIndexType(indexType));
}

void VulkanCommandList::BindUniformBuffer(u32, BufferHandle, u64, u64) {
  throw std::runtime_error("BindUniformBuffer not implemented yet");
}

void VulkanCommandList::PushConstants(ShaderStage stage, u32 offset, u32 size,
                                      const void *data) {
  if (!boundGraphicsPipeline_) {
    throw std::runtime_error(
        "PushConstants called without a bound graphics pipeline");
  }

  const VulkanPipeline &vkPipeline =
      device_.GetPipeline(boundGraphicsPipeline_);

  vkCmdPushConstants(commandBuffer_, vkPipeline.layout, ToVkShaderStage(stage),
                     offset, size, data);
}

void VulkanCommandList::BindDescriptorSet(PipelineHandle pipeline, u32 setIndex,
                                          DescriptorSetHandle descriptorSet) {
  const VulkanPipeline &vkPipeline = device_.GetPipeline(pipeline);
  const VulkanDescriptorSet &vkDescriptorSet =
      device_.GetDescriptorSet(descriptorSet);

  VkDescriptorSet set = vkDescriptorSet.set;

  // std::cout << "[BindDescriptorSet]\n";
  // std::cout << "  pipeline handle: " << pipeline.id << "\n";
  // std::cout << "  descriptor set handle: " << descriptorSet.id << "\n";
  // std::cout << "  VkPipelineLayout: " << vkPipeline.layout << "\n";
  // std::cout << "  VkDescriptorSet: " << set << "\n";
  // std::cout << "  set index: " << setIndex << "\n";

  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vkPipeline.layout, setIndex, 1, &set, 0, nullptr);
}

void VulkanCommandList::CopyBufferToImage(BufferHandle src, ImageHandle dst,
                                          const BufferImageCopyRegion &region) {
  const VulkanBuffer &vkBuffer = device_.GetBuffer(src);
  const VulkanImage &vkImage = device_.GetImage(dst);

  VkBufferImageCopy vkRegion{};
  vkRegion.bufferOffset = region.bufferOffset;
  vkRegion.bufferRowLength = region.bufferRowLength;
  vkRegion.bufferImageHeight = region.bufferImageHeight;

  vkRegion.imageSubresource.aspectMask = ToVkImageAspect(region.aspect);
  vkRegion.imageSubresource.mipLevel = region.mipLevel;
  vkRegion.imageSubresource.baseArrayLayer = region.baseArrayLayer;
  vkRegion.imageSubresource.layerCount = region.layerCount;

  vkRegion.imageOffset = {static_cast<int32_t>(region.imageOffset.x),
                          static_cast<int32_t>(region.imageOffset.y),
                          static_cast<int32_t>(region.imageOffset.z)};

  vkRegion.imageExtent = {
      region.imageExtent.width,
      region.imageExtent.height,
      region.imageExtent.depth,
  };

  vkCmdCopyBufferToImage(commandBuffer_, vkBuffer.buffer, vkImage.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkRegion);
};

void VulkanCommandList::Draw(u32 vertexCount, u32 instanceCount,
                             u32 firstVertex, u32 baseInstance) {
  vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex,
            baseInstance);
}

void VulkanCommandList::DrawIndexed(u32 indexCount, u32 firstIndex,
                                    i32 vertexOffset) {
  vkCmdDrawIndexed(commandBuffer_, indexCount, 1, firstIndex, vertexOffset, 0);
}

} // namespace Velos::RHI
