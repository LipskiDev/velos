#include "command_list.h"
#include "device.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Velos::D3D12 {
using namespace Velos::RHI;

CommandList::CommandList(Device *device, ID3D12GraphicsCommandList *commandList,
                         ID3D12CommandAllocator *commandAllocator,
                         FrameContext *resources)
    : resources_(resources),
      device_(device),
      commandList_(commandList),
      commandAllocator_(commandAllocator) {
  if (!device_ || !commandList_ || !commandAllocator_) {
    throw std::invalid_argument("CommandList requires valid D3D12 objects");
  }
  ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");
}

FrameContext &CommandList::RetainedResources() {
  return resources_ ? *resources_ : device_->frames_.at(device_->currentFrame_);
}

void CommandList::ValidateQueueState(D3D12_RESOURCE_STATES state) const {
  const auto type = commandList_->GetType();
  const UINT copyStates =
      D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_COPY_DEST;
  const UINT computeStates = copyStates |
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                             D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  if ((type == D3D12_COMMAND_LIST_TYPE_COPY && (state & ~copyStates)) ||
      (type == D3D12_COMMAND_LIST_TYPE_COMPUTE && (state & ~computeStates)))
    throw std::invalid_argument(
        "Resource state is unsupported on this D3D12 queue; release it on the "
        "producer queue first");
}

void CommandList::ApplySubmissionDecay() {
  for (const auto id : touchedBuffers_) {
    auto &buffer = device_->buffers_.at(id);
    if (buffer.heapType == D3D12_HEAP_TYPE_DEFAULT)
      buffer.state = D3D12_RESOURCE_STATE_COMMON;
  }
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COPY) {
    for (const auto id : touchedImages_) {
      auto &image = device_->images_.at(id);
      image.state = D3D12_RESOURCE_STATE_COMMON;
      image.layout = ImageLayout::Undefined;
      image.subresourceStates.clear();
      image.subresourceLayouts.clear();
    }
  }
}

void CommandList::ReleaseImagesToCommon() {
  // Unlike buffers, explicitly transitioned textures do not decay on submit.
  // Release them on their producer queue so another queue can acquire them.
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COPY)
    return;
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  for (const auto id : touchedImages_) {
    auto &image = device_->images_.at(id);
    image.InitializeSubresources();
    for (u32 subresource = 0; subresource < image.subresourceStates.size();
         ++subresource) {
      auto &state = image.subresourceStates[subresource];
      if (state != D3D12_RESOURCE_STATE_COMMON) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {image.resourcePtr, subresource, state,
                              D3D12_RESOURCE_STATE_COMMON};
        barriers.push_back(barrier);
        state = D3D12_RESOURCE_STATE_COMMON;
      }
    }
    image.state = D3D12_RESOURCE_STATE_COMMON;
    // Report COMMON as Undefined so the next use emits an acquire transition,
    // including depth textures which cannot implicitly promote to shader reads.
    image.layout = ImageLayout::Undefined;
    std::fill(image.subresourceLayouts.begin(), image.subresourceLayouts.end(),
              ImageLayout::Undefined);
  }
  if (!barriers.empty())
    commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                  barriers.data());
}

void CommandList::SetAllocator(ID3D12CommandAllocator *commandAllocator) {
  if (!commandAllocator) {
    throw std::invalid_argument("Command allocator must not be null");
  }
  if (isRecording_) {
    throw std::logic_error("Cannot replace an allocator while recording");
  }
  commandAllocator_ = commandAllocator;
}

void CommandList::Begin() {
  if (isRecording_) {
    throw std::logic_error("Command list is already recording");
  }

  ThrowIfFailed(commandList_->Reset(commandAllocator_, nullptr),
                "ID3D12GraphicsCommandList::Reset");

  isRendering_ = false;
  touchedBuffers_.clear();
  touchedImages_.clear();
  boundPipeline_ = {};
  boundComputePipeline_ = {};
  heapsBound_ = false;
  descriptorCache_.clear();
  computeActive_ = false;
  graphicsSets_.clear();
  computeSets_.clear();
  boundGraphicsSets_.clear();
  boundComputeSets_.clear();
  for (auto &data : pushData_)
    data.clear();
  vertexBindings_ = {};
  indexBinding_ = {};
  isRecording_ = true;
}

void CommandList::End() {
  if (!isRecording_) {
    throw std::logic_error("Command list is not recording");
  }

  if (isRendering_) {
    throw std::logic_error("Cannot end command list while rendering is active");
  }

  ReleaseImagesToCommon();
  ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

  isRecording_ = false;
  descriptorCache_.clear();
}

void CommandList::SetViewport(const Viewport &viewport) {
  const D3D12_VIEWPORT nativeViewport{
      .TopLeftX = viewport.x,
      .TopLeftY = viewport.y,
      .Width = viewport.width,
      .Height = viewport.height,
      .MinDepth = viewport.minDepth,
      .MaxDepth = viewport.maxDepth,
  };
  commandList_->RSSetViewports(1, &nativeViewport);
}

void CommandList::SetScissor(const Rect2D &scissor) {
  const LONG left = static_cast<LONG>(scissor.offset.x);
  const LONG top = static_cast<LONG>(scissor.offset.y);
  const D3D12_RECT nativeScissor{
      .left = left,
      .top = top,
      .right = left + static_cast<LONG>(scissor.extent.width),
      .bottom = top + static_cast<LONG>(scissor.extent.height),
  };
  commandList_->RSSetScissorRects(1, &nativeScissor);
}

void CommandList::Barrier(const BufferBarrier &barrier) {
  auto &buffer = device_->buffers_.at(barrier.buffer.id);
  if (buffer.heapType != D3D12_HEAP_TYPE_DEFAULT)
    return;
  touchedBuffers_.insert(barrier.buffer.id);
  auto after = ToResourceState(barrier.newState);
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    after &= ~D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  ValidateQueueState(buffer.state);
  ValidateQueueState(after);
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COPY) {
    // COPY queues use COMMON with implicit copy-state promotion.
    buffer.state = after;
    return;
  }
  D3D12_RESOURCE_BARRIER native{};
  if (buffer.state != after) {
    native.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    native.Transition = {buffer.resourcePtr,
                         D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, buffer.state,
                         after};
    commandList_->ResourceBarrier(1, &native);
  } else if (after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    native.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    native.UAV.pResource = buffer.resourcePtr;
    commandList_->ResourceBarrier(1, &native);
  }
  buffer.state = after;
}

void CommandList::Barrier(const ImageBarrier &barrier) {
  if (!isRecording_ || isRendering_)
    throw std::logic_error(
        "Image barriers require recording outside rendering");
  Image &image = device_->GetImage(barrier.image);
  if (!barrier.mipLevelCount || !barrier.layerCount ||
      barrier.baseMipLevel >= image.mipLevels ||
      barrier.mipLevelCount > image.mipLevels - barrier.baseMipLevel ||
      barrier.baseArrayLayer >= image.arrayLayers ||
      barrier.layerCount > image.arrayLayers - barrier.baseArrayLayer)
    throw std::invalid_argument("Image barrier subresource range is invalid");
  D3D12_RESOURCE_STATES after = barrier.useExplicitStates
                                    ? ToResourceState(barrier.newState)
                                    : ToResourceState(barrier.newLayout);
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    after &= ~D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  ValidateQueueState(after);
  image.InitializeSubresources();
  u32 firstPlane = 0;
  u32 planeCount = 1;
  if (image.format == Format::D24_UNORM_S8_UINT) {
    const bool depth = HasFlag(barrier.aspect, ImageAspect::Depth);
    const bool stencil = HasFlag(barrier.aspect, ImageAspect::Stencil);
    firstPlane = stencil && !depth ? 1 : 0;
    planeCount = depth && stencil ? 2 : 1;
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  bool needsUavBarrier = false;
  for (u32 plane = firstPlane; plane < firstPlane + planeCount; ++plane)
    for (u32 layer = barrier.baseArrayLayer;
         layer < barrier.baseArrayLayer + barrier.layerCount; ++layer)
      for (u32 mip = barrier.baseMipLevel;
           mip < barrier.baseMipLevel + barrier.mipLevelCount; ++mip) {
        const u32 subresource = mip + layer * image.mipLevels +
                                plane * image.mipLevels * image.arrayLayers;
        // Generic RHI oldState may include stages unavailable on this queue.
        // Use the actual native state, including earlier per-mip transitions.
        const auto before = image.subresourceStates[subresource];
        ValidateQueueState(before);
        if (commandList_->GetType() != D3D12_COMMAND_LIST_TYPE_COPY) {
          if (before != after) {
            D3D12_RESOURCE_BARRIER native{};
            native.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            native.Transition = {image.resourcePtr, subresource, before, after};
            barriers.push_back(native);
          } else if (after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            needsUavBarrier = true;
          }
        }
        image.subresourceStates[subresource] = after;
        image.subresourceLayouts[subresource] = barrier.newLayout;
      }
  if (needsUavBarrier) {
    D3D12_RESOURCE_BARRIER native{};
    native.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    native.UAV.pResource = image.resourcePtr;
    barriers.push_back(native);
  }
  if (!barriers.empty())
    commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                  barriers.data());
  touchedImages_.insert(barrier.image.id);
  RetainedResources().objects.emplace_back(image.resourcePtr);
  image.state = after;
  image.layout = barrier.newLayout;
}

void CommandList::UpdateBuffer(const BufferUpdateDesc &update) {
  if (!isRecording_ || isRendering_)
    throw std::logic_error("UpdateBuffer requires recording outside rendering");
  auto &buffer = device_->buffers_.at(update.buffer.id);
  if (!update.data || !update.size || update.offset > buffer.size ||
      update.size > buffer.size - update.offset)
    throw std::invalid_argument("Invalid buffer update");
  if (buffer.heapType != D3D12_HEAP_TYPE_DEFAULT) {
    const bool wasMapped = buffer.mapped != nullptr;
    auto *mapped = static_cast<std::byte *>(device_->MapBuffer(update.buffer));
    std::memcpy(mapped + update.offset, update.data,
                static_cast<size_t>(update.size));
    if (!wasMapped)
      device_->UnmapBuffer(update.buffer);
    return;
  }
  if (buffer.memoryUsage == MemoryUsage::GPUOnly &&
      !HasFlag(buffer.usage, BufferUsage::TransferDst))
    throw std::invalid_argument("GPU buffer updates require TransferDst usage");
  auto staging = device_->CreateStagingBuffer(update.size, update.data);
  const auto previous = buffer.state;
  Barrier(BufferBarrier{.buffer = update.buffer,
                        .newState = ResourceState::TransferDst});
  commandList_->CopyBufferRegion(buffer.resourcePtr, update.offset,
                                 staging.Get(), 0, update.size);
  if (previous != buffer.state) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {buffer.resourcePtr,
                          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, buffer.state,
                          previous};
    commandList_->ResourceBarrier(1, &barrier);
    buffer.state = previous;
  }
  RetainedResources().staging.push_back(std::move(staging));
}

void CommandList::ResetQueryPool(QueryPoolHandle pool, u32 firstQuery,
                                 u32 queryCount) {
  static_cast<void>(pool);
  static_cast<void>(firstQuery);
  static_cast<void>(queryCount);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (ResetQueryPool)");
}

void CommandList::WriteTimestamp(QueryPoolHandle pool, u32 queryIndex,
                                 ShaderStage completedStage) {
  static_cast<void>(pool);
  static_cast<void>(queryIndex);
  static_cast<void>(completedStage);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (WriteTimestamp)");
}

void CommandList::BeginRendering(const RenderingInfo &renderingInfo) {
  if (commandList_->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
    throw std::logic_error("Rendering requires a D3D12 graphics command list");
  if (!isRecording_) {
    throw std::logic_error("Command list is not recording");
  }
  if (isRendering_) {
    throw std::logic_error("Rendering is already active");
  }
  if (renderingInfo.colorAttachmentCount >
      D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
    throw std::invalid_argument("Too many D3D12 color attachments");
  }
  if (renderingInfo.colorAttachmentCount != 0 &&
      !renderingInfo.colorAttachments) {
    throw std::invalid_argument("Color attachment array is null");
  }
  if (renderingInfo.colorAttachmentCount == 0 &&
      !renderingInfo.depthAttachment) {
    throw std::invalid_argument("BeginRendering requires an attachment");
  }

  storeDiscardResourceCount_ = 0;
  const auto queueStoreDiscard = [this](ID3D12Resource *resource) {
    storeDiscardResources_[storeDiscardResourceCount_++] = resource;
  };

  std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
             D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT>
      renderTargets{};
  for (u32 index = 0; index < renderingInfo.colorAttachmentCount; ++index) {
    const ColorAttachmentDesc &attachment =
        renderingInfo.colorAttachments[index];
    const ImageView &view = device_->GetImageView(attachment.view);
    if (!view.descriptor.handle.ptr ||
        view.descriptorType != ImageViewDescriptorType::RenderTarget) {
      throw std::invalid_argument("Color attachment does not reference an RTV");
    }
    renderTargets[index] = view.descriptor.handle;
    const Image &image = device_->GetImage(view.image);

    if (attachment.loadOp == LoadOp::Clear) {
      const float clearColor[]{
          attachment.clearValue.r,
          attachment.clearValue.g,
          attachment.clearValue.b,
          attachment.clearValue.a,
      };
      commandList_->ClearRenderTargetView(view.descriptor.handle, clearColor, 0,
                                          nullptr);
    } else if (attachment.loadOp == LoadOp::DontCare) {
      commandList_->DiscardResource(image.resourcePtr, nullptr);
    }
    if (attachment.storeOp == StoreOp::DontCare) {
      queueStoreDiscard(image.resourcePtr);
    }
  }

  D3D12_CPU_DESCRIPTOR_HANDLE depthStencilHandle{};
  const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencil = nullptr;
  if (renderingInfo.depthAttachment) {
    const DepthAttachmentDesc &attachment = *renderingInfo.depthAttachment;
    const ImageView &view = device_->GetImageView(attachment.view);
    if (!view.descriptor.handle.ptr ||
        view.descriptorType != ImageViewDescriptorType::DepthStencil) {
      throw std::invalid_argument("Depth attachment does not reference a DSV");
    }

    const Image &image = device_->GetImage(view.image);
    depthStencilHandle = view.descriptor.handle;
    depthStencil = &depthStencilHandle;
    if (attachment.loadOp == LoadOp::Clear) {
      D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
      if (HasFlag(view.aspect, ImageAspect::Stencil)) {
        clearFlags = static_cast<D3D12_CLEAR_FLAGS>(clearFlags |
                                                    D3D12_CLEAR_FLAG_STENCIL);
      }
      commandList_->ClearDepthStencilView(
          view.descriptor.handle, clearFlags, attachment.clearDepth,
          static_cast<UINT8>(attachment.clearStencil), 0, nullptr);
    } else if (attachment.loadOp == LoadOp::DontCare) {
      commandList_->DiscardResource(image.resourcePtr, nullptr);
    }
    if (attachment.storeOp == StoreOp::DontCare) {
      queueStoreDiscard(image.resourcePtr);
    }
  }

  commandList_->OMSetRenderTargets(
      renderingInfo.colorAttachmentCount,
      renderingInfo.colorAttachmentCount ? renderTargets.data() : nullptr,
      FALSE, depthStencil);
  isRendering_ = true;
}

void CommandList::EndRendering() {
  if (!isRendering_) {
    throw std::logic_error("Rendering is not active");
  }
  for (u32 index = 0; index < storeDiscardResourceCount_; ++index) {
    commandList_->DiscardResource(storeDiscardResources_[index], nullptr);
  }
  storeDiscardResourceCount_ = 0;
  isRendering_ = false;
}

void CommandList::BindPipeline(PipelineHandle pipeline) {
  if (commandList_->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
    throw std::logic_error(
        "Graphics pipelines require a D3D12 graphics command list");
  if (!isRecording_)
    throw std::logic_error("BindPipeline requires recording");
  const auto &native = device_->GetPipeline(pipeline);
  if (native.compute)
    throw std::invalid_argument("Expected a graphics pipeline");
  const bool changed = boundPipeline_.id != pipeline.id;
  if (!changed && !computeActive_)
    return;
  boundPipeline_ = pipeline;
  computeActive_ = false;
  if (changed) {
    auto &frame = RetainedResources();
    frame.objects.emplace_back(native.state);
    frame.objects.emplace_back(native.rootSignature);
    graphicsSets_.assign(native.sets.size(), {});
    boundGraphicsSets_.assign(native.sets.size(), {});
    for (u32 stage = 0; stage < 5; ++stage)
      if (stage != 2)
        pushData_[stage].assign(native.pushSizes[stage], std::byte{});
  }
  if (changed)
    commandList_->SetGraphicsRootSignature(native.rootSignature.Get());
  commandList_->SetPipelineState(native.state.Get());
  if (!native.mesh)
    commandList_->IASetPrimitiveTopology(native.topology);
  const float
      blendConstants[4]{};  // Vulkan's current fixed blend constants are zero.
  commandList_->OMSetBlendFactor(blendConstants);
  for (u32 slot = 0; slot < native.vertexStrides.size(); ++slot)
    if (vertexBindings_[slot].buffer.IsValid())
      ApplyVertexBinding(slot);
  if (changed)
    for (u32 stage = 0; stage < 5; ++stage)
      if (stage != 2 && native.pushSizes[stage])
        PushConstants(static_cast<ShaderStage>(1u << stage), 0,
                      native.pushSizes[stage], pushData_[stage].data());
}

void CommandList::BindComputePipeline(PipelineHandle pipeline) {
  if (commandList_->GetType() == D3D12_COMMAND_LIST_TYPE_COPY)
    throw std::logic_error(
        "Compute pipelines cannot be bound on a D3D12 copy command list");
  if (!isRecording_ || isRendering_)
    throw std::logic_error(
        "BindComputePipeline requires recording outside rendering");
  const auto &native = device_->GetPipeline(pipeline);
  if (!native.compute)
    throw std::invalid_argument("Expected a compute pipeline");
  const bool changed = boundComputePipeline_.id != pipeline.id;
  if (!changed && computeActive_)
    return;
  boundComputePipeline_ = pipeline;
  computeActive_ = true;
  if (changed) {
    computeSets_.assign(native.sets.size(), {});
    boundComputeSets_.assign(native.sets.size(), {});
    pushData_[2].assign(native.pushSizes[2], std::byte{});
    auto &frame = RetainedResources();
    frame.objects.emplace_back(native.state);
    frame.objects.emplace_back(native.rootSignature);
    commandList_->SetComputeRootSignature(native.rootSignature.Get());
  }
  commandList_->SetPipelineState(native.state.Get());
  if (changed && native.pushSizes[2])
    PushConstants(ShaderStage::Compute, 0, native.pushSizes[2],
                  pushData_[2].data());
}

void CommandList::BindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) {
  const auto &resource = device_->GetBuffer(buffer);
  if (slot >= vertexBindings_.size() || offset >= resource.size ||
      resource.size - offset > UINT_MAX ||
      !HasFlag(resource.usage, BufferUsage::Vertex) ||
      resource.heapType == D3D12_HEAP_TYPE_READBACK)
    throw std::invalid_argument("Invalid vertex buffer binding");
  if (vertexBindings_[slot].buffer.id == buffer.id &&
      vertexBindings_[slot].offset == offset)
    return;
  vertexBindings_[slot] = {buffer, offset, resource.size - offset};
  if (boundPipeline_.IsValid() &&
      slot < device_->GetPipeline(boundPipeline_).vertexStrides.size())
    ApplyVertexBinding(slot);
}

void CommandList::ApplyVertexBinding(u32 slot) {
  const auto buffer = vertexBindings_[slot].buffer;
  const auto offset = vertexBindings_[slot].offset;
  const auto &resource = device_->GetBuffer(buffer);
  const auto &pipeline = device_->GetPipeline(boundPipeline_);
  if (offset >= resource.size || slot >= pipeline.vertexStrides.size())
    throw std::invalid_argument("Invalid vertex buffer offset or slot");
  D3D12_VERTEX_BUFFER_VIEW view{
      resource.ownedResource->GetGPUVirtualAddress() + offset,
      static_cast<UINT>(resource.size - offset), pipeline.vertexStrides[slot]};
  commandList_->IASetVertexBuffers(slot, 1, &view);
}

void CommandList::BindIndexBuffer(BufferHandle buffer, IndexType indexType,
                                  u64 offset) {
  static_cast<void>(buffer);
  static_cast<void>(indexType);
  static_cast<void>(offset);

  const auto &resource = device_->GetBuffer(buffer);
  const u32 alignment = indexType == IndexType::U32 ? 4 : 2;
  if (offset >= resource.size || offset % alignment ||
      resource.size - offset > UINT_MAX ||
      !HasFlag(resource.usage, BufferUsage::Index) ||
      resource.heapType == D3D12_HEAP_TYPE_READBACK)
    throw std::invalid_argument("Invalid index buffer offset");
  D3D12_INDEX_BUFFER_VIEW view{
      resource.ownedResource->GetGPUVirtualAddress() + offset,
      static_cast<UINT>(resource.size - offset),
      indexType == IndexType::U32 ? DXGI_FORMAT_R32_UINT
                                  : DXGI_FORMAT_R16_UINT};
  if (indexBinding_.BufferLocation == view.BufferLocation &&
      indexBinding_.SizeInBytes == view.SizeInBytes &&
      indexBinding_.Format == view.Format)
    return;
  commandList_->IASetIndexBuffer(&view);
  indexBinding_ = view;
}

void CommandList::BindUniformBuffer(u32 binding, BufferHandle buffer,
                                    u64 offset, u64 size) {
  const auto &pipeline = device_->GetPipeline(boundPipeline_);
  if (pipeline.sets.empty())
    throw std::invalid_argument("Direct uniform binding requires set zero");
  auto set = graphicsSets_[0] ? std::make_shared<BindingSet>(*graphicsSets_[0])
                              : std::make_shared<BindingSet>();
  if (set->bindings.empty()) {
    set->bindings = pipeline.sets[0].bindings;
    for (auto desc : set->bindings)
      set->values[desc.binding].resize(desc.count);
  }
  const BindingSetHandle handle{device_->nextBindingSetHandle_++};
  device_->bindingSets_.emplace(handle.id, set);
  try {
    const BindingBufferInfo info{buffer, offset, size};
    device_->UpdateBindingSet({.dstSet = handle,
                               .binding = binding,
                               .type = BindingType::UniformBuffer,
                               .bufferInfo = &info});
    BindSet(boundPipeline_, 0, handle, false);
  } catch (...) {
    device_->bindingSets_.erase(handle.id);
    throw;
  }
  device_->bindingSets_.erase(handle.id);
}

void CommandList::SetBindings(PipelineHandle pipeline, u32 setIndex,
                              BindingSetHandle descriptorSet) {
  BindSet(pipeline, setIndex, descriptorSet, false);
}

void CommandList::SetComputeBindings(PipelineHandle pipeline, u32 setIndex,
                                     BindingSetHandle descriptorSet) {
  BindSet(pipeline, setIndex, descriptorSet, true);
}

void CommandList::BindSet(PipelineHandle handle, u32 index,
                          BindingSetHandle set, bool compute) {
  if (!isRecording_ || compute != computeActive_ ||
      handle.id != (compute ? boundComputePipeline_ : boundPipeline_).id)
    throw std::logic_error(
        "Descriptor binding requires the matching bound pipeline");
  const auto &pipeline = device_->GetPipeline(handle);
  if (pipeline.compute != compute || index >= pipeline.sets.size())
    throw std::invalid_argument("Invalid pipeline set index");
  const auto &layout = pipeline.sets[index];
  const auto revision = device_->GetBindingSet(set).revision;
  auto &bound = (compute ? boundComputeSets_ : boundGraphicsSets_)[index];
  if (bound.handle.id == set.id && bound.revision == revision)
    return;
  auto &candidates = descriptorCache_[{set.id, revision}];
  const auto sameBinding = [](const BindingDesc &a, const BindingDesc &b) {
    return a.binding == b.binding && a.type == b.type && a.count == b.count &&
           a.visibility == b.visibility && a.flags == b.flags;
  };
  const auto sameEntry = [&](const TableEntry &a, const TableEntry &b) {
    return sameBinding(a.binding, b.binding) && a.type == b.type &&
           a.offset == b.offset && a.stride == b.stride && a.used == b.used &&
           a.imageType == b.imageType && a.requiredCount == b.requiredCount &&
           a.runtimeArray == b.runtimeArray && a.scalarType == b.scalarType;
  };
  auto cached = std::find_if(
      candidates.begin(), candidates.end(), [&](const auto &value) {
        const auto &other = value->layout;
        return other.resources == layout.resources &&
               other.samplers == layout.samplers &&
               std::equal(other.bindings.begin(), other.bindings.end(),
                          layout.bindings.begin(), layout.bindings.end(),
                          sameBinding) &&
               std::equal(other.entries.begin(), other.entries.end(),
                          layout.entries.begin(), layout.entries.end(),
                          sameEntry);
      });
  if (cached == candidates.end()) {
    auto snapshot = device_->SnapshotBindings(layout, set);
    RetainedResources().descriptors.push_back(snapshot);
    candidates.push_back(std::move(snapshot));
    cached = std::prev(candidates.end());
  }
  const auto &snapshot = *cached;
  if (!heapsBound_) {
    ID3D12DescriptorHeap *heaps[]{device_->shaderResources_.Heap(),
                                  device_->shaderSamplers_.Heap()};
    commandList_->SetDescriptorHeaps(2, heaps);
    heapsBound_ = true;
  }
  auto table = [&](u32 root, D3D12_GPU_DESCRIPTOR_HANDLE pointer) {
    if (compute)
      commandList_->SetComputeRootDescriptorTable(root, pointer);
    else
      commandList_->SetGraphicsRootDescriptorTable(root, pointer);
  };
  if (layout.resourceRoot != UINT_MAX)
    table(layout.resourceRoot,
          device_->shaderResources_.Gpu(snapshot->resourceStart));
  if (layout.samplerRoot != UINT_MAX)
    table(layout.samplerRoot,
          device_->shaderSamplers_.Gpu(snapshot->samplerStart));
  (compute ? computeSets_ : graphicsSets_)[index] =
      device_->bindingSets_.at(set.id);
  bound = {set, revision};
}

void CommandList::GenerateMipmaps(ImageHandle imageHandle, uint32_t width,
                                  uint32_t height, uint32_t mipLevels,
                                  uint32_t arrayLayers) {
  static_cast<void>(imageHandle);
  static_cast<void>(width);
  static_cast<void>(height);
  static_cast<void>(mipLevels);
  static_cast<void>(arrayLayers);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (GenerateMipmaps)");
}

void CommandList::BlitMip(ImageHandle image, uint32_t width, uint32_t height,
                          uint32_t srcMip, uint32_t dstMip,
                          uint32_t arrayLayers) {
  static_cast<void>(image);
  static_cast<void>(width);
  static_cast<void>(height);
  static_cast<void>(srcMip);
  static_cast<void>(dstMip);
  static_cast<void>(arrayLayers);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (BlitMip)");
}

void CommandList::PushConstants(ShaderStage stages, u32 offset, u32 size,
                                const void *data) {
  if (!isRecording_ || !data || !size || offset % 4 || size % 4 ||
      stages == ShaderStage::None || (static_cast<u32>(stages) & ~31u))
    throw std::invalid_argument("Invalid push constant write");
  for (u32 stage = 0; stage < 5; ++stage)
    if (static_cast<u32>(stages) & (1u << stage)) {
      const auto &pipeline = device_->GetPipeline(
          stage == 2 ? boundComputePipeline_ : boundPipeline_);
      if (pipeline.pushRoots[stage] == UINT_MAX ||
          offset > pipeline.pushSizes[stage] ||
          size > pipeline.pushSizes[stage] - offset)
        throw std::invalid_argument(
            "Push constant range/stages are incompatible with bound pipeline");
    }
  for (u32 stage = 0; stage < 5; ++stage) {
    if (!(static_cast<u32>(stages) & (1u << stage)))
      continue;
    const bool compute = stage == 2;
    const auto &pipeline =
        device_->GetPipeline(compute ? boundComputePipeline_ : boundPipeline_);
    if (pipeline.pushRoots[stage] == UINT_MAX ||
        offset > pipeline.pushSizes[stage] ||
        size > pipeline.pushSizes[stage] - offset)
      throw std::invalid_argument(
          "Push constant range/stages are incompatible with bound pipeline");
    auto &shadow = pushData_[stage];
    std::memmove(shadow.data() + offset, data, size);
    if (pipeline.pushInline[stage]) {
      if (compute)
        commandList_->SetComputeRoot32BitConstants(
            pipeline.pushRoots[stage], size / 4, shadow.data() + offset,
            offset / 4);
      else
        commandList_->SetGraphicsRoot32BitConstants(
            pipeline.pushRoots[stage], size / 4, shadow.data() + offset,
            offset / 4);
      continue;
    }

    // Overflow blocks use immutable slices until this context's fence retires.
    // Pages remain mapped and are reused on subsequent frames.
    constexpr u64 pageSize = 1024 * 1024;
    const u64 paddedSize = (shadow.size() + 255) & ~u64{255};
    auto &resources = RetainedResources();
    if (resources.constantPageIndex < resources.constantPages.size() &&
        resources.constantPages[resources.constantPageIndex].used + paddedSize >
            pageSize)
      ++resources.constantPageIndex;
    if (resources.constantPageIndex == resources.constantPages.size()) {
      ConstantUploadPage page;
      page.resource = device_->CreateHostBuffer(pageSize, true);
      D3D12_RANGE readRange{0, 0};
      ThrowIfFailed(page.resource->Map(0, &readRange,
                                       reinterpret_cast<void **>(&page.mapped)),
                    "Map push constant upload page");
      resources.constantPages.push_back(std::move(page));
    }
    auto &page = resources.constantPages[resources.constantPageIndex];
    std::memcpy(page.mapped + page.used, shadow.data(), shadow.size());
    const auto address = page.resource->GetGPUVirtualAddress() + page.used;
    page.used += paddedSize;
    if (compute)
      commandList_->SetComputeRootConstantBufferView(pipeline.pushRoots[stage],
                                                     address);
    else
      commandList_->SetGraphicsRootConstantBufferView(pipeline.pushRoots[stage],
                                                      address);
  }
}

void CommandList::CopyBuffer(BufferHandle src, BufferHandle dst,
                             const BufferCopyRegion &region) {
  const auto &source = device_->GetBuffer(src);
  const auto &destination = device_->GetBuffer(dst);
  if (!isRecording_ || isRendering_ || src.id == dst.id || !region.size ||
      region.srcOffset > source.size ||
      region.size > source.size - region.srcOffset ||
      region.dstOffset > destination.size ||
      region.size > destination.size - region.dstOffset ||
      !HasFlag(source.usage, BufferUsage::TransferSrc) ||
      !HasFlag(destination.usage, BufferUsage::TransferDst) ||
      destination.heapType == D3D12_HEAP_TYPE_UPLOAD ||
      source.heapType == D3D12_HEAP_TYPE_READBACK)
    throw std::invalid_argument("Invalid buffer copy");
  commandList_->CopyBufferRegion(destination.resourcePtr, region.dstOffset,
                                 source.resourcePtr, region.srcOffset,
                                 region.size);
}

void CommandList::CopyBufferToImage(BufferHandle src, ImageHandle dst,
                                    const BufferImageCopyRegion &region) {
  const auto &source = device_->GetBuffer(src);
  const auto &image = device_->GetImage(dst);
  if (!isRecording_ || isRendering_ ||
      !HasFlag(source.usage, BufferUsage::TransferSrc) ||
      !HasFlag(image.usage, ImageUsage::TransferDst) ||
      source.heapType == D3D12_HEAP_TYPE_READBACK ||
      image.type != ImageType::Image2D || region.aspect != ImageAspect::Color ||
      region.mipLevel >= image.mipLevels || !region.layerCount ||
      region.baseArrayLayer >= image.arrayLayers ||
      region.layerCount > image.arrayLayers - region.baseArrayLayer ||
      region.imageOffset.x < 0 || region.imageOffset.y < 0 ||
      region.imageOffset.z != 0 || !region.imageExtent.width ||
      !region.imageExtent.height || region.imageExtent.depth != 1)
    throw std::invalid_argument("Invalid 2D color buffer-to-image copy");
  if (image.format != Format::RGBA8_UNORM &&
      image.format != Format::RGBA8_SRGB &&
      image.format != Format::BGRA8_UNORM && image.format != Format::BGRA8_SRGB)
    throw std::invalid_argument(
        "Buffer-to-image upload currently supports RGBA8/BGRA8 images");
  for (u32 layer = 0; layer < region.layerCount; ++layer) {
    const u32 subresource =
        region.mipLevel + (region.baseArrayLayer + layer) * image.mipLevels;
    const auto state = image.subresourceStates.empty()
                           ? image.state
                           : image.subresourceStates[subresource];
    if (state != D3D12_RESOURCE_STATE_COPY_DEST)
      throw std::invalid_argument("Texture upload requires COPY_DEST state");
  }
  const u32 width = std::max(1u, image.width >> region.mipLevel);
  const u32 height = std::max(1u, image.height >> region.mipLevel);
  const u64 rowPixels = region.bufferRowLength ? region.bufferRowLength
                                               : region.imageExtent.width;
  const u64 rows = region.bufferImageHeight ? region.bufferImageHeight
                                            : region.imageExtent.height;
  const u64 pitch = rowPixels * 4;
  const u64 layerBytes = pitch * rows;
  if (static_cast<u32>(region.imageOffset.x) > width ||
      region.imageExtent.width > width - region.imageOffset.x ||
      static_cast<u32>(region.imageOffset.y) > height ||
      region.imageExtent.height > height - region.imageOffset.y ||
      rowPixels < region.imageExtent.width ||
      rows < region.imageExtent.height || pitch > UINT_MAX ||
      pitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT ||
      region.bufferOffset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT ||
      (region.layerCount > 1 &&
       layerBytes % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
    throw std::invalid_argument(
        "Texture copy bounds or D3D12 row/placement alignment is invalid");
  const u64 lastLayerBytes =
      pitch * (region.imageExtent.height - 1) + region.imageExtent.width * 4ull;
  if (region.bufferOffset > source.size ||
      lastLayerBytes > source.size - region.bufferOffset ||
      region.layerCount - 1 >
          (source.size - region.bufferOffset - lastLayerBytes) / layerBytes)
    throw std::invalid_argument("Texture upload exceeds source buffer");
  if (source.heapType == D3D12_HEAP_TYPE_DEFAULT &&
      source.state != D3D12_RESOURCE_STATE_COMMON &&
      !(source.state & D3D12_RESOURCE_STATE_COPY_SOURCE))
    throw std::invalid_argument(
        "Texture upload source requires COPY_SOURCE state");
  for (u32 layer = 0; layer < region.layerCount; ++layer) {
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = source.resourcePtr;
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Offset = region.bufferOffset + layerBytes * layer;
    from.PlacedFootprint.Footprint = {
        ToDxgiFormat(image.format), region.imageExtent.width,
        region.imageExtent.height, 1, static_cast<UINT>(pitch)};
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = image.resourcePtr;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex =
        region.mipLevel + (region.baseArrayLayer + layer) * image.mipLevels;
    commandList_->CopyTextureRegion(&to, region.imageOffset.x,
                                    region.imageOffset.y, 0, &from, nullptr);
  }
  auto &retained = RetainedResources().objects;
  retained.emplace_back(source.resourcePtr);
  retained.emplace_back(image.resourcePtr);
}

void CommandList::CopyImageToBuffer(ImageHandle src, BufferHandle dst,
                                    const BufferImageCopyRegion &region) {
  static_cast<void>(src);
  static_cast<void>(dst);
  static_cast<void>(region);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (CopyImageToBuffer)");
}

void CommandList::PipelineBarrier(std::span<const BufferBarrier> buffers,
                                  std::span<const ImageBarrier> images) {
  if (!isRecording_ || isRendering_) {
    throw std::logic_error(
        "PipelineBarrier requires recording outside rendering");
  }

  for (const BufferBarrier &barrier : buffers)
    Barrier(barrier);

  for (const ImageBarrier &barrier : images)
    Barrier(barrier);
}

void CommandList::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex,
                       u32 baseInstance) {
  if (!isRecording_ || !isRendering_ || computeActive_ ||
      !boundPipeline_.IsValid() || device_->GetPipeline(boundPipeline_).mesh)
    throw std::logic_error(
        "Draw requires an active graphics pipeline and rendering scope");
  commandList_->DrawInstanced(vertexCount, instanceCount, firstVertex,
                              baseInstance);
}

void CommandList::DrawIndexed(u32 indexCount, u32 firstIndex, i32 vertexOffset,
                              u32 baseInstance) {
  DrawIndexedInstanced(indexCount, 1, firstIndex, vertexOffset, baseInstance);
}

void CommandList::DrawIndexedInstanced(u32 indexCount, u32 instanceCount,
                                       u32 firstIndex, i32 vertexOffset,
                                       u32 baseInstance) {
  if (!isRecording_ || !isRendering_ || computeActive_ ||
      !boundPipeline_.IsValid() || device_->GetPipeline(boundPipeline_).mesh)
    throw std::logic_error(
        "Draw requires an active graphics pipeline and rendering scope");
  commandList_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
                                     vertexOffset, baseInstance);
}

void CommandList::DrawIndexedIndirect(BufferHandle buffer, u64 offset,
                                      u32 drawCount, u32 stride) {
  static_cast<void>(buffer);
  static_cast<void>(offset);
  static_cast<void>(drawCount);
  static_cast<void>(stride);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (DrawIndexedIndirect)");
}

void CommandList::DrawIndexedIndirectCount(BufferHandle buffer, u64 offset,
                                           BufferHandle countBuffer,
                                           u64 countOffset, u32 maxDrawCount,
                                           u32 stride) {
  static_cast<void>(buffer);
  static_cast<void>(offset);
  static_cast<void>(countBuffer);
  static_cast<void>(countOffset);
  static_cast<void>(maxDrawCount);
  static_cast<void>(stride);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (DrawIndexedIndirectCount)");
}

void CommandList::ValidateMeshDraw() const {
  if (!isRecording_ || !isRendering_ || computeActive_ ||
      commandList_->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
      !boundPipeline_.IsValid() || !device_->GetPipeline(boundPipeline_).mesh)
    throw std::logic_error(
        "Mesh draws require an active mesh pipeline and rendering scope");
}

void CommandList::DrawMeshTasks(u32 groupCountX, u32 groupCountY,
                                u32 groupCountZ) {
  ValidateMeshDraw();
  constexpr u64 maxMeshGroupCount = (1ull << 22) - 1;
  if (groupCountX > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
      groupCountY > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
      groupCountZ > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
      static_cast<u64>(groupCountX) * groupCountY * groupCountZ >
          maxMeshGroupCount)
    throw std::invalid_argument("Mesh dispatch exceeds thread group limits");
  // A zero-sized dispatch is a valid no-op.
  if (!groupCountX || !groupCountY || !groupCountZ)
    return;
  if (!meshCommandList_)
    ThrowIfFailed(commandList_->QueryInterface(IID_PPV_ARGS(&meshCommandList_)),
                  "Query mesh command list support");
  meshCommandList_->DispatchMesh(groupCountX, groupCountY, groupCountZ);
}

void CommandList::DrawMeshTasksIndirect(BufferHandle buffer, u64 offset,
                                        u32 drawCount, u32 stride) {
  ValidateMeshDraw();
  if (stride < sizeof(D3D12_DISPATCH_MESH_ARGUMENTS) || stride % sizeof(u32) ||
      offset % sizeof(u32))
    throw std::invalid_argument(
        "Invalid mesh indirect argument alignment/stride");
  const auto &arguments = device_->GetBuffer(buffer);
  if (!HasFlag(arguments.usage, BufferUsage::Indirect) ||
      arguments.heapType == D3D12_HEAP_TYPE_READBACK ||
      (arguments.state != D3D12_RESOURCE_STATE_COMMON &&
       !(arguments.state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)))
    throw std::invalid_argument(
        "Mesh indirect buffer requires indirect usage/state");
  // D3D12 validates the entire strided range, including the final record
  // padding.
  if (offset > arguments.size ||
      static_cast<u64>(drawCount) * stride > arguments.size - offset)
    throw std::invalid_argument("Mesh indirect arguments exceed buffer bounds");
  if (!drawCount)
    return;

  auto *signature = device_->GetMeshDispatchSignature(stride);
  auto &retained = RetainedResources().objects;
  retained.emplace_back(signature);
  retained.emplace_back(arguments.resourcePtr);
  touchedBuffers_.insert(buffer.id);
  // COMMON buffers implicitly promote to INDIRECT_ARGUMENT and decay on submit.
  commandList_->ExecuteIndirect(signature, drawCount, arguments.resourcePtr,
                                offset, nullptr, 0);
}

void CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                           uint32_t groupCountZ) {
  if (!isRecording_ || isRendering_ || !computeActive_ ||
      !boundComputePipeline_.IsValid() || !groupCountX || !groupCountY ||
      !groupCountZ || groupCountX > 65535 || groupCountY > 65535 ||
      groupCountZ > 65535)
    throw std::invalid_argument("Invalid compute dispatch");
  commandList_->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandList::DispatchIndirect(BufferHandle buffer, u64 offset) {
  static_cast<void>(buffer);
  static_cast<void>(offset);
  throw std::runtime_error(
      "D3D12 command lists are not yet implemented (DispatchIndirect)");
}

}  // namespace Velos::D3D12
