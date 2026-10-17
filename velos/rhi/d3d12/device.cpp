#include "device.h"
#include "command_list.h"
#include "upload_context.h"

#include <stdexcept>
#include <cstring>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <cmath>

namespace Velos::D3D12 {

namespace {
// Each stream subobject, including its tag, must be pointer-aligned.
template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, typename Value>
struct alignas(void *) MeshPipelineSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE tag = type;
  Value value{};
};

struct MeshPipelineStream {
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
                        ID3D12RootSignature *>
      rootSignature;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS,
                        D3D12_SHADER_BYTECODE>
      amplificationShader;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,
                        D3D12_SHADER_BYTECODE>
      meshShader;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
                        D3D12_SHADER_BYTECODE>
      fragmentShader;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
                        D3D12_BLEND_DESC>
      blend;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT>
      sampleMask;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
                        D3D12_RASTERIZER_DESC>
      rasterizer;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
                        D3D12_DEPTH_STENCIL_DESC>
      depthStencil;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
                        D3D12_PRIMITIVE_TOPOLOGY_TYPE>
      topology;
  MeshPipelineSubobject<
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
      D3D12_RT_FORMAT_ARRAY>
      renderTargets;
  MeshPipelineSubobject<
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT>
      depthFormat;
  MeshPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
                        DXGI_SAMPLE_DESC>
      samples;
};
}  // namespace

Device::Device(const DeviceDesc &desc) {
  if (desc.pipelineCachePath)
    pipelineCachePath_ = desc.pipelineCachePath;
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
  graphicsBatches_.clear();
  for (auto *queue : {&computeQueueState_, &transferQueueState_}) {
    queue->commands.reset();
    queue->nativeCommands.Reset();
    queue->resources.ResetTransientResources();
  }
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

SwapchainHandle Device::CreateSwapchain(const SwapchainDesc &desc) {
  if (swapchain_) {
    throw std::logic_error(
        "The D3D12 backend currently supports one swapchain");
  }

  swapchain_ = std::make_unique<Swapchain>(*factory_.Get(), *device_.Get(),
                                           *graphicsQueue_.Get(), desc);
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
  graphicsBatches_.clear();
  graphicsBatchCursors_.clear();
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
  if (width == 0 || height == 0 ||
      (width == swapchain_->GetWidth() && height == swapchain_->GetHeight())) {
    return;
  }
  if (graphicsCommandPrepared_ || computeQueueState_.prepared ||
      transferQueueState_.prepared)
    throw std::logic_error(
        "Resize the D3D12 swapchain between submitted frames");

  WaitIdle();
  // GPU completion alone does not release our COM references. DXGI requires
  // every old backbuffer reference to be gone before ResizeBuffers.
  const auto retire = [](FrameContext &resources) {
    resources.ResetTransientResources();
  };
  for (auto &frame : frames_)
    retire(frame);
  for (auto &batches : graphicsBatches_)
    for (auto &batch : batches)
      retire(batch->resources);
  retire(computeQueueState_.resources);
  retire(transferQueueState_.resources);

  swapchain_->Resize(width, height);
  RefreshSwapchainResources();
  currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
}

BufferHandle Device::CreateBuffer(const BufferDesc &desc) {
  if (!desc.size || desc.usage == BufferUsage::None ||
      desc.size > SIZE_MAX - 255)
    throw std::invalid_argument("Invalid buffer size or usage");
  static_cast<void>(desc);
  Buffer buffer;
  const bool mirror =
      (desc.memoryUsage == MemoryUsage::CPUToGPU &&
       (HasFlag(desc.usage, BufferUsage::Storage) ||
        HasFlag(desc.usage, BufferUsage::TransferDst))) ||
      (desc.memoryUsage == MemoryUsage::GPUToCPU &&
       (desc.initialData || desc.usage != BufferUsage::TransferDst));
  buffer.ownedResource = nullptr;

  D3D12_RESOURCE_DESC bufferDesc{};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.allocationSize = HasFlag(desc.usage, BufferUsage::Uniform)
                              ? (desc.size + 255) & ~255ull
                              : desc.size;
  bufferDesc.Width = buffer.allocationSize;
  if (HasFlag(desc.usage, BufferUsage::Storage))
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_HEAP_PROPERTIES heapProperties{};
  if (desc.memoryUsage == MemoryUsage::GPUOnly) {
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
  } else if (desc.memoryUsage == MemoryUsage::CPUToGPU) {
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
  } else if (desc.memoryUsage == MemoryUsage::GPUToCPU) {
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
  } else {
    throw std::invalid_argument("Invalid memory usage for buffer");
  }

  if (mirror)
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
  buffer.heapType = heapProperties.Type;
  buffer.state = buffer.heapType == D3D12_HEAP_TYPE_UPLOAD
                     ? D3D12_RESOURCE_STATE_GENERIC_READ
                 : buffer.heapType == D3D12_HEAP_TYPE_READBACK
                     ? D3D12_RESOURCE_STATE_COPY_DEST
                 : desc.initialData ? D3D12_RESOURCE_STATE_COPY_DEST
                                    : D3D12_RESOURCE_STATE_COMMON;

  ThrowIfFailed(device_->CreateCommittedResource(
                    &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    buffer.state, nullptr, IID_PPV_ARGS(&buffer.ownedResource)),
                "ID3D12Device::CreateCommittedResource");

  const BufferHandle handle{nextBufferHandle_++};
  buffer.resourcePtr = buffer.ownedResource.Get();
  if (mirror)
    buffer.hostResource = CreateHostBuffer(
        buffer.allocationSize, desc.memoryUsage == MemoryUsage::CPUToGPU);
  SetDebugName(buffer.resourcePtr, desc.debugName);
  const auto address = buffer.ownedResource->GetGPUVirtualAddress();
  buffer.vertexBufferView.BufferLocation = address;
  buffer.indexBufferView.BufferLocation = address;
  if (desc.initialData && buffer.heapType == D3D12_HEAP_TYPE_DEFAULT) {
    auto staging = CreateStagingBuffer(desc.size, desc.initialData);
    ExecuteImmediate([&](ID3D12GraphicsCommandList *list) {
      list->CopyBufferRegion(buffer.resourcePtr, 0, staging.Get(), 0,
                             desc.size);
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition = {
          buffer.resourcePtr, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
      list->ResourceBarrier(1, &barrier);
    });
    buffer.state = D3D12_RESOURCE_STATE_COMMON;
  } else if (desc.initialData) {
    void *mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    ThrowIfFailed(buffer.ownedResource->Map(0, &noRead, &mapped),
                  "Map initial buffer data");
    std::memcpy(mapped, desc.initialData, static_cast<size_t>(desc.size));
    buffer.ownedResource->Unmap(0, nullptr);
  }

  buffer.memoryUsage = desc.memoryUsage;
  buffer.size = desc.size;
  buffer.usage = desc.usage;

  if (desc.size <= UINT_MAX && HasFlag(desc.usage, BufferUsage::Vertex)) {
    buffer.vertexBufferView.SizeInBytes = static_cast<UINT>(desc.size);
  }
  if (desc.size <= UINT_MAX && HasFlag(desc.usage, BufferUsage::Index)) {
    buffer.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    buffer.indexBufferView.SizeInBytes = static_cast<UINT>(desc.size);
  }

  try {
    if (HasFlag(desc.usage, BufferUsage::Uniform) &&
        buffer.allocationSize <=
            D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16ull) {
      buffer.cbv = resourceDescriptors_.Allocate();
      const D3D12_CONSTANT_BUFFER_VIEW_DESC view{
          address, static_cast<UINT>(buffer.allocationSize)};
      device_->CreateConstantBufferView(&view, buffer.cbv.handle);
    }
    if (HasFlag(desc.usage, BufferUsage::Storage)) {
      if (desc.size % 4 || desc.size / 4 > UINT_MAX)
        throw std::invalid_argument(
            "Raw storage buffers require a multiple of four bytes and a 32-bit "
            "element count");
      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
      srv.Format = DXGI_FORMAT_R32_TYPELESS;
      srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Buffer.NumElements = static_cast<UINT>(desc.size / 4);
      srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
      buffer.srv = resourceDescriptors_.Allocate();
      device_->CreateShaderResourceView(buffer.resourcePtr, &srv,
                                        buffer.srv.handle);
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
      uav.Format = DXGI_FORMAT_R32_TYPELESS;
      uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav.Buffer.NumElements = static_cast<UINT>(desc.size / 4);
      uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
      if (buffer.heapType == D3D12_HEAP_TYPE_DEFAULT) {
        buffer.uav = resourceDescriptors_.Allocate();
        device_->CreateUnorderedAccessView(buffer.resourcePtr, nullptr, &uav,
                                           buffer.uav.handle);
      }
    }
  } catch (...) {
    if (buffer.cbv.IsValid())
      resourceDescriptors_.Free(buffer.cbv);
    if (buffer.srv.IsValid())
      resourceDescriptors_.Free(buffer.srv);
    if (buffer.uav.IsValid())
      resourceDescriptors_.Free(buffer.uav);
    throw;
  }
  buffers_.emplace(handle.id, std::move(buffer));

  return handle;
}

void Device::DestroyBuffer(BufferHandle handle) {
  WaitIdle();
  UnmapBuffer(handle);
  const auto &buffer = buffers_.at(handle.id);
  if (buffer.cbv.IsValid())
    resourceDescriptors_.Free(buffer.cbv);
  if (buffer.srv.IsValid())
    resourceDescriptors_.Free(buffer.srv);
  if (buffer.uav.IsValid())
    resourceDescriptors_.Free(buffer.uav);
  buffers_.erase(handle.id);
}

const Buffer &Device::GetBuffer(BufferHandle handle) const {
  return buffers_.at(handle.id);
}

u64 Device::GetBufferDeviceAddress(BufferHandle handle) const {
  return GetBuffer(handle).ownedResource->GetGPUVirtualAddress();
}

void *Device::MapBuffer(BufferHandle handle) {
  auto &buffer = buffers_.at(handle.id);
  if (buffer.memoryUsage == MemoryUsage::GPUOnly)
    throw std::invalid_argument("GPUOnly buffers cannot be mapped");
  if (!buffer.mapped) {
    if (buffer.hostResource) {
      if (buffer.memoryUsage == MemoryUsage::GPUToCPU) {
        CopyHostMirror(buffer, buffer.hostResource.Get(), false);
      } else {
        // Refresh the host mirror too: GPU writes/copies may have changed the
        // contents.
        auto readback = CreateHostBuffer(buffer.allocationSize, false);
        CopyHostMirror(buffer, readback.Get(), false);
        void *source, *destination;
        const D3D12_RANGE read{0, static_cast<SIZE_T>(buffer.allocationSize)},
            empty{0, 0};
        ThrowIfFailed(readback->Map(0, &read, &source),
                      "Map mirrored readback");
        ThrowIfFailed(buffer.hostResource->Map(0, &empty, &destination),
                      "Map host mirror");
        std::memcpy(destination, source,
                    static_cast<size_t>(buffer.allocationSize));
        readback->Unmap(0, &empty);
        buffer.hostResource->Unmap(0, nullptr);
      }
    }
    const D3D12_RANGE range{0, buffer.memoryUsage == MemoryUsage::GPUToCPU
                                   ? static_cast<SIZE_T>(buffer.size)
                                   : 0};
    auto *resource =
        buffer.hostResource ? buffer.hostResource.Get() : buffer.resourcePtr;
    ThrowIfFailed(resource->Map(0, &range, &buffer.mapped), "Map buffer");
  }
  if (buffer.hostResource)
    mappedMirrors_.insert(handle.id);
  return buffer.mapped;
}

void Device::UnmapBuffer(BufferHandle handle) {
  auto &buffer = buffers_.at(handle.id);
  if (!buffer.mapped)
    return;
  ComPtr<ID3D12Resource> stagedWrites;
  if (buffer.hostResource && buffer.memoryUsage == MemoryUsage::GPUToCPU)
    stagedWrites = CreateStagingBuffer(buffer.allocationSize, buffer.mapped);
  const D3D12_RANGE range{0, buffer.memoryUsage == MemoryUsage::CPUToGPU
                                 ? static_cast<SIZE_T>(buffer.size)
                                 : 0};
  auto *resource =
      buffer.hostResource ? buffer.hostResource.Get() : buffer.resourcePtr;
  resource->Unmap(0, &range);
  buffer.mapped = nullptr;
  mappedMirrors_.erase(handle.id);
  if (buffer.hostResource && buffer.memoryUsage == MemoryUsage::CPUToGPU)
    CopyHostMirror(buffer, buffer.hostResource.Get(), true);
  if (stagedWrites)
    CopyHostMirror(buffer, stagedWrites.Get(), true);
}

void Device::SynchronizeMappedMirrors(bool upload) {
  for (const auto id : mappedMirrors_) {
    auto &buffer = buffers_.at(id);
    if (upload) {
      if (buffer.memoryUsage == MemoryUsage::CPUToGPU)
        CopyHostMirror(buffer, buffer.hostResource.Get(), true);
      else {
        auto staging =
            CreateStagingBuffer(buffer.allocationSize, buffer.mapped);
        CopyHostMirror(buffer, staging.Get(), true);
      }
    } else if (buffer.memoryUsage == MemoryUsage::GPUToCPU) {
      CopyHostMirror(buffer, buffer.hostResource.Get(), false);
    } else {
      auto readback = CreateHostBuffer(buffer.allocationSize, false);
      CopyHostMirror(buffer, readback.Get(), false);
      void *source;
      const D3D12_RANGE read{0, static_cast<SIZE_T>(buffer.allocationSize)},
          empty{0, 0};
      ThrowIfFailed(readback->Map(0, &read, &source),
                    "Map persistent mirror readback");
      std::memcpy(buffer.mapped, source,
                  static_cast<size_t>(buffer.allocationSize));
      readback->Unmap(0, &empty);
    }
  }
}

ComPtr<ID3D12Resource> Device::CreateHostBuffer(u64 size, bool upload) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> resource;
  ThrowIfFailed(device_->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &desc,
                    upload ? D3D12_RESOURCE_STATE_GENERIC_READ
                           : D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&resource)),
                "Create host buffer");
  return resource;
}

void Device::CopyHostMirror(Buffer &buffer, ID3D12Resource *host, bool upload) {
  ExecuteImmediate([&](ID3D12GraphicsCommandList *list) {
    const auto transfer = upload ? D3D12_RESOURCE_STATE_COPY_DEST
                                 : D3D12_RESOURCE_STATE_COPY_SOURCE;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {buffer.resourcePtr,
                          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_COMMON, transfer};
    list->ResourceBarrier(1, &barrier);
    if (upload)
      list->CopyBufferRegion(buffer.resourcePtr, 0, host, 0,
                             buffer.allocationSize);
    else
      list->CopyBufferRegion(host, 0, buffer.resourcePtr, 0,
                             buffer.allocationSize);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
  });
}

ComPtr<ID3D12Resource> Device::CreateStagingBuffer(u64 size, const void *data) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> resource;
  ThrowIfFailed(
      device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       nullptr, IID_PPV_ARGS(&resource)),
      "Create staging buffer");
  void *mapped;
  const D3D12_RANGE noRead{0, 0};
  ThrowIfFailed(resource->Map(0, &noRead, &mapped), "Map staging buffer");
  std::memcpy(mapped, data, static_cast<size_t>(size));
  resource->Unmap(0, nullptr);
  return resource;
}

void Device::ExecuteImmediate(
    const std::function<void(ID3D12GraphicsCommandList *)> &record) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&allocator)),
                "Create upload allocator");
  ThrowIfFailed(
      device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
      "Create upload list");
  record(list.Get());
  ThrowIfFailed(list->Close(), "Close upload list");
  ID3D12CommandList *lists[]{list.Get()};
  graphicsQueue_->ExecuteCommandLists(1, lists);
  WaitIdle();
}

void Device::CheckFormat(Format format, D3D12_FORMAT_SUPPORT1 first,
                         D3D12_FORMAT_SUPPORT2 second) const {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support{ToDxgiFormat(format)};
  ThrowIfFailed(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                             &support, sizeof(support)),
                "Check format support");
  if ((support.Support1 & first) != first ||
      (support.Support2 & second) != second)
    throw std::invalid_argument(
        "Requested texture format/usage is unsupported by this adapter");
}

ImageHandle Device::CreateImage(const ImageDesc &desc) {
  return CreateImageResource(desc, false);
}

ImageHandle Device::CreateImageResource(const ImageDesc &desc, bool sparse,
                                        u64 memoryBudgetBytes) {
  if (desc.type != ImageType::Image2D && desc.type != ImageType::Cube)
    throw std::invalid_argument("Invalid image type");
  if (desc.width == 0 || desc.height == 0 || desc.depth == 0) {
    throw std::invalid_argument("CreateImage: dimensions must be non-zero");
  }
  if (desc.mipLevels == 0 || desc.arrayLayers == 0) {
    throw std::invalid_argument(
        "CreateImage: mip and array counts must be non-zero");
  }
  if (desc.format == Format::Undefined) {
    throw std::invalid_argument("CreateImage: format must not be Undefined");
  }
  if (desc.usage == ImageUsage::None) {
    throw std::invalid_argument("CreateImage: usage must not be None");
  }
  if (desc.depth != 1) {
    throw std::invalid_argument(
        "CreateImage: the RHI currently exposes only 2D images");
  }
  if (desc.type == ImageType::Cube &&
      (desc.width != desc.height || desc.arrayLayers % 6 != 0)) {
    throw std::invalid_argument(
        "CreateImage: cube images require square faces and a multiple of six "
        "layers");
  }
  if (HasFlag(desc.usage, ImageUsage::ColorAttachment) &&
      HasFlag(desc.usage, ImageUsage::DepthStencil)) {
    throw std::invalid_argument(
        "CreateImage: an image cannot be both a color and depth attachment");
  }

  if (desc.arrayLayers > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION ||
      desc.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      desc.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
      desc.mipLevels >
          1u + static_cast<u32>(std::log2(std::max(desc.width, desc.height))))
    throw std::invalid_argument(
        "Texture dimensions or mip count exceed limits");
  if ((!IsDepthFormat(desc.format) &&
       HasFlag(desc.usage, ImageUsage::DepthStencil)) ||
      (IsDepthFormat(desc.format) &&
       (HasFlag(desc.usage, ImageUsage::ColorAttachment) ||
        HasFlag(desc.usage, ImageUsage::Storage))))
    throw std::invalid_argument("Incompatible depth format and texture usage");
  CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_TEXTURE2D);
  if (HasFlag(desc.usage, ImageUsage::ColorAttachment))
    CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
  if (HasFlag(desc.usage, ImageUsage::DepthStencil))
    CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
  if (HasFlag(desc.usage, ImageUsage::Sampled) && !IsDepthFormat(desc.format))
    CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
  if (HasFlag(desc.usage, ImageUsage::Storage))
    CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
                D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
  if (sparse) {
    if (tiledResourcesTier_ < D3D12_TILED_RESOURCES_TIER_2)
      throw std::runtime_error(
          "CreateSparseImage: tiled resources tier 2 is required");
    if (memoryBudgetBytes < D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
      throw std::invalid_argument(
          "CreateSparseImage: budget is smaller than one 64 KiB page");
    // The RHI page coordinates have no plane index. Stencil requires a
    // separate plane mapping and cannot be represented by this interface.
    if (desc.format == Format::D24_UNORM_S8_UINT)
      throw std::invalid_argument(
          "CreateSparseImage: depth/stencil planes are unsupported");
    CheckFormat(desc.format, D3D12_FORMAT_SUPPORT1_TEXTURE2D,
                D3D12_FORMAT_SUPPORT2_TILED);
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
  resourceDesc.Format = ResourceFormat(desc.format);
  if (resourceDesc.Format == DXGI_FORMAT_UNKNOWN)
    resourceDesc.Format = ToDxgiFormat(desc.format);
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.Layout = sparse ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE
                               : D3D12_TEXTURE_LAYOUT_UNKNOWN;

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
  clearValue.Format = ToDxgiFormat(desc.format);
  const D3D12_CLEAR_VALUE *optimizedClearValue = nullptr;
  if (HasFlag(desc.usage, ImageUsage::ColorAttachment)) {
    clearValue.Color[3] = 1.0f;
    optimizedClearValue = &clearValue;
  } else if (HasFlag(desc.usage, ImageUsage::DepthStencil)) {
    clearValue.DepthStencil.Depth = 1.0f;
    optimizedClearValue = &clearValue;
  }

  Image image{};
  if (sparse) {
    ThrowIfFailed(device_->CreateReservedResource(
                      &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                      optimizedClearValue, IID_PPV_ARGS(&resource)),
                  "CreateSparseImage: CreateReservedResource");

    UINT virtualTileCount = 0;
    UINT subresourceCount = desc.mipLevels * desc.arrayLayers;
    D3D12_PACKED_MIP_INFO packedMips{};
    image.sparseTilings.resize(subresourceCount);
    device_->GetResourceTiling(resource.Get(), &virtualTileCount, &packedMips,
                               &image.sparseTileShape, &subresourceCount, 0,
                               image.sparseTilings.data());
    if (packedMips.NumPackedMips != 0 ||
        subresourceCount != image.sparseTilings.size() || !virtualTileCount ||
        !image.sparseTileShape.WidthInTexels ||
        !image.sparseTileShape.HeightInTexels ||
        image.sparseTileShape.DepthInTexels != 1) {
      throw std::runtime_error(
          "CreateSparseImage: packed mip tails cannot be represented by "
          "per-block residency");
    }

    constexpr u64 pageSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    image.sparsePageCount = static_cast<u32>(
        std::min<u64>(memoryBudgetBytes / pageSize, virtualTileCount));
    D3D12_HEAP_DESC heapDesc{};
    heapDesc.SizeInBytes = pageSize * image.sparsePageCount;
    heapDesc.Properties = heapProperties;
    heapDesc.Flags = HasFlag(desc.usage, ImageUsage::ColorAttachment) ||
                             HasFlag(desc.usage, ImageUsage::DepthStencil)
                         ? D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
                         : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    ThrowIfFailed(
        device_->CreateHeap(&heapDesc, IID_PPV_ARGS(&image.sparseHeap)),
        "CreateSparseImage: CreateHeap");
    SetDebugName(image.sparseHeap.Get(), desc.debugName);
  } else {
    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_COMMON, optimizedClearValue,
                      IID_PPV_ARGS(&resource)),
                  "ID3D12Device::CreateCommittedResource");
  }

  const ImageHandle handle{nextImageHandle_++};

  SetDebugName(resource.Get(), desc.debugName);
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

ImageHandle Device::CreateSparseImage(const ImageDesc &desc,
                                      u64 memoryBudgetBytes) {
  return CreateImageResource(desc, true, memoryBudgetBytes);
}

SparseImageMemoryInfo Device::GetSparseImageMemoryInfo(
    ImageHandle handle) const {
  const Image &image = GetImage(handle);
  if (!image.sparseHeap)
    throw std::invalid_argument(
        "GetSparseImageMemoryInfo: image is not sparse");
  return {
      .pageWidth = image.sparseTileShape.WidthInTexels,
      .pageHeight = image.sparseTileShape.HeightInTexels,
      .pageSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .pageCount = image.sparsePageCount,
  };
}

void Device::BindSparseImagePages(ImageHandle handle,
                                  std::span<const SparseImagePageBind> pages) {
  Image &image = GetImage(handle);
  if (!image.sparseHeap)
    throw std::invalid_argument("BindSparseImagePages: image is not sparse");

  // Same complete-residency-set semantics as the Vulkan implementation.
  const auto keyFor = [](const SparseImagePageBind &page) -> u64 {
    return (static_cast<u64>(page.mipLevel) << 56) |
           (static_cast<u64>(page.arrayLayer) << 32) |
           (static_cast<u64>(page.blockY) << 16) | page.blockX;
  };
  std::unordered_map<u64, u32> desired;
  desired.reserve(pages.size());
  for (const auto &page : pages) {
    if (page.mipLevel >= image.mipLevels ||
        page.arrayLayer >= image.arrayLayers ||
        page.memoryPage >= image.sparsePageCount)
      throw std::out_of_range(
          "BindSparseImagePages: page binding is out of range");
    const auto &tiling =
        image.sparseTilings[page.mipLevel + page.arrayLayer * image.mipLevels];
    if (page.blockX >= tiling.WidthInTiles ||
        page.blockY >= tiling.HeightInTiles)
      throw std::out_of_range(
          "BindSparseImagePages: block is outside mip extent");
    if (!desired.emplace(keyFor(page), page.memoryPage).second)
      throw std::invalid_argument(
          "BindSparseImagePages: duplicate image block");
  }

  std::vector<D3D12_TILED_RESOURCE_COORDINATE> coordinates;
  std::vector<D3D12_TILE_RANGE_FLAGS> flags;
  std::vector<UINT> heapOffsets;
  std::vector<UINT> tileCounts;
  const auto appendMapping = [&](u64 key, D3D12_TILE_RANGE_FLAGS flag,
                                 u32 memoryPage) {
    const u32 mip = static_cast<u32>(key >> 56);
    const u32 layer = static_cast<u32>((key >> 32) & 0xffffffu);
    coordinates.push_back({
        .X = static_cast<UINT>(key & 0xffffu),
        .Y = static_cast<UINT>((key >> 16) & 0xffffu),
        .Z = 0,
        .Subresource = mip + layer * image.mipLevels,
    });
    flags.push_back(flag);
    heapOffsets.push_back(memoryPage);
    tileCounts.push_back(1);
  };
  for (const auto &[key, oldPage] : image.sparseBindings) {
    const auto replacement = desired.find(key);
    if (replacement == desired.end())
      appendMapping(key, D3D12_TILE_RANGE_FLAG_NULL, 0);
    else if (replacement->second != oldPage)
      appendMapping(key, D3D12_TILE_RANGE_FLAG_NONE, replacement->second);
  }
  for (const auto &[key, memoryPage] : desired) {
    if (!image.sparseBindings.contains(key))
      appendMapping(key, D3D12_TILE_RANGE_FLAG_NONE, memoryPage);
  }
  if (coordinates.empty())
    return;

  // Finish accesses on all queues before physical tiles can be reassigned
  // to different blocks. Mapping updates themselves use the direct queue.
  WaitIdle();
  const UINT count = static_cast<UINT>(coordinates.size());
  graphicsQueue_->UpdateTileMappings(
      image.resourcePtr, count, coordinates.data(), nullptr,
      image.sparseHeap.Get(), count, flags.data(), heapOffsets.data(),
      tileCounts.data(), D3D12_TILE_MAPPING_FLAG_NONE);
  // Activate the updated mapping and invalidate any cached alias metadata.
  // Callers must clear/copy newly mapped RT/DS regions before using them.
  ExecuteImmediate([&](ID3D12GraphicsCommandList *list) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Aliasing.pResourceAfter = image.resourcePtr;
    list->ResourceBarrier(1, &barrier);
  });
  image.sparseBindings = std::move(desired);
}

void Device::DestroyImage(ImageHandle handle) {
  const auto found = images_.find(handle.id);
  if (found == images_.end()) {
    throw std::invalid_argument("Invalid D3D12 image handle");
  }
  for (const auto &[id, view] : imageViews_) {
    static_cast<void>(id);
    if (view.image.id == handle.id) {
      throw std::logic_error("DestroyImage: image still has live views");
    }
  }
  images_.erase(found);
}

Image &Device::GetImage(ImageHandle handle) {
  const auto found = images_.find(handle.id);
  if (found == images_.end()) {
    throw std::invalid_argument("Invalid D3D12 image handle");
  }
  return found->second;
}

const Image &Device::GetImage(ImageHandle handle) const {
  const auto found = images_.find(handle.id);
  if (found == images_.end()) {
    throw std::invalid_argument("Invalid D3D12 image handle");
  }
  return found->second;
}

void Device::SetImageLayout(ImageHandle handle, u32 baseMipLevel,
                            u32 mipLevelCount, ImageLayout layout) {
  auto &image = GetImage(handle);
  if (!mipLevelCount || baseMipLevel >= image.mipLevels ||
      mipLevelCount > image.mipLevels - baseMipLevel)
    throw std::out_of_range("Image layout mip range is invalid");
  image.InitializeSubresources();
  for (u32 subresource = 0; subresource < image.subresourceLayouts.size();
       ++subresource) {
    const u32 mip = subresource % image.mipLevels;
    if (mip >= baseMipLevel && mip < baseMipLevel + mipLevelCount)
      image.subresourceLayouts[subresource] = layout;
  }
  image.layout = image.subresourceLayouts[0];
}

ImageViewHandle Device::CreateImageView(const ImageViewDesc &desc) {
  const Image &image = GetImage(desc.image);
  const Format format =
      desc.format == Format::Undefined ? image.format : desc.format;
  if (format != image.format &&
      (ResourceFormat(format) == DXGI_FORMAT_UNKNOWN ||
       ResourceFormat(format) != ResourceFormat(image.format)))
    throw std::invalid_argument("Incompatible texture view format");
  if (!desc.mipLevelCount || desc.baseMipLevel >= image.mipLevels ||
      desc.mipLevelCount > image.mipLevels - desc.baseMipLevel ||
      !desc.arrayLayerCount || desc.baseArrayLayer >= image.arrayLayers ||
      desc.arrayLayerCount > image.arrayLayers - desc.baseArrayLayer)
    throw std::out_of_range("Image view subresource range is out of bounds");
  const bool depth = IsDepthFormat(image.format);
  if (depth && desc.aspect != ImageAspect::Depth &&
      desc.aspect != ImageAspect::Stencil &&
      desc.aspect != (ImageAspect::Depth | ImageAspect::Stencil))
    throw std::invalid_argument("Invalid depth/stencil view aspect");
  if ((!depth && desc.aspect != ImageAspect::Color) ||
      (depth && (desc.aspect == ImageAspect::None ||
                 HasFlag(desc.aspect, ImageAspect::Color) ||
                 (HasFlag(desc.aspect, ImageAspect::Stencil) &&
                  image.format != Format::D24_UNORM_S8_UINT))))
    throw std::invalid_argument("Invalid image view aspect");
  const bool cube =
      desc.type == ImageViewType::Cube || desc.type == ImageViewType::CubeArray;
  if (desc.type != ImageViewType::View2D &&
      desc.type != ImageViewType::View2DArray && !cube)
    throw std::invalid_argument("Invalid image view type");
  if ((desc.type == ImageViewType::View2D && desc.arrayLayerCount != 1) ||
      (cube && (image.type != ImageType::Cube || desc.baseArrayLayer % 6 ||
                desc.arrayLayerCount % 6)) ||
      (desc.type == ImageViewType::Cube && desc.arrayLayerCount != 6))
    throw std::invalid_argument("Invalid image view type/layer range");
  ImageView view{};
  view.image = desc.image;
  view.aspect = desc.aspect;
  view.owned = true;
  view.baseMipLevel = desc.baseMipLevel;
  view.mipLevelCount = desc.mipLevelCount;
  view.baseArrayLayer = desc.baseArrayLayer;
  view.arrayLayerCount = desc.arrayLayerCount;
  try {
    // Multi-mip views are shader views. Attachment descriptors select one mip.
    if (desc.mipLevelCount == 1 &&
        HasFlag(image.usage, ImageUsage::ColorAttachment)) {
      CheckFormat(format, D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
      view.descriptor = rtvDescriptors_.Allocate();
      view.descriptorType = ImageViewDescriptorType::RenderTarget;
      D3D12_RENDER_TARGET_VIEW_DESC native{};
      native.Format = ToDxgiFormat(format);
      if (desc.type == ImageViewType::View2D && image.arrayLayers == 1) {
        native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        native.Texture2D.MipSlice = desc.baseMipLevel;
      } else {
        native.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        native.Texture2DArray.MipSlice = desc.baseMipLevel;
        native.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
        native.Texture2DArray.ArraySize = desc.arrayLayerCount;
      }
      device_->CreateRenderTargetView(image.resourcePtr, &native,
                                      view.descriptor.handle);
    }
    if (desc.mipLevelCount == 1 &&
        HasFlag(image.usage, ImageUsage::DepthStencil)) {
      view.descriptor = dsvDescriptors_.Allocate();
      view.descriptorType = ImageViewDescriptorType::DepthStencil;
      D3D12_DEPTH_STENCIL_VIEW_DESC native{};
      native.Format = ToDxgiFormat(format);
      if (desc.type == ImageViewType::View2D && image.arrayLayers == 1) {
        native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        native.Texture2D.MipSlice = desc.baseMipLevel;
      } else {
        native.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        native.Texture2DArray.MipSlice = desc.baseMipLevel;
        native.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
        native.Texture2DArray.ArraySize = desc.arrayLayerCount;
      }
      device_->CreateDepthStencilView(image.resourcePtr, &native,
                                      view.descriptor.handle);
    }
    if (HasFlag(image.usage, ImageUsage::Sampled)) {
      if (depth && HasFlag(desc.aspect, ImageAspect::Depth) &&
          HasFlag(desc.aspect, ImageAspect::Stencil))
        throw std::invalid_argument(
            "Sampled depth/stencil views must select one aspect");
      D3D12_SHADER_RESOURCE_VIEW_DESC native{};
      native.Format =
          depth ? (image.format == Format::D32_FLOAT ? DXGI_FORMAT_R32_FLOAT
                   : desc.aspect == ImageAspect::Stencil
                       ? DXGI_FORMAT_X24_TYPELESS_G8_UINT
                       : DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
                : ToDxgiFormat(format);
      D3D12_FEATURE_DATA_FORMAT_SUPPORT support{native.Format};
      ThrowIfFailed(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                                 &support, sizeof(support)),
                    "Check SRV format");
      if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
        throw std::invalid_argument("Unsupported shader resource view format");
      native.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      if (desc.type == ImageViewType::Cube && desc.baseArrayLayer == 0 &&
          image.arrayLayers == 6) {
        native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        native.TextureCube.MostDetailedMip = desc.baseMipLevel;
        native.TextureCube.MipLevels = desc.mipLevelCount;
      } else if (cube) {
        native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        native.TextureCubeArray.MostDetailedMip = desc.baseMipLevel;
        native.TextureCubeArray.MipLevels = desc.mipLevelCount;
        native.TextureCubeArray.First2DArrayFace = desc.baseArrayLayer;
        native.TextureCubeArray.NumCubes = desc.arrayLayerCount / 6;
      } else if (desc.type == ImageViewType::View2D && image.arrayLayers == 1) {
        native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        native.Texture2D.MostDetailedMip = desc.baseMipLevel;
        native.Texture2D.MipLevels = desc.mipLevelCount;
        native.Texture2D.PlaneSlice =
            desc.aspect == ImageAspect::Stencil ? 1 : 0;
      } else {
        native.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        native.Texture2DArray.MostDetailedMip = desc.baseMipLevel;
        native.Texture2DArray.MipLevels = desc.mipLevelCount;
        native.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
        native.Texture2DArray.ArraySize = desc.arrayLayerCount;
        native.Texture2DArray.PlaneSlice =
            desc.aspect == ImageAspect::Stencil ? 1 : 0;
      }
      view.srv = resourceDescriptors_.Allocate();
      view.srvDescription = native;
      device_->CreateShaderResourceView(image.resourcePtr, &native,
                                        view.srv.handle);
    }
    if (HasFlag(image.usage, ImageUsage::Storage) && desc.mipLevelCount == 1) {
      CheckFormat(format, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
                  D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
      D3D12_UNORDERED_ACCESS_VIEW_DESC native{};
      native.Format = ToDxgiFormat(format);
      if (desc.type == ImageViewType::View2D && image.arrayLayers == 1) {
        native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        native.Texture2D.MipSlice = desc.baseMipLevel;
      } else {
        native.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        native.Texture2DArray.MipSlice = desc.baseMipLevel;
        native.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
        native.Texture2DArray.ArraySize = desc.arrayLayerCount;
      }
      view.uav = resourceDescriptors_.Allocate();
      view.uavDescription = native;
      device_->CreateUnorderedAccessView(image.resourcePtr, nullptr, &native,
                                         view.uav.handle);
    }
    if (!view.descriptor.IsValid() && !view.srv.IsValid() &&
        !view.uav.IsValid())
      throw std::invalid_argument(
          "Image usage/range does not support any view");
  } catch (...) {
    if (view.descriptor.IsValid()) {
      if (depth)
        dsvDescriptors_.Free(view.descriptor);
      else
        rtvDescriptors_.Free(view.descriptor);
    }
    if (view.srv.IsValid())
      resourceDescriptors_.Free(view.srv);
    if (view.uav.IsValid())
      resourceDescriptors_.Free(view.uav);
    throw;
  }
  const ImageViewHandle handle{nextImageViewHandle_++};
  imageViews_.emplace(handle.id, view);
  return handle;
}

void Device::DestroyImageView(ImageViewHandle handle) {
  const auto &view = imageViews_.at(handle.id);
  WaitIdle();
  if (view.owned && view.descriptor.IsValid()) {
    if (view.descriptorType == ImageViewDescriptorType::RenderTarget)
      rtvDescriptors_.Free(view.descriptor);
    else
      dsvDescriptors_.Free(view.descriptor);
  }
  if (view.srv.IsValid())
    resourceDescriptors_.Free(view.srv);
  if (view.uav.IsValid())
    resourceDescriptors_.Free(view.uav);
  imageViews_.erase(handle.id);
}

const ImageView &Device::GetImageView(ImageViewHandle handle) const {
  return imageViews_.at(handle.id);
}

SamplerHandle Device::CreateSampler(const SamplerDesc &desc) {
  if ((desc.minFilter != Filter::Nearest && desc.minFilter != Filter::Linear) ||
      (desc.magFilter != Filter::Nearest && desc.magFilter != Filter::Linear))
    throw std::invalid_argument("Invalid sampler filter");
  if (!std::isfinite(desc.minLod) || !std::isfinite(desc.maxLod) ||
      desc.minLod > desc.maxLod || !std::isfinite(desc.maxAnisotropy) ||
      desc.maxAnisotropy < 1 || desc.maxAnisotropy > 16)
    throw std::invalid_argument("Invalid sampler LOD/anisotropy");
  auto address = [](SamplerAddressMode mode) {
    switch (mode) {
      case SamplerAddressMode::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      case SamplerAddressMode::ClampToEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
    throw std::invalid_argument("Invalid sampler address mode");
  };
  D3D12_SAMPLER_DESC native{};
  native.Filter =
      desc.enableAnisotropy
          ? D3D12_FILTER_ANISOTROPIC
          : D3D12_ENCODE_BASIC_FILTER(
                desc.minFilter == Filter::Linear ? D3D12_FILTER_TYPE_LINEAR
                                                 : D3D12_FILTER_TYPE_POINT,
                desc.magFilter == Filter::Linear ? D3D12_FILTER_TYPE_LINEAR
                                                 : D3D12_FILTER_TYPE_POINT,
                D3D12_FILTER_TYPE_LINEAR, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
  native.AddressU = address(desc.addressU);
  native.AddressV = address(desc.addressV);
  native.AddressW = address(desc.addressW);
  native.MaxAnisotropy = static_cast<UINT>(std::ceil(desc.maxAnisotropy));
  native.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  native.MinLOD = desc.minLod;
  native.MaxLOD = desc.maxLod;
  const auto descriptor = samplerDescriptors_.Allocate();
  device_->CreateSampler(&native, descriptor.handle);
  const SamplerHandle handle{nextSamplerHandle_++};
  samplers_.emplace(handle.id, Sampler{descriptor, native});
  return handle;
}

void Device::DestroySampler(SamplerHandle handle) {
  const auto descriptor = samplers_.at(handle.id).descriptor;
  WaitIdle();
  samplerDescriptors_.Free(descriptor);
  samplers_.erase(handle.id);
}

const Sampler &Device::GetSampler(SamplerHandle handle) const {
  return samplers_.at(handle.id);
}
ShaderHandle Device::CreateShader(const ShaderDesc &desc) {
  if (!desc.bytecode || desc.bytecodeSize < 4 || desc.bytecodeSize > SIZE_MAX ||
      std::memcmp(desc.bytecode, "DXBC", 4) != 0 ||
      (desc.stage != ShaderStage::Vertex &&
       desc.stage != ShaderStage::Fragment &&
       desc.stage != ShaderStage::Compute && desc.stage != ShaderStage::Task &&
       desc.stage != ShaderStage::Mesh))
    throw std::invalid_argument(std::string("D3D12 shader '") +
                                (desc.debugName ? desc.debugName : "unnamed") +
                                "' requires DXIL bytecode");
  const auto *bytes = static_cast<const std::byte *>(desc.bytecode);
  const ShaderHandle handle{nextShaderHandle_++};
  shaders_.emplace(
      handle.id,
      Shader{{bytes, bytes + desc.bytecodeSize}, desc.stage, desc.reflection});
  return handle;
}

void Device::DestroyShader(ShaderHandle handle) {
  shaders_.erase(handle.id);
}

const Shader &Device::GetShader(ShaderHandle handle) const {
  return shaders_.at(handle.id);
}
const Pipeline &Device::GetPipeline(PipelineHandle handle) const {
  return pipelines_.at(handle.id);
}

PipelineHandle Device::CreateGraphicsPipeline(
    const GraphicsPipelineDesc &desc) {
  Pipeline pipeline;
  const auto &vertex = GetShader(desc.vertexShader);
  if (vertex.stage != ShaderStage::Vertex)
    throw std::invalid_argument("Expected a vertex shader");
  const Shader *pixel =
      desc.fragmentShader.IsValid() ? &GetShader(desc.fragmentShader) : nullptr;
  if (pixel && pixel->stage != ShaderStage::Fragment)
    throw std::invalid_argument("Expected a fragment shader");
  const Shader *shaders[]{&vertex, pixel};
  auto signature = BuildRootSignature(pipeline, desc.layout, shaders);
  if (desc.vertexLayouts.size() > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
    throw std::invalid_argument("Too many vertex layouts");
  D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
  native.pRootSignature = pipeline.rootSignature.Get();
  native.VS = {vertex.bytecode.data(), vertex.bytecode.size()};
  if (pixel)
    native.PS = {pixel->bytecode.data(), pixel->bytecode.size()};
  std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
  for (u32 slot = 0; slot < desc.vertexLayouts.size(); ++slot) {
    const auto &layout = desc.vertexLayouts[slot];
    if (layout.inputRate != VertexInputRate::PerVertex &&
        layout.inputRate != VertexInputRate::PerInstance)
      throw std::invalid_argument("Invalid vertex input rate");
    if (!layout.stride ||
        layout.stride > D3D12_REQ_MULTI_ELEMENT_STRUCTURE_SIZE_IN_BYTES)
      throw std::invalid_argument("Invalid vertex stride");
    pipeline.vertexStrides.push_back(layout.stride);
    for (const auto &attribute : layout.attributes) {
      DXGI_FORMAT format;
      switch (attribute.format) {
        case VertexFormat::Float32:
          format = DXGI_FORMAT_R32_FLOAT;
          break;
        case VertexFormat::Float32x2:
          format = DXGI_FORMAT_R32G32_FLOAT;
          break;
        case VertexFormat::Float32x3:
          format = DXGI_FORMAT_R32G32B32_FLOAT;
          break;
        case VertexFormat::Float32x4:
          format = DXGI_FORMAT_R32G32B32A32_FLOAT;
          break;
        case VertexFormat::UNorm8x4:
          format = DXGI_FORMAT_R8G8B8A8_UNORM;
          break;
        default:
          throw std::invalid_argument("Unsupported vertex format");
      }
      const u32 width = attribute.format == VertexFormat::Float32x2   ? 8
                        : attribute.format == VertexFormat::Float32x3 ? 12
                        : attribute.format == VertexFormat::Float32x4 ? 16
                                                                      : 4;
      if (attribute.binding != slot || attribute.offset > layout.stride ||
          width > layout.stride - attribute.offset)
        throw std::invalid_argument(
            "Vertex attribute binding/offset exceeds layout");
      for (const auto &existing : inputs)
        if (existing.SemanticIndex == attribute.semanticIndex)
          throw std::invalid_argument("Duplicate SHADER_INPUT semantic");
      inputs.push_back(
          {"SHADER_INPUT", attribute.semanticIndex, format, slot,
           attribute.offset,
           layout.inputRate == VertexInputRate::PerInstance
               ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
               : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
           layout.inputRate == VertexInputRate::PerInstance ? 1u : 0u});
    }
  }
  native.InputLayout = {inputs.data(), static_cast<UINT>(inputs.size())};
  native.SampleMask = UINT_MAX;
  native.RasterizerState.FillMode =
      desc.raster.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  native.RasterizerState.CullMode =
      desc.raster.cullBackFaces ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
  native.RasterizerState.FrontCounterClockwise = desc.raster.frontFaceCCW;
  native.RasterizerState.DepthClipEnable = TRUE;
  for (auto &blend : native.BlendState.RenderTarget) {
    blend.BlendEnable = desc.blend.enable;
    blend.SrcBlend =
        desc.blend.enable ? ToBlend(desc.blend.srcColor) : D3D12_BLEND_ONE;
    blend.DestBlend =
        desc.blend.enable ? ToBlend(desc.blend.dstColor) : D3D12_BLEND_ZERO;
    blend.BlendOp =
        desc.blend.enable ? ToBlendOp(desc.blend.colorOp) : D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = desc.blend.enable ? ToBlend(desc.blend.srcAlpha, true)
                                            : D3D12_BLEND_ONE;
    blend.DestBlendAlpha = desc.blend.enable
                               ? ToBlend(desc.blend.dstAlpha, true)
                               : D3D12_BLEND_ZERO;
    blend.BlendOpAlpha =
        desc.blend.enable ? ToBlendOp(desc.blend.alphaOp) : D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  }
  native.DepthStencilState.DepthEnable = desc.depth.depthTestEnable;
  native.DepthStencilState.DepthWriteMask = desc.depth.depthWriteEnable
                                                ? D3D12_DEPTH_WRITE_MASK_ALL
                                                : D3D12_DEPTH_WRITE_MASK_ZERO;
  native.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  native.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
      D3D12_COMPARISON_FUNC_ALWAYS};
  native.DepthStencilState.BackFace = native.DepthStencilState.FrontFace;
  switch (desc.topology) {
    case PrimitiveTopology::TriangleList:
      pipeline.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      break;
    case PrimitiveTopology::TriangleStrip:
      pipeline.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      break;
    case PrimitiveTopology::LineList:
      pipeline.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    default:
      throw std::invalid_argument("Invalid topology");
  }
  if (desc.colorAttachments.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
    throw std::invalid_argument("Too many color attachments");
  std::vector<Format> colors;
  for (const auto &attachment : desc.colorAttachments)
    colors.push_back(attachment.format);
  if (colors.empty() && desc.colorFormat != Format::Undefined)
    colors.push_back(desc.colorFormat);
  if (colors.empty() && desc.depth.depthFormat == Format::Undefined)
    throw std::invalid_argument("Pipeline requires at least one attachment");
  if (!colors.empty() && !pixel)
    throw std::invalid_argument("Color pipeline requires a fragment shader");
  for (u32 index = 0; index < colors.size(); ++index) {
    CheckFormat(colors[index], D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
    if (IsDepthFormat(colors[index]))
      throw std::invalid_argument("Depth format used for color target");
    if (desc.blend.enable)
      CheckFormat(colors[index], D3D12_FORMAT_SUPPORT1_BLENDABLE);
    native.RTVFormats[index] = ToDxgiFormat(colors[index]);
  }
  native.NumRenderTargets = static_cast<UINT>(colors.size());
  if (desc.depth.depthFormat != Format::Undefined) {
    if (!IsDepthFormat(desc.depth.depthFormat))
      throw std::invalid_argument("Invalid depth format");
    CheckFormat(desc.depth.depthFormat, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
    native.DSVFormat = ToDxgiFormat(desc.depth.depthFormat);
  } else if (desc.depth.depthTestEnable || desc.depth.depthWriteEnable) {
    throw std::invalid_argument("Depth state requires a depth format");
  }
  native.SampleDesc.Count = 1;
  // Hash only stable values; pointers and cached driver blobs are not part of
  // the key.
  u64 hash = 14695981039346656037ull;
  const auto hashBytes = [&](const void *bytes, size_t size) {
    const auto *data = static_cast<const unsigned char *>(bytes);
    for (size_t i = 0; i < size; ++i) {
      hash ^= data[i];
      hash *= 1099511628211ull;
    }
  };
  const auto hashValue = [&](const auto &value) {
    hashBytes(&value, sizeof(value));
  };
  // Field-wise hashing avoids structure padding and keeps keys stable across
  // processes.
  hashValue(native.VS.BytecodeLength);
  hashValue(native.PS.BytecodeLength);
  hashValue(native.SampleMask);
  hashValue(native.PrimitiveTopologyType);
  hashValue(native.IBStripCutValue);
  hashValue(native.NumRenderTargets);
  hashBytes(native.RTVFormats, sizeof(native.RTVFormats));
  hashValue(native.DSVFormat);
  hashValue(native.SampleDesc.Count);
  hashValue(native.SampleDesc.Quality);
  hashValue(native.BlendState.AlphaToCoverageEnable);
  hashValue(native.BlendState.IndependentBlendEnable);
  for (const auto &blend : native.BlendState.RenderTarget) {
    hashValue(blend.BlendEnable);
    hashValue(blend.LogicOpEnable);
    hashValue(blend.SrcBlend);
    hashValue(blend.DestBlend);
    hashValue(blend.BlendOp);
    hashValue(blend.SrcBlendAlpha);
    hashValue(blend.DestBlendAlpha);
    hashValue(blend.BlendOpAlpha);
    hashValue(blend.LogicOp);
    hashValue(blend.RenderTargetWriteMask);
  }
  const auto &raster = native.RasterizerState;
  hashValue(raster.FillMode);
  hashValue(raster.CullMode);
  hashValue(raster.FrontCounterClockwise);
  hashValue(raster.DepthBias);
  hashValue(raster.DepthBiasClamp);
  hashValue(raster.SlopeScaledDepthBias);
  hashValue(raster.DepthClipEnable);
  hashValue(raster.MultisampleEnable);
  hashValue(raster.AntialiasedLineEnable);
  hashValue(raster.ForcedSampleCount);
  hashValue(raster.ConservativeRaster);
  const auto &depth = native.DepthStencilState;
  hashValue(depth.DepthEnable);
  hashValue(depth.DepthWriteMask);
  hashValue(depth.DepthFunc);
  hashValue(depth.StencilEnable);
  hashValue(depth.StencilReadMask);
  hashValue(depth.StencilWriteMask);
  for (const auto &face : {depth.FrontFace, depth.BackFace}) {
    hashValue(face.StencilFailOp);
    hashValue(face.StencilDepthFailOp);
    hashValue(face.StencilPassOp);
    hashValue(face.StencilFunc);
  }
  hashValue(native.InputLayout.NumElements);
  hashBytes(signature->GetBufferPointer(), signature->GetBufferSize());
  hashBytes(vertex.bytecode.data(), vertex.bytecode.size());
  if (pixel)
    hashBytes(pixel->bytecode.data(), pixel->bytecode.size());
  for (const auto &input : inputs) {
    hashValue(input.SemanticIndex);
    hashValue(input.Format);
    hashValue(input.InputSlot);
    hashValue(input.AlignedByteOffset);
    hashValue(input.InputSlotClass);
    hashValue(input.InstanceDataStepRate);
  }
  DXGI_ADAPTER_DESC3 adapterDesc{};
  ThrowIfFailed(adapter_->GetDesc3(&adapterDesc), "Get cache adapter identity");
  LARGE_INTEGER driver{};
  adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
  hashBytes(&adapterDesc.AdapterLuid, sizeof(adapterDesc.AdapterLuid));
  hashBytes(&driver, sizeof(driver));
  std::filesystem::path cacheFile;
  std::vector<char> cache;
  if (!pipelineCachePath_.empty()) {
    cacheFile = std::filesystem::path(pipelineCachePath_ + ".d3d12") /
                (std::to_string(hash) + ".pso");
    std::ifstream file(cacheFile, std::ios::binary | std::ios::ate);
    if (file && file.tellg() > 0 && file.tellg() <= 64 * 1024 * 1024) {
      cache.resize(static_cast<size_t>(file.tellg()));
      file.seekg(0);
      file.read(cache.data(), cache.size());
      if (!file)
        cache.clear();
    }
    native.CachedPSO = {cache.data(), cache.size()};
  }
  HRESULT pipelineResult = device_->CreateGraphicsPipelineState(
      &native, IID_PPV_ARGS(&pipeline.state));
  if (FAILED(pipelineResult) && !cache.empty()) {
    native.CachedPSO = {};
    pipelineResult = device_->CreateGraphicsPipelineState(
        &native, IID_PPV_ARGS(&pipeline.state));
  }
  if (FAILED(pipelineResult)) {
    ComPtr<ID3D12InfoQueue> messages;
    if (SUCCEEDED(device_.As(&messages))) {
      for (UINT64 i = 0;
           i < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0;
        messages->GetMessage(i, nullptr, &size);
        std::vector<std::byte> storage(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        if (SUCCEEDED(messages->GetMessage(i, message, &size)))
          std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
      }
    }
    ThrowIfFailed(pipelineResult, "Create graphics pipeline");
  }
  SetDebugName(pipeline.state.Get(), desc.debugName);
  if (!cacheFile.empty()) {
    ComPtr<ID3DBlob> blob;
    if (SUCCEEDED(pipeline.state->GetCachedBlob(&blob))) {
      std::error_code error;
      std::filesystem::create_directories(cacheFile.parent_path(), error);
      if (!error) {
        std::ofstream file(cacheFile, std::ios::binary | std::ios::trunc);
        if (file)
          file.write(static_cast<const char *>(blob->GetBufferPointer()),
                     blob->GetBufferSize());
      }
    }
  }
  const PipelineHandle handle{nextPipelineHandle_++};
  pipelines_.emplace(handle.id, std::move(pipeline));
  return handle;
}

PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc &desc) {
  const auto &shader = GetShader(desc.computeShader);
  if (shader.stage != ShaderStage::Compute)
    throw std::invalid_argument("Expected a compute shader");
  Pipeline pipeline;
  pipeline.compute = true;
  const Shader *shaders[]{&shader};
  BuildRootSignature(pipeline, desc.layout, shaders);
  D3D12_COMPUTE_PIPELINE_STATE_DESC native{};
  native.pRootSignature = pipeline.rootSignature.Get();
  native.CS = {shader.bytecode.data(), shader.bytecode.size()};
  ThrowIfFailed(device_->CreateComputePipelineState(
                    &native, IID_PPV_ARGS(&pipeline.state)),
                "Create compute pipeline");
  SetDebugName(pipeline.state.Get(), desc.debugName);
  const PipelineHandle handle{nextPipelineHandle_++};
  pipelines_.emplace(handle.id, std::move(pipeline));
  return handle;
}

PipelineHandle Device::CreateMeshPipeline(const MeshPipelineDesc &desc) {
  if (!desc.meshShader.IsValid() || !desc.fragmentShader.IsValid())
    throw std::invalid_argument(
        "Mesh pipeline requires mesh and fragment shaders");

  const Shader &meshShader = GetShader(desc.meshShader);
  const Shader &fragmentShader = GetShader(desc.fragmentShader);
  // Amplification shader
  const Shader *taskShader =
      desc.taskShader.IsValid() ? &GetShader(desc.taskShader) : nullptr;

  if (meshShader.stage != ShaderStage::Mesh)
    throw std::invalid_argument("Expected a mesh shader");

  if (fragmentShader.stage != ShaderStage::Fragment)
    throw std::invalid_argument("Expected a fragment shader");

  if (taskShader && taskShader->stage != ShaderStage::Task)
    throw std::invalid_argument("Expected a task shader");

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
  ThrowIfFailed(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                                             &options, sizeof(options)),
                "Query mesh shader support");
  if (options.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    throw std::runtime_error("D3D12 device does not support mesh shaders");

  Pipeline pipeline;
  pipeline.mesh = true;

  std::vector<const Shader *> shaders{&meshShader, &fragmentShader};
  if (taskShader)
    shaders.push_back(taskShader);

  BuildRootSignature(pipeline, desc.layout, shaders);
  // Reuse the same fixed-function state and validation as graphics PSOs.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
  native.SampleMask = UINT_MAX;
  native.RasterizerState.FillMode =
      desc.raster.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  native.RasterizerState.CullMode =
      desc.raster.cullBackFaces ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
  native.RasterizerState.FrontCounterClockwise = desc.raster.frontFaceCCW;
  native.RasterizerState.DepthClipEnable = TRUE;
  for (auto &blend : native.BlendState.RenderTarget) {
    blend.BlendEnable = desc.blend.enable;
    blend.SrcBlend =
        desc.blend.enable ? ToBlend(desc.blend.srcColor) : D3D12_BLEND_ONE;
    blend.DestBlend =
        desc.blend.enable ? ToBlend(desc.blend.dstColor) : D3D12_BLEND_ZERO;
    blend.BlendOp =
        desc.blend.enable ? ToBlendOp(desc.blend.colorOp) : D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = desc.blend.enable ? ToBlend(desc.blend.srcAlpha, true)
                                            : D3D12_BLEND_ONE;
    blend.DestBlendAlpha = desc.blend.enable
                               ? ToBlend(desc.blend.dstAlpha, true)
                               : D3D12_BLEND_ZERO;
    blend.BlendOpAlpha =
        desc.blend.enable ? ToBlendOp(desc.blend.alphaOp) : D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  }
  native.DepthStencilState.DepthEnable = desc.depth.depthTestEnable;
  native.DepthStencilState.DepthWriteMask = desc.depth.depthWriteEnable
                                                ? D3D12_DEPTH_WRITE_MASK_ALL
                                                : D3D12_DEPTH_WRITE_MASK_ZERO;
  native.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  native.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
      D3D12_COMPARISON_FUNC_ALWAYS};
  native.DepthStencilState.BackFace = native.DepthStencilState.FrontFace;
  if (desc.colorAttachments.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
    throw std::invalid_argument("Too many color attachments");
  std::vector<Format> colors;
  for (const auto &attachment : desc.colorAttachments)
    colors.push_back(attachment.format);
  if (colors.empty() && desc.colorFormat != Format::Undefined)
    colors.push_back(desc.colorFormat);
  if (colors.empty() && desc.depth.depthFormat == Format::Undefined)
    throw std::invalid_argument("Pipeline requires at least one attachment");
  for (u32 index = 0; index < colors.size(); ++index) {
    CheckFormat(colors[index], D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
    if (IsDepthFormat(colors[index]))
      throw std::invalid_argument("Depth format used for color target");
    if (desc.blend.enable)
      CheckFormat(colors[index], D3D12_FORMAT_SUPPORT1_BLENDABLE);
    native.RTVFormats[index] = ToDxgiFormat(colors[index]);
  }
  native.NumRenderTargets = static_cast<UINT>(colors.size());
  if (desc.depth.depthFormat != Format::Undefined) {
    if (!IsDepthFormat(desc.depth.depthFormat))
      throw std::invalid_argument("Invalid depth format");
    CheckFormat(desc.depth.depthFormat, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
    native.DSVFormat = ToDxgiFormat(desc.depth.depthFormat);
  } else if (desc.depth.depthTestEnable || desc.depth.depthWriteEnable) {
    throw std::invalid_argument("Depth state requires a depth format");
  }
  native.SampleDesc.Count = 1;

  MeshPipelineStream stream;
  stream.rootSignature.value = pipeline.rootSignature.Get();
  if (taskShader)
    stream.amplificationShader.value = {taskShader->bytecode.data(),
                                        taskShader->bytecode.size()};
  stream.meshShader.value = {meshShader.bytecode.data(),
                             meshShader.bytecode.size()};
  stream.fragmentShader.value = {fragmentShader.bytecode.data(),
                                 fragmentShader.bytecode.size()};
  stream.blend.value = native.BlendState;
  stream.sampleMask.value = native.SampleMask;
  stream.rasterizer.value = native.RasterizerState;
  stream.depthStencil.value = native.DepthStencilState;
  stream.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  stream.renderTargets.value.NumRenderTargets = native.NumRenderTargets;
  std::copy(std::begin(native.RTVFormats), std::end(native.RTVFormats),
            std::begin(stream.renderTargets.value.RTFormats));
  stream.depthFormat.value = native.DSVFormat;
  stream.samples.value = native.SampleDesc;

  ComPtr<ID3D12Device2> pipelineDevice;
  ThrowIfFailed(device_.As(&pipelineDevice), "Query pipeline stream support");
  const D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(stream), &stream};
  ThrowIfFailed(pipelineDevice->CreatePipelineState(
                    &streamDesc, IID_PPV_ARGS(&pipeline.state)),
                "Create mesh pipeline");
  SetDebugName(pipeline.state.Get(), desc.debugName);
  const PipelineHandle handle{nextPipelineHandle_++};
  pipelines_.emplace(handle.id, std::move(pipeline));
  return handle;
}

void Device::DestroyPipeline(PipelineHandle handle) {
  WaitIdle();
  pipelines_.erase(handle.id);
}

QueryPoolHandle Device::CreateTimestampQueryPool(const QueryPoolDesc &desc) {
  static_cast<void>(desc);
  return {};
}

void Device::DestroyQueryPool(QueryPoolHandle handle) {
  static_cast<void>(handle);
}

bool Device::GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
                                      u32 queryCount, u64 *results) {
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
  if (type != QueueType::Graphics && type != QueueType::Compute &&
      type != QueueType::Transfer)
    throw std::invalid_argument("Invalid D3D12 queue type");
  return {.type = type,
          .dedicated = type != QueueType::Graphics,
          .aliasesGraphics = type == QueueType::Graphics};
}

QueueRelationship Device::GetQueueRelationship(QueueType first,
                                               QueueType second) const {
  GetQueueInfo(first);
  GetQueueInfo(second);
  // D3D12 has distinct queues, but no Vulkan-style family ownership transfers.
  return first == second ? QueueRelationship::SameQueue
                         : QueueRelationship::SameFamily;
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
  if (type != SemaphoreType::Binary && type != SemaphoreType::Timeline)
    throw std::invalid_argument("CreateSemaphore: invalid semaphore type");
  if (initialValue == UINT64_MAX)
    throw std::invalid_argument(
        "CreateSemaphore: UINT64_MAX is reserved for device removal");
  if (type == SemaphoreType::Binary && initialValue != 0)
    throw std::invalid_argument(
        "CreateSemaphore: binary semaphores must start unsignaled");
  if (nextSemaphoreHandle_ == kInvalidHandle)
    throw std::overflow_error("CreateSemaphore: semaphore handles exhausted");

  SemaphoreResource semaphore{.type = type};
  ThrowIfFailed(device_->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&semaphore.fence)),
                "CreateSemaphore: ID3D12Device::CreateFence");
  const SemaphoreHandle handle{nextSemaphoreHandle_};
  semaphores_.emplace(handle.id, std::move(semaphore));
  ++nextSemaphoreHandle_;
  return handle;
}

void Device::DestroySemaphore(SemaphoreHandle handle) {
  semaphores_.erase(handle.id);
}

void Device::SignalSemaphore(SemaphoreHandle handle, u64 value) {
  static_cast<void>(handle);
  static_cast<void>(value);
}

u64 Device::GetSemaphoreValue(SemaphoreHandle handle) const {
  const auto found = semaphores_.find(handle.id);
  if (found == semaphores_.end())
    throw std::invalid_argument("GetSemaphoreValue: invalid semaphore handle");
  if (found->second.type != SemaphoreType::Timeline)
    throw std::invalid_argument(
        "GetSemaphoreValue: semaphore is not a timeline");
  const u64 value = found->second.fence->GetCompletedValue();
  if (value == UINT64_MAX)
    throw std::runtime_error("GetSemaphoreValue: D3D12 device was removed");
  return value;
}

void Device::WaitSemaphore(SemaphoreHandle handle, u64 value,
                           u64 timeoutNanoseconds) {
  static_cast<void>(handle);
  static_cast<void>(value);
  static_cast<void>(timeoutNanoseconds);
}

ImageLayout Device::GetImageLayout(ImageHandle imageHandle,
                                   u32 mipLevel) const {
  const auto &image = GetImage(imageHandle);
  if (mipLevel >= image.mipLevels) {
    throw std::out_of_range("Image mip level is out of range");
  }
  return image.subresourceLayouts.empty() ? image.layout
                                          : image.subresourceLayouts[mipLevel];
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
  FrameContext &frame = frames_.at(currentFrame_);
  WaitForFenceValue(frame.fenceValue);
  graphicsBatchCursors_.at(currentFrame_) = 0;
  frame.ResetTransientResources();
  ThrowIfFailed(frame.allocator->Reset(), "ID3D12CommandAllocator::Reset");
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

ICommandList &Device::AcquireCommandList(QueueType type) {
  if (type == QueueType::Graphics) {
    if (!graphicsCommandPrepared_)
      throw std::logic_error(
          "Begin a D3D12 frame before acquiring graphics batches");
    auto &batches = graphicsBatches_.at(currentFrame_);
    auto &cursor = graphicsBatchCursors_.at(currentFrame_);
    if (cursor == batches.size()) {
      auto batch = std::make_unique<GraphicsBatch>();
      ThrowIfFailed(device_->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(&batch->resources.allocator)),
                    "Create graphics batch allocator");
      ThrowIfFailed(
          device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     batch->resources.allocator.Get(), nullptr,
                                     IID_PPV_ARGS(&batch->nativeCommands)),
          "Create graphics batch command list");
      batch->commands = std::make_unique<CommandList>(
          this, batch->nativeCommands.Get(), batch->resources.allocator.Get(),
          &batch->resources);
      batches.push_back(std::move(batch));
    }
    auto &batch = *batches.at(cursor);
    // BeginFrame waited for this frame slot, including all its batch
    // submissions.
    batch.resources.ResetTransientResources();
    ThrowIfFailed(batch.resources.allocator->Reset(),
                  "Reset graphics batch allocator");
    batch.prepared = true;
    ++cursor;
    return *batch.commands;
  }
  auto &queue = GetAuxiliaryQueue(type);
  if (queue.prepared)
    throw std::logic_error(
        "Submit the acquired D3D12 command list before acquiring another");
  if (queue.fence->GetCompletedValue() < queue.resources.fenceValue)
    ThrowIfFailed(
        queue.fence->SetEventOnCompletion(queue.resources.fenceValue, nullptr),
        "Wait for D3D12 queue allocator reuse");
  queue.resources.ResetTransientResources();
  ThrowIfFailed(queue.resources.allocator->Reset(),
                "Reset D3D12 queue allocator");
  queue.prepared = true;
  return *queue.commands;
}

void Device::Submit(QueueType type, ICommandList &commandList,
                    const SubmitDesc &desc) {
  ValidateTimelineSubmission(desc);

  if (type != QueueType::Graphics) {
    auto &queue = GetAuxiliaryQueue(type);

    if (!queue.prepared || &commandList != queue.commands.get())
      throw std::invalid_argument(
          "Command list does not belong to the requested queue");

    if (queue.commands->IsRecording())
      throw std::logic_error("End the command list before submission");

    for (auto &snapshot : queue.resources.descriptors) {
      if (snapshot->deferred)
        PopulateSnapshot(*snapshot);
    }

    QueueTimelineWaits(*queue.queue.Get(), desc);

    ID3D12CommandList *lists[]{queue.nativeCommands.Get()};
    queue.queue->ExecuteCommandLists(1, lists);

    QueueTimelineSignals(*queue.queue.Get(), desc);

    ThrowIfFailed(
        queue.queue->Signal(queue.fence.Get(), ++queue.resources.fenceValue),
        "Signal queue completion");

    queue.prepared = false;
    queue.commands->ApplySubmissionDecay();
    return;
  }

  if (!graphicsCommandPrepared_) {
    throw std::invalid_argument("Graphics command list is not prepared");
  }

  if (&commandList != graphicsCommandList_.get()) {
    for (auto &batch : graphicsBatches_.at(currentFrame_)) {
      if (batch->commands.get() != &commandList)
        continue;
      if (!batch->prepared)
        throw std::logic_error(
            "Graphics batch was not acquired or was already submitted");
      if (batch->commands->IsRecording())
        throw std::logic_error("End the graphics batch before submission");
      QueueTimelineWaits(*graphicsQueue_.Get(), desc);
      ExecuteGraphicsCommands(*batch->commands, batch->resources);
      QueueTimelineSignals(*graphicsQueue_.Get(), desc);
      const u64 completion = ++idleFenceValue_;
      ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), completion),
                    "Signal graphics batch completion");
      batch->resources.fenceValue = completion;
      frames_.at(currentFrame_).fenceValue = completion;
      batch->prepared = false;
      return;
    }
    throw std::invalid_argument(
        "Graphics command list does not belong to this frame");
  }

  ValidateMainGraphicsSubmission();

  QueueTimelineWaits(*graphicsQueue_.Get(), desc);

  ExecuteGraphicsCommandList();

  QueueTimelineSignals(*graphicsQueue_.Get(), desc);

  FrameContext &frame = frames_.at(currentFrame_);
  frame.fenceValue = ++idleFenceValue_;

  ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), frame.fenceValue),
                "Signal graphics completion");

  graphicsCommandPrepared_ = false;
}

ICommandList &Device::GetCommandList(QueueType type) {
  if (type != QueueType::Graphics) {
    auto &queue = GetAuxiliaryQueue(type);
    if (!queue.prepared)
      return AcquireCommandList(type);
    return *queue.commands;
  }
  if (!graphicsCommandPrepared_ || !graphicsCommandList_) {
    throw std::logic_error("D3D12 graphics command list is not prepared");
  }
  return *graphicsCommandList_;
}

void Device::Submit(QueueType type, const SubmitDesc &desc) {
  Submit(type, GetCommandList(type), desc);
}

void Device::SubmitAndPresent(SwapchainHandle swapchain,
                              const SubmitDesc &desc) {
  if (!swapchain_ || swapchain.id != swapchainHandle_.id) {
    throw std::invalid_argument("Invalid D3D12 swapchain handle");
  }
  ValidateTimelineSubmission(desc);
  ValidateMainGraphicsSubmission();
  QueueTimelineWaits(*graphicsQueue_.Get(), desc);
  ExecuteGraphicsCommandList();
  QueueTimelineSignals(*graphicsQueue_.Get(), desc);
  swapchain_->Present();

  FrameContext &frame = frames_.at(currentFrame_);
  frame.fenceValue = ++idleFenceValue_;
  ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), frame.fenceValue),
                "ID3D12CommandQueue::Signal");
  graphicsCommandPrepared_ = false;
  currentBackbufferIndex_ = swapchain_->GetCurrentBackbufferIndex();
  currentFrame_ = currentBackbufferIndex_;
}

void Device::WaitIdle() {
  // Submit all completion markers before waiting, so queue dependencies may
  // finish in any order. Waiting graphics alone cannot retire compute/copy
  // work.
  const u64 computeValue = ++computeQueueState_.resources.fenceValue;
  const u64 transferValue = ++transferQueueState_.resources.fenceValue;
  ThrowIfFailed(computeQueueState_.queue->Signal(computeQueueState_.fence.Get(),
                                                 computeValue),
                "Signal compute idle fence");
  ThrowIfFailed(transferQueueState_.queue->Signal(
                    transferQueueState_.fence.Get(), transferValue),
                "Signal transfer idle fence");
  const u64 fenceValue = ++idleFenceValue_;
  ThrowIfFailed(graphicsQueue_->Signal(idleFence_.Get(), fenceValue),
                "ID3D12CommandQueue::Signal");
  WaitForFenceValue(fenceValue);
  ThrowIfFailed(
      computeQueueState_.fence->SetEventOnCompletion(computeValue, nullptr),
      "Wait for compute idle");
  ThrowIfFailed(
      transferQueueState_.fence->SetEventOnCompletion(transferValue, nullptr),
      "Wait for transfer idle");
}

void Device::CollectGarbage() {}

std::unique_ptr<IUploadContext> Device::CreateUploadContext(
    u64 stagingBufferSize) {
  return std::make_unique<UploadContext>(*this, stagingBufferSize);
}

void Device::AcquireUploadedBuffers(
    std::span<const PendingBufferAcquire> pendingAcquires) {
  static_cast<void>(pendingAcquires);
}

void Device::AcquireUploadedImages(
    std::span<const PendingImageAcquire> pendingAcquires) {
  static_cast<void>(pendingAcquires);
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

void Device::CreateInstance(const DeviceDesc &desc) {
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
    ThrowIfFailed(candidate->GetDesc1(&description), "IDXGIAdapter1::GetDesc1");
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      continue;
    }

    if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
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

AuxiliaryQueue &Device::GetAuxiliaryQueue(QueueType type) {
  if (type == QueueType::Compute)
    return computeQueueState_;
  if (type == QueueType::Transfer)
    return transferQueueState_;
  throw std::invalid_argument("Expected a D3D12 compute or transfer queue");
}

void Device::CreateAuxiliaryQueue(AuxiliaryQueue &queue,
                                  D3D12_COMMAND_LIST_TYPE type) {
  D3D12_COMMAND_QUEUE_DESC desc{};
  desc.Type = type;
  ThrowIfFailed(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue.queue)),
                "Create D3D12 auxiliary queue");
  ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&queue.fence)),
                "Create D3D12 queue completion fence");
  ThrowIfFailed(device_->CreateCommandAllocator(
                    type, IID_PPV_ARGS(&queue.resources.allocator)),
                "Create D3D12 queue command allocator");
  ThrowIfFailed(
      device_->CreateCommandList(0, type, queue.resources.allocator.Get(),
                                 nullptr, IID_PPV_ARGS(&queue.nativeCommands)),
      "Create D3D12 queue command list");
  queue.commands = std::make_unique<CommandList>(
      this, queue.nativeCommands.Get(), queue.resources.allocator.Get(),
      &queue.resources);
  const bool compute = type == D3D12_COMMAND_LIST_TYPE_COMPUTE;
  SetDebugName(queue.queue.Get(),
               compute ? "Velos compute queue" : "Velos transfer queue");
}

void Device::ValidateTimelineSubmission(const SubmitDesc &desc) {
  const auto validate = [&](const TimelineSemaphorePoint &point) {
    const auto found = semaphores_.find(point.semaphore.id);
    if (found == semaphores_.end())
      throw std::invalid_argument(
          "Timeline submission references an invalid semaphore");
    if (found->second.type != SemaphoreType::Timeline)
      throw std::invalid_argument(
          "Timeline submission references a non-timeline semaphore");
    if (point.value == UINT64_MAX)
      throw std::invalid_argument(
          "Timeline submission uses reserved semaphore value UINT64_MAX");
  };

  for (const auto &wait : desc.waits)
    validate(wait);

  for (const auto &signal : desc.signals)
    validate(signal);
}

void Device::QueueTimelineWaits(ID3D12CommandQueue &queue,
                                const SubmitDesc &desc) {
  for (const auto &wait : desc.waits) {
    auto &semaphore = semaphores_.at(wait.semaphore.id);

    ThrowIfFailed(queue.Wait(semaphore.fence.Get(), wait.value),
                  "Queue timeline wait");
  }
}

void Device::QueueTimelineSignals(ID3D12CommandQueue &queue,
                                  const SubmitDesc &desc) {
  for (const auto &signal : desc.signals) {
    auto &semaphore = semaphores_.at(signal.semaphore.id);

    ThrowIfFailed(queue.Signal(semaphore.fence.Get(), signal.value),
                  "Queue timeline signal");
  }
}

void Device::CreateLogicalDevice() {
  ThrowIfFailed(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0,
                                  IID_PPV_ARGS(&device_)),
                "D3D12CreateDevice");

  D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
  ThrowIfFailed(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
                                             &options, sizeof(options)),
                "ID3D12Device::CheckFeatureSupport");
  tiledResourcesTier_ = options.TiledResourcesTier;

  if (options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED) {
    throw std::runtime_error("D3D12 device does not support tiled resources");
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  ThrowIfFailed(
      device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&graphicsQueue_)),
      "ID3D12Device::CreateCommandQueue");

  CreateAuxiliaryQueue(computeQueueState_, D3D12_COMMAND_LIST_TYPE_COMPUTE);
  CreateAuxiliaryQueue(transferQueueState_, D3D12_COMMAND_LIST_TYPE_COPY);
  computeQueue_ = computeQueueState_.queue;
  transferQueue_ = transferQueueState_.queue;

  rtvDescriptors_.Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                             kCpuDescriptorCapacity);
  dsvDescriptors_.Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                             kCpuDescriptorCapacity);
  resourceDescriptors_.Initialize(*device_.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                  kCpuDescriptorCapacity);
  samplerDescriptors_.Initialize(*device_.Get(),
                                 D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                 kCpuDescriptorCapacity);
  shaderResources_.Initialize(*device_.Get(),
                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000);
  shaderSamplers_.Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                             2048);
}

ID3D12CommandSignature *Device::GetMeshDispatchSignature(u32 stride) {
  if (stride < sizeof(D3D12_DISPATCH_MESH_ARGUMENTS) || stride % sizeof(u32))
    throw std::invalid_argument("Invalid mesh dispatch command stride");
  const auto existing = meshDispatchSignatures_.find(stride);
  if (existing != meshDispatchSignatures_.end())
    return existing->second.Get();

  D3D12_INDIRECT_ARGUMENT_DESC argument{};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
  D3D12_COMMAND_SIGNATURE_DESC desc{};
  desc.ByteStride = stride;
  desc.NumArgumentDescs = 1;
  desc.pArgumentDescs = &argument;
  ComPtr<ID3D12CommandSignature> signature;
  // No root arguments are changed, so this works with any mesh root signature.
  ThrowIfFailed(
      device_->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&signature)),
      "Create mesh dispatch command signature");
  return meshDispatchSignatures_.emplace(stride, std::move(signature))
      .first->second.Get();
}

void Device::CreateCommandObjects() {
  const u32 frameCount = swapchain_->GetBufferCount();
  if (frameCount == 0) {
    throw std::logic_error(
        "Cannot create command objects without swapchain buffers");
  }

  frames_.clear();
  frames_.resize(frameCount);
  graphicsBatches_.clear();
  graphicsBatches_.resize(frameCount);
  graphicsBatchCursors_.assign(frameCount, 0);
  for (FrameContext &frame : frames_) {
    ThrowIfFailed(
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&frame.allocator)),
        "ID3D12Device::CreateCommandAllocator");
  }

  ThrowIfFailed(
      device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
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
    const SwapchainImage &backbuffer = swapchain_->GetBackbuffer(index);
    const ImageHandle imageHandle{nextImageHandle_++};
    const ImageViewHandle viewHandle{nextImageViewHandle_++};

    images_.emplace(imageHandle.id,
                    Image{
                        .resourcePtr = backbuffer.resource.Get(),
                        .state = D3D12_RESOURCE_STATE_PRESENT,
                        .layout = ImageLayout::Present,
                        .format = swapchainFormat_,
                        .usage = ImageUsage::ColorAttachment,
                        .width = swapchain_->GetWidth(),
                        .height = swapchain_->GetHeight(),
                    });
    imageViews_.emplace(
        viewHandle.id,
        ImageView{
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
    throw std::logic_error(
        "D3D12 swapchain buffer count changed during resize");
  }

  for (u32 index = 0; index < swapchain_->GetBufferCount(); ++index) {
    const SwapchainImage &backbuffer = swapchain_->GetBackbuffer(index);
    Image &image = GetImage(swapchainImageHandles_[index]);
    image.resourcePtr = backbuffer.resource.Get();
    image.state = D3D12_RESOURCE_STATE_PRESENT;
    image.layout = ImageLayout::Present;
    image.subresourceStates.clear();
    image.subresourceLayouts.clear();
    image.format = swapchainFormat_;
    image.width = swapchain_->GetWidth();
    image.height = swapchain_->GetHeight();

    ImageView &view = imageViews_.at(swapchainImageViewHandles_[index].id);
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

void Device::ValidateMainGraphicsSubmission() const {
  if (!graphicsCommandPrepared_ || !graphicsCommandList_) {
    throw std::logic_error("D3D12 graphics command list is not prepared");
  }
  if (graphicsCommandList_->IsRecording()) {
    throw std::logic_error("End the D3D12 command list before submission");
  }

  for (const auto &batch : graphicsBatches_.at(currentFrame_)) {
    if (batch->prepared)
      throw std::logic_error(
          "Submit all acquired graphics batches before the main command list");
  }
}

void Device::ExecuteGraphicsCommandList() {
  ValidateMainGraphicsSubmission();
  ExecuteGraphicsCommands(*graphicsCommandList_, frames_.at(currentFrame_));
}

void Device::ExecuteGraphicsCommands(CommandList &commands,
                                     FrameContext &resources) {
  ID3D12CommandList *commandLists[]{commands.GetNative()};
  for (auto &snapshot : resources.descriptors)
    if (snapshot->deferred)
      PopulateSnapshot(*snapshot);
  SynchronizeMappedMirrors(true);
  graphicsQueue_->ExecuteCommandLists(1, commandLists);
  commands.ApplySubmissionDecay();
  SynchronizeMappedMirrors(false);
}

void Device::CreateSyncObjects() {
  ThrowIfFailed(
      device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence_)),
      "ID3D12Device::CreateFence");
  idleFenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!idleFenceEvent_) {
    throw std::runtime_error("CreateEventW failed for the D3D12 idle fence");
  }
}

}  // namespace Velos::D3D12
