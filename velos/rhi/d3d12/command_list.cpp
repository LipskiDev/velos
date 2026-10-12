#include "command_list.h"
#include "device.h"

#include <array>

namespace Velos::D3D12 {
	using namespace Velos::RHI;
	
	CommandList::CommandList(Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* commandAllocator) : device_(device), commandList_(commandList), commandAllocator_(commandAllocator)
	{
		if (!device_ || !commandList_ || !commandAllocator_) {
			throw std::invalid_argument("CommandList requires valid D3D12 objects");
		}
		ThrowIfFailed(
			commandList_->Close(),
			"ID3D12GraphicsCommandList::Close"
		);
	}

	void CommandList::SetAllocator(ID3D12CommandAllocator* commandAllocator) {
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

		ThrowIfFailed(
			commandList_->Reset(commandAllocator_, nullptr),
			"ID3D12GraphicsCommandList::Reset");

		isRendering_ = false;
		isRecording_ = true;
	}

	void CommandList::End() {
	
		if (!isRecording_) {
			throw std::logic_error("Command list is not recording");
		}

		if (isRendering_) {
			throw std::logic_error(
				"Cannot end command list while rendering is active");
		}

		ThrowIfFailed(
			commandList_->Close(),
			"ID3D12GraphicsCommandList::Close");

		isRecording_ = false;
	}

	void CommandList::SetViewport(const Viewport& viewport) {
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

	void CommandList::SetScissor(const Rect2D& scissor) {
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

	void CommandList::Barrier(const BufferBarrier& barrier) {
		static_cast<void>(barrier);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::Barrier(const ImageBarrier& barrier) {
		Image& image = device_->GetImage(barrier.image);
		const D3D12_RESOURCE_STATES before = barrier.useExplicitStates
			? ToResourceState(barrier.oldState)
			: image.state;
		const D3D12_RESOURCE_STATES after = barrier.useExplicitStates
			? ToResourceState(barrier.newState)
			: ToResourceState(barrier.newLayout);

		if (before != after) {
			D3D12_RESOURCE_BARRIER nativeBarrier{};
			nativeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			nativeBarrier.Transition.pResource = image.resourcePtr;
			nativeBarrier.Transition.StateBefore = before;
			nativeBarrier.Transition.StateAfter = after;
			nativeBarrier.Transition.Subresource =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList_->ResourceBarrier(1, &nativeBarrier);
		}

		image.state = after;
		image.layout = barrier.newLayout;
	}
	
	void CommandList::UpdateBuffer(const BufferUpdateDesc& update) {
		static_cast<void>(update);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::ResetQueryPool(QueryPoolHandle pool, u32 firstQuery, u32 queryCount) {
		static_cast<void>(pool);
		static_cast<void>(firstQuery);
		static_cast<void>(queryCount);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::WriteTimestamp(QueryPoolHandle pool, u32 queryIndex, ShaderStage completedStage) {
		static_cast<void>(pool);
		static_cast<void>(queryIndex);
		static_cast<void>(completedStage);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BeginRendering(const RenderingInfo& renderingInfo) {
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
		const auto queueStoreDiscard = [this](ID3D12Resource* resource) {
			storeDiscardResources_[storeDiscardResourceCount_++] = resource;
		};

		std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
			D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
		for (u32 index = 0; index < renderingInfo.colorAttachmentCount; ++index) {
			const ColorAttachmentDesc& attachment =
				renderingInfo.colorAttachments[index];
			const ImageView& view = device_->GetImageView(attachment.view);
			if (view.descriptorType != ImageViewDescriptorType::RenderTarget) {
				throw std::invalid_argument("Color attachment does not reference an RTV");
			}
			renderTargets[index] = view.descriptor.handle;
			const Image& image = device_->GetImage(view.image);

			if (attachment.loadOp == LoadOp::Clear) {
				const float clearColor[]{
					attachment.clearValue.r,
					attachment.clearValue.g,
					attachment.clearValue.b,
					attachment.clearValue.a,
				};
				commandList_->ClearRenderTargetView(
					view.descriptor.handle, clearColor, 0, nullptr);
			} else if (attachment.loadOp == LoadOp::DontCare) {
				commandList_->DiscardResource(image.resourcePtr, nullptr);
			}
			if (attachment.storeOp == StoreOp::DontCare) {
				queueStoreDiscard(image.resourcePtr);
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE depthStencilHandle{};
		const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencil = nullptr;
		if (renderingInfo.depthAttachment) {
			const DepthAttachmentDesc& attachment =
				*renderingInfo.depthAttachment;
			const ImageView& view = device_->GetImageView(attachment.view);
			if (view.descriptorType != ImageViewDescriptorType::DepthStencil) {
				throw std::invalid_argument(
					"Depth attachment does not reference a DSV");
			}

			const Image& image = device_->GetImage(view.image);
			depthStencilHandle = view.descriptor.handle;
			depthStencil = &depthStencilHandle;
			if (attachment.loadOp == LoadOp::Clear) {
				D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
				if (HasFlag(view.aspect, ImageAspect::Stencil)) {
					clearFlags = static_cast<D3D12_CLEAR_FLAGS>(
						clearFlags | D3D12_CLEAR_FLAG_STENCIL);
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
		static_cast<void>(pipeline);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BindComputePipeline(PipelineHandle pipeline) {
		static_cast<void>(pipeline);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BindVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) {
		static_cast<void>(slot);
		static_cast<void>(buffer);
		static_cast<void>(offset);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BindIndexBuffer(BufferHandle buffer, IndexType indexType, u64 offset) {
		static_cast<void>(buffer);
		static_cast<void>(indexType);
		static_cast<void>(offset);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BindUniformBuffer(u32 binding, BufferHandle buffer, u64 offset, u64 size) {
		static_cast<void>(binding);
		static_cast<void>(buffer);
		static_cast<void>(offset);
		static_cast<void>(size);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::SetBindings(PipelineHandle pipeline, u32 setIndex, BindingSetHandle descriptorSet) {
		static_cast<void>(pipeline);
		static_cast<void>(setIndex);
		static_cast<void>(descriptorSet);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::SetComputeBindings(PipelineHandle pipeline, u32 setIndex, BindingSetHandle descriptorSet) {
		static_cast<void>(pipeline);
		static_cast<void>(setIndex);
		static_cast<void>(descriptorSet);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::GenerateMipmaps(ImageHandle imageHandle, uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t arrayLayers) {
		static_cast<void>(imageHandle);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(mipLevels);
		static_cast<void>(arrayLayers);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::BlitMip(ImageHandle image, uint32_t width, uint32_t height, uint32_t srcMip, uint32_t dstMip, uint32_t arrayLayers) {
		static_cast<void>(image);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(srcMip);
		static_cast<void>(dstMip);
		static_cast<void>(arrayLayers);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::PushConstants(ShaderStage stages, u32 offset, u32 size, const void* data) {
		static_cast<void>(stages);
		static_cast<void>(offset);
		static_cast<void>(size);
		static_cast<void>(data);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::CopyBuffer(BufferHandle src, BufferHandle dst, const BufferCopyRegion& region) {
		static_cast<void>(src);
		static_cast<void>(dst);
		static_cast<void>(region);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::CopyBufferToImage(BufferHandle src, ImageHandle dst, const BufferImageCopyRegion& region) {
		static_cast<void>(src);
		static_cast<void>(dst);
		static_cast<void>(region);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::CopyImageToBuffer(ImageHandle src, BufferHandle dst, const BufferImageCopyRegion& region) {
		static_cast<void>(src);
		static_cast<void>(dst);
		static_cast<void>(region);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::PipelineBarrier(std::span<const BufferBarrier> buffers, std::span<const ImageBarrier> images) {
		static_cast<void>(buffers);
		static_cast<void>(images);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 baseInstance) {
		static_cast<void>(vertexCount);
		static_cast<void>(instanceCount);
		static_cast<void>(firstVertex);
		static_cast<void>(baseInstance);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawIndexed(u32 indexCount, u32 firstIndex, i32 vertexOffset, u32 baseInstance) {
		static_cast<void>(indexCount);
		static_cast<void>(firstIndex);
		static_cast<void>(vertexOffset);
		static_cast<void>(baseInstance);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawIndexedInstanced(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 baseInstance) {
		static_cast<void>(indexCount);
		static_cast<void>(instanceCount);
		static_cast<void>(firstIndex);
		static_cast<void>(vertexOffset);
		static_cast<void>(baseInstance);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawIndexedIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) {
		static_cast<void>(buffer);
		static_cast<void>(offset);
		static_cast<void>(drawCount);
		static_cast<void>(stride);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawIndexedIndirectCount(BufferHandle buffer, u64 offset, BufferHandle countBuffer, u64 countOffset, u32 maxDrawCount, u32 stride) {
		static_cast<void>(buffer);
		static_cast<void>(offset);
		static_cast<void>(countBuffer);
		static_cast<void>(countOffset);
		static_cast<void>(maxDrawCount);
		static_cast<void>(stride);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
		static_cast<void>(groupCountX);
		static_cast<void>(groupCountY);
		static_cast<void>(groupCountZ);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DrawMeshTasksIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) {
		static_cast<void>(buffer);
		static_cast<void>(offset);
		static_cast<void>(drawCount);
		static_cast<void>(stride);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
		static_cast<void>(groupCountX);
		static_cast<void>(groupCountY);
		static_cast<void>(groupCountZ);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

	void CommandList::DispatchIndirect(BufferHandle buffer, u64 offset) {
		static_cast<void>(buffer);
		static_cast<void>(offset);
		throw std::runtime_error("D3D12 command lists are not yet implemented");
	}

} // namespace Velos::D3D12
