#pragma once

#include "rhi/types.h"

#if !defined(_WIN32)
#error The D3D12 backend is only available on Windows
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// Avoid Win32 A/W macros rewriting RHI method names.
#ifdef CreateSemaphore
#undef CreateSemaphore
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>

namespace Velos::D3D12 {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

inline void ThrowIfFailed(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) {
        return;
    }

    char message[256]{};
    std::snprintf(message, sizeof(message), "%s failed (HRESULT 0x%08X)",
                  operation, static_cast<unsigned int>(result));
    throw std::runtime_error(message);
}

inline DXGI_FORMAT ToDxgiFormat(RHI::Format format) {
    switch (format) {
    case RHI::Format::RGBA8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHI::Format::BGRA8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHI::Format::RGBA8_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHI::Format::BGRA8_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case RHI::Format::RGBA16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHI::Format::R32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case RHI::Format::RG32_FLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case RHI::Format::RGB32_FLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHI::Format::RGBA32_FLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHI::Format::D32_FLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case RHI::Format::D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    default:
        throw std::invalid_argument("Unsupported D3D12 swapchain format");
    }
}

inline D3D12_RESOURCE_STATES ToResourceState(RHI::ResourceState state) {
    switch (state) {
    case RHI::ResourceState::Undefined:
    case RHI::ResourceState::Common:
        return D3D12_RESOURCE_STATE_COMMON;
    case RHI::ResourceState::TransferSrc:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHI::ResourceState::TransferDst:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHI::ResourceState::VertexBuffer:
    case RHI::ResourceState::UniformBuffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case RHI::ResourceState::IndexBuffer:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case RHI::ResourceState::ShaderRead:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case RHI::ResourceState::ShaderWrite:
    case RHI::ResourceState::ShaderReadWrite:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RHI::ResourceState::ColorAttachmentRead:
    case RHI::ResourceState::ColorAttachmentWrite:
    case RHI::ResourceState::RenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHI::ResourceState::DepthWrite:
    case RHI::ResourceState::DepthReadWrite:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHI::ResourceState::DepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RHI::ResourceState::IndirectArgument:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case RHI::ResourceState::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    throw std::invalid_argument("Unsupported D3D12 resource state");
}

inline D3D12_RESOURCE_STATES ToResourceState(RHI::ImageLayout layout) {
    switch (layout) {
    case RHI::ImageLayout::Undefined:
        return D3D12_RESOURCE_STATE_COMMON;
    case RHI::ImageLayout::ColorAttachment:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHI::ImageLayout::DepthAttachment:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHI::ImageLayout::ShaderReadOnly:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case RHI::ImageLayout::TransferSrc:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHI::ImageLayout::TransferDst:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHI::ImageLayout::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    case RHI::ImageLayout::General:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    throw std::invalid_argument("Unsupported D3D12 image layout");
}

} // namespace Velos::D3D12
