#include "rhi/vulkan/vk_command_list.h"
#include "rhi/vulkan/vk_common.h"

#include <stdexcept>

namespace Velos::RHI {

VulkanCommandList::VulkanCommandList(VkCommandBuffer commandBuffer)
    : commandBuffer_(commandBuffer) {}

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

void VulkanCommandList::BeginRendering(const RenderingInfo &) {
  throw std::runtime_error("BeginRendering not implemented yet");
}

void VulkanCommandList::EndRendering() {
  throw std::runtime_error("EndRendering not implemented yet");
}

void VulkanCommandList::BindPipeline(PipelineHandle) {
  throw std::runtime_error("BindPipeline not implemented yet");
}

void VulkanCommandList::BindVertexBuffer(u32, BufferHandle, u64) {
  throw std::runtime_error("BindVertexBuffer not implemented yet");
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

void VulkanCommandList::PushConstants(ShaderStage, u32, u32, const void *) {
  throw std::runtime_error("PushConstants not implemented yet");
}

void VulkanCommandList::Draw(u32, u32) {
  throw std::runtime_error("Draw not implemented yet");
}

void VulkanCommandList::DrawIndexed(u32, u32, i32) {
  throw std::runtime_error("DrawIndexed not implemented yet");
}

} // namespace Velos::RHI
