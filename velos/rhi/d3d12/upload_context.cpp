#include "upload_context.h"
#include "rhi/d3d12/command_list.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace Velos::D3D12 {
using namespace Velos::RHI;

static u64 AlignUp(u64 value, u64 alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

UploadContext::UploadContext(Device &device, u64 size)
    : device_(device), capacity_(size) {
  if (!size)
    throw std::invalid_argument("Upload staging capacity must be nonzero");

  auto *nativeDevice = device_.GetNativeDevice();
  // TODO: Use a COPY queue for uploads once available.
  ThrowIfFailed(
      nativeDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&commandAllocator_)),
      "Create upload command allocator");
  ThrowIfFailed(nativeDevice->CreateCommandList(
                    0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(),
                    nullptr, IID_PPV_ARGS(&commandBuffer_)),
                "Create upload command list");
  ThrowIfFailed(nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                          IID_PPV_ARGS(&fence_)),
                "Create upload fence");
  cmd_ = std::make_unique<CommandList>(&device_, commandBuffer_.Get(),
                                       commandAllocator_.Get());

  stagingBuffer_ = device_.CreateBuffer({
      .size = size,
      .usage = BufferUsage::TransferSrc,
      .memoryUsage = MemoryUsage::CPUToGPU,
      .debugName = "Upload Staging Buffer",
  });
  try {
    // CPUToGPU chooses an UPLOAD heap; creating it does not map it.
    mappedPtr_ = static_cast<u8 *>(device_.MapBuffer(stagingBuffer_));
  } catch (...) {
    device_.DestroyBuffer(stagingBuffer_);
    stagingBuffer_ = {};
    throw;
  }
}

UploadContext::~UploadContext() {
  // ComPtr releases the fence and command list automatically.
  if (stagingBuffer_.IsValid()) {
    device_.UnmapBuffer(stagingBuffer_);
    device_.DestroyBuffer(stagingBuffer_);
  }
  for (const auto buffer : retiredStagingBuffers_)
    device_.DestroyBuffer(buffer);
}

void UploadContext::Begin() {
  if (cmd_->IsRecording())
    throw std::logic_error("Upload context is already recording");
  ThrowIfFailed(commandAllocator_->Reset(), "Reset upload command allocator");
  head_ = 0;
  cmd_->Begin();
}

u64 UploadContext::Allocate(u64 size, u64 alignment) {
  if (!cmd_->IsRecording())
    throw std::logic_error("Begin uploads before allocating staging memory");
  if (!alignment || (alignment & (alignment - 1)) ||
      head_ > UINT64_MAX - (alignment - 1))
    throw std::invalid_argument("Invalid staging alignment");
  u64 aligned = AlignUp(head_, alignment);
  if (aligned > capacity_ || size > capacity_ - aligned) {
    // Retain prior chunks: commands already recorded still reference them.
    const u64 nextCapacity = std::max(capacity_, size);
    const auto next =
        device_.CreateBuffer({.size = nextCapacity,
                              .usage = BufferUsage::TransferSrc,
                              .memoryUsage = MemoryUsage::CPUToGPU,
                              .debugName = "Upload Staging Buffer"});
    u8 *nextMapped = nullptr;
    try {
      nextMapped = static_cast<u8 *>(device_.MapBuffer(next));
      retiredStagingBuffers_.push_back(stagingBuffer_);
    } catch (...) {
      device_.DestroyBuffer(next);
      throw;
    }
    stagingBuffer_ = next;
    mappedPtr_ = nextMapped;
    capacity_ = nextCapacity;
    aligned = 0;
  }
  head_ = aligned + size;
  return aligned;
}

void UploadContext::UploadBuffer(const BufferUploadDesc &desc) {
  if (!desc.data || desc.size == 0) {
    return;
  }

  u64 offset = Allocate(desc.size, 16);
  std::memcpy(mappedPtr_ + offset, desc.data, desc.size);

  cmd_->Barrier(BufferBarrier{
      .buffer = desc.dstBuffer,
      .newState = ResourceState::TransferDst,
  });

  cmd_->CopyBuffer(stagingBuffer_, desc.dstBuffer,
                   BufferCopyRegion{
                       .srcOffset = offset,
                       .dstOffset = desc.dstOffset,
                       .size = desc.size,
                   });

  cmd_->Barrier(BufferBarrier{
      .buffer = desc.dstBuffer,
      .oldState = ResourceState::TransferDst,
      .newState = desc.finalState,
  });
}

void UploadContext::UploadImage(const ImageUploadDesc &desc, const void *data,
                                u64 dataSize) {
  if (!data || dataSize == 0) {
    return;
  }

  const auto &image = device_.GetImage(desc.dstImage);
  if (image.format != Format::RGBA8_UNORM &&
      image.format != Format::RGBA8_SRGB &&
      image.format != Format::BGRA8_UNORM && image.format != Format::BGRA8_SRGB)
    throw std::invalid_argument(
        "UploadImage currently supports RGBA8/BGRA8 color images");
  if (desc.aspect != ImageAspect::Color || !desc.width || !desc.height ||
      desc.depth != 1 || !desc.layerCount || desc.mipLevel >= image.mipLevels ||
      desc.baseArrayLayer >= image.arrayLayers ||
      desc.layerCount > image.arrayLayers - desc.baseArrayLayer ||
      desc.width > std::max(1u, image.width >> desc.mipLevel) ||
      desc.height > std::max(1u, image.height >> desc.mipLevel))
    throw std::invalid_argument("Invalid upload image region");
  const u64 sourceRowBytes =
      static_cast<u64>(desc.bufferRowLength ? desc.bufferRowLength
                                            : desc.width) *
      4;
  const u64 sourceRows =
      desc.bufferImageHeight ? desc.bufferImageHeight : desc.height;
  if (sourceRowBytes < desc.width * 4ull || sourceRows < desc.height ||
      sourceRowBytes > UINT64_MAX / sourceRows)
    throw std::invalid_argument("Invalid source image strides");
  const u64 sourceLayerBytes = sourceRowBytes * sourceRows;
  const u64 lastLayerBytes =
      sourceRowBytes * (desc.height - 1) + desc.width * 4ull;
  if (lastLayerBytes > dataSize ||
      desc.layerCount - 1 > (dataSize - lastLayerBytes) / sourceLayerBytes)
    throw std::invalid_argument("Upload image data is too small");
  const u64 rowPitch =
      AlignUp(desc.width * 4ull, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
  // Even row counts make each array-layer offset 512-byte aligned too.
  const u32 paddedRows = (desc.height + 1) & ~1u;
  const u64 layerBytes = rowPitch * paddedRows;
  const u64 offset = Allocate(layerBytes * desc.layerCount,
                              D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
  for (u32 layer = 0; layer < desc.layerCount; ++layer)
    for (u32 row = 0; row < desc.height; ++row)
      std::memcpy(mappedPtr_ + offset + layer * layerBytes + row * rowPitch,
                  static_cast<const u8 *>(data) + layer * sourceLayerBytes +
                      row * sourceRowBytes,
                  static_cast<size_t>(desc.width) * 4);

  cmd_->Barrier(ImageBarrier{
      .image = desc.dstImage,
      .oldLayout = desc.oldLayout,
      .newLayout = ImageLayout::TransferDst,
      .aspect = desc.aspect,
      .baseMipLevel = desc.mipLevel,
      .mipLevelCount = 1,
      .baseArrayLayer = desc.baseArrayLayer,
      .layerCount = desc.layerCount,
  });

  BufferImageCopyRegion region{};
  region.bufferOffset = offset;
  region.bufferRowLength = static_cast<u32>(rowPitch / 4);
  region.bufferImageHeight = paddedRows;
  region.mipLevel = desc.mipLevel;
  region.baseArrayLayer = desc.baseArrayLayer;
  region.layerCount = desc.layerCount;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {desc.width, desc.height, desc.depth};
  region.aspect = desc.aspect;

  cmd_->CopyBufferToImage(stagingBuffer_, desc.dstImage, region);

  cmd_->Barrier(ImageBarrier{
      .image = desc.dstImage,
      .oldLayout = ImageLayout::TransferDst,
      .newLayout = desc.finalLayout,
      .oldState = ResourceState::TransferDst,
      .newState = ResourceState::TransferDst,
      .useExplicitStates = false,
      .aspect = desc.aspect,
      .srcQueueFamilyIndex = INT_MAX,
      .dstQueueFamilyIndex = INT_MAX,
      .baseMipLevel = desc.mipLevel,
      .mipLevelCount = 1,
      .baseArrayLayer = desc.baseArrayLayer,
      .layerCount = desc.layerCount,
  });
}

void UploadContext::Flush() {
  cmd_->End();

  ComPtr<ID3D12CommandQueue> queue = device_.GetGraphicsQueue();

  ID3D12CommandList *lists[] = {commandBuffer_.Get()};
  queue->ExecuteCommandLists(1, lists);

  const u64 value = ++fenceValue_;

  ThrowIfFailed(queue->Signal(fence_.Get(), value), "Signal upload fence");

  if (fence_->GetCompletedValue() < value) {
    ThrowIfFailed(fence_->SetEventOnCompletion(value, nullptr),
                  "SetEventOnCompletion upload fence");
  }
  // DEFAULT buffers decay to COMMON at the ExecuteCommandLists boundary.
  for (auto &[id, buffer] : device_.buffers_)
    if (buffer.heapType == D3D12_HEAP_TYPE_DEFAULT)
      buffer.state = D3D12_RESOURCE_STATE_COMMON;
  for (const auto buffer : retiredStagingBuffers_)
    device_.DestroyBuffer(buffer);
  retiredStagingBuffers_.clear();
}

std::vector<PendingImageAcquire> UploadContext::TakePendingImageAcquires() {
  std::vector<PendingImageAcquire> pending;
  pending.swap(pendingImageAcquires_);
  return pending;
}

std::vector<PendingBufferAcquire> UploadContext::TakePendingBufferAcquires() {
  std::vector<PendingBufferAcquire> pending;
  pending.swap(pendingBufferAcquires_);
  return pending;
}

}  // namespace Velos::D3D12
