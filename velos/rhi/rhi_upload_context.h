#pragma once

#include "rhi/rhi_handles.h"
#include "rhi_types.h"

namespace Velos::RHI {

struct BufferUploadDesc {
  BufferHandle dstBuffer;
  u64 dstOffset = 0;
  u64 size = 0;
  const void *data = nullptr;
};

struct ImageUploadDesc {
  ImageHandle dstImage;
  ImageLayout oldLayout = ImageLayout::Undefined;
  ImageLayout finalLayout = ImageLayout::ShaderReadOnly;
  ImageAspect aspect = ImageAspect::Color;

  u32 mipLevel = 0;
  u32 baseArrayLayer = 0;
  u32 layerCount = 1;

  u32 width = 1;
  u32 height = 1;
  u32 depth = 1;

  u32 bufferRowLength = 0;
  u32 bufferImageHeight = 0;
};

class IUploadContext {
public:
  virtual ~IUploadContext() = default;

  virtual void Begin() = 0;
  virtual void UploadBuffer(const BufferUploadDesc &desc) = 0;
  virtual void UploadImage(const ImageUploadDesc &desc, const void *data,
                           u64 dataSize) = 0;
  virtual void Flush() = 0;
};

} // namespace Velos::RHI
