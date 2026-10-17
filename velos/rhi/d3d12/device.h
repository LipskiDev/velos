#pragma once

#include <rhi/d3d12/common.h>
#include <rhi/d3d12/descriptor_allocator.h>
#include <rhi/device.h>
#include <rhi/d3d12/swapchain.h>
#include <rhi/types.h>
#include "shader/shader_compiler.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <string>
#include <array>

namespace Velos::D3D12 {
using namespace Velos::RHI;

class CommandList;
class UploadContext;
struct DescriptorSnapshot;

struct ConstantUploadPage {
  ComPtr<ID3D12Resource> resource;
  std::byte *mapped = nullptr;
  u64 used = 0;
};

struct FrameContext {
  ComPtr<ID3D12CommandAllocator> allocator;
  u64 fenceValue = 0;
  std::vector<ComPtr<ID3D12Resource>> staging;
  std::vector<std::shared_ptr<DescriptorSnapshot>> descriptors;
  std::vector<ComPtr<ID3D12Object>> objects;
  std::vector<ConstantUploadPage> constantPages;
  size_t constantPageIndex = 0;

  // Call only after this context's submitted GPU work has completed.
  void ResetTransientResources() {
    staging.clear();
    descriptors.clear();
    objects.clear();
    constantPageIndex = 0;
    for (auto &page : constantPages)
      page.used = 0;
  }
};

struct AuxiliaryQueue {
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  FrameContext resources;
  ComPtr<ID3D12GraphicsCommandList> nativeCommands;
  std::unique_ptr<CommandList> commands;
  bool prepared = false;
};

struct GraphicsBatch {
  FrameContext resources;
  ComPtr<ID3D12GraphicsCommandList> nativeCommands;
  std::unique_ptr<CommandList> commands;
  bool prepared = false;
};

struct Shader {
  std::vector<std::byte> bytecode;
  ShaderStage stage;
  ShaderReflectionData reflection;
};

struct TableEntry {
  BindingDesc binding;
  D3D12_DESCRIPTOR_RANGE_TYPE type;
  u32 offset = 0;
  u32 stride = 0;
  bool used = false;
  ImageViewType imageType = ImageViewType::View2D;
  u32 requiredCount = 0;
  bool runtimeArray = false;
  ShaderResourceScalarType scalarType = ShaderResourceScalarType::Float;
};
struct PipelineSetLayout {
  std::vector<BindingDesc> bindings;
  std::vector<TableEntry> entries;
  u32 resourceRoot = UINT_MAX, samplerRoot = UINT_MAX;
  u32 resources = 0, samplers = 0;
};
struct Pipeline {
  ComPtr<ID3D12RootSignature> rootSignature;
  ComPtr<ID3D12PipelineState> state;
  std::vector<u32> vertexStrides;
  std::vector<PipelineSetLayout> sets;
  std::array<u32, 5> pushRoots{UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX,
                               UINT_MAX};
  std::array<u32, 5> pushSizes{};
  std::array<bool, 5> pushInline{};
  u32 pushSize = 0;
  ShaderStage pushStages = ShaderStage::None;
  bool compute = false;
  bool mesh = false;
  D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct D3D12Texture {};

struct Image {
  // Declared before the resource so the resource is released first.
  ComPtr<ID3D12Heap> sparseHeap;
  ComPtr<ID3D12Resource> ownedResource;
  ID3D12Resource *resourcePtr = nullptr;

  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  ImageLayout layout = ImageLayout::Undefined;
  // Native state is tracked per mip, array layer and depth/stencil plane.
  std::vector<D3D12_RESOURCE_STATES> subresourceStates;
  std::vector<ImageLayout> subresourceLayouts;
  void InitializeSubresources() {
    if (!subresourceStates.empty())
      return;
    const u32 planes = format == Format::D24_UNORM_S8_UINT ? 2 : 1;
    subresourceStates.assign(mipLevels * arrayLayers * planes, state);
    subresourceLayouts.assign(subresourceStates.size(), layout);
  }

  Format format = Format::Undefined;
  ImageType type = ImageType::Image2D;
  ImageUsage usage = ImageUsage::None;
  u32 width = 0;
  u32 height = 0;
  u32 depth = 1;
  u32 mipLevels = 1;
  u32 arrayLayers = 1;
  D3D12_TILE_SHAPE sparseTileShape{};
  u32 sparsePageCount = 0;
  std::vector<D3D12_SUBRESOURCE_TILING> sparseTilings;
  std::unordered_map<u64, u32> sparseBindings;
};

enum class ImageViewDescriptorType {
  RenderTarget,
  DepthStencil,
};

struct ImageView {
  ImageHandle image{};
  CpuDescriptorAllocation descriptor{};
  ImageAspect aspect = ImageAspect::Color;
  ImageViewDescriptorType descriptorType =
      ImageViewDescriptorType::RenderTarget;
  bool owned = false;
  CpuDescriptorAllocation srv{}, uav{};
  u32 baseMipLevel = 0, mipLevelCount = 1, baseArrayLayer = 0,
      arrayLayerCount = 1;
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
};

struct Buffer {
  ComPtr<ID3D12Resource> ownedResource;
  ComPtr<ID3D12Resource> hostResource;
  D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
  ID3D12Resource *resourcePtr = nullptr;
  u64 size = 0;
  BufferUsage usage = BufferUsage::None;
  MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
  D3D12_INDEX_BUFFER_VIEW indexBufferView{};
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  void *mapped = nullptr;
  u64 allocationSize = 0;
  CpuDescriptorAllocation cbv{}, srv{}, uav{};
};

struct Sampler {
  CpuDescriptorAllocation descriptor{};
  D3D12_SAMPLER_DESC description{};
};

struct BindingLayout {
  std::vector<BindingDesc> bindings;
};

struct BindingPool {
  std::unordered_map<u32, u32> remaining;
  u32 remainingSets = 0;
  BindingPoolFlags flags = BindingPoolFlags::None;
};
struct BindingValue {
  bool initialized = false, hasSrv = false, hasUav = false, hasSampler = false;
  ComPtr<ID3D12Resource> owner;
  u64 offset = 0, range = 0;
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  D3D12_SAMPLER_DESC sampler{};
};
struct BindingSet {
  u64 revision = 0;
  BindingPoolHandle pool;
  std::vector<BindingDesc> bindings;
  std::unordered_map<u32, std::vector<BindingValue>> values;
};
struct DescriptorSnapshot {
  std::shared_ptr<BindingSet> set;
  PipelineSetLayout layout;
  u32 resourceStart = UINT_MAX, samplerStart = UINT_MAX;
  std::vector<BindingValue> keepAlive;
  bool deferred = false;
  std::unordered_map<u32, std::vector<BindingValue>> values;
};

struct QueryPool {};

class Device : public IDevice {
 public:
  explicit Device(const DeviceDesc &desc);
  ~Device() override;

  GraphicsAPI GetBackend() const override;
  ID3D12Device *GetNativeDevice() const noexcept { return device_.Get(); }

  SwapchainHandle CreateSwapchain(const SwapchainDesc &desc) override;
  void DestroySwapchain(SwapchainHandle handle) override;
  void ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height) override;

  BufferHandle CreateBuffer(const BufferDesc &desc) override;
  void DestroyBuffer(BufferHandle handle) override;
  const Buffer &GetBuffer(BufferHandle handle) const;
  u64 GetBufferDeviceAddress(BufferHandle handle) const override;
  void *MapBuffer(BufferHandle handle) override;
  void UnmapBuffer(BufferHandle handle) override;

  ImageHandle CreateImage(const ImageDesc &desc) override;
  ImageHandle CreateSparseImage(const ImageDesc &desc,
                                u64 memoryBudgetBytes) override;
  SparseImageMemoryInfo GetSparseImageMemoryInfo(
      ImageHandle image) const override;
  void BindSparseImagePages(
      ImageHandle image, std::span<const SparseImagePageBind> pages) override;
  void DestroyImage(ImageHandle handle) override;
  Image &GetImage(ImageHandle handle);
  const Image &GetImage(ImageHandle handle) const;
  void SetImageLayout(ImageHandle handle, u32 baseMipLevel, u32 mipLevelCount,
                      ImageLayout layout);

  ImageViewHandle CreateImageView(const ImageViewDesc &desc) override;
  void DestroyImageView(ImageViewHandle view) override;
  const ImageView &GetImageView(ImageViewHandle handle) const;

  SamplerHandle CreateSampler(const SamplerDesc &desc) override;
  void DestroySampler(SamplerHandle handle) override;
  const Sampler &GetSampler(SamplerHandle handle) const;

  ShaderHandle CreateShader(const ShaderDesc &desc) override;
  void DestroyShader(ShaderHandle handle) override;
  const Shader &GetShader(ShaderHandle handle) const;

  PipelineHandle CreateGraphicsPipeline(
      const GraphicsPipelineDesc &desc) override;
  PipelineHandle CreateComputePipeline(
      const ComputePipelineDesc &desc) override;
  PipelineHandle CreateMeshPipeline(const MeshPipelineDesc &desc) override;
  void DestroyPipeline(PipelineHandle handle) override;
  const Pipeline &GetPipeline(PipelineHandle handle) const;

  BindingLayoutHandle CreateBindingLayout(
      const BindingLayoutDesc &desc) override;
  void DestroyBindingLayout(BindingLayoutHandle handle) override;
  const BindingLayout &GetBindingLayout(BindingLayoutHandle handle) const;

  BindingPoolHandle CreateBindingPool(const BindingPoolDesc &desc) override;
  void DestroyBindingPool(BindingPoolHandle handle) override;
  const BindingPool &GetBindingPool(BindingPoolHandle handle) const;
  BindingSetHandle AllocateBindingSet(
      const BindingSetAllocationDesc &desc) override;

  void UpdateBindingSet(const BindingWriteDesc &desc) override;
  const BindingSet &GetBindingSet(BindingSetHandle handle) const;

  QueryPoolHandle CreateTimestampQueryPool(const QueryPoolDesc &desc) override;
  void DestroyQueryPool(QueryPoolHandle handle) override;
  bool GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
                                u32 queryCount, u64 *results) override;
  double GetTimestampPeriodNanoseconds() const override;
  u32 GetCurrentFrameIndex() const override { return currentFrame_; }
  QueueInfo GetQueueInfo(QueueType type) const override;
  QueueRelationship GetQueueRelationship(QueueType first,
                                         QueueType second) const override;
  FenceHandle CreateFence(bool signaled = false) override;
  void DestroyFence(FenceHandle handle) override;
  bool IsFenceSignaled(FenceHandle handle) const override;
  void WaitFence(FenceHandle handle,
                 u64 timeoutNanoseconds = UINT64_MAX) override;
  void ResetFence(FenceHandle handle) override;
  SemaphoreHandle CreateSemaphore(SemaphoreType type = SemaphoreType::Binary,
                                  u64 initialValue = 0) override;
  void DestroySemaphore(SemaphoreHandle handle) override;
  void SignalSemaphore(SemaphoreHandle handle, u64 value) override;
  u64 GetSemaphoreValue(SemaphoreHandle handle) const override;
  void WaitSemaphore(SemaphoreHandle handle, u64 value,
                     u64 timeoutNanoseconds = UINT64_MAX) override;
  const QueryPool &GetQueryPool(QueryPoolHandle handle) const;

  ImageLayout GetImageLayout(ImageHandle imageHandle, u32 mipLevel) const;

  FrameBeginResult BeginFrame(SwapchainHandle handle) override;
  ICommandList &AcquireCommandList(QueueType type) override;
  void Submit(QueueType type, ICommandList &commandList,
              const SubmitDesc &desc = {}) override;
  ICommandList &GetCommandList(QueueType type = QueueType::Graphics) override;
  using IDevice::Submit;
  void Submit(QueueType type, const SubmitDesc &desc = {}) override;
  void SubmitAndPresent(SwapchainHandle swapchain,
                        const SubmitDesc &desc = {}) override;

  void ClearCurrentSwapchainImage(float r, float g, float b, float a);

  void WaitIdle() override;
  void CollectGarbage() override;

  std::unique_ptr<IUploadContext> CreateUploadContext(
      u64 stagingBufferSize = 1024 * 16 * 1024) override;

  void AcquireUploadedBuffers(
      std::span<const PendingBufferAcquire> pendingAcquires) override;
  void AcquireUploadedImages(
      std::span<const PendingImageAcquire> pendingAcquires) override;

  void DumpLiveResources() const;

  GeneratedPipelineLayout BuildPipelineLayout(
      const PipelineReflectionData &reflection,
      const PipelineLayoutOverrides &overrides = {}) override;

  Extent2D GetSwapchainDimensions() const override;

 private:
  void CreateInstance(const DeviceDesc &desc);
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateCommandObjects();
  ID3D12CommandSignature *GetMeshDispatchSignature(u32 stride);
  void CreateSyncObjects();
  void CreateSwapchainResources();
  void RefreshSwapchainResources();
  void DestroySwapchainResources();
  void WaitForFenceValue(u64 value);
  void ExecuteGraphicsCommandList();
  void ValidateMainGraphicsSubmission() const;
  void ExecuteGraphicsCommands(CommandList &commands, FrameContext &resources);
  ComPtr<ID3D12Resource> CreateStagingBuffer(u64 size, const void *data);
  ComPtr<ID3D12Resource> CreateHostBuffer(u64 size, bool upload);
  void CopyHostMirror(Buffer &buffer, ID3D12Resource *host, bool upload);
  void SynchronizeMappedMirrors(bool upload);
  ComPtr<ID3DBlob> BuildRootSignature(Pipeline &pipeline,
                                      const PipelineLayoutDesc &layout,
                                      std::span<const Shader *const> shaders);
  std::shared_ptr<DescriptorSnapshot> SnapshotBindings(
      const PipelineSetLayout &layout, BindingSetHandle set);
  void PopulateSnapshot(DescriptorSnapshot &snapshot);
  void ExecuteImmediate(
      const std::function<void(ID3D12GraphicsCommandList *)> &record);
  void CheckFormat(
      Format format, D3D12_FORMAT_SUPPORT1 first,
      D3D12_FORMAT_SUPPORT2 second = D3D12_FORMAT_SUPPORT2_NONE) const;
  ImageHandle CreateImageResource(const ImageDesc &desc, bool sparse,
                                  u64 memoryBudgetBytes = 0);
  AuxiliaryQueue &GetAuxiliaryQueue(QueueType type);
  void CreateAuxiliaryQueue(AuxiliaryQueue &queue,
                            D3D12_COMMAND_LIST_TYPE type);
  void ValidateTimelineSubmission(const SubmitDesc &desc);
  void QueueTimelineWaits(ID3D12CommandQueue &queue, const SubmitDesc &desc);
  void QueueTimelineSignals(ID3D12CommandQueue &queue, const SubmitDesc &desc);

  friend class CommandList;
  friend class UploadContext;

 public:
  ComPtr<ID3D12CommandQueue> GetGraphicsQueue() const noexcept {
    return graphicsQueue_;
  }
  ComPtr<ID3D12CommandQueue> GetComputeQueue() const noexcept {
    return computeQueue_;
  }
  ComPtr<ID3D12CommandQueue> GetTransferQueue() const noexcept {
    return transferQueue_;
  }

 private:
  u32 currentFrame_ = 0;
  u32 currentBackbufferIndex_ = 0;

  ComPtr<IDXGIFactory6> factory_;
  ComPtr<IDXGIAdapter4> adapter_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> graphicsQueue_;
  AuxiliaryQueue computeQueueState_;
  AuxiliaryQueue transferQueueState_;
  ComPtr<ID3D12CommandQueue> computeQueue_;
  ComPtr<ID3D12CommandQueue> transferQueue_;
  ComPtr<ID3D12CommandQueue> presentQueue_;

  ComPtr<ID3D12Fence> idleFence_;
  HANDLE idleFenceEvent_ = nullptr;
  u64 idleFenceValue_ = 0;

  std::vector<FrameContext> frames_;
  std::vector<std::vector<std::unique_ptr<GraphicsBatch>>> graphicsBatches_;
  std::vector<size_t> graphicsBatchCursors_;
  ComPtr<ID3D12GraphicsCommandList> nativeGraphicsCommandList_;
  std::unique_ptr<CommandList> graphicsCommandList_;
  bool graphicsCommandPrepared_ = false;

  std::unique_ptr<Swapchain> swapchain_;
  SwapchainHandle swapchainHandle_{};
  u32 nextSwapchainHandle_ = 0;
  Format swapchainFormat_ = Format::Undefined;

  std::unordered_map<u32, Image> images_;
  std::unordered_map<u32, Shader> shaders_;
  std::unordered_map<u32, Pipeline> pipelines_;
  std::unordered_map<u32, ComPtr<ID3D12CommandSignature>>
      meshDispatchSignatures_;
  std::unordered_map<u32, Buffer> buffers_;
  std::unordered_set<u32> mappedMirrors_;
  u32 nextShaderHandle_ = 0;
  u32 nextPipelineHandle_ = 0;
  std::unordered_map<u32, ImageView> imageViews_;
  std::vector<ImageHandle> swapchainImageHandles_;
  std::vector<ImageViewHandle> swapchainImageViewHandles_;
  u32 nextImageHandle_ = 0;
  u32 nextImageViewHandle_ = 0;
  u32 nextBufferHandle_ = 0;
  std::unordered_map<u32, Sampler> samplers_;
  std::unordered_map<u32, BindingLayout> bindingLayouts_;
  std::unordered_map<u32, BindingPool> bindingPools_;
  std::unordered_map<u32, std::shared_ptr<BindingSet>> bindingSets_;
  u32 nextBindingPoolHandle_ = 0, nextBindingSetHandle_ = 0;
  u32 nextSamplerHandle_ = 0, nextBindingLayoutHandle_ = 0;
  struct SemaphoreResource {
    ComPtr<ID3D12Fence> fence;
    SemaphoreType type = SemaphoreType::Binary;
  };
  std::unordered_map<u32, SemaphoreResource> semaphores_;
  u32 nextSemaphoreHandle_ = 0;
  std::string pipelineCachePath_;

  static constexpr u32 kCpuDescriptorCapacity = 4096;
  CpuDescriptorAllocator rtvDescriptors_;
  CpuDescriptorAllocator dsvDescriptors_;
  CpuDescriptorAllocator resourceDescriptors_, samplerDescriptors_;
  ShaderDescriptorArena shaderResources_, shaderSamplers_;

  // Extensions
  D3D12_TILED_RESOURCES_TIER tiledResourcesTier_ =
      D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
};
}  // namespace Velos::D3D12
