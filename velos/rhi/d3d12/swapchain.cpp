#include "swapchain.h"

#include <algorithm>

namespace Velos::D3D12 {

namespace {

bool SupportsTearing(IDXGIFactory6 &factory) {
  BOOL supported = FALSE;
  const HRESULT result = factory.CheckFeatureSupport(
      DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported));
  return SUCCEEDED(result) && supported == TRUE;
}

}  // namespace

Swapchain::Swapchain(IDXGIFactory6 &factory, ID3D12Device &device,
                     ID3D12CommandQueue &queue, const SwapchainDesc &desc)
    : device_(&device),
      format_(ToDxgiFormat(desc.format)),
      width_(desc.width),
      height_(desc.height),
      bufferCount_(std::max(desc.bufferCount, 2u)),
      vsync_(desc.vsync),
      allowTearing_(!desc.vsync && SupportsTearing(factory)) {
  if (desc.window.type != WindowHandleType::Win32 || !desc.window.value) {
    throw std::invalid_argument(
        "The D3D12 swapchain requires a valid Win32 window handle");
  }
  if (width_ == 0 || height_ == 0) {
    throw std::invalid_argument(
        "The D3D12 swapchain dimensions must be non-zero");
  }

  DXGI_SWAP_CHAIN_DESC1 swapchainDesc{};
  swapchainDesc.Width = width_;
  swapchainDesc.Height = height_;
  swapchainDesc.Format = format_;
  swapchainDesc.SampleDesc = {.Count = 1, .Quality = 0};
  swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchainDesc.BufferCount = bufferCount_;
  swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
  swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  swapchainDesc.Flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

  ComPtr<IDXGISwapChain1> baseSwapchain;
  ThrowIfFailed(factory.CreateSwapChainForHwnd(
                    &queue, static_cast<HWND>(desc.window.value),
                    &swapchainDesc, nullptr, nullptr, &baseSwapchain),
                "IDXGIFactory6::CreateSwapChainForHwnd");
  ThrowIfFailed(
      factory.MakeWindowAssociation(static_cast<HWND>(desc.window.value),
                                    DXGI_MWA_NO_ALT_ENTER),
      "IDXGIFactory6::MakeWindowAssociation");
  ThrowIfFailed(baseSwapchain.As(&swapchain_), "Query IDXGISwapChain4");

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.NumDescriptors = bufferCount_;
  ThrowIfFailed(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)),
                "ID3D12Device::CreateDescriptorHeap(RTV)");
  rtvDescriptorSize_ =
      device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  CreateBackbuffers();
}

void Swapchain::Resize(u32 width, u32 height) {
  if (width == 0 || height == 0) {
    return;
  }
  if (width == width_ && height == height_) {
    return;
  }

  ReleaseBackbuffers();
  const UINT flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
  ThrowIfFailed(
      swapchain_->ResizeBuffers(bufferCount_, width, height, format_, flags),
      "IDXGISwapChain4::ResizeBuffers");
  width_ = width;
  height_ = height;
  CreateBackbuffers();
}

void Swapchain::Present() {
  const UINT syncInterval = vsync_ ? 1u : 0u;
  const UINT flags = !vsync_ && allowTearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
  ThrowIfFailed(swapchain_->Present(syncInterval, flags),
                "IDXGISwapChain4::Present");
}

u32 Swapchain::GetCurrentBackbufferIndex() const {
  return swapchain_->GetCurrentBackBufferIndex();
}

const SwapchainImage &Swapchain::GetCurrentBackbuffer() const {
  return GetBackbuffer(GetCurrentBackbufferIndex());
}

const SwapchainImage &Swapchain::GetBackbuffer(u32 index) const {
  if (index >= backbuffers_.size()) {
    throw std::out_of_range("D3D12 swapchain backbuffer index out of range");
  }
  return backbuffers_[index];
}

void Swapchain::CreateBackbuffers() {
  backbuffers_.resize(bufferCount_);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtvHeap_->GetCPUDescriptorHandleForHeapStart();

  for (u32 index = 0; index < bufferCount_; ++index) {
    SwapchainImage &backbuffer = backbuffers_[index];
    ThrowIfFailed(
        swapchain_->GetBuffer(index, IID_PPV_ARGS(&backbuffer.resource)),
        "IDXGISwapChain4::GetBuffer");
    backbuffer.rtv = rtv;
    device_->CreateRenderTargetView(backbuffer.resource.Get(), nullptr, rtv);
    rtv.ptr += rtvDescriptorSize_;
  }
}

void Swapchain::ReleaseBackbuffers() {
  for (SwapchainImage &backbuffer : backbuffers_) {
    backbuffer.resource.Reset();
    backbuffer.rtv = {};
  }
  backbuffers_.clear();
}

}  // namespace Velos::D3D12
