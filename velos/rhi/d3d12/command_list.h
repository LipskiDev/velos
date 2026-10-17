#pragma once

#include "../command_list.h"
#include <rhi/d3d12/common.h>
#include <rhi/device.h>

#include <array>
#include <unordered_set>
#include <map>

namespace Velos::D3D12 {
using namespace Velos::RHI;

class Device;
struct FrameContext;
struct BindingSet;
struct DescriptorSnapshot;

class CommandList final : public ICommandList {
 public:
  CommandList(Device *device, ID3D12GraphicsCommandList *commandList,
              ID3D12CommandAllocator *commandAllocator,
              FrameContext *resources = nullptr);

  void SetAllocator(ID3D12CommandAllocator *commandAllocator);
  ID3D12GraphicsCommandList *GetNative() const noexcept { return commandList_; }
  bool IsRecording() const noexcept { return isRecording_; }

  void Begin() override;
  PipelineHandle boundPipeline_{};
  void End() override;

  void SetViewport(const Viewport &viewport) override;
  void SetScissor(const Rect2D &scissor) override;

  void Barrier(const BufferBarrier &barrier) override;
  void Barrier(const ImageBarrier &barrier) override;

  void UpdateBuffer(const BufferUpdateDesc &update) override;
  void ResetQueryPool(QueryPoolHandle pool, u32 firstQuery,
                      u32 queryCount) override;
  void WriteTimestamp(QueryPoolHandle pool, u32 queryIndex,
                      ShaderStage completedStage) override;

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
  void CopyImageToBuffer(ImageHandle src, BufferHandle dst,
                         const BufferImageCopyRegion &region) override;

  void PipelineBarrier(std::span<const BufferBarrier> buffers,
                       std::span<const ImageBarrier> images) override;

  void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0,
            u32 baseInstance = 0) override;
  void DrawIndexed(u32 indexCount, u32 firstIndex = 0, i32 vertexOffset = 0,
                   u32 baseInstance = 0) override;
  void DrawIndexedInstanced(u32 indexCount, u32 instanceCount,
                            u32 firstIndex = 0, i32 vertexOffset = 0,
                            u32 baseInstance = 0) override;
  void DrawIndexedIndirect(BufferHandle buffer, u64 offset, u32 drawCount,
                           u32 stride) override;
  void DrawIndexedIndirectCount(BufferHandle buffer, u64 offset,
                                BufferHandle countBuffer, u64 countOffset,
                                u32 maxDrawCount, u32 stride) override;
  void DrawMeshTasks(u32 groupCountX, u32 groupCountY = 1,
                     u32 groupCountZ = 1) override;
  void DrawMeshTasksIndirect(BufferHandle buffer, u64 offset, u32 drawCount = 1,
                             u32 stride = sizeof(u32) * 3) override;

  void Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                uint32_t groupCountZ) override;
  void DispatchIndirect(BufferHandle buffer, u64 offset) override;

 private:
  friend class Device;
  void ApplySubmissionDecay();
  void ReleaseImagesToCommon();
  void ValidateMeshDraw() const;
  void ValidateQueueState(D3D12_RESOURCE_STATES state) const;
  std::unordered_set<u32> touchedBuffers_;
  std::unordered_set<u32> touchedImages_;
  FrameContext &RetainedResources();
  FrameContext *resources_ = nullptr;
  Device *device_;
  ID3D12GraphicsCommandList *commandList_ = nullptr;
  ComPtr<ID3D12GraphicsCommandList6> meshCommandList_;
  ID3D12CommandAllocator *commandAllocator_ = nullptr;

  bool isRecording_ = false;
  bool isRendering_ = false;
  std::array<BufferBinding, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT>
      vertexBindings_{};
  D3D12_INDEX_BUFFER_VIEW indexBinding_{};
  void ApplyVertexBinding(u32 slot);
  PipelineHandle boundComputePipeline_{};
  bool heapsBound_ = false;
  // Per-recording cache: deferred UpdateAfterBind snapshots stay
  // submission-local.
  std::map<std::pair<u32, u64>,
           std::vector<std::shared_ptr<DescriptorSnapshot>>>
      descriptorCache_;
  bool computeActive_ = false;
  std::array<std::vector<std::byte>, 5> pushData_;
  std::vector<std::shared_ptr<BindingSet>> graphicsSets_, computeSets_;
  struct BoundSet {
    BindingSetHandle handle{};
    u64 revision = 0;
  };
  std::vector<BoundSet> boundGraphicsSets_, boundComputeSets_;
  void BindSet(PipelineHandle pipeline, u32 setIndex, BindingSetHandle set,
               bool compute);
  std::array<ID3D12Resource *, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1>
      storeDiscardResources_{};
  u32 storeDiscardResourceCount_ = 0;
};

}  // namespace Velos::D3D12
