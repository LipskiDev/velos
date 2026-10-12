#include "device.h"
#include "command_list.h"

#include <stdexcept>

namespace Velos::D3D12 {

Device::Device(const DeviceDesc& desc) {
    CreateInstance(desc);
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSyncObjects();
}

Device::~Device() {
    if (graphicsQueue_ && idleFence_) {
        try {
            WaitIdle();
        } catch (...) {
        }
    }
    graphicsCommandList_.reset();
    nativeGraphicsCommandList_.Reset();
    frames_.clear();
    DestroySwapchainResources();
    swapchain_.reset();
    if (idleFenceEvent_) {
        CloseHandle(idleFenceEvent_);
        idleFenceEvent_ = nullptr;
    }
}

GraphicsAPI Device::GetBackend() const {
    return GraphicsAPI::D3D12;
}

SwapchainHandle Device::CreateSwapchain(const SwapchainDesc& desc) {
    if (swapchain_) {
        throw std::logic_error("The D3D12 backend currently supports one swapchain");
    }

    swapchain_ = std::make_unique<Swapchain>(
        *factory_.Get(), *device_.Get(), *graphicsQueue_.Get(), desc);
    swapchainFormat_ = desc.format;
    swapchainHandle_ = SwapchainHandle{nextSwapchainHandle_++};
    currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
    CreateSwapchainResources();
    CreateCommandObjects();
    return swapchainHandle_;
}

void Device::DestroySwapchain(SwapchainHandle handle) {
    if (!swapchain_ || handle.id != swapchainHandle_.id) {
        throw std::invalid_argument("Invalid D3D12 swapchain handle");
    }
    WaitIdle();
    graphicsCommandList_.reset();
    nativeGraphicsCommandList_.Reset();
    frames_.clear();
    DestroySwapchainResources();
    swapchain_.reset(); 
    swapchainHandle_ = {};
    swapchainFormat_ = Format::Undefined;
    graphicsCommandPrepared_ = false;
}

void Device::ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height) {
    if (!swapchain_ || handle.id != swapchainHandle_.id) {
        throw std::invalid_argument("Invalid D3D12 swapchain handle");
    }
    if (width == 0 || height == 0) {
        return;
    }

    WaitIdle();
    swapchain_->Resize(width, height);
    RefreshSwapchainResources();
    currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
}

BufferHandle Device::CreateBuffer(const BufferDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyBuffer(BufferHandle handle) {
    static_cast<void>(handle);
}

u64 Device::GetBufferDeviceAddress(BufferHandle handle) const {
    static_cast<void>(handle);
    return 0;
}

void* Device::MapBuffer(BufferHandle handle) {
    static_cast<void>(handle);
    return nullptr;
}

void Device::UnmapBuffer(BufferHandle handle) {
    static_cast<void>(handle);
}

ImageHandle Device::CreateImage(const ImageDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0) {
        throw std::invalid_argument("CreateImage: dimensions must be non-zero");
    }
    if (desc.mipLevels == 0 || desc.arrayLayers == 0) {
        throw std::invalid_argument("CreateImage: mip and array counts must be non-zero");
    }
    if (desc.format == Format::Undefined) {
        throw std::invalid_argument("CreateImage: format must not be Undefined");
    }
    if (desc.usage == ImageUsage::None) {
        throw std::invalid_argument("CreateImage: usage must not be None");
    }
    if (desc.depth != 1) {
        throw std::invalid_argument("CreateImage: the RHI currently exposes only 2D images");
    }
    if (desc.type == ImageType::Cube &&
        (desc.width != desc.height || desc.arrayLayers % 6 != 0)) {
        throw std::invalid_argument(
            "CreateImage: cube images require square faces and a multiple of six layers");
    }
    if (HasFlag(desc.usage, ImageUsage::ColorAttachment) &&
        HasFlag(desc.usage, ImageUsage::DepthStencil)) {
        throw std::invalid_argument(
            "CreateImage: an image cannot be both a color and depth attachment");
    }
    
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arrayLayers);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = ToDxgiFormat(desc.format);
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

	if (HasFlag(desc.usage, ImageUsage::ColorAttachment)) {
		flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}

	if (HasFlag(desc.usage, ImageUsage::DepthStencil)) {
		flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	}

	if (HasFlag(desc.usage, ImageUsage::Storage)) {
		flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

    resourceDesc.Flags = flags;

    ComPtr<ID3D12Resource> resource;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resourceDesc.Format;
    const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr;
    if (HasFlag(desc.usage, ImageUsage::ColorAttachment)) {
        clearValue.Color[3] = 1.0f;
        optimizedClearValue = &clearValue;
    } else if (HasFlag(desc.usage, ImageUsage::DepthStencil)) {
        clearValue.DepthStencil.Depth = 1.0f;
        optimizedClearValue = &clearValue;
    }

    ThrowIfFailed(device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		optimizedClearValue,
		IID_PPV_ARGS(&resource)),
		"ID3D12Device::CreateCommittedResource"
	);

	const ImageHandle handle{ nextImageHandle_++ };

    Image image{};
	image.ownedResource = resource;
	image.resourcePtr = resource.Get();
	image.state = D3D12_RESOURCE_STATE_COMMON;
	image.layout = ImageLayout::Undefined;
	image.format = desc.format;
	image.type = desc.type;
	image.usage = desc.usage;
	image.width = desc.width;
	image.height = desc.height;
	image.depth = desc.depth;
	image.mipLevels = desc.mipLevels;
	image.arrayLayers = desc.arrayLayers;

	images_.emplace(handle.id, std::move(image));
	return handle;
}

ImageHandle Device::CreateSparseImage(const ImageDesc& desc,
                                      u64 memoryBudgetBytes) {
    static_cast<void>(desc);
    static_cast<void>(memoryBudgetBytes);
    return {};
}

SparseImageMemoryInfo Device::GetSparseImageMemoryInfo(ImageHandle image) const {
    static_cast<void>(image);
    return {};
}

void Device::BindSparseImagePages(
    ImageHandle image, std::span<const SparseImagePageBind> pages) {
    static_cast<void>(image);
    static_cast<void>(pages);
}

void Device::DestroyImage(ImageHandle handle) {
    const auto found = images_.find(handle.id);
    if (found == images_.end()) {
        throw std::invalid_argument("Invalid D3D12 image handle");
    }
    for (const auto& [id, view] : imageViews_) {
        static_cast<void>(id);
        if (view.image.id == handle.id) {
            throw std::logic_error("DestroyImage: image still has live views");
        }
    }
    images_.erase(found);
}

Image& Device::GetImage(ImageHandle handle) {
    const auto found = images_.find(handle.id);
    if (found == images_.end()) {
        throw std::invalid_argument("Invalid D3D12 image handle");
    }
    return found->second;
}

const Image& Device::GetImage(ImageHandle handle) const {
    const auto found = images_.find(handle.id);
    if (found == images_.end()) {
        throw std::invalid_argument("Invalid D3D12 image handle");
    }
    return found->second;
}

void Device::SetImageLayout(ImageHandle handle, u32 baseMipLevel,
                            u32 mipLevelCount, ImageLayout layout) {
    static_cast<void>(baseMipLevel);
    static_cast<void>(mipLevelCount);
    GetImage(handle).layout = layout;
}

ImageViewHandle Device::CreateImageView(const ImageViewDesc& desc) {
    if (!desc.image.IsValid()) {
        throw std::invalid_argument("CreateImageView: invalid image handle");
    }

    const Image& image = GetImage(desc.image);
    const Format viewFormat =
        desc.format == Format::Undefined ? image.format : desc.format;
    if (viewFormat != image.format) {
        throw std::invalid_argument(
            "CreateImageView: format reinterpretation is not implemented");
    }
    const bool isColor = desc.aspect == ImageAspect::Color;
    const bool isDepthStencil =
        HasFlag(desc.aspect, ImageAspect::Depth) ||
        HasFlag(desc.aspect, ImageAspect::Stencil);
    if (isColor == isDepthStencil) {
        throw std::invalid_argument(
            "CreateImageView: select either color or depth/stencil aspects");
    }
    if (isColor && !HasFlag(image.usage, ImageUsage::ColorAttachment)) {
        throw std::invalid_argument(
            "CreateImageView: image was not created for color attachment use");
    }
    if (isDepthStencil && !HasFlag(image.usage, ImageUsage::DepthStencil)) {
        throw std::invalid_argument(
            "CreateImageView: image was not created for depth/stencil use");
    }
    if (isDepthStencil && viewFormat != Format::D32_FLOAT &&
        viewFormat != Format::D24_UNORM_S8_UINT) {
        throw std::invalid_argument(
            "CreateImageView: unsupported D3D12 depth/stencil format");
    }
    if (desc.mipLevelCount != 1) {
        throw std::invalid_argument(
            "CreateImageView: an attachment view selects exactly one mip level");
    }
    if (desc.baseMipLevel >= image.mipLevels || desc.arrayLayerCount == 0 ||
        desc.baseArrayLayer + desc.arrayLayerCount > image.arrayLayers) {
        throw std::out_of_range("CreateImageView: subresource range is out of bounds");
    }

    if (desc.type == ImageViewType::View2D && desc.arrayLayerCount != 1) {
        throw std::invalid_argument(
            "CreateImageView: View2D requires one array layer");
    }
    if (isDepthStencil && (desc.type == ImageViewType::Cube ||
                           desc.type == ImageViewType::CubeArray)) {
        throw std::invalid_argument(
            "CreateImageView: cube depth/stencil views are not implemented");
    }

    CpuDescriptorAllocation descriptor{};
    ImageViewDescriptorType descriptorType{};
    if (isColor) {
        descriptor = rtvDescriptors_.Allocate();
        descriptorType = ImageViewDescriptorType::RenderTarget;

        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = ToDxgiFormat(viewFormat);
        if (desc.type == ImageViewType::View2D) {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = desc.baseMipLevel;
        } else {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = desc.baseMipLevel;
            viewDesc.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
            viewDesc.Texture2DArray.ArraySize = desc.arrayLayerCount;
        }
        device_->CreateRenderTargetView(
            image.resourcePtr, &viewDesc, descriptor.handle);
    } else {
        descriptor = dsvDescriptors_.Allocate();
        descriptorType = ImageViewDescriptorType::DepthStencil;

        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        viewDesc.Format = ToDxgiFormat(viewFormat);
        if (desc.type == ImageViewType::View2D) {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = desc.baseMipLevel;
        } else {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = desc.baseMipLevel;
            viewDesc.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
            viewDesc.Texture2DArray.ArraySize = desc.arrayLayerCount;
        }
        device_->CreateDepthStencilView(
            image.resourcePtr, &viewDesc, descriptor.handle);
    }

    const ImageViewHandle handle{nextImageViewHandle_++};
    imageViews_.emplace(handle.id, ImageView{
        .image = desc.image,
        .descriptor = descriptor,
        .aspect = desc.aspect,
        .descriptorType = descriptorType,
        .owned = true,
    });
    return handle;
}

void Device::DestroyImageView(ImageViewHandle view) {
    const auto found = imageViews_.find(view.id);
    if (found == imageViews_.end()) {
        throw std::invalid_argument("Invalid D3D12 image-view handle");
    }
    if (found->second.owned) {
        if (found->second.descriptorType ==
            ImageViewDescriptorType::RenderTarget) {
            rtvDescriptors_.Free(found->second.descriptor);
        } else {
            dsvDescriptors_.Free(found->second.descriptor);
        }
    }
    imageViews_.erase(found);
}

const ImageView& Device::GetImageView(ImageViewHandle handle) const {
    const auto found = imageViews_.find(handle.id);
    if (found == imageViews_.end()) {
        throw std::invalid_argument("Invalid D3D12 image-view handle");
    }
    return found->second;
}

SamplerHandle Device::CreateSampler(const SamplerDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroySampler(SamplerHandle handle) {
    static_cast<void>(handle);
}

ShaderHandle Device::CreateShader(const ShaderDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyShader(ShaderHandle handle) {
    static_cast<void>(handle);
}

PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    static_cast<void>(desc);
    return {};
}

PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
    static_cast<void>(desc);
    return {};
}

PipelineHandle Device::CreateMeshPipeline(const MeshPipelineDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyPipeline(PipelineHandle handle) {
    static_cast<void>(handle);
}

BindingLayoutHandle Device::CreateBindingLayout(const BindingLayoutDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyBindingLayout(BindingLayoutHandle handle) {
    static_cast<void>(handle);
}

BindingPoolHandle Device::CreateBindingPool(const BindingPoolDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyBindingPool(BindingPoolHandle handle) {
    static_cast<void>(handle);
}

BindingSetHandle Device::AllocateBindingSet(
    const BindingSetAllocationDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::UpdateBindingSet(const BindingWriteDesc& desc) {
    static_cast<void>(desc);
}

QueryPoolHandle Device::CreateTimestampQueryPool(const QueryPoolDesc& desc) {
    static_cast<void>(desc);
    return {};
}

void Device::DestroyQueryPool(QueryPoolHandle handle) {
    static_cast<void>(handle);
}

bool Device::GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
                                      u32 queryCount, u64* results) {
    static_cast<void>(handle);
    static_cast<void>(firstQuery);
    static_cast<void>(queryCount);
    static_cast<void>(results);
    return false;
}

double Device::GetTimestampPeriodNanoseconds() const {
    return 0.0;
}

QueueInfo Device::GetQueueInfo(QueueType type) const {
    static_cast<void>(type);
    return {};
}

QueueRelationship Device::GetQueueRelationship(QueueType first,
                                               QueueType second) const {
    static_cast<void>(first);
    static_cast<void>(second);
    return QueueRelationship::SameQueue;
}

FenceHandle Device::CreateFence(bool signaled) {
    static_cast<void>(signaled);
    return {};
}

void Device::DestroyFence(FenceHandle handle) {
    static_cast<void>(handle);
}

bool Device::IsFenceSignaled(FenceHandle handle) const {
    static_cast<void>(handle);
    return false;
}

void Device::WaitFence(FenceHandle handle, u64 timeoutNanoseconds) {
    static_cast<void>(handle);
    static_cast<void>(timeoutNanoseconds);
}

void Device::ResetFence(FenceHandle handle) {
    static_cast<void>(handle);
}

SemaphoreHandle Device::CreateSemaphore(SemaphoreType type, u64 initialValue) {
    static_cast<void>(type);
    static_cast<void>(initialValue);
    return {};
}

void Device::DestroySemaphore(SemaphoreHandle handle) {
    static_cast<void>(handle);
}

void Device::SignalSemaphore(SemaphoreHandle handle, u64 value) {
    static_cast<void>(handle);
    static_cast<void>(value);
}

u64 Device::GetSemaphoreValue(SemaphoreHandle handle) const {
    static_cast<void>(handle);
    return 0;
}

void Device::WaitSemaphore(SemaphoreHandle handle, u64 value,
                           u64 timeoutNanoseconds) {
    static_cast<void>(handle);
    static_cast<void>(value);
    static_cast<void>(timeoutNanoseconds);
}

ImageLayout Device::GetImageLayout(ImageHandle imageHandle, u32 mipLevel) const {
    if (mipLevel != 0) {
        throw std::out_of_range("D3D12 swapchain images only have one mip level");
    }
    return GetImage(imageHandle).layout;
}

FrameBeginResult Device::BeginFrame(SwapchainHandle handle) {
    if (!swapchain_ || handle.id != swapchainHandle_.id) {
        throw std::invalid_argument("Invalid D3D12 swapchain handle");
    }
    if (graphicsCommandPrepared_) {
        throw std::logic_error("The previous D3D12 frame has not been submitted");
    }

    currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
    currentFrame_ = currentBackbufferIndex_;
    FrameContext& frame = frames_.at(currentFrame_);
    WaitForFenceValue(frame.fenceValue);
    ThrowIfFailed(frame.allocator->Reset(),
                  "ID3D12CommandAllocator::Reset");
    graphicsCommandList_->SetAllocator(frame.allocator.Get());
    graphicsCommandPrepared_ = true;

    return {
        .backbuffer = swapchainImageViewHandles_.at(currentBackbufferIndex_),
        .backbufferImage = swapchainImageHandles_.at(currentBackbufferIndex_),
        .backbufferIndex = currentBackbufferIndex_,
        .success = true,
        .frameIndex = currentFrame_,
        .imageIndex = currentBackbufferIndex_,
    };
}

ICommandList& Device::AcquireCommandList(QueueType type) {
    return GetCommandList(type);
}

void Device::Submit(QueueType type, ICommandList& commandList,
                    const SubmitDesc& desc) {
    if (type != QueueType::Graphics || &commandList != graphicsCommandList_.get()) {
        throw std::invalid_argument("Unsupported D3D12 command-list submission");
    }
    if (!desc.waits.empty() || !desc.signals.empty()) {
        throw std::logic_error("D3D12 timeline submissions are not implemented");
    }
    ExecuteGraphicsCommandList();
    FrameContext& frame = frames_.at(currentFrame_);
    frame.fenceValue = ++idleFenceValue_;
    ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), frame.fenceValue),
                  "ID3D12CommandQueue::Signal");
    graphicsCommandPrepared_ = false;
}

ICommandList& Device::GetCommandList(QueueType type) {
    if (type != QueueType::Graphics) {
        throw std::logic_error("Only the D3D12 graphics queue is implemented");
    }
    if (!graphicsCommandPrepared_ || !graphicsCommandList_) {
        throw std::logic_error("D3D12 graphics command list is not prepared");
    }
    return *graphicsCommandList_;
}

void Device::Submit(QueueType type, const SubmitDesc& desc) {
    Submit(type, GetCommandList(type), desc);
}

void Device::SubmitAndPresent(SwapchainHandle swapchain,
                              const SubmitDesc& desc) {
    if (!swapchain_ || swapchain.id != swapchainHandle_.id) {
        throw std::invalid_argument("Invalid D3D12 swapchain handle");
    }
    if (!desc.waits.empty() || !desc.signals.empty()) {
        throw std::logic_error("D3D12 timeline submissions are not implemented");
    }
    ExecuteGraphicsCommandList();
    swapchain_->Present();

    FrameContext& frame = frames_.at(currentFrame_);
    frame.fenceValue = ++idleFenceValue_;
    ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), frame.fenceValue),
                  "ID3D12CommandQueue::Signal");
    graphicsCommandPrepared_ = false;
    currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
    currentFrame_ = currentBackbufferIndex_;
}

void Device::WaitIdle() {
    const u64 fenceValue = ++idleFenceValue_;
    ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), fenceValue),
                  "ID3D12CommandQueue::Signal");
    WaitForFenceValue(fenceValue);
}

void Device::CollectGarbage() {}

std::unique_ptr<IUploadContext> Device::CreateUploadContext(
    u64 stagingBufferSize) {
    static_cast<void>(stagingBufferSize);
    return {};
}

void Device::AcquireUploadedBuffers(
    std::span<const PendingBufferAcquire> pendingAcquires) {
    static_cast<void>(pendingAcquires);
}

void Device::AcquireUploadedImages(
    std::span<const PendingImageAcquire> pendingAcquires) {
    static_cast<void>(pendingAcquires);
}

GeneratedPipelineLayout Device::BuildPipelineLayout(
    const PipelineReflectionData& reflection,
    const PipelineLayoutOverrides& overrides) {
    static_cast<void>(reflection);
    static_cast<void>(overrides);
    return {};
}

Extent2D Device::GetSwapchainDimensions() const {
    if (!swapchain_) {
        return {};
    }
    return {
        .width = swapchain_->GetWidth(),
        .height = swapchain_->GetHeight(),
    };
}

void Device::CreateInstance(const DeviceDesc& desc) {
    UINT factoryFlags = 0;
    if (desc.enableValidation) {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)),
                  "CreateDXGIFactory2");
}

void Device::PickPhysicalDevice() {
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory_->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 description{};
        ThrowIfFailed(candidate->GetDesc1(&description),
                      "IDXGIAdapter1::GetDesc1");
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(
                candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                __uuidof(ID3D12Device), nullptr))) {
            ThrowIfFailed(candidate.As(&adapter_), "Query IDXGIAdapter4");
            return;
        }
    }

    ComPtr<IDXGIAdapter> warpAdapter;
    ThrowIfFailed(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)),
                  "IDXGIFactory6::EnumWarpAdapter");
    ThrowIfFailed(warpAdapter.As(&adapter_), "Query WARP IDXGIAdapter4");
}

void Device::CreateLogicalDevice() {
    ThrowIfFailed(D3D12CreateDevice(
                      adapter_.Get(), D3D_FEATURE_LEVEL_12_0,
                      IID_PPV_ARGS(&device_)),
                  "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    ThrowIfFailed(device_->CreateCommandQueue(
                      &queueDesc, IID_PPV_ARGS(&graphicsQueue_)),
                  "ID3D12Device::CreateCommandQueue");

    rtvDescriptors_.Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                               kCpuDescriptorCapacity);
    dsvDescriptors_.Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                               kCpuDescriptorCapacity);
}

void Device::CreateCommandObjects() {
    const u32 frameCount = swapchain_->GetBufferCount();
    if (frameCount == 0) {
        throw std::logic_error("Cannot create command objects without swapchain buffers");
    }

    frames_.clear();
    frames_.resize(frameCount);
    for (FrameContext& frame : frames_) {
        ThrowIfFailed(device_->CreateCommandAllocator(
                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                          IID_PPV_ARGS(&frame.allocator)),
                      "ID3D12Device::CreateCommandAllocator");
    }

    ThrowIfFailed(device_->CreateCommandList(
                      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                      frames_.front().allocator.Get(), nullptr,
                      IID_PPV_ARGS(&nativeGraphicsCommandList_)),
                  "ID3D12Device::CreateCommandList");
    graphicsCommandList_ = std::make_unique<CommandList>(
        this, nativeGraphicsCommandList_.Get(), frames_.front().allocator.Get());
}

void Device::CreateSwapchainResources() {
    const u32 bufferCount = swapchain_->GetBufferCount();
    swapchainImageHandles_.reserve(bufferCount);
    swapchainImageViewHandles_.reserve(bufferCount);

    for (u32 index = 0; index < bufferCount; ++index) {
        const SwapchainImage& backbuffer = swapchain_->GetBackbuffer(index);
        const ImageHandle imageHandle{nextImageHandle_++};
        const ImageViewHandle viewHandle{nextImageViewHandle_++};

        images_.emplace(imageHandle.id, Image{
            .resourcePtr = backbuffer.resource.Get(),
            .state = D3D12_RESOURCE_STATE_PRESENT,
            .layout = ImageLayout::Present,
            .format = swapchainFormat_,
            .usage = ImageUsage::ColorAttachment,
            .width = swapchain_->GetWidth(),
            .height = swapchain_->GetHeight(),
        });
        imageViews_.emplace(viewHandle.id, ImageView{
            .image = imageHandle,
            .descriptor = {.handle = backbuffer.rtv},
            .aspect = ImageAspect::Color,
            .descriptorType = ImageViewDescriptorType::RenderTarget,
            .owned = false,
        });
        swapchainImageHandles_.push_back(imageHandle);
        swapchainImageViewHandles_.push_back(viewHandle);
    }
}

void Device::RefreshSwapchainResources() {
    if (swapchainImageHandles_.size() != swapchain_->GetBufferCount() ||
        swapchainImageViewHandles_.size() != swapchain_->GetBufferCount()) {
        throw std::logic_error("D3D12 swapchain buffer count changed during resize");
    }

    for (u32 index = 0; index < swapchain_->GetBufferCount(); ++index) {
        const SwapchainImage& backbuffer = swapchain_->GetBackbuffer(index);
        Image& image = GetImage(swapchainImageHandles_[index]);
        image.resourcePtr = backbuffer.resource.Get();
        image.state = D3D12_RESOURCE_STATE_PRESENT;
        image.layout = ImageLayout::Present;
        image.format = swapchainFormat_;
        image.width = swapchain_->GetWidth();
        image.height = swapchain_->GetHeight();

        ImageView& view = imageViews_.at(swapchainImageViewHandles_[index].id);
        view.descriptor.handle = backbuffer.rtv;
    }
}

void Device::DestroySwapchainResources() {
    for (const ImageViewHandle handle : swapchainImageViewHandles_) {
        imageViews_.erase(handle.id);
    }
    for (const ImageHandle handle : swapchainImageHandles_) {
        images_.erase(handle.id);
    }
    swapchainImageViewHandles_.clear();
    swapchainImageHandles_.clear();
}

void Device::WaitForFenceValue(u64 value) {
    if (value == 0 || idleFence_->GetCompletedValue() >= value) {
        return;
    }

    ThrowIfFailed(idleFence_->SetEventOnCompletion(value, idleFenceEvent_),
                  "ID3D12Fence::SetEventOnCompletion");
    if (WaitForSingleObject(idleFenceEvent_, INFINITE) != WAIT_OBJECT_0) {
        throw std::runtime_error("Failed to wait for a D3D12 fence");
    }
}

void Device::ExecuteGraphicsCommandList() {
    if (!graphicsCommandPrepared_ || !graphicsCommandList_) {
        throw std::logic_error("D3D12 graphics command list is not prepared");
    }
    if (graphicsCommandList_->IsRecording()) {
        throw std::logic_error("End the D3D12 command list before submission");
    }

    ID3D12CommandList* commandLists[]{graphicsCommandList_->GetNative()};
    graphicsQueue_->ExecuteCommandLists(1, commandLists);
}

void Device::CreateSyncObjects() {
    ThrowIfFailed(device_->CreateFence(
                      0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence_)),
                  "ID3D12Device::CreateFence");
    idleFenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idleFenceEvent_) {
        throw std::runtime_error("CreateEventW failed for the D3D12 idle fence");
    }
}

} // namespace Velos::D3D12
