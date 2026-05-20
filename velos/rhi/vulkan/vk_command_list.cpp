#include "rhi/vulkan/vk_command_list.h"
#include "rhi/rhi_command_list.h"
#include "rhi/vulkan/vk_common.h"
#include "rhi/vulkan/vk_device.h"
#include "vlpch.h"

#include <stdexcept>

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

void VulkanCommandList::Barrier(const BufferBarrier &barrier) {
  PipelineBarrier(std::span<const BufferBarrier>(&barrier, 1), {});
}

void VulkanCommandList::Barrier(const ImageBarrier &barrier) {
  PipelineBarrier({}, std::span<const ImageBarrier>(&barrier, 1));
}

void VulkanCommandList::BeginRendering(const RenderingInfo &renderingInfo) {
  const bool hasColor = renderingInfo.colorAttachmentCount > 0 &&
                        renderingInfo.colorAttachments != nullptr;

  const bool hasDepth = renderingInfo.depthAttachment != nullptr;

  if (!hasColor && !hasDepth) {
    throw std::runtime_error("BeginRendering requires at least one attachment");
  }

  if (renderingInfo.colorAttachmentCount > 1) {
    throw std::runtime_error(
        "BeginRendering currently supports at most one color attachment");
  }

  VkRenderingAttachmentInfo colorAttachmentInfo{};

  if (hasColor) {
    const ColorAttachmentDesc &colorAttachment =
        renderingInfo.colorAttachments[0];

    const VulkanImageView &colorView =
        device_.GetImageView(colorAttachment.view);

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

    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;

    colorAttachmentInfo.imageView = colorView.view;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    colorAttachmentInfo.loadOp = loadOp;
    colorAttachmentInfo.storeOp = storeOp;
    colorAttachmentInfo.clearValue = clearValue;
  }

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
  vkRenderingInfo.colorAttachmentCount =
      hasColor ? renderingInfo.colorAttachmentCount : 0;

  vkRenderingInfo.pColorAttachments = hasColor ? &colorAttachmentInfo : nullptr;
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

void VulkanCommandList::BindComputePipeline(PipelineHandle pipeline) {
  const VulkanPipeline &vkPipeline = device_.GetPipeline(pipeline);

  vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                    vkPipeline.pipeline);

  boundComputePipeline_ = pipeline;
}

void VulkanCommandList::GenerateMipmaps(ImageHandle imageHandle, uint32_t width,
                                        uint32_t height, uint32_t mipLevels,
                                        uint32_t arrayLayers) {
  const VulkanImage &image = device_.GetImage(imageHandle);

  if (mipLevels <= 1)
    return;

  int32_t mipWidth = static_cast<int32_t>(width);
  int32_t mipHeight = static_cast<int32_t>(height);

  for (uint32_t mip = 1; mip < mipLevels; ++mip) {
    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = image.image;
    toSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toSrc.subresourceRange.baseMipLevel = mip - 1;
    toSrc.subresourceRange.levelCount = 1;
    toSrc.subresourceRange.baseArrayLayer = 0;
    toSrc.subresourceRange.layerCount = arrayLayers;

    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toSrc);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = mip - 1;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = arrayLayers;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = mip;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = arrayLayers;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {
        mipWidth > 1 ? mipWidth / 2 : 1,
        mipHeight > 1 ? mipHeight / 2 : 1,
        1,
    };

    vkCmdBlitImage(commandBuffer_, image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = image.image;
    toShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toShaderRead.subresourceRange.baseMipLevel = mip - 1;
    toShaderRead.subresourceRange.levelCount = 1;
    toShaderRead.subresourceRange.baseArrayLayer = 0;
    toShaderRead.subresourceRange.layerCount = arrayLayers;

    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toShaderRead);

    mipWidth = std::max(1, mipWidth / 2);
    mipHeight = std::max(1, mipHeight / 2);
  }

  VkImageMemoryBarrier lastMipToShaderRead{};
  lastMipToShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  lastMipToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  lastMipToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  lastMipToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  lastMipToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  lastMipToShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  lastMipToShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  lastMipToShaderRead.image = image.image;
  lastMipToShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  lastMipToShaderRead.subresourceRange.baseMipLevel = mipLevels - 1;
  lastMipToShaderRead.subresourceRange.levelCount = 1;
  lastMipToShaderRead.subresourceRange.baseArrayLayer = 0;
  lastMipToShaderRead.subresourceRange.layerCount = arrayLayers;

  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &lastMipToShaderRead);
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

void VulkanCommandList::CopyBuffer(BufferHandle src, BufferHandle dst,
                                   const BufferCopyRegion &region) {
  VkBuffer srcBuffer = device_.GetBuffer(src).buffer;
  VkBuffer dstBuffer = device_.GetBuffer(dst).buffer;

  VkBufferCopy copy{};
  copy.srcOffset = region.srcOffset;
  copy.dstOffset = region.dstOffset;
  copy.size = region.size;

  vkCmdCopyBuffer(commandBuffer_, srcBuffer, dstBuffer, 1, &copy);
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

void VulkanCommandList::BindComputeDescriptorSet(PipelineHandle pipeline,
                                                 uint32_t setIndex,
                                                 DescriptorSetHandle set) {
  const VulkanPipeline &vkPipeline = device_.GetPipeline(pipeline);
  const VulkanDescriptorSet &vkSet = device_.GetDescriptorSet(set);

  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                          vkPipeline.layout, setIndex, 1, &vkSet.set, 0,
                          nullptr);
}

void VulkanCommandList::UpdateBuffer(const BufferUpdateDesc &update) {
  const VulkanBuffer &dst = device_.GetBuffer(update.buffer);

  if (update.data == nullptr) {
    throw std::runtime_error(
        "VulkanCommandList::UpdateBuffer: update.data must not be null");
  }

  if (update.offset + update.size > dst.size) {
    throw std::runtime_error(
        "VulkanCommandList::UpdateBuffer: write out of bounds");
  }

  if (dst.memoryUsage == MemoryUsage::GPUOnly) {
    throw std::runtime_error("VulkanCommandList::UpdateBuffer: cannot update "
                             "GPUOnly buffer directly");
  }

  void *mapped = dst.allocationInfo.pMappedData;

  if (mapped == nullptr) {
    VkResult result =
        vmaMapMemory(device_.GetAllocator(), dst.allocation, &mapped);
    if (result != VK_SUCCESS || mapped == nullptr) {
      throw std::runtime_error(
          "VulkanCommandList::UpdateBuffer: vmaMapMemory failed");
    }

    memcpy(static_cast<std::byte *>(mapped) + update.offset, update.data,
           static_cast<size_t>(update.size));

    vmaFlushAllocation(device_.GetAllocator(), dst.allocation, update.offset,
                       update.size);

    vmaUnmapMemory(device_.GetAllocator(), dst.allocation);
  } else {
    memcpy(static_cast<std::byte *>(mapped) + update.offset, update.data,
           static_cast<size_t>(update.size));

    vmaFlushAllocation(device_.GetAllocator(), dst.allocation, update.offset,
                       update.size);
  }
}
void VulkanCommandList::CopyBufferToImage(BufferHandle src, ImageHandle dst,
                                          const BufferImageCopyRegion &region) {

  VkBuffer buffer = device_.GetBuffer(src).buffer;
  VkImage image = device_.GetImage(dst).image;

  VkBufferImageCopy copy{};

  copy.bufferOffset = region.bufferOffset;
  copy.bufferRowLength = region.bufferRowLength;
  copy.bufferImageHeight = region.bufferImageHeight;

  copy.imageSubresource.aspectMask = ToVkImageAspect(region.aspect);
  copy.imageSubresource.mipLevel = region.mipLevel;
  copy.imageSubresource.baseArrayLayer = region.baseArrayLayer;
  copy.imageSubresource.layerCount = region.layerCount;

  copy.imageOffset = {(int32_t)region.imageOffset.x,
                      (int32_t)region.imageOffset.y,
                      (int32_t)region.imageOffset.z};

  copy.imageExtent = {region.imageExtent.width, region.imageExtent.height,
                      region.imageExtent.depth};

  vkCmdCopyBufferToImage(commandBuffer_, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void VulkanCommandList::PipelineBarrier(std::span<const BufferBarrier> buffers,
                                        std::span<const ImageBarrier> images) {

  std::vector<VkBufferMemoryBarrier> vkBufferBarriers;
  std::vector<VkImageMemoryBarrier> vkImageBarriers;

  VkPipelineStageFlags srcStageMask = 0;
  VkPipelineStageFlags dstStageMask = 0;

  vkBufferBarriers.reserve(buffers.size());
  vkImageBarriers.reserve(images.size());

  for (const auto &b : buffers) {
    const auto srcInfo = GetBufferBarrierInfo(b.oldState);
    const auto dstInfo = GetBufferBarrierInfo(b.newState);

    VkBufferMemoryBarrier vkBarrier{};
    vkBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    vkBarrier.srcAccessMask = srcInfo.access;
    vkBarrier.dstAccessMask = dstInfo.access;
    vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkBarrier.buffer = device_.GetBuffer(b.buffer).buffer;
    vkBarrier.offset = 0;
    vkBarrier.size = VK_WHOLE_SIZE;

    vkBufferBarriers.push_back(vkBarrier);

    srcStageMask |= srcInfo.stage;
    dstStageMask |= dstInfo.stage;
  }

  for (const auto &i : images) {
    const auto srcInfo = GetImageBarrierInfo(i.oldLayout);
    const auto dstInfo = GetImageBarrierInfo(i.newLayout);

    VkImageMemoryBarrier vkBarrier{};
    vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    vkBarrier.srcAccessMask = srcInfo.access;
    vkBarrier.dstAccessMask = dstInfo.access;
    vkBarrier.oldLayout = ToVkImageLayout(i.oldLayout);
    vkBarrier.newLayout = ToVkImageLayout(i.newLayout);
    vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkBarrier.image = device_.GetImage(i.image).image;
    vkBarrier.subresourceRange.aspectMask = ToVkImageAspect(i.aspect);
    vkBarrier.subresourceRange.baseMipLevel = i.baseMipLevel;
    vkBarrier.subresourceRange.levelCount = i.mipLevelCount;
    vkBarrier.subresourceRange.baseArrayLayer = i.baseArrayLayer;
    vkBarrier.subresourceRange.layerCount = i.layerCount;

    vkImageBarriers.push_back(vkBarrier);

    srcStageMask |= srcInfo.stage;
    dstStageMask |= dstInfo.stage;
  }

  if (srcStageMask == 0)
    srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (dstStageMask == 0)
    dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

  vkCmdPipelineBarrier(
      commandBuffer_, srcStageMask, dstStageMask, 0, 0, nullptr,
      static_cast<u32>(vkBufferBarriers.size()), vkBufferBarriers.data(),
      static_cast<u32>(vkImageBarriers.size()), vkImageBarriers.data());
}
void VulkanCommandList::Draw(u32 vertexCount, u32 instanceCount,
                             u32 firstVertex, u32 baseInstance) {
  vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex,
            baseInstance);
}

void VulkanCommandList::DrawIndexed(u32 indexCount, u32 firstIndex,
                                    i32 vertexOffset) {
  vkCmdDrawIndexed(commandBuffer_, indexCount, 1, firstIndex, vertexOffset, 0);
}

void VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
  if (!boundComputePipeline_.IsValid()) {
    throw std::runtime_error("Dispatch called without bound compute pipeline");
  }

  if (x == 0 || y == 0 || z == 0) {
    return;
  }

  vkCmdDispatch(commandBuffer_, x, y, z);
}

} // namespace Velos::RHI
