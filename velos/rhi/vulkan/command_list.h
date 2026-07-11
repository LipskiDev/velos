#pragma once

#include "../command_list.h"
#include <rhi/vulkan/common.h>

namespace Velos::Vulkan {
using namespace Velos::RHI;

class Device;

class CommandList final : public ICommandList {
public:
  CommandList(Device &device, VkCommandBuffer commandBuffer);
  explicit CommandList(VkCommandBuffer commandBuffer);
  ~CommandList() override = default;

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
  void BindComputePipeline(PipelineHandle pipeline) override;
  void BindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset = 0) override;
  void BindIndexBuffer(BufferHandle buffer, IndexType indexType,
                       u64 offset = 0) override;

  void BindUniformBuffer(u32 binding, BufferHandle buffer, u64 offset,
                         u64 size) override;

  void SetBindings(PipelineHandle pipeline, u32 setIndex,
                         BindingSetHandle descriptorSet) override;
  void SetComputeBindings(PipelineHandle pipeline, u32 setIndex,
                                BindingSetHandle descriptorSet) override;

  void GenerateMipmaps(ImageHandle imageHandle, uint32_t width, uint32_t height,
                       uint32_t mipLevels, uint32_t arrayLayers) override;

  void BlitMip(ImageHandle image, uint32_t width, uint32_t height,
               uint32_t srcMip, uint32_t dstMip, uint32_t arrayLayers) override;

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

  void Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                uint32_t groupCountZ) override;

  VkCommandBuffer GetVkCommandBuffer() const { return commandBuffer_; }

private:
  Device &device_;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  PipelineHandle boundGraphicsPipeline_{};
  PipelineHandle boundComputePipeline_{};
};
} // namespace Velos::Vulkan
