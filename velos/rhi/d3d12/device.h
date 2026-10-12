#pragma once

#include <rhi/d3d12/common.h>
#include <rhi/d3d12/descriptor_allocator.h>
#include <rhi/device.h>
#include <rhi/d3d12/swapchain.h>
#include <rhi/types.h>
#include "shader/shader_compiler.h"

#include <unordered_map>
#include <vector>

namespace Velos::D3D12 {
	using namespace Velos::RHI;

    class CommandList;
	class UploadContext;

    struct FrameContext {
		ComPtr<ID3D12CommandAllocator> allocator;
        u64 fenceValue = 0;
    };

    struct Shader {

    };

    struct Pipeline {

    };

    struct D3D12Texture {

    };

    struct Image {
        ComPtr<ID3D12Resource> ownedResource;
        ID3D12Resource* resourcePtr = nullptr;

        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        ImageLayout layout = ImageLayout::Undefined;
        Format format = Format::Undefined;
        ImageType type = ImageType::Image2D;
        ImageUsage usage = ImageUsage::None;
        u32 width = 0;
        u32 height = 0;
        u32 depth = 1;
        u32 mipLevels = 1;
        u32 arrayLayers = 1;
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
    };

    struct Buffer {

    };

    struct Sampler {

    };

    struct BindingLayout {

    };

	struct BindingPool {
    
    };

	struct BindingSet {
    
    };

	struct QueryPool {
    
    };

	class Device : public IDevice {
	public:
		explicit Device(const DeviceDesc& desc);
		~Device() override;

		GraphicsAPI GetBackend() const override;

        SwapchainHandle CreateSwapchain(const SwapchainDesc& desc) override;
        void DestroySwapchain(SwapchainHandle handle) override;
        void ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height) override;

        BufferHandle CreateBuffer(const BufferDesc& desc) override;
        void DestroyBuffer(BufferHandle handle) override;
        const Buffer& GetBuffer(BufferHandle handle) const;
        u64 GetBufferDeviceAddress(BufferHandle handle) const override;
        void* MapBuffer(BufferHandle handle) override;
        void UnmapBuffer(BufferHandle handle) override;

        ImageHandle CreateImage(const ImageDesc& desc) override;
        ImageHandle CreateSparseImage(const ImageDesc& desc,
            u64 memoryBudgetBytes) override;
        SparseImageMemoryInfo
            GetSparseImageMemoryInfo(ImageHandle image) const override;
        void BindSparseImagePages(
            ImageHandle image, std::span<const SparseImagePageBind> pages) override;
        void DestroyImage(ImageHandle handle) override;
        Image& GetImage(ImageHandle handle);
        const Image& GetImage(ImageHandle handle) const;
        void SetImageLayout(ImageHandle handle, u32 baseMipLevel, u32 mipLevelCount,
            ImageLayout layout);

        ImageViewHandle CreateImageView(const ImageViewDesc& desc) override;
        void DestroyImageView(ImageViewHandle view) override;
        const ImageView& GetImageView(ImageViewHandle handle) const;

        SamplerHandle CreateSampler(const SamplerDesc& desc) override;
        void DestroySampler(SamplerHandle handle) override;
        const Sampler& GetSampler(SamplerHandle handle) const;

        ShaderHandle CreateShader(const ShaderDesc& desc) override;
        void DestroyShader(ShaderHandle handle) override;
        const Shader& GetShader(ShaderHandle handle) const;

        PipelineHandle
            CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
        PipelineHandle
            CreateComputePipeline(const ComputePipelineDesc& desc) override;
        PipelineHandle CreateMeshPipeline(const MeshPipelineDesc& desc) override;
        void DestroyPipeline(PipelineHandle handle) override;
        const Pipeline& GetPipeline(PipelineHandle handle) const;

        BindingLayoutHandle
            CreateBindingLayout(const BindingLayoutDesc& desc) override;
        void DestroyBindingLayout(BindingLayoutHandle handle) override;
        const BindingLayout&
            GetBindingLayout(BindingLayoutHandle handle) const;

        BindingPoolHandle
            CreateBindingPool(const BindingPoolDesc& desc) override;
        void DestroyBindingPool(BindingPoolHandle handle) override;
        const BindingPool&
            GetBindingPool(BindingPoolHandle handle) const;
        BindingSetHandle
            AllocateBindingSet(const BindingSetAllocationDesc& desc) override;

        void UpdateBindingSet(const BindingWriteDesc& desc) override;
        const BindingSet& GetBindingSet(BindingSetHandle handle) const;

        QueryPoolHandle
            CreateTimestampQueryPool(const QueryPoolDesc& desc) override;
        void DestroyQueryPool(QueryPoolHandle handle) override;
        bool GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
            u32 queryCount, u64* results) override;
        double GetTimestampPeriodNanoseconds() const override;
        u32 GetCurrentFrameIndex() const override { return currentFrame_; }
        QueueInfo GetQueueInfo(QueueType type) const override;
        QueueRelationship GetQueueRelationship(QueueType first,
            QueueType second) const override;
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
        const QueryPool& GetQueryPool(QueryPoolHandle handle) const;

        ImageLayout GetImageLayout(ImageHandle imageHandle, u32 mipLevel) const;

        FrameBeginResult BeginFrame(SwapchainHandle handle) override;
        ICommandList& AcquireCommandList(QueueType type) override;
        void Submit(QueueType type, ICommandList& commandList,
            const SubmitDesc& desc = {}) override;
        ICommandList& GetCommandList(
            QueueType type = QueueType::Graphics) override;
        using IDevice::Submit;
        void Submit(QueueType type, const SubmitDesc& desc = {}) override;
        void SubmitAndPresent(SwapchainHandle swapchain, const SubmitDesc& desc = {}) override;

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

        Extent2D GetSwapchainDimensions() const override;

    private:
        void CreateInstance(const DeviceDesc& desc);
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandObjects();
        void CreateSyncObjects();
        void CreateSwapchainResources();
        void RefreshSwapchainResources();
        void DestroySwapchainResources();
        void WaitForFenceValue(u64 value);
        void ExecuteGraphicsCommandList();

        friend class CommandList;
        friend class UploadContext;

    private:
        u32 currentFrame_ = 0;
        u32 currentBackbufferIndex_ = 0;

        ComPtr<IDXGIFactory6> factory_;
        ComPtr<IDXGIAdapter4> adapter_;
        ComPtr<ID3D12Device> device_;
        ComPtr<ID3D12CommandQueue> graphicsQueue_;
        ComPtr<ID3D12Fence> idleFence_;
        HANDLE idleFenceEvent_ = nullptr;
        u64 idleFenceValue_ = 0;

        std::vector<FrameContext> frames_;
        ComPtr<ID3D12GraphicsCommandList> nativeGraphicsCommandList_;
        std::unique_ptr<CommandList> graphicsCommandList_;
        bool graphicsCommandPrepared_ = false;

        std::unique_ptr<Swapchain> swapchain_;
        SwapchainHandle swapchainHandle_{};
        u32 nextSwapchainHandle_ = 0;
        Format swapchainFormat_ = Format::Undefined;

        std::unordered_map<u32, Image> images_;
        std::unordered_map<u32, ImageView> imageViews_;
        std::vector<ImageHandle> swapchainImageHandles_;
        std::vector<ImageViewHandle> swapchainImageViewHandles_;
        u32 nextImageHandle_ = 0;
        u32 nextImageViewHandle_ = 0;

        static constexpr u32 kCpuDescriptorCapacity = 4096;
        CpuDescriptorAllocator rtvDescriptors_;
        CpuDescriptorAllocator dsvDescriptors_;

	};	
}
