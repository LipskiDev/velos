#include "rhi/vulkan/vk_command_list.h"
#include "rhi/rhi_command_list.h"
#include "rhi/vulkan/vk_common.h"
#include "rhi/vulkan/vk_device.h"

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
  beginInfo.flags = 0;
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
  vkScissor.offset.x = scissor.offset.x;
  vkScissor.extent.width = scissor.extent.width;
  vkScissor.extent.height = scissor.extent.height;

  vkCmdSetScissor(commandBuffer_, 0, 1, &vkScissor);
}

void VulkanCommandList::Barrier(const BufferBarrier &) {
  throw std::runtime_error("Buffer barrier not implemented yet");
}

void VulkanCommandList::Barrier(const TextureBarrier &) {
  throw std::runtime_error("Texture barrier not implemented yet");
}

void VulkanCommandList::UpdateBuffer(const BufferUpdateDesc &) {
  throw std::runtime_error("UpdateBuffer not implemented yet");
}

void VulkanCommandList::UploadTexture(const TextureUploadDesc &) {
  throw std::runtime_error("UploadTexture not implemented yet");
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
  const VulkanTexture &colorTexture =
      device_.GetTexture(colorAttachment.texture);

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
  colorAttachmentInfo.imageView = colorTexture.view;
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = loadOp;
  colorAttachmentInfo.storeOp = storeOp;
  colorAttachmentInfo.clearValue = clearValue;

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
  vkRenderingInfo.pDepthAttachment = nullptr;
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

void VulkanCommandList::BindIndexBuffer(BufferHandle, IndexType, u64) {
  throw std::runtime_error("BindIndexBuffer not implemented yet");
}

void VulkanCommandList::BindUniformBuffer(u32, BufferHandle, u64, u64) {
  throw std::runtime_error("BindUniformBuffer not implemented yet");
}

void VulkanCommandList::BindSampledTexture(u32, TextureHandle, SamplerHandle) {
  throw std::runtime_error("BindSampledTexture not implemented yet");
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

void VulkanCommandList::Draw(u32 vertexCount, u32 firstVertex) {
  vkCmdDraw(commandBuffer_, vertexCount, 1, firstVertex, 0);
}

void VulkanCommandList::DrawIndexed(u32, u32, i32) {
  throw std::runtime_error("DrawIndexed not implemented yet");
}

} // namespace Velos::RHI
