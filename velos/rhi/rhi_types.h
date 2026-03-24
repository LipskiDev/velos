#pragma once
#include <cstdint>

#include "../core/types.h"

namespace Velos::RHI {

template <typename T> constexpr T operator|(T a, T b) {
  return static_cast<T>(static_cast<u32>(a) | static_cast<u32>(b));
}

template <typename T> constexpr T operator&(T a, T b) {
  return static_cast<T>(static_cast<u32>(a) & static_cast<u32>(b));
}

template <typename T> constexpr bool HasFlag(T value, T flag) {
  return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class Format {
  Undefined,

  RGBA8_UNORM,
  BGRA8_UNORM,
  RGBA16_FLOAT,

  R32_FLOAT,
  RG32_FLOAT,
  RGB32_FLOAT,
  RGBA32_FLOAT,

  D32_FLOAT,
  D24_UNORM_S8_UINT,
};

enum class VertexFormat {
  Float32,
  Float32x2,
  Float32x3,
  Float32x4,
};

enum class PrimitiveTopology {
  TriangleList,
  TriangleStrip,
  LineList,
};

enum class IndexType {
  U16,
  U32,
};

enum class ShaderStage : u32 {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  Compute = 1 << 2,
};

enum class BufferUsage : u32 {
  None = 0,
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  Storage = 1 << 3,
  TransferSrc = 1 << 4,
  TransferDst = 1 << 5,
};

enum class TextureUsage : u32 {
  None = 0,
  Sampled = 1 << 0,
  ColorAttachment = 1 << 1,
  DepthStencil = 1 << 2,
  TransferSrc = 1 << 3,
  TransferDst = 1 << 4,
  Storage = 1 << 5,
};

enum class MemoryUsage {
  GPUOnly,
  CPUToGPU,
  GPUToCPU,
};

enum class LoadOp {
  Load,
  Clear,
  DontCare,
};

enum class StoreOp {
  Store,
  DontCare,
};

enum class ResourceState {
  Undefined,
  Common,
  TransferSrc,
  TransferDst,
  VertexBuffer,
  IndexBuffer,
  UniformBuffer,
  ShaderRead,
  RenderTarget,
  DepthWrite,
  Present,
};

struct Extent2D {
  u32 width = 0;
  u32 height = 0;
};

struct Offset2D {
  i32 x = 0;
  i32 y = 0;
};

struct Rect2D {
  Offset2D offset{};
  Extent2D extent{};
};

struct Viewport {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float minDepth = 0.0f;
  float maxDepth = 1.0f;
};

struct ClearColor {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

struct ClearDepthStencil {
  float depth = 1.0f;
  u32 stencil = 0;
};

} // namespace Velos::RHI
