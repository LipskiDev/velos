#pragma once

#include "../rhi_command_list.h"
#include "vk_common.h"

namespace Velos::RHI {
class VulkanCommandList final : public ICommandList {
public:
  explicit VulkanCommandList(VkCommandBuffer commandBuffer);
  ~VulkanCommandList() override = default;

  void Begin() override;
  void End() override;

  void SetViewport(const Viewport &viewport) override;
  void SetScissor(const Rect2D &scissor) override;

  void Barrier(const BufferBarrier &barrier) override;
  void Barrier(const TextureBarrier &barrier) override;

  void UpdateBuffer(const BufferUpdateDesc &update) override;
  void UploadTexture(const TextureUploadDesc &upload) override;

  void BeginRendering(const RenderingInfo &renderingInfo) override;
  void EndRendering() override;

  void BindPipeline(PipelineHandle pipeline) override;
  void BindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset = 0) override;
  void BindIndexBuffer(BufferHandle buffer, IndexType indexType,
                       u64 offset = 0) override;

  void BindUniformBuffer(u32 binding, BufferHandle buffer, u64 offset,
                         u64 size) override;
  void BindSampledTexture(u32 binding, TextureHandle texture,
                          SamplerHandle sampler) override;

  void PushConstants(ShaderStage stages, u32 offset, u32 size,
                     const void *data) override;

  void Draw(u32 vertexCount, u32 firstVertex = 0) override;
  void DrawIndexed(u32 indexCount, u32 firstIndex = 0,
                   i32 vertexOffset = 0) override;

  VkCommandBuffer GetVkCommandBuffer() const { return commandBuffer_; }

private:
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
};
} // namespace Velos::RHI
