#pragma once

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
  RGBA8_SRGB,
  BGRA8_SRGB,

  R32_FLOAT,
  RG32_FLOAT,
  RGB32_FLOAT,
  RGBA32_FLOAT,

  D32_FLOAT,
  D24_UNORM_S8_UINT,
};

enum class ImageType {
  Image2D,
  Cube,
};

enum class ImageViewType {
  View2D,
  View2DArray,
  Cube,
  CubeArray,
};

enum class VertexFormat {
  Float32,
  Float32x2,
  Float32x3,
  Float32x4,
  UNorm8x4,
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
  ShaderDeviceAddress = 1 << 6,
  Indirect = 1 << 7
};

enum class ImageUsage : u32 {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Sampled = 1 << 2,
  ColorAttachment = 1 << 3,
  DepthStencil = 1 << 4,
  Storage = 1 << 5,
};

enum class ImageLayout {
  Undefined,
  ColorAttachment,
  DepthAttachment,
  ShaderReadOnly,
  TransferSrc,
  TransferDst,
  Present,
  General,
};

enum class ExecutionStage {
  TopOfPipe,
  Transfer,
  VertexShader,
  FragmentShader,
  ColorAttachmentOutput,
  BottomOfPipe,
};

enum class MemoryAccess {
  None,
  TransferRead,
  TransferWrite,
  ShaderRead,
  ColorAttachmentWrite,
};

enum class ImageAspect : u32 {
  None = 0,
  Color = 1 << 0,
  Depth = 1 << 1,
  Stencil = 1 << 2,
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

  // Transfer
  TransferSrc,
  TransferDst,

  // Geometry
  VertexBuffer,
  IndexBuffer,

  // Shader resources
  UniformBuffer,
  ShaderRead,
  ShaderWrite,

  // Render targets
  ColorAttachmentRead,
  ColorAttachmentWrite,
  RenderTarget,
  DepthWrite,
  DepthRead,

  IndirectArgument,

  // Present
  Present,
};

enum class Filter {
  Nearest,
  Linear,
};

enum class SamplerAddressMode {
  Repeat,
  ClampToEdge,
};

enum class BindingType {
  UniformBuffer,
  CombinedImageSampler,
  StorageBuffer,
  StorageImage,
  SampledTexture,
  Sampler,
  StorageTexture
};

enum class BlendFactor {
  Zero,
  One,

  SrcColor,
  OneMinusSrcColor,

  DstColor,
  OneMinusDstColor,

  SrcAlpha,
  OneMinusSrcAlpha,

  DstAlpha,
  OneMinusDstAlpha,

  ConstantColor,
  OneMinusConstantColor,

  ConstantAlpha,
  OneMinusConstantAlpha,
};

enum class BlendOp {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
};

struct Extent2D {
  u32 width = 0;
  u32 height = 0;
};

struct Extent3D {
  u32 width = 0;
  u32 height = 0;
  u32 depth = 0;
};

struct Offset2D {
  i32 x = 0;
  i32 y = 0;
};

struct Offset3D {
  i32 x = 0;
  i32 y = 0;
  i32 z = 0;
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

struct BindingPoolSize {
  BindingType type = BindingType::UniformBuffer;
  u32 count = 0;
};

} // namespace Velos::RHI
