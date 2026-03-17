#pragma once

#include "core/types.h"
#include "rhi_handles.h"
#include "rhi_resources.h"
#include "rhi_types.h"

namespace Velos::RHI {

struct ColorAttachmentDesc {
  TextureHandle texture{};
  LoadOp loadOp = LoadOp::Clear;
  StoreOp storeOp = StoreOp::Store;
  ClearColor clearValue{};
};

struct DepthAttachmentDesc {
  TextureHandle texture{};
  LoadOp loadOp = LoadOp::Clear;
  StoreOp storeOp = StoreOp::Store;
  ClearDepthStencil clearValue{};
};

struct RenderingInfo {
  Rect2D renderArea{};
  const ColorAttachmentDesc *colorAttachments = nullptr;
  u32 colorAttachmentCount = 0;
  const DepthAttachmentDesc *depthAttachment = nullptr;
};

struct BufferBarrier {
  BufferHandle buffer{};
  ResourceState oldState = ResourceState::Undefined;
  ResourceState newState = ResourceState::Common;
};

struct TextureBarrier {
  TextureHandle texture{};
  ResourceState oldState = ResourceState::Undefined;
  ResourceState newState = ResourceState::Common;
};

struct BufferBinding {
  BufferHandle buffer{};
  u64 offset = 0;
  u64 size = 0;
};

struct TextureBinding {
  TextureHandle texture{};
  SamplerHandle sampler{};
};

class ICommandList {
public:
  virtual ~ICommandList() = default;

  virtual void Begin() = 0;
  virtual void End() = 0;

  virtual void SetViewport(const Viewport &viewport) = 0;
  virtual void SetScissor(const Rect2D &scissor) = 0;

  virtual void Barrier(const BufferBarrier &barrier) = 0;
  virtual void Barrier(const TextureBarrier &barrier) = 0;

  virtual void UpdateBuffer(const BufferUpdateDesc &update) = 0;
  virtual void UploadTexture(const TextureUploadDesc &upload) = 0;

  virtual void BeginRendering(const RenderingInfo &renderingInfo) = 0;
  virtual void EndRendering() = 0;

  virtual void BindPipeline(PipelineHandle pipeline) = 0;
  virtual void BindVertexBuffer(u32 slot, BufferHandle buffer,
                                u64 offset = 0) = 0;
  virtual void BindIndexBuffer(BufferHandle buffer, IndexType indexType,
                               u64 offset = 0) = 0;

  virtual void BindUniformBuffer(u32 binding, BufferHandle buffer, u64 offset,
                                 u64 size) = 0;
  virtual void BindSampledTexture(u32 binding, TextureHandle texture,
                                  SamplerHandle sampler) = 0;

  virtual void PushConstants(ShaderStage stages, u32 offset, u32 size,
                             const void *data) = 0;

  virtual void Draw(u32 vertexCount, u32 firstVertex = 0) = 0;
  virtual void DrawIndexed(u32 indexCount, u32 firstIndex = 0,
                           i32 vertexOffset = 0) = 0;
};
} // namespace Velos::RHI
