#pragma once

#include "common.h"
#include "rhi/resources.h"
#include "types.h"

#include <vector>

namespace Velos::D3D12 {
	using namespace Velos::RHI;
	
	struct SwapchainImage {
		ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
	};

	class Swapchain {
	public:
		Swapchain(IDXGIFactory6& factory, ID3D12Device& device,
			ID3D12CommandQueue& queue, const SwapchainDesc& desc);
		~Swapchain() = default;

		Swapchain(const Swapchain&) = delete;
		Swapchain& operator=(const Swapchain&) = delete;

		void Resize(u32 width, u32 height);
		void Present();

		u32 GetCurrentBackbufferIndex() const;
		const SwapchainImage& GetCurrentBackbuffer() const;
		const SwapchainImage& GetBackbuffer(u32 index) const;
		u32 GetWidth() const noexcept { return width_; }
		u32 GetHeight() const noexcept { return height_; }
		u32 GetBufferCount() const noexcept {
			return static_cast<u32>(backbuffers_.size());
		}

	private:
		void CreateBackbuffers();
		void ReleaseBackbuffers();

	private:
		ID3D12Device* device_ = nullptr;
		ComPtr<IDXGISwapChain4> swapchain_;
		ComPtr<ID3D12DescriptorHeap> rtvHeap_;
		std::vector<SwapchainImage> backbuffers_;
		DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
		u32 rtvDescriptorSize_ = 0;
		u32 width_ = 0;
		u32 height_ = 0;
		u32 bufferCount_ = 0;
		bool vsync_ = true;
		bool allowTearing_ = false;
	};
}
