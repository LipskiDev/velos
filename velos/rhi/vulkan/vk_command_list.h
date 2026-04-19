#pragma once

#include "../rhi_command_list.h"
#include "rhi/vulkan/vk_device.h"
#include "vk_common.h"

namespace Velos::RHI {

class VulkanDevice;

class VulkanCommandList final : public ICommandList {
public:
  VulkanCommandList(VulkanDevice &device, VkCommandBuffer commandBuffer);
  explicit VulkanCommandList(VkCommandBuffer commandBuffer);
  ~VulkanCommandList() override = default;

  void Begin() override;
  void End() override;

  void SetViewport(const Viewport &viewport) override;
  void SetScissor(const Rect2D &scissor) override;

  void Barrier(const BufferBarrier &barrier) override;
  void Barrier(const ImageBarrier &barrier) override;

  void UpdateBuffer(const BufferUpdateDesc &update) override;

  void BeginRendering(const RenderingInfo &renderingInfo) override;
  void EndRendering() override;

  void BindPipeline(PipelineHandle pipeline) override;
  void BindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset = 0) override;
  void BindIndexBuffer(BufferHandle buffer, IndexType indexType,
                       u64 offset = 0) override;

  void BindUniformBuffer(u32 binding, BufferHandle buffer, u64 offset,
                         u64 size) override;

  void BindDescriptorSet(PipelineHandle pipeline, u32 setIndex,
                         DescriptorSetHandle descriptorSet) override;

  void PushConstants(ShaderStage stages, u32 offset, u32 size,
                     const void *data) override;

  void CopyBuffer(BufferHandle src, BufferHandle dst,
                  const BufferCopyRegion &region) override;

  void CopyBufferToImage(BufferHandle src, ImageHandle dst,
                         const BufferImageCopyRegion &region) override;

  void PipelineBarrier(std::span<const BufferBarrier> buffers,
                       std::span<const ImageBarrier> images) override;

  void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0,
            u32 baseInstance = 0) override;
  void DrawIndexed(u32 indexCount, u32 firstIndex = 0,
                   i32 vertexOffset = 0) override;

  VkCommandBuffer GetVkCommandBuffer() const { return commandBuffer_; }

private:
  VulkanDevice &device_;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  PipelineHandle boundGraphicsPipeline_{};
};
} // namespace Velos::RHI
