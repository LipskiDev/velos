#pragma once

#include <rhi/vulkan/profiler.h>

#include <array>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>

#include "rhi/device.h"
#include "rhi/types.h"
#include "rhi/vulkan/profiler.h"
#include "rhi/vulkan/swapchain.h"
#include "shader/shader_compiler.h"

namespace Velos::Vulkan {
using namespace Velos::RHI;
class CommandList;
class UploadContext;

struct Shader {
  VkShaderModule module = VK_NULL_HANDLE;
  ShaderStage stage = ShaderStage::None;
  ShaderReflectionData reflection;
  std::string entryPoint = "main";
};

struct Pipeline {
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct VulkanTexture {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  Format format = Format::Undefined;
  bool owned = false;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

  Format format = Format::Undefined;
  ImageType type = ImageType::Image2D;
  ImageUsage usage = ImageUsage::None;
  std::vector<ImageLayout> mipLayouts;

  u32 width = 0;
  u32 height = 0;
  u32 depth = 1;
  u32 mipLevels = 1;
  u32 arrayLayers = 1;

  bool owned = true;
};

struct ImageView {
  VkImageView view = VK_NULL_HANDLE;
  ImageHandle image;
  Format format = Format::Undefined;
  ImageAspect aspect = ImageAspect::Color;

  bool owned = true;
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VmaAllocationInfo allocationInfo{};

  u64 size = 0;
  u64 deviceAddress = 0;

  BufferUsage usage = BufferUsage::None;
  MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
};

struct Sampler {
  VkSampler sampler = VK_NULL_HANDLE;
};

struct BindingLayout {
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  bool usesUpdateAfterBind = false;
  struct BindingMetadata {
    BindingType type = BindingType::UniformBuffer;
    u32 maximumCount = 0;
    ShaderStage visibility = ShaderStage::None;
    BindingFlags flags = BindingFlags::None;
  };
  std::unordered_map<u32, BindingMetadata> bindings;
  bool hasVariableCountBinding = false;
  u32 variableCountBinding = 0;
  u32 variableCountMaximum = 0;
};

struct BindingPool {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  bool supportsUpdateAfterBind = false;
};

struct BindingSet {
  VkDescriptorSet set = VK_NULL_HANDLE;
  BindingLayoutHandle layout;
  BindingPoolHandle pool;
  u32 variableBindingCount = 0;
};

struct QueryPool {
  VkQueryPool pool = VK_NULL_HANDLE;
  u32 queryCount = 0;
};

class Device final : public IDevice {
public:
  explicit Device(const DeviceDesc &desc);
  ~Device() override;

  void CreateAllocator();

  GraphicsAPI GetBackend() const override;

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
  void DestroyImage(ImageHandle handle) override;
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

  PipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  PipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &desc) override;
  void DestroyPipeline(PipelineHandle handle) override;
  const Pipeline &GetPipeline(PipelineHandle handle) const;

  BindingLayoutHandle
  CreateBindingLayout(const BindingLayoutDesc &desc) override;
  void DestroyBindingLayout(BindingLayoutHandle handle) override;
  const BindingLayout &
  GetBindingLayout(BindingLayoutHandle handle) const;

  BindingPoolHandle
  CreateBindingPool(const BindingPoolDesc &desc) override;
  void DestroyBindingPool(BindingPoolHandle handle) override;
  const BindingPool &
  GetBindingPool(BindingPoolHandle handle) const;
  BindingSetHandle
  AllocateBindingSet(const BindingSetAllocationDesc& desc) override;

  void UpdateBindingSet(const BindingWriteDesc &desc) override;
  const BindingSet &GetBindingSet(BindingSetHandle handle) const;

  QueryPoolHandle
  CreateTimestampQueryPool(const QueryPoolDesc &desc) override;
  void DestroyQueryPool(QueryPoolHandle handle) override;
  bool GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
                                u32 queryCount, u64 *results) override;
  double GetTimestampPeriodNanoseconds() const override;
  u32 GetCurrentFrameIndex() const override { return currentFrame_; }
  QueueInfo GetQueueInfo(QueueType type) const override;
  FenceHandle CreateFence(bool signaled = false) override;
  void DestroyFence(FenceHandle handle) override;
  bool IsFenceSignaled(FenceHandle handle) const override;
  void WaitFence(FenceHandle handle, u64 timeoutNanoseconds = UINT64_MAX) override;
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
  ICommandList &GetCommandList(
      QueueType type = QueueType::Graphics) override;
  using IDevice::Submit;
  void Submit(QueueType type, const SubmitDesc &desc = {}) override;
  void SubmitAndPresent(SwapchainHandle swapchain, const SubmitDesc &desc = {}) override;

  void ClearCurrentSwapchainImage(float r, float g, float b, float a);

  void WaitIdle() override;
  void CollectGarbage() override;

  std::unique_ptr<IUploadContext>
  CreateUploadContext(u64 stagingBufferSize = 1024 * 16 * 1024) override;

  void AcquireUploadedBuffers(std::span<const PendingBufferAcquire> pendingAcquires) override;
  void AcquireUploadedImages(std::span<const PendingImageAcquire> pendingAcquires) override;

  void DumpLiveResources() const;

  GeneratedPipelineLayout BuildPipelineLayout(
      const PipelineReflectionData& reflection,
      const PipelineLayoutOverrides& overrides = {}
  ) override;

private:
  u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const;
  void SubmitWithTimeline(VkQueue queue, VkCommandBuffer cmd, VkFence fence,
                          const SubmitDesc &desc,
                          VkSemaphore binaryWait = VK_NULL_HANDLE,
                          VkSemaphore binarySignal = VK_NULL_HANDLE);
  VkQueue GetVkQueue(QueueType type) const;
  VkFence GetFrameFence(QueueType type, u32 frameIndex) const;
  VkCommandBuffer GetFrameCommandBuffer(QueueType type, u32 frameIndex) const;
  std::unique_ptr<CommandList>& GetFrameCommandList(QueueType type,
                                                    u32 frameIndex);
  bool& GetCommandPrepared(QueueType type, u32 frameIndex);
  VkCommandBuffer AllocateTransferCommandBuffer();
  void FreeTransferCommandBuffer(VkCommandBuffer commandBuffer);
  std::vector<u32> GetResourceQueueFamilies() const;

  friend class UploadContext;

private:
  void CreateInstance(const DeviceDesc &desc);
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateCommandObjects();
  void CreateSyncObjects();
  void CreateSwapchainSyncObjects();

  void DestroySwapchainSyncObjects();

public:
  VkInstance GetVkInstance() const { return instance_; }
  VkPhysicalDevice GetVkPhysicalDevice() const { return physicalDevice_; }
  VkDevice GetVkDevice() const { return device_; }
  VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
  u32 GetGraphicsQueueFamily() const { return mainQueueFamily; }
  VkQueue GetTransferQueue() const { return transferQueue_; }
  u32 GetTransferQueueFamily() const { return transferQueueFamily_; }
  const VkPhysicalDeviceProperties &GetPhysicalDeviceProperties() const {
    return physicalDeviceProperties_;
  }
  VkCommandBuffer GetCommandBuffer() const {
    return commandBuffers_[currentFrame_];
  }

  Extent2D GetSwapchainDimensions() const override;

  VmaAllocator GetAllocator() { return allocator_; }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VmaAllocator allocator_ = VK_NULL_HANDLE;

#if VL_PROFILING
  TracyVkCtx tracyContext_ = nullptr;
#endif

  VkPhysicalDeviceProperties physicalDeviceProperties_{};

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  u32 mainQueueFamily = 0;

  VkQueue computeQueue_ = VK_NULL_HANDLE;
  u32 computeQueueFamily_ = 0;
  u32 computeQueueIndex_ = 0;

  VkQueue presentQueue_ = VK_NULL_HANDLE;
  u32 presentQueueFamily_ = 0;

  VkQueue transferQueue_ = VK_NULL_HANDLE;
  u32 transferQueueFamily_ = 0;

  VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
  std::filesystem::path pipelineCachePath_;

  static constexpr u32 k_MaxFramesInFlight = 2;

  struct CommandPools;
  std::unique_ptr<CommandPools> commandPools_;

  std::array<VkCommandBuffer, k_MaxFramesInFlight> commandBuffers_;
  std::array<std::unique_ptr<CommandList>, k_MaxFramesInFlight>
      commandLists_;
  std::array<VkCommandBuffer, k_MaxFramesInFlight> computeCommandBuffers_{};
  std::array<std::unique_ptr<CommandList>, k_MaxFramesInFlight>
      computeCommandLists_;
  std::array<VkCommandBuffer, k_MaxFramesInFlight> transferCommandBuffers_{};
  std::array<std::unique_ptr<CommandList>, k_MaxFramesInFlight>
      transferCommandLists_;
  std::array<VkFence, k_MaxFramesInFlight> computeInFlightFences_{};
  std::array<VkFence, k_MaxFramesInFlight> transferInFlightFences_{};
  std::array<bool, k_MaxFramesInFlight> graphicsCommandPrepared_{};
  std::array<bool, k_MaxFramesInFlight> computeCommandPrepared_{};
  std::array<bool, k_MaxFramesInFlight> transferCommandPrepared_{};

  struct PooledCommandList {
    QueueType queue = QueueType::Graphics;
    u32 frameIndex = 0;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    std::unique_ptr<CommandList> commandList;
    VkFence fence = VK_NULL_HANDLE;
    u64 lastAcquireEpoch = 0;
    bool acquired = false;
    bool inFlight = false;
  };
  std::vector<PooledCommandList> pooledCommandLists_;
  u64 commandListEpoch_ = 1;
  bool imageAvailablePending_ = false;

  std::unique_ptr<Swapchain> swapchain_;
  std::vector<ImageHandle> swapchainImageHandles_;
  std::vector<ImageViewHandle> swapchainImageViewHandles_;

  u32 currentFrame_ = 0;
  u32 currentBackbufferIndex_ = 0;

  struct FrameSyncData {
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
  };

  std::array<FrameSyncData, k_MaxFramesInFlight> frames_;
  std::vector<VkSemaphore> swapchainRenderFinishedSemaphores_;

private:
  u32 nextShaderHandle_ = 1;
  std::unordered_map<u32, Shader> shaders_;

  u32 nextPipelineHandle_ = 1;
  std::unordered_map<u32, Pipeline> pipelines_;

  u32 nextBufferHandle_ = 1;
  std::unordered_map<u32, Buffer> buffers_;

  u32 nextImageHandle_ = 1;
  std::unordered_map<u32, Image> images_;

  u32 nextImageViewHandle_ = 1;
  std::unordered_map<u32, ImageView> imageViews_;

  u32 nextSamplerHandle_ = 1;
  std::unordered_map<u32, Sampler> samplers_;

  u32 nextBindingLayoutHandle_ = 1;
  std::unordered_map<u32, BindingLayout> descriptorSetLayouts_;

  u32 nextBindingPoolHandle_ = 1;
  std::unordered_map<u32, BindingPool> descriptorPools_;

  u32 nextBindingSetHandle_ = 1;
  std::unordered_map<u32, BindingSet> descriptorSets_;

  u32 nextQueryPoolHandle_ = 1;
  std::unordered_map<u32, QueryPool> queryPools_;
  u32 nextFenceHandle_ = 1;
  std::unordered_map<u32, VkFence> fences_;
  u32 nextSemaphoreHandle_ = 1;
  struct SemaphoreResource {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    SemaphoreType type = SemaphoreType::Binary;
  };
  std::unordered_map<u32, SemaphoreResource> semaphores_;
};
} // namespace Velos::Vulkan
