#include <rhi/d3d12/device.h>
#include <rhi/d3d12/command_list.h>
#include <shader/shader_compiler.h>
#include <d3d12sdklayers.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>

using namespace Velos;
using namespace Velos::RHI;
namespace DX = Velos::D3D12;

static void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F>
static void Reject(F action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &) {
    rejected = true;
  }
  Require(rejected, "Expected invalid request to be rejected");
}

struct Context {
  DX::Device device;
  HWND window = nullptr;
  SwapchainHandle swapchain;
  DX::ComPtr<ID3D12InfoQueue> messages;
  Context(const char *cache = "cache/d3d12-parity")
      : device({.graphicsAPI = GraphicsAPI::D3D12,
                .enableValidation = true,
                .pipelineCachePath = cache}) {
    window = CreateWindowExW(0, L"STATIC", L"D3D12 parity tests",
                             WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "Create test window");
    swapchain =
        device.CreateSwapchain({.window = {WindowHandleType::Win32, window},
                                .width = 32,
                                .height = 32});
    DX::ThrowIfFailed(
        device.GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&messages)),
        "Get validation queue");
    messages->ClearStoredMessages();
  }
  ~Context() {
    device.WaitIdle();
    device.DestroySwapchain(swapchain);
    DestroyWindow(window);
  }
  DX::CommandList &Begin() {
    Require(device.BeginFrame(swapchain).success, "Begin frame");
    auto &list = static_cast<DX::CommandList &>(device.GetCommandList());
    list.Begin();
    return list;
  }
  void End(DX::CommandList &list) {
    list.End();
    device.SubmitAndPresent(swapchain);
    device.WaitIdle();
  }
  void Validate() {
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T size = 0;
      messages->GetMessage(i, nullptr, &size);
      std::vector<std::byte> bytes(size);
      auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
      DX::ThrowIfFailed(messages->GetMessage(i, message, &size),
                        "Read validation message");
      if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
          message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        throw std::runtime_error(message->pDescription);
    }
    messages->ClearStoredMessages();
  }
  ShaderHandle Shader(ShaderStage stage, const char *entry) {
    const auto compiled = ShaderCompiler::CompileFile(
        {.path =
             std::filesystem::exists("external/velos/tests/d3d12/parity.hlsl")
                 ? "external/velos/tests/d3d12/parity.hlsl"
                 : "tests/d3d12/parity.hlsl",
         .stage = stage,
         .entryPoint = entry,
         .language = ShaderSourceLanguage::HLSL,
         .outputFormat = ShaderBinaryFormat::Dxil});
    const auto bytes = compiled.GetBytecode();
    return device.CreateShader({.stage = stage,
                                .bytecode = bytes.data(),
                                .bytecodeSize = bytes.size(),
                                .entryPoint = entry,
                                .reflection = compiled.reflection});
  }
};

static void Buffers(Context &c) {
  Reject([&] { c.device.CreateUploadContext(0); });
  // Repeated creation/destruction checks native allocator/list initialization
  // and persistent staging-map ownership without submitting unfinished uploads.
  for (int i = 0; i < 3; ++i) {
    const auto uploadContext = c.device.CreateUploadContext(4096);
    Require(uploadContext != nullptr, "Upload context factory returned null");
  }
  std::array<uint32_t, 16> data;
  for (u32 i = 0; i < data.size(); ++i)
    data[i] = 0x12340000 + i;
  auto upload = c.device.CreateBuffer({.size = sizeof(data),
                                       .usage = BufferUsage::TransferSrc,
                                       .memoryUsage = MemoryUsage::CPUToGPU,
                                       .initialData = data.data()});
  auto gpu = c.device.CreateBuffer({.size = sizeof(data),
                                    .usage = BufferUsage::TransferSrc |
                                             BufferUsage::TransferDst |
                                             BufferUsage::ShaderDeviceAddress,
                                    .initialData = data.data()});
  auto readback = c.device.CreateBuffer({.size = sizeof(data),
                                         .usage = BufferUsage::TransferDst,
                                         .memoryUsage = MemoryUsage::GPUToCPU});
  D3D12_HEAP_PROPERTIES heap{};
  DX::ThrowIfFailed(
      c.device.GetBuffer(gpu).resourcePtr->GetHeapProperties(&heap, nullptr),
      "Inspect buffer heap");
  Require(heap.Type == D3D12_HEAP_TYPE_DEFAULT,
          "GPUOnly buffer is not in DEFAULT memory");
  Reject([&] { c.device.MapBuffer(gpu); });
  Require(c.device.GetBufferDeviceAddress(gpu) != 0, "Missing GPU address");
  auto &first = c.Begin();
  first.Barrier(
      BufferBarrier{.buffer = gpu, .newState = ResourceState::TransferSrc});
  first.CopyBuffer(gpu, readback, {0, 0, sizeof(data)});
  c.End(first);
  Require(
      std::memcmp(c.device.MapBuffer(readback), data.data(), sizeof(data)) == 0,
      "GPU-only initial upload mismatch");
  c.device.UnmapBuffer(readback);
  const u32 replacement = 0xabcdef01;
  auto &second = c.Begin();
  second.UpdateBuffer({.buffer = gpu,
                       .offset = 12,
                       .data = &replacement,
                       .size = sizeof(replacement)});
  second.CopyBuffer(gpu, readback, {0, 0, sizeof(data)});
  c.End(second);
  data[3] = replacement;
  Require(
      std::memcmp(c.device.MapBuffer(readback), data.data(), sizeof(data)) == 0,
      "Staged partial update mismatch");
  c.device.UnmapBuffer(readback);
  auto *mapped = static_cast<u32 *>(c.device.MapBuffer(upload));
  mapped[0] = 42;
  c.device.UnmapBuffer(upload);
  auto &third = c.Begin();
  third.CopyBuffer(upload, readback, {0, 0, sizeof(data)});
  c.End(third);
  Require(*static_cast<u32 *>(c.device.MapBuffer(readback)) == 42,
          "Mapped upload buffer mismatch");
  c.device.UnmapBuffer(readback);
  const auto persistentUpload = c.device.MapBuffer(upload);
  auto &fourth = c.Begin();
  fourth.UpdateBuffer({.buffer = upload,
                       .offset = 0,
                       .data = &replacement,
                       .size = sizeof(replacement)});
  fourth.CopyBuffer(upload, readback, {0, 0, sizeof(data)});
  c.End(fourth);
  Require(c.device.GetBuffer(upload).mapped == persistentUpload,
          "UpdateBuffer invalidated a persistent mapping");
  c.device.UnmapBuffer(upload);
  Require(*static_cast<u32 *>(c.device.MapBuffer(readback)) == replacement,
          "CPU-visible UpdateBuffer mismatch");
  c.device.UnmapBuffer(readback);
  auto uniform = c.device.CreateBuffer({.size = 17,
                                        .usage = BufferUsage::Uniform,
                                        .memoryUsage = MemoryUsage::CPUToGPU});
  Require(c.device.GetBuffer(uniform).allocationSize == 256 &&
              c.device.GetBuffer(uniform).cbv.IsValid(),
          "Uniform alignment/CBV mismatch");
  auto storage =
      c.device.CreateBuffer({.size = 256, .usage = BufferUsage::Storage});
  Require(c.device.GetBuffer(storage).srv.IsValid() &&
              c.device.GetBuffer(storage).uav.IsValid(),
          "Missing storage views");
  for (auto buffer : {upload, gpu, readback, uniform, storage})
    c.device.DestroyBuffer(buffer);
  Reject([&] {
    c.device.CreateBuffer({.size = 0, .usage = BufferUsage::Vertex});
  });
  c.Validate();
}

static void Views(Context &c) {
  auto texture =
      c.device.CreateImage({.width = 32,
                            .height = 32,
                            .mipLevels = 3,
                            .arrayLayers = 12,
                            .format = Format::RGBA8_UNORM,
                            .type = ImageType::Cube,
                            .usage = ImageUsage::Sampled | ImageUsage::Storage |
                                     ImageUsage::ColorAttachment});
  std::vector<ImageViewHandle> views;
  Require(c.device.GetImageLayout(texture, 2) == ImageLayout::Undefined,
          "Valid mip layout query rejected");
  Reject([&] { c.device.GetImageLayout(texture, 3); });
  for (auto type : {ImageViewType::View2D, ImageViewType::View2DArray,
                    ImageViewType::Cube, ImageViewType::CubeArray}) {
    const u32 layers = type == ImageViewType::View2D      ? 1
                       : type == ImageViewType::CubeArray ? 12
                                                          : 6;
    auto view = c.device.CreateImageView({.image = texture,
                                          .type = type,
                                          .baseMipLevel = 1,
                                          .arrayLayerCount = layers});
    const auto &native = c.device.GetImageView(view);
    Require(native.srv.IsValid() && native.uav.IsValid() &&
                native.descriptor.IsValid(),
            "Missing multi-use image descriptors");
    views.push_back(view);
  }
  views.push_back(c.device.CreateImageView({.image = texture,
                                            .type = ImageViewType::CubeArray,
                                            .mipLevelCount = 3,
                                            .arrayLayerCount = 12}));
  auto srgb = c.device.CreateImageView(
      {.image = texture, .format = Format::RGBA8_SRGB, .mipLevelCount = 3});
  views.push_back(srgb);
  Reject(
      [&] { c.device.CreateImageView({.image = texture, .baseMipLevel = 3}); });
  Reject([&] {
    c.device.CreateImageView(
        {.image = texture, .format = Format::RGBA16_FLOAT});
  });
  for (auto view : views)
    c.device.DestroyImageView(view);
  c.device.DestroyImage(texture);
  for (auto format : {Format::D32_FLOAT, Format::D24_UNORM_S8_UINT}) {
    auto depth = c.device.CreateImage(
        {.width = 16,
         .height = 16,
         .format = format,
         .usage = ImageUsage::DepthStencil | ImageUsage::Sampled});
    auto view = c.device.CreateImageView(
        {.image = depth, .aspect = ImageAspect::Depth});
    Require(c.device.GetImageView(view).srv.IsValid(),
            "Missing sampled depth view");
    c.device.DestroyImageView(view);
    if (format == Format::D24_UNORM_S8_UINT) {
      view = c.device.CreateImageView(
          {.image = depth, .aspect = ImageAspect::Stencil});
      c.device.DestroyImageView(view);
    }
    c.device.DestroyImage(depth);
  }
  for (auto filter : {Filter::Nearest, Filter::Linear})
    for (auto address :
         {SamplerAddressMode::Repeat, SamplerAddressMode::ClampToEdge}) {
      auto sampler = c.device.CreateSampler({.minFilter = filter,
                                             .magFilter = filter,
                                             .addressU = address,
                                             .addressV = address,
                                             .addressW = address,
                                             .enableAnisotropy = true,
                                             .maxAnisotropy = 8,
                                             .minLod = 0,
                                             .maxLod = 4});
      Require(c.device.GetSampler(sampler).descriptor.IsValid(),
              "Missing sampler descriptor");
      c.device.DestroySampler(sampler);
    }
  const auto sampledDepth = c.device.CreateImage(
      {.width = 8,
       .height = 8,
       .format = Format::D32_FLOAT,
       .usage = ImageUsage::Sampled | ImageUsage::TransferDst});
  const auto sampledDepthView = c.device.CreateImageView(
      {.image = sampledDepth, .aspect = ImageAspect::Depth});
  Require(c.device.GetImageView(sampledDepthView).srv.IsValid(),
          "Sampled-only depth view missing");
  c.device.DestroyImageView(sampledDepthView);
  c.device.DestroyImage(sampledDepth);
  Reject([&] { c.device.CreateSampler({.minLod = 5, .maxLod = 1}); });
  c.Validate();
}

static void MirroredBuffers(Context &c) {
  const std::array<u32, 4> initial{1, 2, 3, 4};
  const auto usage = BufferUsage::Storage | BufferUsage::TransferSrc |
                     BufferUsage::TransferDst;
  const auto upload =
      c.device.CreateBuffer({.size = sizeof(initial),
                             .usage = usage,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = initial.data()});
  const auto download =
      c.device.CreateBuffer({.size = sizeof(initial),
                             .usage = usage,
                             .memoryUsage = MemoryUsage::GPUToCPU,
                             .initialData = initial.data()});
  Require(c.device.GetBuffer(upload).hostResource &&
              c.device.GetBuffer(upload).uav.IsValid(),
          "CPU-visible storage requires writable GPU backing");
  Require(std::memcmp(c.device.MapBuffer(upload), initial.data(),
                      sizeof(initial)) == 0,
          "Upload mirror initial data mismatch");
  static_cast<u32 *>(c.device.MapBuffer(upload))[1] = 99;
  c.device.UnmapBuffer(upload);
  const u32 replacement = 123;
  auto &list = c.Begin();
  list.UpdateBuffer(
      {.buffer = upload, .offset = 8, .data = &replacement, .size = 4});
  list.Barrier(
      BufferBarrier{.buffer = upload, .newState = ResourceState::TransferSrc});
  list.Barrier(BufferBarrier{.buffer = download,
                             .newState = ResourceState::TransferDst});
  list.CopyBuffer(upload, download, {0, 0, sizeof(initial)});
  c.End(list);
  const auto *values = static_cast<u32 *>(c.device.MapBuffer(download));
  Require(
      values[0] == 1 && values[1] == 99 && values[2] == 123 && values[3] == 4,
      "Readback storage mirror mismatch");
  c.device.UnmapBuffer(download);
  values = static_cast<u32 *>(c.device.MapBuffer(upload));
  Require(values[2] == 123, "Upload mirror did not refresh GPU data");
  c.device.UnmapBuffer(upload);
  auto *persistentUpload = static_cast<u32 *>(c.device.MapBuffer(upload));
  auto *persistentDownload = static_cast<u32 *>(c.device.MapBuffer(download));
  persistentUpload[0] = 1000;
  auto &persistent = c.Begin();
  persistent.UpdateBuffer(
      {.buffer = upload, .offset = 4, .data = &replacement, .size = 4});
  persistent.Barrier(
      BufferBarrier{.buffer = upload, .newState = ResourceState::TransferSrc});
  persistent.Barrier(BufferBarrier{.buffer = download,
                                   .newState = ResourceState::TransferDst});
  persistent.CopyBuffer(upload, download, {0, 0, sizeof(initial)});
  c.End(persistent);
  Require(persistentDownload[0] == 1000 && persistentDownload[1] == 123 &&
              persistentUpload[1] == 123,
          "Persistent mirror coherence mismatch");
  c.device.UnmapBuffer(upload);
  c.device.UnmapBuffer(download);
  c.device.DestroyBuffer(upload);
  c.device.DestroyBuffer(download);
  c.Validate();
}

static void ReadPixel(Context &c, DX::CommandList &list, ImageHandle image,
                      BufferHandle &readback, u32 subresource = 0) {
  const auto &texture = c.device.GetImage(image);
  const auto desc = texture.resourcePtr->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows;
  UINT64 pitch, size;
  c.device.GetNativeDevice()->GetCopyableFootprints(
      &desc, subresource, 1, 0, &footprint, &rows, &pitch, &size);
  readback = c.device.CreateBuffer({.size = size,
                                    .usage = BufferUsage::TransferDst,
                                    .memoryUsage = MemoryUsage::GPUToCPU});
  const u32 plane = subresource / (texture.mipLevels * texture.arrayLayers);
  list.Barrier(ImageBarrier{
      .image = image,
      .newLayout = ImageLayout::TransferSrc,
      .aspect = texture.format == Format::D24_UNORM_S8_UINT && plane == 1
                    ? ImageAspect::Stencil
                    : ImageAspect::Depth,
      .baseMipLevel = subresource % texture.mipLevels,
      .baseArrayLayer =
          (subresource / texture.mipLevels) % texture.arrayLayers});
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = texture.resourcePtr;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = subresource;
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = c.device.GetBuffer(readback).resourcePtr;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  list.GetNative()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

static void UploadContextTransfers(Context &c) {
  const std::array<u8, 16> pixels{255, 0, 255, 255, 0,   0, 0,   255,
                                  0,   0, 0,   255, 255, 0, 255, 255};
  const auto image = c.device.CreateImage({.width = 2,
                                           .height = 2,
                                           .format = Format::RGBA8_SRGB,
                                           .usage = ImageUsage::TransferDst |
                                                    ImageUsage::TransferSrc |
                                                    ImageUsage::Sampled});
  const auto buffer = c.device.CreateBuffer(
      {.size = 8,
       .usage = BufferUsage::TransferDst | BufferUsage::TransferSrc});
  const auto readback =
      c.device.CreateBuffer({.size = 8,
                             .usage = BufferUsage::TransferDst,
                             .memoryUsage = MemoryUsage::GPUToCPU});
  auto upload = c.device.CreateUploadContext(16);
  const std::array<u32, 2> initial{17, 29};
  upload->Begin();
  upload->UploadBuffer({.dstBuffer = buffer,
                        .size = sizeof(initial),
                        .data = initial.data(),
                        .finalState = ResourceState::TransferSrc});
  // Tight data exceeds the tiny staging capacity after D3D12 row padding;
  // the old chunk must survive until its buffer copy has completed.
  upload->UploadImage({.dstImage = image, .width = 2, .height = 2},
                      pixels.data(), pixels.size());
  upload->Flush();
  const u32 replacement = 41;
  upload->Begin();
  upload->UploadBuffer({.dstBuffer = buffer,
                        .dstOffset = 4,
                        .size = 4,
                        .data = &replacement,
                        .finalState = ResourceState::TransferSrc});
  upload->Flush();
  upload.reset();  // ComPtr ownership must not cause a double Release.
  auto &list = c.Begin();
  list.Barrier(
      BufferBarrier{.buffer = buffer, .newState = ResourceState::TransferSrc});
  list.CopyBuffer(buffer, readback, {0, 0, 8});
  BufferHandle textureReadback;
  ReadPixel(c, list, image, textureReadback);
  c.End(list);
  const auto *actual = static_cast<const u32 *>(c.device.MapBuffer(readback));
  Require(actual[0] == 17 && actual[1] == 41,
          "Repeated upload batches corrupted buffer data");
  c.device.UnmapBuffer(readback);
  const auto *texture =
      static_cast<const u8 *>(c.device.MapBuffer(textureReadback));
  Require(std::memcmp(texture, pixels.data(), 8) == 0 &&
              std::memcmp(texture + 256, pixels.data() + 8, 8) == 0,
          "Tightly packed checkerboard upload mismatch");
  c.device.UnmapBuffer(textureReadback);
  c.device.DestroyBuffer(textureReadback);
  c.device.DestroyBuffer(readback);
  c.device.DestroyBuffer(buffer);
  c.device.DestroyImage(image);
  c.Validate();
}

static void TextureUpload(Context &c) {
  std::array<u8, 256 * 16> pixels{};
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 16; ++x) {
      const u32 offset = y * 256 + x * 4;
      pixels[offset] = static_cast<u8>(x * 13);
      pixels[offset + 1] = static_cast<u8>(y * 11);
      pixels[offset + 2] = 73;
      pixels[offset + 3] = 255;
    }
  const auto upload =
      c.device.CreateBuffer({.size = pixels.size(),
                             .usage = BufferUsage::TransferSrc,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = pixels.data()});
  const auto image = c.device.CreateImage({.width = 16,
                                           .height = 16,
                                           .format = Format::RGBA8_SRGB,
                                           .usage = ImageUsage::TransferDst |
                                                    ImageUsage::TransferSrc |
                                                    ImageUsage::Sampled});
  auto &list = c.Begin();
  list.Barrier(
      ImageBarrier{.image = image, .newLayout = ImageLayout::TransferDst});
  Reject([&] {
    list.CopyBufferToImage(upload, image, {.imageExtent = {16, 16, 1}});
  });
  Reject([&] {
    list.CopyBufferToImage(
        upload, image,
        {.bufferOffset = 1, .bufferRowLength = 64, .imageExtent = {16, 16, 1}});
  });
  Reject([&] {
    list.CopyBufferToImage(upload, image,
                           {.bufferRowLength = 64, .imageExtent = {17, 16, 1}});
  });
  list.CopyBufferToImage(upload, image,
                         {.bufferRowLength = 64, .imageExtent = {16, 16, 1}});
  BufferHandle readback;
  ReadPixel(c, list, image, readback);
  c.End(list);
  const auto *actual = static_cast<const u8 *>(c.device.MapBuffer(readback));
  for (u32 y = 0; y < 16; ++y)
    Require(std::memcmp(actual + y * 256, pixels.data() + y * 256, 16 * 4) == 0,
            "Texture upload pixel mismatch");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  c.device.DestroyBuffer(upload);
  c.device.DestroyImage(image);
  c.Validate();
}

static void CheckColor(Context &c, BufferHandle readback,
                       std::array<u8, 4> expected, int tolerance = 0) {
  const auto *pixels = static_cast<const u8 *>(c.device.MapBuffer(readback));
  // All pixel fixtures are 16x16 RGBA8; D3D12 rows are aligned to 256 bytes.
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 16; ++x)
      for (u32 channel = 0; channel < 4; ++channel)
        Require(std::abs(int(pixels[y * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT +
                                    x * 4 + channel]) -
                         int(expected[channel])) <= tolerance,
                "Rendered pixel mismatch");
  c.device.UnmapBuffer(readback);
}

static void Graphics(Context &c) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS");
  const auto ps = c.Shader(ShaderStage::Fragment, "PS");
  const auto mrt = c.Shader(ShaderStage::Fragment, "MRT");
  auto render = [&](bool multiple, bool blend,
                    BlendOp operation = BlendOp::Add) {
    std::array<ImageHandle, 2> images;
    std::array<ImageViewHandle, 2> views;
    for (u32 i = 0; i < 2; ++i) {
      images[i] = c.device.CreateImage(
          {.width = 16,
           .height = 16,
           .format = Format::RGBA8_UNORM,
           .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      views[i] = c.device.CreateImageView({.image = images[i]});
    }
    GraphicsPipelineDesc desc{.vertexShader = vs,
                              .fragmentShader = multiple ? mrt : ps,
                              .raster = {.cullBackFaces = false},
                              .blend = {.enable = blend,
                                        .srcColor = BlendFactor::One,
                                        .dstColor = BlendFactor::One,
                                        .colorOp = operation,
                                        .srcAlpha = BlendFactor::One,
                                        .dstAlpha = BlendFactor::Zero},
                              .colorFormat = Format::RGBA8_UNORM};
    if (multiple)
      desc.colorAttachments = {{.format = Format::RGBA8_UNORM},
                               {.format = Format::RGBA8_UNORM}};
    const auto pipeline = c.device.CreateGraphicsPipeline(desc);
    const auto cached =
        c.device.CreateGraphicsPipeline(desc);  // Reload the persisted PSO.
    c.device.DestroyPipeline(cached);
    auto &list = c.Begin();
    std::array<ColorAttachmentDesc, 2> colors{
        {{.view = views[0], .clearValue = {0.25f, 0, 0, 1}},
         {.view = views[1]}}};
    for (auto image : images)
      list.Barrier(ImageBarrier{.image = image,
                                .newLayout = ImageLayout::ColorAttachment});
    list.SetViewport({.width = 16, .height = 16});
    list.SetScissor({.extent = {16, 16}});
    list.BeginRendering({.renderArea = {.extent = {16, 16}},
                         .colorAttachments = colors.data(),
                         .colorAttachmentCount = multiple ? 2u : 1u});
    list.BindPipeline(pipeline);
    list.Draw(3);
    list.EndRendering();
    std::array<BufferHandle, 2> readback;
    ReadPixel(c, list, images[0], readback[0]);
    if (multiple)
      ReadPixel(c, list, images[1], readback[1]);
    c.End(list);
    const auto expected = multiple ? std::array<u8, 4>{255, 0, 0, 255}
                          : operation == BlendOp::ReverseSubtract
                              ? std::array<u8, 4>{0, 0, 0, 255}
                              : std::array<u8, 4>{128, 128, 191, 255};
    CheckColor(c, readback[0], expected, multiple ? 0 : 1);
    if (multiple)
      CheckColor(c, readback[1], {0, 255, 0, 255});
    const auto *pixel = static_cast<u8 *>(c.device.MapBuffer(readback[0]));
    Require(multiple ? pixel[0] == 255 && pixel[1] == 0
            : operation == BlendOp::ReverseSubtract
                ? pixel[0] == 0 && pixel[1] == 0
                : pixel[0] >= 127 && pixel[0] <= 128 && pixel[1] >= 127 &&
                      pixel[1] <= 128,
            "Rendered MRT/blend pixel mismatch");
    c.device.UnmapBuffer(readback[0]);
    c.device.DestroyBuffer(readback[0]);
    if (multiple) {
      pixel = static_cast<u8 *>(c.device.MapBuffer(readback[1]));
      Require(pixel[0] == 0 && pixel[1] == 255, "Second MRT mismatch");
      c.device.UnmapBuffer(readback[1]);
      c.device.DestroyBuffer(readback[1]);
    }
    c.device.DestroyPipeline(pipeline);
    for (auto view : views)
      c.device.DestroyImageView(view);
    for (auto image : images)
      c.device.DestroyImage(image);
    c.Validate();
  };
  render(true, false);
  render(false, true);
  render(false, true, BlendOp::ReverseSubtract);
  for (auto topology :
       {PrimitiveTopology::TriangleList, PrimitiveTopology::TriangleStrip,
        PrimitiveTopology::LineList}) {
    auto pipeline =
        c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                         .fragmentShader = ps,
                                         .topology = topology,
                                         .colorFormat = Format::RGBA8_UNORM});
    c.device.DestroyPipeline(pipeline);
  }
  auto depthOnly = c.device.CreateGraphicsPipeline(
      {.vertexShader = vs, .depth = {true, true, Format::D32_FLOAT}});
  c.device.DestroyPipeline(depthOnly);
  auto layout = c.device.CreateBindingLayout({});
  auto withLayout =
      c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                       .fragmentShader = ps,
                                       .layout = {&layout, 1},
                                       .colorFormat = Format::RGBA8_UNORM});
  c.device.DestroyPipeline(withLayout);
  c.device.DestroyBindingLayout(layout);
  Reject([&] {
    c.device.CreateGraphicsPipeline({.vertexShader = ps,
                                     .fragmentShader = ps,
                                     .colorFormat = Format::RGBA8_UNORM});
  });
  for (auto shader : {vs, ps, mrt})
    c.device.DestroyShader(shader);
  c.Validate();
}

static void Sampling(Context &c) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "SamplePS");
  const BindingDesc binding{.binding = 0,
                            .type = BindingType::CombinedImageSampler,
                            .visibility = ShaderStage::Fragment};
  const auto layout = c.device.CreateBindingLayout({&binding, 1});
  const auto pipeline =
      c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                       .fragmentShader = ps,
                                       .layout = {&layout, 1},
                                       .raster = {.cullBackFaces = false},
                                       .colorFormat = Format::RGBA8_UNORM});
  const auto source = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled});
  const auto target = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto sourceView = c.device.CreateImageView({.image = source}),
             targetView = c.device.CreateImageView({.image = target});
  const auto sampler = c.device.CreateSampler(
      {.minFilter = Filter::Nearest, .magFilter = Filter::Nearest});
  const BindingPoolSize budget{BindingType::CombinedImageSampler, 1};
  const auto pool = c.device.CreateBindingPool({&budget, 1, 1});
  const auto set = c.device.AllocateBindingSet({pool, layout});
  const BindingImageInfo imageInfo{sampler, sourceView};
  c.device.UpdateBindingSet({.dstSet = set,
                             .binding = 0,
                             .type = BindingType::CombinedImageSampler,
                             .imageInfo = &imageInfo});
  auto &list = c.Begin();
  list.Barrier(
      ImageBarrier{.image = source, .newLayout = ImageLayout::ColorAttachment});
  const ColorAttachmentDesc clear{.view = sourceView,
                                  .clearValue = {0, 0, 1, 1}};
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &clear,
                       .colorAttachmentCount = 1});
  list.EndRendering();
  list.Barrier(
      ImageBarrier{.image = source, .newLayout = ImageLayout::ShaderReadOnly});
  list.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  const ColorAttachmentDesc color{.view = targetView};
  list.SetViewport({.width = 16, .height = 16});
  list.SetScissor({.extent = {16, 16}});
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  list.BindPipeline(pipeline);
  list.SetBindings(pipeline, 0, set);
  list.Draw(3);
  list.EndRendering();
  BufferHandle readback;
  ReadPixel(c, list, target, readback);
  c.End(list);
  CheckColor(c, readback, {0, 0, 255, 255});
  const auto *pixel = static_cast<u8 *>(c.device.MapBuffer(readback));
  Require(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 && pixel[3] == 255,
          "Sampled texture output mismatch");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  c.device.DestroyPipeline(pipeline);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.device.DestroySampler(sampler);
  c.device.DestroyBindingLayout(layout);
  c.device.DestroyBindingPool(pool);
  c.device.DestroyImageView(sourceView);
  c.device.DestroyImageView(targetView);
  c.device.DestroyImage(source);
  c.device.DestroyImage(target);
  c.Validate();
}

static void VertexInput(Context &c) {
  struct Vertex {
    float position[2];
    u8 color[4];
  };
  const std::array<Vertex, 5> vertices{{{{0, 0}, {0, 0, 0, 0}},
                                        {{-1, -1}, {255, 0, 0, 255}},
                                        {{-1, 3}, {255, 0, 0, 255}},
                                        {{3, -1}, {255, 0, 0, 255}},
                                        {{0, 0}, {0, 0, 0, 0}}}};
  const std::array<u16, 4> indices{99, 0, 1, 2};
  const auto vertexBuffer =
      c.device.CreateBuffer({.size = sizeof(vertices),
                             .usage = BufferUsage::Vertex,
                             .initialData = vertices.data()});
  const auto indexBuffer =
      c.device.CreateBuffer({.size = sizeof(indices),
                             .usage = BufferUsage::Index,
                             .initialData = indices.data()});
  const auto vs = c.Shader(ShaderStage::Vertex, "InputVS"),
             ps = c.Shader(ShaderStage::Fragment, "PS");
  GraphicsPipelineDesc desc{
      .vertexShader = vs,
      .fragmentShader = ps,
      .vertexLayouts = {{.stride = sizeof(Vertex),
                         .attributes = {{.format = VertexFormat::Float32x2,
                                         .offset = 0,
                                         .semanticIndex = 0},
                                        {.location = 1,
                                         .format = VertexFormat::UNorm8x4,
                                         .offset = 8,
                                         .semanticIndex = 1}}}},
      .topology = PrimitiveTopology::TriangleStrip,
      .raster = {.cullBackFaces = false},
      .colorFormat = Format::RGBA8_UNORM};
  const auto pipeline = c.device.CreateGraphicsPipeline(desc);
  auto secondDesc = desc;
  secondDesc.vertexLayouts[0].stride = sizeof(Vertex) * 2;
  const auto second = c.device.CreateGraphicsPipeline(secondDesc);
  const auto target = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto view = c.device.CreateImageView({.image = target});
  auto &list = c.Begin();
  list.BindVertexBuffer(
      0, vertexBuffer,
      sizeof(Vertex));  // Bind before any pipeline; skip the prefix vertex.
  list.BindIndexBuffer(indexBuffer, IndexType::U16,
                       sizeof(u16));  // Skip the prefix index.
  list.BindPipeline(second);
  list.BindPipeline(pipeline);  // Refresh stride from the final pipeline.
  list.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  const ColorAttachmentDesc color{.view = view};
  list.SetViewport({.width = 16, .height = 16});
  list.SetScissor({.extent = {16, 16}});
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  list.DrawIndexed(3);
  list.EndRendering();
  BufferHandle readback;
  ReadPixel(c, list, target, readback);
  c.End(list);
  CheckColor(c, readback, {255, 0, 0, 255});
  const auto *pixel = static_cast<u8 *>(c.device.MapBuffer(readback));
  Require(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0,
          "Vertex/index offset or normalized input mismatch");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  c.device.DestroyImageView(view);
  c.device.DestroyImage(target);
  for (auto p : {pipeline, second})
    c.device.DestroyPipeline(p);
  for (auto shader : {vs, ps})
    c.device.DestroyShader(shader);
  c.device.DestroyBuffer(vertexBuffer);
  c.device.DestroyBuffer(indexBuffer);
  c.Validate();
}

static void Presentation(Context &c) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "PS");
  const auto pipeline =
      c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                       .fragmentShader = ps,
                                       .raster = {.cullBackFaces = false},
                                       .colorFormat = Format::BGRA8_UNORM});
  const auto frame = c.device.BeginFrame(c.swapchain);
  auto &list = static_cast<DX::CommandList &>(c.device.GetCommandList());
  list.Begin();
  list.Barrier(ImageBarrier{.image = frame.backbufferImage,
                            .newLayout = ImageLayout::ColorAttachment});
  const ColorAttachmentDesc color{.view = frame.backbuffer};
  list.SetViewport({.width = 32, .height = 32});
  list.SetScissor({.extent = {32, 32}});
  list.BeginRendering({.renderArea = {.extent = {32, 32}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  list.BindPipeline(pipeline);
  list.Draw(3);
  list.EndRendering();
  list.Barrier(ImageBarrier{.image = frame.backbufferImage,
                            .newLayout = ImageLayout::Present});
  c.End(list);
  c.device.DestroyPipeline(pipeline);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.Validate();
}

static void Depth(Context &c) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "PS");
  const auto depthPipeline = c.device.CreateGraphicsPipeline(
      {.vertexShader = vs,
       .raster = {.cullBackFaces = false},
       .depth = {true, true, Format::D32_FLOAT}});
  const auto colorPipeline = c.device.CreateGraphicsPipeline(
      {.vertexShader = vs,
       .fragmentShader = ps,
       .raster = {.cullBackFaces = false},
       .depth = {true, false, Format::D32_FLOAT},
       .colorFormat = Format::RGBA8_UNORM});
  const auto depth = c.device.CreateImage({.width = 16,
                                           .height = 16,
                                           .format = Format::D32_FLOAT,
                                           .usage = ImageUsage::DepthStencil});
  const auto depthView =
      c.device.CreateImageView({.image = depth, .aspect = ImageAspect::Depth});
  const auto target = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto view = c.device.CreateImageView({.image = target});
  auto &list = c.Begin();
  list.SetViewport({.width = 16, .height = 16});
  list.SetScissor({.extent = {16, 16}});
  list.Barrier(
      ImageBarrier{.image = depth, .newLayout = ImageLayout::DepthAttachment});
  list.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  DepthAttachmentDesc attachment{.view = depthView, .clearDepth = 1};
  list.BeginRendering(
      {.renderArea = {.extent = {16, 16}}, .depthAttachment = &attachment});
  list.BindPipeline(depthPipeline);
  list.Draw(3);
  list.EndRendering();
  attachment.loadOp = LoadOp::Load;
  const ColorAttachmentDesc color{.view = view};
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1,
                       .depthAttachment = &attachment});
  list.BindPipeline(colorPipeline);
  list.Draw(3);
  list.EndRendering();
  BufferHandle readback;
  ReadPixel(c, list, target, readback);
  c.End(list);
  CheckColor(c, readback, {64, 128, 191, 255},
             1);  // Equal depth must pass, matching Vulkan LESS_OR_EQUAL.
  c.device.DestroyBuffer(readback);
  c.device.DestroyPipeline(depthPipeline);
  c.device.DestroyPipeline(colorPipeline);
  c.device.DestroyImageView(view);
  c.device.DestroyImageView(depthView);
  c.device.DestroyImage(target);
  c.device.DestroyImage(depth);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.Validate();
}

static void CacheRecovery() {
  Context c("cache/d3d12-cache-recovery");
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "PS");
  const GraphicsPipelineDesc desc{.vertexShader = vs,
                                  .fragmentShader = ps,
                                  .colorFormat = Format::RGBA8_UNORM};
  auto pipeline = c.device.CreateGraphicsPipeline(desc);
  c.device.DestroyPipeline(pipeline);
  c.Validate();
  std::vector<std::filesystem::path> files;
  for (const auto &file : std::filesystem::directory_iterator(
           "cache/d3d12-cache-recovery.d3d12")) {
    if (file.path().extension() != ".pso")
      continue;
    std::ofstream corrupt(file.path(), std::ios::binary | std::ios::trunc);
    corrupt.write("bad!", 4);
    Require(bool(corrupt), "Could not write deliberately invalid cache blob");
    files.push_back(file.path());
  }
  Require(!files.empty(), "Pipeline cache was not persisted");
  pipeline = c.device.CreateGraphicsPipeline(desc);
  // The deliberately corrupt native cache can emit a validation error before
  // the uncached retry succeeds. Only this expected failure's messages are
  // discarded.
  c.messages->ClearStoredMessages();
  bool repaired = false;
  for (const auto &file : files)
    if (std::filesystem::file_size(file) > 4)
      repaired = true;
  Require(repaired, "Rejected pipeline cache was not replaced");
  c.device.DestroyPipeline(pipeline);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.Validate();
}

static GeneratedPipelineLayout Reflected(
    Context &c, std::initializer_list<ShaderHandle> shaders) {
  std::vector<ShaderReflectionData> reflection;
  for (auto shader : shaders)
    reflection.push_back(c.device.GetShader(shader).reflection);
  return c.device.BuildPipelineLayout(
      ShaderCompiler::MergeShaderReflection(reflection));
}
static void ReleaseLayouts(Context &c, const GeneratedPipelineLayout &layouts) {
  for (auto layout : layouts.ownedSetLayouts)
    c.device.DestroyBindingLayout(layout);
}
static void CheckHalves(Context &c, BufferHandle readback,
                        std::array<u8, 4> left, std::array<u8, 4> right) {
  const auto *pixels = static_cast<const u8 *>(c.device.MapBuffer(readback));
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 16; ++x)
      for (u32 channel = 0; channel < 4; ++channel)
        Require(pixels[y * 256 + x * 4 + channel] ==
                    (x < 8 ? left : right)[channel],
                "Descriptor/push snapshot pixel mismatch");
  c.device.UnmapBuffer(readback);
}

static void UniformAndPush(Context &c) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "UniformPS");
  auto layouts = Reflected(c, {vs, ps});
  const auto pipeline = c.device.CreateGraphicsPipeline(
      {.vertexShader = vs,
       .fragmentShader = ps,
       .layout = {layouts.setLayouts.data(),
                  static_cast<u32>(layouts.setLayouts.size())},
       .raster = {.cullBackFaces = false},
       .colorFormat = Format::RGBA8_UNORM});
  const BindingPoolSize budget{BindingType::UniformBuffer, 1};
  const auto pool = c.device.CreateBindingPool({&budget, 1, 1});
  const auto set = c.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
  ReleaseLayouts(c, layouts);
  layouts.ownedSetLayouts
      .clear();  // Native pipelines/sets retain their layout descriptions.
  std::array<std::byte, 272> red{}, blue{};
  const std::array<float, 4> r{1, 0, 0, 1}, b{0, 0, 1, 1};
  std::memcpy(red.data() + 256, r.data(), 16);
  std::memcpy(blue.data() + 256, b.data(), 16);
  const auto redBuffer =
      c.device.CreateBuffer({.size = red.size(),
                             .usage = BufferUsage::Uniform,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = red.data()});
  const auto blueBuffer =
      c.device.CreateBuffer({.size = blue.size(),
                             .usage = BufferUsage::Uniform,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = blue.data()});
  const BindingBufferInfo info{redBuffer, 256, 16};
  c.device.UpdateBindingSet({.dstSet = set,
                             .binding = 1,
                             .type = BindingType::UniformBuffer,
                             .bufferInfo = &info});
  const BindingBufferInfo bad{redBuffer, 4, 16};
  Reject([&] {
    c.device.UpdateBindingSet({.dstSet = set,
                               .binding = 1,
                               .type = BindingType::UniformBuffer,
                               .bufferInfo = &bad});
  });
  const auto target = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto view = c.device.CreateImageView({.image = target});
  const ColorAttachmentDesc color{.view = view};
  auto &list = c.Begin();
  list.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  list.SetViewport({.width = 16, .height = 16});
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  list.BindPipeline(pipeline);
  list.SetBindings(pipeline, 0, set);
  list.SetScissor({.extent = {8, 16}});
  list.Draw(3);
  list.BindUniformBuffer(1, blueBuffer, 256, 16);
  list.SetScissor({.offset = {8, 0}, .extent = {8, 16}});
  list.Draw(3);
  list.EndRendering();
  c.device.DestroyBindingPool(
      pool);  // Snapshot lifetime is independent of pool lifetime.
  BufferHandle readback;
  ReadPixel(c, list, target, readback);
  c.End(list);
  CheckHalves(c, readback, {255, 0, 0, 255}, {0, 0, 255, 255});
  c.device.DestroyBuffer(readback);
  c.device.DestroyPipeline(pipeline);
  ReleaseLayouts(c, layouts);
  c.device.DestroyShader(ps);
  const auto pushPS = c.Shader(ShaderStage::Fragment, "PushPS");
  const auto pushPipeline =
      c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                       .fragmentShader = pushPS,
                                       .raster = {.cullBackFaces = false},
                                       .colorFormat = Format::RGBA8_UNORM});
  auto &push = c.Begin();
  push.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  push.SetViewport({.width = 16, .height = 16});
  push.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  push.BindPipeline(pushPipeline);
  push.SetScissor({.extent = {8, 16}});
  push.PushConstants(ShaderStage::Fragment, 0, 16, r.data());
  push.Draw(3);
  const std::array<float, 4> green{0, 1, 0, 1};
  const u32 one = 1;
  push.PushConstants(ShaderStage::Fragment, 0, 16, green.data());
  push.PushConstants(ShaderStage::Fragment, 16, 4, &one);
  Reject([&] { push.PushConstants(ShaderStage::Fragment, 1, 4, &one); });
  push.BindPipeline(pushPipeline);
  push.SetScissor({.offset = {8, 0}, .extent = {8, 16}});
  push.Draw(3);
  push.EndRendering();
  ReadPixel(c, push, target, readback);
  c.End(push);
  CheckHalves(c, readback, {255, 0, 0, 255}, {255, 255, 0, 255});
  c.device.DestroyBuffer(readback);
  c.device.DestroyPipeline(pushPipeline);
  c.device.DestroyShader(pushPS);
  c.device.DestroyShader(vs);
  c.device.DestroyImageView(view);
  c.device.DestroyImage(target);
  c.device.DestroyBuffer(redBuffer);
  c.device.DestroyBuffer(blueBuffer);
  c.Validate();
}

static void ComputeBindings(Context &c) {
  const auto graphicsVertex = c.Shader(ShaderStage::Vertex, "VS");
  const auto graphicsPixel = c.Shader(ShaderStage::Fragment, "PS");
  const auto graphicsPipeline =
      c.device.CreateGraphicsPipeline({.vertexShader = graphicsVertex,
                                       .fragmentShader = graphicsPixel,
                                       .raster = {.cullBackFaces = false},
                                       .colorFormat = Format::RGBA8_UNORM});
  const std::array<float, 8> data{9, 9, 9, 9, 1, 2, 3, 4};
  const auto input = c.device.CreateBuffer({.size = sizeof(data),
                                            .usage = BufferUsage::Storage,
                                            .initialData = data.data()});
  const auto output = c.device.CreateBuffer(
      {.size = 16, .usage = BufferUsage::Storage | BufferUsage::TransferSrc});
  const auto readback =
      c.device.CreateBuffer({.size = 16,
                             .usage = BufferUsage::TransferDst,
                             .memoryUsage = MemoryUsage::GPUToCPU});
  for (const auto *entry : {"RawCS", "StructuredCS", "PushCS"}) {
    const auto shader = c.Shader(ShaderStage::Compute, entry);
    auto layouts = Reflected(c, {shader});
    const auto pipeline = c.device.CreateComputePipeline(
        {.computeShader = shader,
         .layout = {layouts.setLayouts.data(),
                    static_cast<u32>(layouts.setLayouts.size())}});
    const BindingPoolSize sizes[]{{BindingType::StorageBuffer, 2},
                                  {BindingType::UniformBuffer, 1}};
    const auto pool = c.device.CreateBindingPool({sizes, 2, 1});
    const auto set = c.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
    Require(std::string(entry) != "PushCS" ||
                c.device.GetPipeline(pipeline).pushInline[2],
            "Small compute push block must use native root constants");
    const bool raw = std::string(entry) == "RawCS",
               pushed = std::string(entry) == "PushCS";
    const BindingBufferInfo src{input, 16, 16}, dst{output, 0, 16};
    BufferHandle uniform;
    if (pushed) {
      const std::array<float, 4> base{1, 2, 3, 4};
      uniform = c.device.CreateBuffer({.size = 16,
                                       .usage = BufferUsage::Uniform,
                                       .memoryUsage = MemoryUsage::CPUToGPU,
                                       .initialData = base.data()});
      const BindingBufferInfo ub{uniform, 0, 16};
      c.device.UpdateBindingSet({.dstSet = set,
                                 .binding = 1,
                                 .type = BindingType::UniformBuffer,
                                 .bufferInfo = &ub});
    } else
      c.device.UpdateBindingSet({.dstSet = set,
                                 .binding = raw ? 2u : 4u,
                                 .type = BindingType::StorageBuffer,
                                 .bufferInfo = &src});
    c.device.UpdateBindingSet({.dstSet = set,
                               .binding = raw ? 3u : 5u,
                               .type = BindingType::StorageBuffer,
                               .bufferInfo = &dst});
    auto &list = c.Begin();
    list.Barrier(
        BufferBarrier{.buffer = input, .newState = ResourceState::ShaderRead});
    list.Barrier(BufferBarrier{.buffer = output,
                               .newState = ResourceState::ShaderReadWrite});
    list.BindComputePipeline(pipeline);
    list.SetComputeBindings(pipeline, 0, set);
    if (pushed) {
      const std::array<float, 4> add{1, 1, 1, 1};
      list.PushConstants(ShaderStage::Compute, 0, 16, add.data());
    }
    list.BindComputePipeline(pipeline);
    list.SetComputeBindings(pipeline, 0, set);
    list.Dispatch(1, 1, 1);
    list.Barrier(BufferBarrier{.buffer = output,
                               .oldState = ResourceState::ShaderReadWrite,
                               .newState = ResourceState::ShaderReadWrite});
    // Graphics and compute share the native PSO slot, but keep separate roots.
    list.BindPipeline(graphicsPipeline);
    list.BindPipeline(graphicsPipeline);
    list.BindComputePipeline(pipeline);
    list.SetComputeBindings(pipeline, 0, set);
    list.Dispatch(1, 1, 1);
    list.Barrier(BufferBarrier{.buffer = output,
                               .newState = ResourceState::TransferSrc});
    list.CopyBuffer(output, readback, {0, 0, 16});
    c.End(list);
    const auto *result =
        static_cast<const float *>(c.device.MapBuffer(readback));
    for (u32 i = 0; i < 4; ++i)
      Require(result[i] == float(i + 1 + (pushed ? 1 : 0)),
              "Compute buffer descriptor/stride/push output mismatch");
    c.device.UnmapBuffer(readback);
    c.device.DestroyBindingPool(pool);
    c.device.DestroyPipeline(pipeline);
    ReleaseLayouts(c, layouts);
    c.device.DestroyShader(shader);
    if (uniform.IsValid())
      c.device.DestroyBuffer(uniform);
    c.Validate();
  }
  c.device.DestroyBuffer(input);
  c.device.DestroyBuffer(output);
  c.device.DestroyBuffer(readback);
  c.device.DestroyPipeline(graphicsPipeline);
  c.device.DestroyShader(graphicsVertex);
  c.device.DestroyShader(graphicsPixel);
}

static void DescriptorArrays(Context &c, bool updateAfterBind) {
  const auto vs = c.Shader(ShaderStage::Vertex, "VS"),
             ps = c.Shader(ShaderStage::Fragment, "ArrayPS");
  const BindingDesc arrayBindings[]{
      {.binding = 0,
       .type = BindingType::UniformBuffer,
       .visibility = ShaderStage::Fragment},
      {.binding = 1,
       .type = BindingType::SampledTexture,
       .count = 4,
       .visibility = ShaderStage::Fragment,
       .flags = BindingFlags::PartiallyBound | BindingFlags::VariableCount |
                (updateAfterBind ? BindingFlags::UpdateAfterBind
                                 : BindingFlags::None)}};
  const auto arrayLayout = c.device.CreateBindingLayout({arrayBindings, 2});
  const std::array<ShaderReflectionData, 2> reflected{
      c.device.GetShader(vs).reflection, c.device.GetShader(ps).reflection};
  const auto merged = ShaderCompiler::MergeShaderReflection(reflected);
  Reject([&] { c.device.BuildPipelineLayout(merged); });
  PipelineLayoutOverrides overrides;
  overrides.existingSetLayouts[1] = arrayLayout;
  auto layouts = c.device.BuildPipelineLayout(merged, overrides);
  const auto pipeline = c.device.CreateGraphicsPipeline(
      {.vertexShader = vs,
       .fragmentShader = ps,
       .layout = {layouts.setLayouts.data(),
                  static_cast<u32>(layouts.setLayouts.size())},
       .raster = {.cullBackFaces = false},
       .colorFormat = Format::RGBA8_UNORM});
  const BindingPoolSize budget[]{{BindingType::Sampler, 1},
                                 {BindingType::UniformBuffer, 1},
                                 {BindingType::SampledTexture, 2}};
  const auto pool = c.device.CreateBindingPool(
      {budget, 3, 2,
       updateAfterBind ? BindingPoolFlags::UpdateAfterBind
                       : BindingPoolFlags::None});
  Reject([&] { c.device.AllocateBindingSet({pool, arrayLayout, 5}); });
  const auto samplerSet =
      c.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
  const auto arraySet = c.device.AllocateBindingSet({pool, arrayLayout, 2});
  Reject([&] { c.device.AllocateBindingSet({pool, arrayLayout, 1}); });
  const u32 index = 1;
  const auto uniform =
      c.device.CreateBuffer({.size = 4,
                             .usage = BufferUsage::Uniform,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = &index});
  const BindingBufferInfo bufferInfo{uniform, 0, 4};
  c.device.UpdateBindingSet({.dstSet = arraySet,
                             .binding = 0,
                             .type = BindingType::UniformBuffer,
                             .bufferInfo = &bufferInfo});
  const auto sampler = c.device.CreateSampler({});
  const BindingImageInfo samplerInfo{.sampler = sampler};
  c.device.UpdateBindingSet({.dstSet = samplerSet,
                             .binding = 0,
                             .type = BindingType::Sampler,
                             .imageInfo = &samplerInfo});
  std::array<ImageHandle, 3> images;
  std::array<ImageViewHandle, 3> views;
  for (u32 i = 0; i < 3; ++i) {
    images[i] = c.device.CreateImage({.width = 16,
                                      .height = 16,
                                      .format = Format::RGBA8_UNORM,
                                      .usage = ImageUsage::ColorAttachment |
                                               ImageUsage::Sampled |
                                               ImageUsage::TransferSrc});
    views[i] = c.device.CreateImageView({.image = images[i]});
  }
  const BindingImageInfo red{.imageView = views[0]},
      blue{.imageView = views[1]};
  c.device.UpdateBindingSet({.dstSet = arraySet,
                             .binding = 1,
                             .arrayElement = 1,
                             .type = BindingType::SampledTexture,
                             .imageInfo = &red});
  Reject([&] {
    c.device.UpdateBindingSet({.dstSet = arraySet,
                               .binding = 1,
                               .arrayElement = 2,
                               .type = BindingType::SampledTexture,
                               .imageInfo = &blue});
  });
  auto &list = c.Begin();
  for (u32 i = 0; i < 2; ++i) {
    list.Barrier(ImageBarrier{.image = images[i],
                              .newLayout = ImageLayout::ColorAttachment});
    const ColorAttachmentDesc clear{
        .view = views[i],
        .clearValue = i ? ClearColor{0, 0, 1, 1} : ClearColor{1, 0, 0, 1}};
    list.BeginRendering({.renderArea = {.extent = {16, 16}},
                         .colorAttachments = &clear,
                         .colorAttachmentCount = 1});
    list.EndRendering();
    list.Barrier(ImageBarrier{.image = images[i],
                              .newLayout = ImageLayout::ShaderReadOnly});
  }
  auto draw = [&](DX::CommandList &commands) {
    commands.Barrier(ImageBarrier{.image = images[2],
                                  .newLayout = ImageLayout::ColorAttachment});
    const ColorAttachmentDesc color{.view = views[2]};
    commands.SetViewport({.width = 16, .height = 16});
    commands.SetScissor({.extent = {16, 16}});
    commands.BeginRendering({.renderArea = {.extent = {16, 16}},
                             .colorAttachments = &color,
                             .colorAttachmentCount = 1});
    commands.BindPipeline(pipeline);
    commands.SetBindings(pipeline, 0, samplerSet);
    commands.SetBindings(pipeline, 1, arraySet);
    commands.Draw(3);
    commands.EndRendering();
  };
  draw(list);
  c.device.UpdateBindingSet({.dstSet = arraySet,
                             .binding = 1,
                             .arrayElement = 1,
                             .type = BindingType::SampledTexture,
                             .imageInfo = &blue});
  BufferHandle first, second;
  ReadPixel(c, list, images[2], first);
  list.End();
  c.device.SubmitAndPresent(
      c.swapchain);  // Keep the first frame in flight during the next update.
  if (updateAfterBind)
    c.device.UpdateBindingSet({.dstSet = arraySet,
                               .binding = 1,
                               .arrayElement = 1,
                               .type = BindingType::SampledTexture,
                               .imageInfo = &red});
  auto &next = c.Begin();
  draw(next);
  ReadPixel(c, next, images[2], second);
  next.End();
  c.device.SubmitAndPresent(c.swapchain);
  c.device.DestroyBindingPool(pool);
  c.device.WaitIdle();
  CheckColor(c, first,
             updateAfterBind ? std::array<u8, 4>{0, 0, 255, 255}
                             : std::array<u8, 4>{255, 0, 0, 255});
  CheckColor(c, second,
             updateAfterBind ? std::array<u8, 4>{255, 0, 0, 255}
                             : std::array<u8, 4>{0, 0, 255, 255});
  c.device.DestroyBuffer(first);
  c.device.DestroyBuffer(second);
  c.device.DestroyBuffer(uniform);
  c.device.DestroySampler(sampler);
  for (auto view : views)
    c.device.DestroyImageView(view);
  for (auto image : images)
    c.device.DestroyImage(image);
  c.device.DestroyPipeline(pipeline);
  ReleaseLayouts(c, layouts);
  c.device.DestroyBindingLayout(arrayLayout);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.Validate();
}

static void StorageImages(Context &c) {
  const auto shader = c.Shader(ShaderStage::Compute, "ImageCS");
  const BindingDesc binding{.binding = 6,
                            .type = BindingType::StorageImage,
                            .visibility = ShaderStage::Compute,
                            .flags = BindingFlags::UpdateAfterBind};
  const auto layout = c.device.CreateBindingLayout({&binding, 1});
  const auto pipeline = c.device.CreateComputePipeline(
      {.computeShader = shader, .layout = {&layout, 1}});
  const BindingPoolSize budget{BindingType::StorageImage, 1};
  const auto pool = c.device.CreateBindingPool(
      {&budget, 1, 1, BindingPoolFlags::UpdateAfterBind});
  const auto set = c.device.AllocateBindingSet({pool, layout});
  std::array<ImageHandle, 2> images;
  std::array<ImageViewHandle, 2> views;
  for (u32 i = 0; i < 2; ++i) {
    images[i] = c.device.CreateImage({.width = 16,
                                      .height = 16,
                                      .format = Format::RGBA8_UNORM,
                                      .usage = ImageUsage::Storage |
                                               ImageUsage::TransferSrc |
                                               ImageUsage::ColorAttachment});
    views[i] = c.device.CreateImageView({.image = images[i]});
  }
  const BindingImageInfo before{.imageView = views[0],
                                .imageLayout = ImageLayout::General},
      after{.imageView = views[1], .imageLayout = ImageLayout::General};
  c.device.UpdateBindingSet({.dstSet = set,
                             .binding = 6,
                             .type = BindingType::StorageTexture,
                             .imageInfo = &before});
  auto &list = c.Begin();
  for (u32 i = 0; i < 2; ++i) {
    list.Barrier(ImageBarrier{.image = images[i],
                              .newLayout = ImageLayout::ColorAttachment});
    const ColorAttachmentDesc clear{.view = views[i],
                                    .clearValue = {0, 0, 0, 1}};
    list.BeginRendering({.renderArea = {.extent = {16, 16}},
                         .colorAttachments = &clear,
                         .colorAttachmentCount = 1});
    list.EndRendering();
    list.Barrier(
        ImageBarrier{.image = images[i], .newLayout = ImageLayout::General});
  }
  list.BindComputePipeline(pipeline);
  list.SetComputeBindings(pipeline, 0, set);
  c.device.UpdateBindingSet({.dstSet = set,
                             .binding = 6,
                             .type = BindingType::StorageImage,
                             .imageInfo = &after});
  list.Dispatch(16, 16, 1);
  std::array<BufferHandle, 2> readback;
  for (u32 i = 0; i < 2; ++i)
    ReadPixel(c, list, images[i], readback[i]);
  c.device.DestroyBindingPool(pool);
  c.End(list);
  CheckColor(c, readback[0], {0, 0, 0, 255});
  CheckColor(c, readback[1], {0, 255, 0, 255});
  c.device.DestroyPipeline(pipeline);
  c.device.DestroyBindingLayout(layout);
  c.device.DestroyShader(shader);
  for (auto buffer : readback)
    c.device.DestroyBuffer(buffer);
  for (auto view : views)
    c.device.DestroyImageView(view);
  for (auto image : images)
    c.device.DestroyImage(image);
  c.Validate();
}

static void StagePushConstants(Context &c, bool overflow = false) {
  const auto vs = c.Shader(ShaderStage::Vertex,
                           overflow ? "WidePushVS" : "PushVS"),
             ps = c.Shader(ShaderStage::Fragment,
                           overflow ? "WideStagePS" : "StagePS");
  const auto pipeline =
      c.device.CreateGraphicsPipeline({.vertexShader = vs,
                                       .fragmentShader = ps,
                                       .raster = {.cullBackFaces = false},
                                       .colorFormat = Format::RGBA8_UNORM});
  Require(c.device.GetPipeline(pipeline).pushInline[0] == !overflow &&
              c.device.GetPipeline(pipeline).pushInline[1],
          "Stage constants must spill only when the root budget requires it");
  const auto target = c.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto view = c.device.CreateImageView({.image = target});
  const ColorAttachmentDesc color{.view = view};
  auto &list = c.Begin();
  list.Barrier(
      ImageBarrier{.image = target, .newLayout = ImageLayout::ColorAttachment});
  list.SetViewport({.width = 16, .height = 16});
  list.SetScissor({.extent = {16, 16}});
  list.BeginRendering({.renderArea = {.extent = {16, 16}},
                       .colorAttachments = &color,
                       .colorAttachmentCount = 1});
  list.BindPipeline(pipeline);
  const std::array<float, 4> vertex{1, 0, 0, 1}, fragment{0, 1, 0, 1};
  list.PushConstants(ShaderStage::Vertex, 0, 16, vertex.data());
  list.PushConstants(ShaderStage::Fragment, 0, 16, fragment.data());
  list.Draw(3);
  list.EndRendering();
  BufferHandle readback;
  ReadPixel(c, list, target, readback);
  c.End(list);
  CheckColor(c, readback, {255, 255, 0, 255});
  c.device.DestroyBuffer(readback);
  c.device.DestroyImageView(view);
  c.device.DestroyImage(target);
  c.device.DestroyPipeline(pipeline);
  c.device.DestroyShader(vs);
  c.device.DestroyShader(ps);
  c.Validate();
}

static void RootConstantFallbacks(Context &c) {
  constexpr u32 dispatchCount = 2200;
  const u64 bufferSize = dispatchCount * 16;
  const auto output = c.device.CreateBuffer(
      {.size = bufferSize,
       .usage = BufferUsage::Storage | BufferUsage::TransferSrc});
  const auto readback =
      c.device.CreateBuffer({.size = bufferSize,
                             .usage = BufferUsage::TransferDst,
                             .memoryUsage = MemoryUsage::GPUToCPU});
  for (const char *entry : {"LargePushCS", "ArrayPushCS"}) {
    const auto shader = c.Shader(ShaderStage::Compute, entry);
    auto layouts = Reflected(c, {shader});
    const auto pipeline = c.device.CreateComputePipeline(
        {.computeShader = shader,
         .layout = {layouts.setLayouts.data(),
                    static_cast<u32>(layouts.setLayouts.size())}});
    const auto &native = c.device.GetPipeline(pipeline);
    Require(!native.pushInline[2], "Invalid block must use the CBV fallback");
    const BindingPoolSize poolSize{BindingType::StorageBuffer, 1};
    const auto pool = c.device.CreateBindingPool({&poolSize, 1, 1});
    const auto set = c.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
    const BindingBufferInfo bufferInfo{output, 0, bufferSize};
    c.device.UpdateBindingSet({.dstSet = set,
                               .binding = 5,
                               .type = BindingType::StorageBuffer,
                               .bufferInfo = &bufferInfo});
    const bool large = std::string(entry) == "LargePushCS";
    // Exercise page rollover, partial writes, frame slots and the compute
    // queue.
    for (u32 frame = 0; frame < 5; ++frame) {
      const bool auxiliary = frame == 4;
      auto &commands = auxiliary
                           ? c.device.AcquireCommandList(QueueType::Compute)
                           : c.Begin();
      if (auxiliary)
        commands.Begin();
      commands.Barrier(BufferBarrier{
          .buffer = output, .newState = ResourceState::ShaderReadWrite});
      commands.BindComputePipeline(pipeline);
      commands.SetComputeBindings(pipeline, 0, set);
      std::vector<float> constants(native.pushSizes[2] / 4, 0);
      constants[1] = float(frame + 7);
      constants.back() = 42;
      commands.PushConstants(ShaderStage::Compute, 0, native.pushSizes[2],
                             constants.data());
      const u32 count = large ? dispatchCount : 1;
      for (u32 index = 0; index < count; ++index) {
        if (large)
          commands.PushConstants(ShaderStage::Compute, 0, sizeof(index),
                                 &index);
        commands.Dispatch(1, 1, 1);
      }
      commands.Barrier(BufferBarrier{.buffer = output,
                                     .newState = ResourceState::TransferSrc});
      commands.CopyBuffer(output, readback, {0, 0, bufferSize});
      if (auxiliary) {
        commands.End();
        c.device.Submit(QueueType::Compute, commands, {});
        c.device.WaitIdle();
      } else {
        c.End(static_cast<DX::CommandList &>(commands));
      }
      const auto *values =
          static_cast<const float *>(c.device.MapBuffer(readback));
      for (u32 index = 0; index < count; ++index) {
        Require(large ? values[index * 4] == float(index) &&
                            values[index * 4 + 1] == float(frame + 7) &&
                            values[index * 4 + 2] == 42
                      : values[3] == 42,
                "Fallback push constants lost partial writes or GPU snapshots");
      }
      c.device.UnmapBuffer(readback);
      c.Validate();
    }
    c.device.DestroyBindingPool(pool);
    c.device.DestroyPipeline(pipeline);
    ReleaseLayouts(c, layouts);
    c.device.DestroyShader(shader);
  }
  c.device.DestroyBuffer(output);
  c.device.DestroyBuffer(readback);
  std::cout << "PASS root constant overflow/array fallback, partial writes, "
               "upload page rollover, frame reuse and compute queue\n";
}

static void SparseImages(Context &c) {
  constexpr u64 pageSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  ImageDesc colorDesc{.width = 512,
                      .height = 512,
                      .mipLevels = 2,
                      .arrayLayers = 2,
                      .format = Format::RGBA8_UNORM,
                      .usage = ImageUsage::ColorAttachment |
                               ImageUsage::Sampled | ImageUsage::TransferSrc};
  Reject([&] { c.device.CreateSparseImage(colorDesc, pageSize - 1); });
  auto invalidDesc = colorDesc;
  invalidDesc.width = 0;
  Reject([&] { c.device.CreateSparseImage(invalidDesc, pageSize); });
  invalidDesc = colorDesc;
  invalidDesc.mipLevels = 10;  // Includes a packed mip tail.
  Reject([&] { c.device.CreateSparseImage(invalidDesc, pageSize); });
  const auto ordinary = c.device.CreateImage(colorDesc);
  Reject([&] { c.device.GetSparseImageMemoryInfo(ordinary); });
  Reject([&] { c.device.BindSparseImagePages(ordinary, {}); });
  c.device.DestroyImage(ordinary);

  const auto image = c.device.CreateSparseImage(colorDesc, pageSize * 2 + 100);
  const auto info = c.device.GetSparseImageMemoryInfo(image);
  Require(info.pageWidth && info.pageHeight && info.pageSize == pageSize &&
              info.pageCount == 2,
          "Sparse image page shape/budget mismatch");
  const auto &nativeImage = c.device.GetImage(image);
  Require(nativeImage.resourcePtr->GetDesc().Layout ==
              D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE,
          "Sparse image is not a reserved tiled texture");
  const SparseImagePageBind origin{.memoryPage = 0};
  c.device.BindSparseImagePages(image, {&origin, 1});
  c.device.BindSparseImagePages(
      image, {&origin, 1});  // Unchanged residency is a no-op.
  const std::array duplicate{origin, origin};
  Reject([&] { c.device.BindSparseImagePages(image, duplicate); });
  auto invalidPage = origin;
  invalidPage.memoryPage = info.pageCount;
  Reject([&] { c.device.BindSparseImagePages(image, {&invalidPage, 1}); });
  invalidPage = origin;
  invalidPage.blockX = (colorDesc.width + info.pageWidth - 1) / info.pageWidth;
  Reject([&] { c.device.BindSparseImagePages(image, {&invalidPage, 1}); });
  invalidPage = origin;
  invalidPage.mipLevel = colorDesc.mipLevels;
  Reject([&] { c.device.BindSparseImagePages(image, {&invalidPage, 1}); });
  invalidPage = origin;
  invalidPage.arrayLayer = colorDesc.arrayLayers;
  Reject([&] { c.device.BindSparseImagePages(image, {&invalidPage, 1}); });
  Require(nativeImage.sparseBindings.size() == 1,
          "Invalid binding changed residency");

  const auto view = c.device.CreateImageView({.image = image});
  auto &clear = c.Begin();
  clear.Barrier(
      ImageBarrier{.image = image, .newLayout = ImageLayout::ColorAttachment});
  const ColorAttachmentDesc attachment{.view = view,
                                       .clearValue = {1, 0, 0, 1}};
  clear.BeginRendering({.renderArea = {.extent = {512, 512}},
                        .colorAttachments = &attachment,
                        .colorAttachmentCount = 1});
  clear.EndRendering();
  BufferHandle readback;
  ReadPixel(c, clear, image, readback);
  c.End(clear);
  const auto *pixels = static_cast<const u8 *>(c.device.MapBuffer(readback));
  Require(pixels[0] == 255 && pixels[1] == 0 && pixels[3] == 255,
          "Mapped sparse tile did not receive the clear");
  Require(
      pixels[info.pageWidth * 4] == 0 && pixels[info.pageWidth * 4 + 3] == 0,
      "Unmapped sparse tile did not read as zero");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);

  // Move the physical page to a different array layer and mip, unmapping the
  // old tile.
  const SparseImagePageBind relocated{
      .mipLevel = 1, .arrayLayer = 1, .memoryPage = 0};
  c.device.BindSparseImagePages(image, {&relocated, 1});
  Require(nativeImage.sparseBindings.size() == 1,
          "Old sparse binding was not removed");
  const auto relocatedView = c.device.CreateImageView(
      {.image = image, .baseMipLevel = 1, .baseArrayLayer = 1});
  auto &relocatedClear = c.Begin();
  relocatedClear.Barrier(ImageBarrier{.image = image,
                                      .newLayout = ImageLayout::ColorAttachment,
                                      .baseMipLevel = 1,
                                      .baseArrayLayer = 1});
  const ColorAttachmentDesc relocatedAttachment{.view = relocatedView,
                                                .clearValue = {0, 1, 0, 1}};
  relocatedClear.BeginRendering({.renderArea = {.extent = {256, 256}},
                                 .colorAttachments = &relocatedAttachment,
                                 .colorAttachmentCount = 1});
  relocatedClear.EndRendering();
  BufferHandle relocatedReadback;
  ReadPixel(c, relocatedClear, image, relocatedReadback,
            1 + colorDesc.mipLevels);
  ReadPixel(c, relocatedClear, image, readback);
  c.End(relocatedClear);
  pixels = static_cast<const u8 *>(c.device.MapBuffer(relocatedReadback));
  Require(pixels[0] == 0 && pixels[1] == 255 && pixels[3] == 255,
          "Sparse mapping addressed the wrong mip or array layer");
  c.device.UnmapBuffer(relocatedReadback);
  c.device.DestroyBuffer(relocatedReadback);
  pixels = static_cast<const u8 *>(c.device.MapBuffer(readback));
  Require(pixels[0] == 0 && pixels[3] == 0,
          "Old sparse tile remained resident");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  // Replacing the physical page for the same virtual block is also supported.
  auto replacement = relocated;
  replacement.memoryPage = 1;
  c.device.BindSparseImagePages(image, {&replacement, 1});
  Require(nativeImage.sparseBindings.begin()->second == 1,
          "Sparse remap did not replace the page");
  c.device.BindSparseImagePages(image, {});
  Require(nativeImage.sparseBindings.empty(),
          "Empty residency set did not unmap all pages");
  c.device.DestroyImageView(relocatedView);
  c.device.DestroyImageView(view);
  c.device.DestroyImage(image);
  c.Validate();

  // Match the engine's four-tier depth cubemap, including an array-layer DSV
  // and cube SRV.
  const auto depth = c.device.CreateSparseImage(
      {.width = 1024,
       .height = 1024,
       .mipLevels = 4,
       .arrayLayers = 6,
       .format = Format::D32_FLOAT,
       .type = ImageType::Cube,
       .usage = ImageUsage::DepthStencil | ImageUsage::Sampled |
                ImageUsage::TransferSrc},
      pageSize * 6);
  const SparseImagePageBind depthPage{
      .mipLevel = 3, .arrayLayer = 5, .memoryPage = 0};
  c.device.BindSparseImagePages(depth, {&depthPage, 1});
  const auto cubeView = c.device.CreateImageView({.image = depth,
                                                  .type = ImageViewType::Cube,
                                                  .aspect = ImageAspect::Depth,
                                                  .mipLevelCount = 4,
                                                  .arrayLayerCount = 6});
  const auto depthView = c.device.CreateImageView({.image = depth,
                                                   .aspect = ImageAspect::Depth,
                                                   .baseMipLevel = 3,
                                                   .baseArrayLayer = 5});
  auto &depthClear = c.Begin();
  depthClear.Barrier(ImageBarrier{.image = depth,
                                  .newLayout = ImageLayout::DepthAttachment,
                                  .aspect = ImageAspect::Depth,
                                  .baseMipLevel = 3,
                                  .baseArrayLayer = 5});
  const DepthAttachmentDesc depthAttachment{.view = depthView,
                                            .clearDepth = 0.375f};
  depthClear.BeginRendering({.renderArea = {.extent = {128, 128}},
                             .depthAttachment = &depthAttachment});
  depthClear.EndRendering();
  ReadPixel(c, depthClear, depth, readback, 3 + 5 * 4);
  c.End(depthClear);
  const auto *depthValues =
      static_cast<const float *>(c.device.MapBuffer(readback));
  Require(depthValues[0] == 0.375f,
          "Sparse depth tile did not receive the depth clear");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  c.device.BindSparseImagePages(depth, {});
  c.device.DestroyImageView(depthView);
  c.device.DestroyImageView(cubeView);
  c.device.DestroyImage(depth);
  c.Validate();

  // Sampled/upload textures use the non-render-target heap class.
  const auto sampled = c.device.CreateSparseImage(
      {.width = 512,
       .height = 512,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::Sampled | ImageUsage::TransferDst |
                ImageUsage::TransferSrc},
      pageSize);
  c.device.BindSparseImagePages(sampled, {&origin, 1});
  D3D12_HEAP_DESC sampledHeap =
      c.device.GetImage(sampled).sparseHeap->GetDesc();
  Require((sampledHeap.Flags & D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES) ==
              D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES,
          "Sparse sampled texture used the wrong heap class");
  const std::array<u8, 4> uploadedPixel{17, 83, 191, 255};
  auto upload = c.device.CreateUploadContext(4096);
  upload->Begin();
  upload->UploadImage({.dstImage = sampled, .width = 1, .height = 1},
                      uploadedPixel.data(), uploadedPixel.size());
  upload->Flush();
  auto &sampledReadback = c.Begin();
  ReadPixel(c, sampledReadback, sampled, readback);
  c.End(sampledReadback);
  pixels = static_cast<const u8 *>(c.device.MapBuffer(readback));
  Require(std::memcmp(pixels, uploadedPixel.data(), uploadedPixel.size()) == 0,
          "Sparse sampled texture upload/readback mismatch");
  c.device.UnmapBuffer(readback);
  c.device.DestroyBuffer(readback);
  c.device.DestroyImage(sampled);
  c.Validate();
}

static void AuxiliaryQueues(Context &c) {
  Require(c.device.GetGraphicsQueue()->GetDesc().Type ==
              D3D12_COMMAND_LIST_TYPE_DIRECT,
          "Graphics queue has the wrong native type");
  Require(c.device.GetComputeQueue()->GetDesc().Type ==
              D3D12_COMMAND_LIST_TYPE_COMPUTE,
          "Compute queue has the wrong native type");
  Require(c.device.GetTransferQueue()->GetDesc().Type ==
              D3D12_COMMAND_LIST_TYPE_COPY,
          "Transfer queue has the wrong native type");
  Require(
      c.device.GetComputeQueue().Get() != c.device.GetGraphicsQueue().Get() &&
          c.device.GetTransferQueue().Get() !=
              c.device.GetGraphicsQueue().Get(),
      "Auxiliary queues alias the graphics queue");
  Require(c.device.GetQueueInfo(QueueType::Compute).dedicated &&
              !c.device.GetQueueInfo(QueueType::Transfer).aliasesGraphics,
          "Queue capabilities report graphics aliases");
  Require(
      c.device.GetQueueRelationship(QueueType::Compute, QueueType::Transfer) ==
          QueueRelationship::SameFamily,
      "D3D12 queues must be distinct without family ownership transfers");

  const auto input = c.device.CreateBuffer(
      {.size = 16, .usage = BufferUsage::TransferDst | BufferUsage::Storage});
  const auto output = c.device.CreateBuffer(
      {.size = 16, .usage = BufferUsage::Storage | BufferUsage::TransferSrc});
  const auto readback =
      c.device.CreateBuffer({.size = 16,
                             .usage = BufferUsage::TransferDst,
                             .memoryUsage = MemoryUsage::GPUToCPU});
  const auto shader = c.Shader(ShaderStage::Compute, "RawCS");
  auto layouts = Reflected(c, {shader});
  const auto pipeline = c.device.CreateComputePipeline(
      {.computeShader = shader,
       .layout = {layouts.setLayouts.data(),
                  static_cast<u32>(layouts.setLayouts.size())}});
  DX::ComPtr<ID3D12Fence> dependency;
  DX::ThrowIfFailed(c.device.GetNativeDevice()->CreateFence(
                        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dependency)),
                    "Create queue dependency fence");

  for (u32 iteration = 0; iteration < 3; ++iteration) {
    const std::array<u32, 4> expected{11 + iteration, 22 + iteration,
                                      33 + iteration, 44 + iteration};
    auto &transfer = c.device.AcquireCommandList(QueueType::Transfer);
    Require(static_cast<DX::CommandList &>(transfer).GetNative()->GetType() ==
                D3D12_COMMAND_LIST_TYPE_COPY,
            "Transfer command list has the wrong type");
    Reject([&] { c.device.AcquireCommandList(QueueType::Transfer); });
    transfer.Begin();
    Reject([&] { transfer.BindComputePipeline(pipeline); });
    transfer.UpdateBuffer(
        {.buffer = input, .data = expected.data(), .size = sizeof(expected)});
    transfer.End();
    Reject([&] { c.device.Submit(QueueType::Compute, transfer); });
    c.device.Submit(QueueType::Transfer, transfer);
    DX::ThrowIfFailed(c.device.GetTransferQueue()->Signal(dependency.Get(),
                                                          iteration * 2 + 1),
                      "Signal copy completion");

    const BindingPoolSize budget{BindingType::StorageBuffer, 2};
    const auto pool = c.device.CreateBindingPool({&budget, 1, 1});
    const auto set = c.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
    const BindingBufferInfo source{input, 0, 16}, destination{output, 0, 16};
    c.device.UpdateBindingSet({.dstSet = set,
                               .binding = 2,
                               .type = BindingType::StorageBuffer,
                               .bufferInfo = &source});
    c.device.UpdateBindingSet({.dstSet = set,
                               .binding = 3,
                               .type = BindingType::StorageBuffer,
                               .bufferInfo = &destination});
    DX::ThrowIfFailed(
        c.device.GetComputeQueue()->Wait(dependency.Get(), iteration * 2 + 1),
        "Wait for copy");
    auto &compute = c.device.AcquireCommandList(QueueType::Compute);
    Require(static_cast<DX::CommandList &>(compute).GetNative()->GetType() ==
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
            "Compute command list has the wrong type");
    compute.Begin();
    Reject([&] { compute.BeginRendering({}); });
    compute.Barrier(
        BufferBarrier{.buffer = input, .newState = ResourceState::ShaderRead});
    compute.Barrier(BufferBarrier{.buffer = output,
                                  .newState = ResourceState::ShaderReadWrite});
    compute.BindComputePipeline(pipeline);
    compute.SetComputeBindings(pipeline, 0, set);
    compute.Dispatch(1, 1, 1);
    compute.End();
    c.device.Submit(QueueType::Compute, compute);
    c.device.DestroyBindingPool(
        pool);  // In-flight compute descriptors remain retained.
    DX::ThrowIfFailed(
        c.device.GetComputeQueue()->Signal(dependency.Get(), iteration * 2 + 2),
        "Signal compute completion");

    auto &download = c.device.AcquireCommandList(QueueType::Transfer);
    download.Begin();
    download.Barrier(BufferBarrier{.buffer = output,
                                   .newState = ResourceState::TransferSrc});
    download.CopyBuffer(output, readback, {0, 0, 16});
    download.End();
    DX::ThrowIfFailed(
        c.device.GetTransferQueue()->Wait(dependency.Get(), iteration * 2 + 2),
        "Wait for compute");
    c.device.Submit(QueueType::Transfer, download);
    c.device.WaitIdle();
    const auto *actual = c.device.MapBuffer(readback);
    Require(std::memcmp(actual, expected.data(), sizeof(expected)) == 0,
            "Copy/compute/copy queue chain produced incorrect data");
    c.device.UnmapBuffer(readback);
    c.Validate();
  }
  c.device.DestroyPipeline(pipeline);
  ReleaseLayouts(c, layouts);
  c.device.DestroyShader(shader);
  c.device.DestroyBuffer(input);
  c.device.DestroyBuffer(output);
  c.device.DestroyBuffer(readback);

  const auto texture = c.device.CreateImage(
      {.width = 2,
       .height = 2,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc});
  std::array<u8, 512> paddedPixels{};
  for (u32 row = 0; row < 2; ++row) {
    for (u32 column = 0; column < 2; ++column) {
      const u32 offset = row * 256 + column * 4;
      paddedPixels[offset] = 17;
      paddedPixels[offset + 1] = 83;
      paddedPixels[offset + 2] = 191;
      paddedPixels[offset + 3] = 255;
    }
  }
  const auto staging =
      c.device.CreateBuffer({.size = paddedPixels.size(),
                             .usage = BufferUsage::TransferSrc,
                             .memoryUsage = MemoryUsage::CPUToGPU,
                             .initialData = paddedPixels.data()});
  auto &upload = c.device.AcquireCommandList(QueueType::Transfer);
  upload.Begin();
  upload.Barrier(
      ImageBarrier{.image = texture, .newLayout = ImageLayout::TransferDst});
  upload.CopyBufferToImage(staging, texture,
                           {.bufferRowLength = 64,
                            .bufferImageHeight = 2,
                            .imageExtent = {2, 2, 1}});
  upload.End();
  c.device.Submit(QueueType::Transfer, upload);
  c.device.WaitIdle();
  Require(c.device.GetImage(texture).state == D3D12_RESOURCE_STATE_COMMON,
          "Copy-queue texture state did not decay to COMMON");
  auto &graphics = c.Begin();
  BufferHandle textureReadback;
  ReadPixel(c, graphics, texture, textureReadback);
  c.End(graphics);
  const auto *uploaded =
      static_cast<const u8 *>(c.device.MapBuffer(textureReadback));
  Require(
      std::memcmp(uploaded, paddedPixels.data(), 8) == 0 &&
          std::memcmp(uploaded + 256, paddedPixels.data() + 256, 8) == 0,
      "Copy-queue texture upload did not survive graphics-queue acquisition");
  c.device.UnmapBuffer(textureReadback);
  c.device.DestroyBuffer(textureReadback);
  c.device.DestroyBuffer(staging);
  c.device.DestroyImage(texture);
  c.Validate();
}

static void GraphicsBatches(Context &c) {
  const auto gpu = c.device.CreateBuffer(
      {.size = 12,
       .usage = BufferUsage::TransferSrc | BufferUsage::TransferDst});
  std::array<BufferHandle, 6> readbacks;
  for (auto &readback : readbacks)
    readback = c.device.CreateBuffer({.size = 12,
                                      .usage = BufferUsage::TransferDst,
                                      .memoryUsage = MemoryUsage::GPUToCPU});
  const auto timeline = c.device.CreateSemaphore(SemaphoreType::Timeline, 0);
  for (u32 frame = 0; frame < 6; ++frame) {
    auto &main = c.Begin();  // The renderer's main list is already recording.
    auto &first = c.device.AcquireCommandList(QueueType::Graphics);
    auto &second = c.device.AcquireCommandList(QueueType::Graphics);
    Require(&first != &main && &second != &main && &first != &second,
            "Graphics batches alias the recording main list or each other");
    Require(&c.device.GetCommandList(QueueType::Graphics) == &main,
            "Acquiring a graphics batch replaced the main command list");
    const std::array<u32, 3> expected{10 + frame, 20 + frame, 30 + frame};
    first.Begin();
    first.UpdateBuffer(
        {.buffer = gpu, .offset = 0, .data = &expected[0], .size = 4});
    Reject([&] { c.device.Submit(QueueType::Graphics, first); });
    first.End();
    const TimelineSemaphorePoint firstSignal{timeline, frame * 3ull + 1};
    c.device.Submit(QueueType::Graphics, first, {.signals = {&firstSignal, 1}});
    Reject([&] { c.device.Submit(QueueType::Graphics, first); });

    second.Begin();
    second.UpdateBuffer(
        {.buffer = gpu, .offset = 4, .data = &expected[1], .size = 4});
    second.End();
    const TimelineSemaphorePoint secondSignal{timeline, frame * 3ull + 2};
    c.device.Submit(
        QueueType::Graphics, second,
        {.waits = {&firstSignal, 1}, .signals = {&secondSignal, 1}});
    Require(static_cast<DX::CommandList &>(main).IsRecording(),
            "Submitting graphics batches ended the main command list");
    main.UpdateBuffer(
        {.buffer = gpu, .offset = 8, .data = &expected[2], .size = 4});
    main.Barrier(
        BufferBarrier{.buffer = gpu, .newState = ResourceState::TransferSrc});
    main.CopyBuffer(gpu, readbacks[frame], {0, 0, 12});
    main.End();
    const TimelineSemaphorePoint finalSignal{timeline, frame * 3ull + 3};
    c.device.SubmitAndPresent(c.swapchain, {.waits = {&secondSignal, 1},
                                            .signals = {&finalSignal, 1}});
  }
  // No per-frame WaitIdle: returning to a frame slot must retire its batches.
  c.device.WaitIdle();
  Require(c.device.GetSemaphoreValue(timeline) == 18,
          "Presentation did not signal the timeline after the main list");
  for (u32 frame = 0; frame < readbacks.size(); ++frame) {
    const std::array<u32, 3> expected{10 + frame, 20 + frame, 30 + frame};
    const auto *values = c.device.MapBuffer(readbacks[frame]);
    Require(std::memcmp(values, expected.data(), sizeof(expected)) == 0,
            "Graphics batch or main-list writes were lost");
    c.device.UnmapBuffer(readbacks[frame]);
    c.device.DestroyBuffer(readbacks[frame]);
  }
  c.Validate();
  c.device.DestroySemaphore(timeline);
  c.device.DestroyBuffer(gpu);
}

static void SwapchainResize(Context &context) {
  std::vector<ImageHandle> images;
  for (const auto size :
       {Extent2D{48, 24}, Extent2D{96, 64}, Extent2D{32, 32}}) {
    // Both frames and independent graphics batches retain the old backbuffer.
    for (u32 frame = 0; frame < 4; ++frame) {
      const auto acquired = context.device.BeginFrame(context.swapchain);
      Require(acquired.success, "Begin resize regression frame");
      if (std::none_of(images.begin(), images.end(), [&](auto image) {
            return image.id == acquired.backbufferImage.id;
          }))
        images.push_back(acquired.backbufferImage);
      auto &main = context.device.GetCommandList();
      main.Begin();
      Reject([&] {
        context.device.ResizeSwapchain(context.swapchain, size.width + 1,
                                       size.height);
      });
      const auto render = [&](ICommandList &commands, LoadOp load) {
        commands.Barrier(
            ImageBarrier{.image = acquired.backbufferImage,
                         .newLayout = ImageLayout::ColorAttachment});
        const ColorAttachmentDesc color{.view = acquired.backbuffer,
                                        .loadOp = load,
                                        .clearValue = {0, 1, 0, 1}};
        const auto extent = context.device.GetSwapchainDimensions();
        commands.BeginRendering({.renderArea = {.extent = extent},
                                 .colorAttachments = &color,
                                 .colorAttachmentCount = 1});
        commands.EndRendering();
      };
      auto &batch = context.device.AcquireCommandList(QueueType::Graphics);
      batch.Begin();
      render(batch, LoadOp::Clear);
      batch.End();
      context.device.Submit(QueueType::Graphics, batch);
      render(main, LoadOp::Load);
      main.End();
      context.device.SubmitAndPresent(context.swapchain);
    }
    // Resize itself must retire all submitted frames; no explicit WaitIdle
    // here.
    context.device.ResizeSwapchain(context.swapchain, size.width, size.height);
    const auto actual = context.device.GetSwapchainDimensions();
    Require(actual.width == size.width && actual.height == size.height,
            "Resized swapchain dimensions are stale");
    for (const auto image : images) {
      const auto &resource = context.device.GetImage(image);
      Require(resource.width == size.width && resource.height == size.height &&
                  resource.resourcePtr->GetDesc().Width == size.width,
              "Swapchain image handle was not refreshed after resize");
    }
    context.device.ResizeSwapchain(context.swapchain, 0, 0);
    context.device.ResizeSwapchain(context.swapchain, size.width, size.height);
    context.Validate();
  }
  std::cout
      << "PASS repeated swapchain resizing after main/batch rendering, "
         "stable handles, zero-size requests and continued presentation\n";
}

static void SemaphoreCreation(Context &c) {
  const auto zero = c.device.CreateSemaphore(SemaphoreType::Timeline, 0);
  const auto initialized =
      c.device.CreateSemaphore(SemaphoreType::Timeline, (1ull << 40) + 7);
  const auto binary = c.device.CreateSemaphore();
  Require(zero && initialized && binary,
          "Semaphore creation returned an invalid handle");
  Require(zero.id != initialized.id && zero.id != binary.id &&
              initialized.id != binary.id,
          "Semaphore handles are not distinct");
  Require(c.device.GetSemaphoreValue(zero) == 0,
          "Timeline zero initial value mismatch");
  Require(c.device.GetSemaphoreValue(initialized) == (1ull << 40) + 7,
          "Timeline initial value was truncated or ignored");
  Reject([&] { c.device.GetSemaphoreValue(binary); });
  Reject([&] { c.device.CreateSemaphore(SemaphoreType::Binary, 1); });
  Reject(
      [&] { c.device.CreateSemaphore(SemaphoreType::Timeline, UINT64_MAX); });
  c.device.DestroySemaphore(initialized);
  Reject([&] { c.device.GetSemaphoreValue(initialized); });
  Require(c.device.GetSemaphoreValue(zero) == 0,
          "Destroying a semaphore affected another fence");
  c.device.DestroySemaphore(
      initialized);  // Matches idempotent Vulkan destruction.
  c.device.DestroySemaphore(zero);
  c.device.DestroySemaphore(binary);
  c.Validate();
}

static void DescriptorReuse(Context &context) {
  const auto vertex = context.Shader(ShaderStage::Vertex, "VS");
  const auto pixel = context.Shader(ShaderStage::Fragment, "DescriptorReusePS");
  const BindingDesc bindings[]{{.binding = 0,
                                .type = BindingType::SampledTexture,
                                .count = 4096,
                                .visibility = ShaderStage::Fragment,
                                .flags = BindingFlags::PartiallyBound},
                               {.binding = 1,
                                .type = BindingType::Sampler,
                                .count = 128,
                                .visibility = ShaderStage::Fragment,
                                .flags = BindingFlags::PartiallyBound}};
  const auto layout = context.device.CreateBindingLayout({bindings, 2});
  const GraphicsPipelineDesc desc{.vertexShader = vertex,
                                  .fragmentShader = pixel,
                                  .layout = {&layout, 1},
                                  .raster = {.cullBackFaces = false},
                                  .colorFormat = Format::RGBA8_UNORM};
  const PipelineHandle pipelines[]{context.device.CreateGraphicsPipeline(desc),
                                   context.device.CreateGraphicsPipeline(desc)};
  const BindingPoolSize budgets[]{{BindingType::SampledTexture, 4096},
                                  {BindingType::Sampler, 128}};
  const auto pool = context.device.CreateBindingPool({budgets, 2, 1});
  const auto set = context.device.AllocateBindingSet({pool, layout});
  const u8 texels[]{255, 0, 0, 255, 0, 0, 255, 255};
  const auto texture = context.device.CreateImage(
      {.width = 2,
       .height = 1,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::Sampled | ImageUsage::TransferDst});
  auto upload = context.device.CreateUploadContext(1024);
  upload->Begin();
  upload->UploadImage({.dstImage = texture, .width = 2, .height = 1}, texels,
                      sizeof(texels));
  upload->Flush();
  const auto textureView = context.device.CreateImageView({.image = texture});
  const auto repeat = context.device.CreateSampler(
      {.minFilter = Filter::Nearest, .magFilter = Filter::Nearest});
  const auto clamp = context.device.CreateSampler(
      {.minFilter = Filter::Nearest,
       .magFilter = Filter::Nearest,
       .addressU = SamplerAddressMode::ClampToEdge});
  const BindingImageInfo source{.imageView = textureView,
                                .imageLayout = ImageLayout::ShaderReadOnly};
  const BindingImageInfo samplers[]{{.sampler = repeat}, {.sampler = clamp}};
  context.device.UpdateBindingSet({.dstSet = set,
                                   .binding = 0,
                                   .arrayElement = 2049,
                                   .type = BindingType::SampledTexture,
                                   .imageInfo = &source});
  context.device.UpdateBindingSet({.dstSet = set,
                                   .binding = 1,
                                   .type = BindingType::Sampler,
                                   .imageInfo = samplers,
                                   .descriptorCount = 2});
  const auto target = context.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto targetView = context.device.CreateImageView({.image = target});
  for (u32 frame = 0; frame < 5; ++frame) {
    auto &commands = context.Begin();
    commands.Barrier(ImageBarrier{.image = texture,
                                  .newLayout = ImageLayout::ShaderReadOnly});
    commands.Barrier(ImageBarrier{.image = target,
                                  .newLayout = ImageLayout::ColorAttachment});
    commands.SetViewport({.width = 16, .height = 16});
    commands.SetScissor({.extent = {16, 16}});
    const ColorAttachmentDesc color{.view = targetView};
    commands.BeginRendering({.renderArea = {.extent = {16, 16}},
                             .colorAttachments = &color,
                             .colorAttachmentCount = 1});
    for (u32 bind = 0; bind < 2000; ++bind) {
      const auto pipeline = pipelines[(bind / 10) % 2];
      commands.BindPipeline(pipeline);
      commands.SetBindings(pipeline, 0, set);
    }
    commands.Draw(3);
    commands.EndRendering();
    BufferHandle readback;
    ReadPixel(context, commands, target, readback);
    context.End(commands);
    CheckHalves(context, readback, {255, 0, 0, 255}, {0, 0, 255, 255});
    context.device.DestroyBuffer(readback);
    context.Validate();
  }
  context.device.DestroyBindingPool(pool);
  for (const auto pipeline : pipelines)
    context.device.DestroyPipeline(pipeline);
  context.device.DestroyBindingLayout(layout);
  context.device.DestroyShader(vertex);
  context.device.DestroyShader(pixel);
  context.device.DestroyImageView(targetView);
  context.device.DestroyImage(target);
  context.device.DestroyImageView(textureView);
  context.device.DestroyImage(texture);
  context.device.DestroySampler(repeat);
  context.device.DestroySampler(clamp);
  std::cout
      << "PASS 4096 textures, independent shared samplers and 10000 "
         "cached bindings across compatible pipelines and frame retirement\n";
}

static void ImageQueueHandoffs(Context &context) {
  const auto depth = context.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::D32_FLOAT,
       .usage = ImageUsage::DepthStencil | ImageUsage::Sampled});
  const auto depthView = context.device.CreateImageView(
      {.image = depth, .aspect = ImageAspect::Depth});
  const auto pyramid = context.device.CreateImage(
      {.width = 16,
       .height = 16,
       .mipLevels = 2,
       .format = Format::R32_FLOAT,
       .usage = ImageUsage::Storage | ImageUsage::Sampled |
                ImageUsage::TransferSrc});
  std::array<ImageViewHandle, 2> mipViews;
  for (u32 mip = 0; mip < 2; ++mip)
    mipViews[mip] =
        context.device.CreateImageView({.image = pyramid, .baseMipLevel = mip});
  const auto shader = context.Shader(ShaderStage::Compute, "ScalarCopyCS");
  const auto layouts = Reflected(context, {shader});
  const auto pipeline = context.device.CreateComputePipeline(
      {.computeShader = shader,
       .layout = {layouts.setLayouts.data(),
                  static_cast<u32>(layouts.setLayouts.size())}});
  const BindingPoolSize budgets[]{{BindingType::SampledTexture, 2},
                                  {BindingType::StorageImage, 2}};
  const auto pool = context.device.CreateBindingPool({budgets, 2, 2});
  std::array<BindingSetHandle, 2> sets;
  for (u32 mip = 0; mip < 2; ++mip) {
    sets[mip] =
        context.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
    const BindingImageInfo source{.imageView = mip ? mipViews[0] : depthView,
                                  .imageLayout = ImageLayout::ShaderReadOnly};
    const BindingImageInfo destination{.imageView = mipViews[mip],
                                       .imageLayout = ImageLayout::General};
    context.device.UpdateBindingSet({.dstSet = sets[mip],
                                     .binding = 7,
                                     .type = BindingType::SampledTexture,
                                     .imageInfo = &source});
    context.device.UpdateBindingSet({.dstSet = sets[mip],
                                     .binding = 8,
                                     .type = BindingType::StorageImage,
                                     .imageInfo = &destination});
  }
  const auto timeline =
      context.device.CreateSemaphore(SemaphoreType::Timeline, 0);
  for (u32 iteration = 0; iteration < 3; ++iteration) {
    auto &main = context.Begin();
    auto &graphics = context.device.AcquireCommandList(QueueType::Graphics);
    graphics.Begin();
    graphics.Barrier(ImageBarrier{.image = depth,
                                  .newLayout = ImageLayout::DepthAttachment,
                                  .aspect = ImageAspect::Depth});
    graphics.Barrier(ImageBarrier{.image = pyramid,
                                  .newLayout = ImageLayout::ShaderReadOnly,
                                  .mipLevelCount = 2});
    const float expected = 0.25f + iteration * 0.125f;
    const DepthAttachmentDesc attachment{.view = depthView,
                                         .clearDepth = expected};
    graphics.BeginRendering(
        {.renderArea = {.extent = {16, 16}}, .depthAttachment = &attachment});
    graphics.EndRendering();
    graphics.End();
    Require(context.device.GetImage(depth).state == D3D12_RESOURCE_STATE_COMMON,
            "Graphics depth was not released to COMMON");
    const TimelineSemaphorePoint produced[]{{timeline, iteration * 2 + 1}};
    context.device.Submit(QueueType::Graphics, graphics, {.signals = produced});
    auto &compute = context.device.AcquireCommandList(QueueType::Compute);
    compute.Begin();
    Reject([&] {
      compute.Barrier(ImageBarrier{.image = depth,
                                   .newLayout = ImageLayout::DepthAttachment});
    });
    compute.Barrier(ImageBarrier{.image = depth,
                                 .newLayout = ImageLayout::ShaderReadOnly,
                                 .oldState = ResourceState::DepthWrite,
                                 .newState = ResourceState::ShaderRead,
                                 .useExplicitStates = true,
                                 .aspect = ImageAspect::Depth});
    compute.BindComputePipeline(pipeline);
    for (u32 mip = 0; mip < 2; ++mip) {
      compute.Barrier(ImageBarrier{.image = pyramid,
                                   .newLayout = ImageLayout::General,
                                   .oldState = ResourceState::ShaderRead,
                                   .newState = ResourceState::ShaderWrite,
                                   .useExplicitStates = true,
                                   .baseMipLevel = mip});
      if (!mip)
        Require(context.device.GetImage(pyramid).subresourceStates[1] ==
                    D3D12_RESOURCE_STATE_COMMON,
                "Transitioning mip zero modified mip one");
      compute.SetComputeBindings(pipeline, 0, sets[mip]);
      compute.Dispatch(16 >> mip, 16 >> mip, 1);
      compute.Barrier(ImageBarrier{.image = pyramid,
                                   .newLayout = ImageLayout::ShaderReadOnly,
                                   .oldState = ResourceState::ShaderWrite,
                                   .newState = ResourceState::ShaderRead,
                                   .useExplicitStates = true,
                                   .baseMipLevel = mip});
      Require(context.device.GetImage(pyramid).subresourceStates[mip] ==
                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
              "Compute shader read retained pixel-shader state");
    }
    compute.End();
    const TimelineSemaphorePoint completed[]{{timeline, iteration * 2 + 2}};
    context.device.Submit(QueueType::Compute, compute,
                          {.waits = produced, .signals = completed});
    std::array<BufferHandle, 2> readbacks;
    for (u32 mip = 0; mip < 2; ++mip)
      ReadPixel(context, main, pyramid, readbacks[mip], mip);
    main.End();
    context.device.SubmitAndPresent(context.swapchain, {.waits = completed});
    context.device.WaitIdle();
    for (u32 mip = 0; mip < 2; ++mip) {
      const auto *pixels =
          static_cast<const u8 *>(context.device.MapBuffer(readbacks[mip]));
      for (u32 y = 0; y < (16u >> mip); ++y)
        for (u32 x = 0; x < (16u >> mip); ++x)
          Require(
              *reinterpret_cast<const float *>(pixels + y * 256 + x * 4) ==
                  expected,
              "Graphics/compute texture handoff lost depth or mip contents");
      context.device.UnmapBuffer(readbacks[mip]);
      context.device.DestroyBuffer(readbacks[mip]);
    }
    context.Validate();
  }
  context.device.DestroySemaphore(timeline);
  context.device.DestroyBindingPool(pool);
  context.device.DestroyPipeline(pipeline);
  ReleaseLayouts(context, layouts);
  context.device.DestroyShader(shader);
  for (const auto view : mipViews)
    context.device.DestroyImageView(view);
  context.device.DestroyImageView(depthView);
  context.device.DestroyImage(pyramid);
  context.device.DestroyImage(depth);
  std::cout << "PASS graphics/compute depth handoffs, explicit shader states, "
               "independent mip barriers and preserved pixels across frames\n";
}

static void MeshDrawing(Context &context, PipelineHandle direct,
                        PipelineHandle amplified) {
  const auto image = context.device.CreateImage(
      {.width = 16,
       .height = 16,
       .format = Format::RGBA8_UNORM,
       .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
  const auto view = context.device.CreateImageView({.image = image});
  const auto vertex = context.Shader(ShaderStage::Vertex, "VS");
  const auto pixel = context.Shader(ShaderStage::Fragment, "PS");
  const auto conventional = context.device.CreateGraphicsPipeline(
      {.vertexShader = vertex,
       .fragmentShader = pixel,
       .colorFormat = Format::RGBA8_UNORM});
  const auto compute = context.Shader(ShaderStage::Compute, "MeshArgumentsCS");
  const auto layouts = Reflected(context, {compute});
  const auto generator = context.device.CreateComputePipeline(
      {.computeShader = compute,
       .layout = {layouts.setLayouts.data(),
                  static_cast<u32>(layouts.setLayouts.size())}});
  const BindingPoolSize budget{BindingType::StorageBuffer, 1};
  const auto pool = context.device.CreateBindingPool({&budget, 1, 1});
  const auto set =
      context.device.AllocateBindingSet({pool, layouts.setLayouts[0]});
  const auto wrongUsage = context.device.CreateBuffer(
      {.size = 64, .usage = BufferUsage::TransferSrc});
  const auto wrongState = context.device.CreateBuffer(
      {.size = 64, .usage = BufferUsage::Storage | BufferUsage::Indirect});

  for (const auto pipeline : {direct, amplified}) {
    // Direct, packed indirect, padded indirect, no-op, GPU-generated indirect.
    for (u32 mode = 0; mode < 5; ++mode) {
      const u32 stride = mode == 1 ? 12 : 20;
      std::vector<u32> records(1 + 2 * stride / sizeof(u32), 0);
      if (mode != 4) {
        for (u32 record = 0; record < 2; ++record)
          for (u32 axis = 0; axis < 3; ++axis)
            records[1 + record * stride / sizeof(u32) + axis] = 1;
      }
      const auto arguments = context.device.CreateBuffer(
          {.size = records.size() * sizeof(u32),
           .usage = mode == 1 ? BufferUsage::Indirect
                              : BufferUsage::Indirect | BufferUsage::Storage,
           .memoryUsage =
               mode == 1 ? MemoryUsage::CPUToGPU : MemoryUsage::GPUOnly,
           .initialData = records.data()});
      auto &commands = context.Begin();
      Reject([&] { commands.DrawMeshTasks(1, 1, 1); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 4, 2, stride); });
      commands.Barrier(BufferBarrier{.buffer = wrongState,
                                     .newState = ResourceState::ShaderWrite});
      if (mode == 4) {
        const BindingBufferInfo destination{arguments, 0,
                                            records.size() * sizeof(u32)};
        context.device.UpdateBindingSet({.dstSet = set,
                                         .binding = 3,
                                         .type = BindingType::StorageBuffer,
                                         .bufferInfo = &destination});
        commands.Barrier(BufferBarrier{.buffer = arguments,
                                       .newState = ResourceState::ShaderWrite});
        commands.BindComputePipeline(generator);
        commands.SetComputeBindings(generator, 0, set);
        commands.Dispatch(1, 1, 1);
        commands.Barrier(
            BufferBarrier{.buffer = arguments,
                          .oldState = ResourceState::ShaderWrite,
                          .newState = ResourceState::IndirectArgument});
      }
      commands.Barrier(ImageBarrier{.image = image,
                                    .newLayout = ImageLayout::ColorAttachment});
      commands.SetViewport({.width = 16, .height = 16});
      commands.SetScissor({.extent = {16, 16}});
      const ColorAttachmentDesc color{.view = view, .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {.extent = {16, 16}},
                               .colorAttachments = &color,
                               .colorAttachmentCount = 1});
      commands.BindPipeline(conventional);
      Reject([&] { commands.DrawMeshTasks(1, 1, 1); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 4, 2, stride); });
      commands.BindPipeline(pipeline);
      Reject([&] { commands.Draw(3); });
      Reject([&] { commands.DrawIndexed(3); });
      Reject([&] { commands.DrawMeshTasks(65536, 1, 1); });
      Reject([&] { commands.DrawMeshTasks(65535, 65535, 1); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 2, 1, stride); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 4, 1, 8); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 4, 1, 14); });
      Reject([&] { commands.DrawMeshTasksIndirect(arguments, 4, 3, stride); });
      Reject([&] {
        commands.DrawMeshTasksIndirect(arguments, ~u64{3}, 1, stride);
      });
      Reject([&] {
        commands.DrawMeshTasksIndirect(arguments, 4, ~u32{0}, stride);
      });
      Reject([&] { commands.DrawMeshTasksIndirect(wrongUsage, 0, 1, stride); });
      Reject([&] { commands.DrawMeshTasksIndirect(wrongState, 0, 1, stride); });
      if (mode == 0)
        commands.DrawMeshTasks(1, 1, 1);
      else if (mode == 3) {
        commands.DrawMeshTasks(0, 1, 1);
        commands.DrawMeshTasks(1, 0, 1);
        commands.DrawMeshTasks(1, 1, 0);
        commands.DrawMeshTasksIndirect(arguments, 4, 0, stride);
      } else
        commands.DrawMeshTasksIndirect(arguments, 4, 2, stride);
      commands.EndRendering();
      BufferHandle readback;
      ReadPixel(context, commands, image, readback);
      context.End(commands);
      const std::array<u8, 4> expected =
          mode == 0   ? std::array<u8, 4>{64, 128, 191, 255}
          : mode == 3 ? std::array<u8, 4>{0, 0, 0, 255}
                      : std::array<u8, 4>{128, 255, 255, 255};
      CheckColor(context, readback, expected, 1);
      context.device.DestroyBuffer(readback);
      context.device.DestroyBuffer(arguments);
      context.Validate();
    }
  }
  context.device.DestroyBindingPool(pool);
  context.device.DestroyPipeline(generator);
  ReleaseLayouts(context, layouts);
  context.device.DestroyPipeline(conventional);
  for (const auto shader : {vertex, pixel, compute})
    context.device.DestroyShader(shader);
  context.device.DestroyBuffer(wrongUsage);
  context.device.DestroyBuffer(wrongState);
  context.device.DestroyImageView(view);
  context.device.DestroyImage(image);
  context.Validate();
  std::cout << "PASS direct and indirect mesh pixels, amplification, padded "
               "strides, GPU-generated arguments, no-ops and validation\n";
}

static void MeshPipelines(Context &context) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
  DX::ThrowIfFailed(
      context.device.GetNativeDevice()->CheckFeatureSupport(
          D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options)),
      "Query mesh shader test support");
  if (options.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
    std::cout << "SKIP mesh pipeline creation: unsupported adapter\n";
    return;
  }
  const auto mesh = context.Shader(ShaderStage::Mesh, "TriangleMS");
  const auto payloadMesh = context.Shader(ShaderStage::Mesh, "PayloadMS");
  const auto task = context.Shader(ShaderStage::Task, "TaskAS");
  const auto pixel = context.Shader(ShaderStage::Fragment, "PS");
  MeshPipelineDesc desc{};
  desc.meshShader = mesh;
  desc.fragmentShader = pixel;
  desc.colorFormat = Format::RGBA8_UNORM;
  desc.raster.cullBackFaces = false;
  desc.blend = {.enable = true,
                .srcColor = BlendFactor::One,
                .dstColor = BlendFactor::One,
                .srcAlpha = BlendFactor::One,
                .dstAlpha = BlendFactor::Zero};
  desc.depth.depthTestEnable = false;
  desc.depth.depthWriteEnable = false;
  const auto direct = context.device.CreateMeshPipeline(desc);
  Require(direct.IsValid(), "Mesh-only pipeline handle is invalid");
  Require(context.device.GetPipeline(direct).state != nullptr,
          "Mesh-only native PSO is missing");
  desc.meshShader = payloadMesh;
  desc.taskShader = task;
  const auto amplified = context.device.CreateMeshPipeline(desc);
  Require(amplified.IsValid(), "Amplified pipeline handle is invalid");
  context.Validate();
  MeshDrawing(context, direct, amplified);
  desc.taskShader = pixel;
  Reject([&] { context.device.CreateMeshPipeline(desc); });
  desc.taskShader = task;
  desc.meshShader = pixel;
  Reject([&] { context.device.CreateMeshPipeline(desc); });
  desc.meshShader = payloadMesh;
  desc.fragmentShader = task;
  Reject([&] { context.device.CreateMeshPipeline(desc); });
  desc.fragmentShader = pixel;
  desc.colorFormat = Format::Undefined;
  Reject([&] { context.device.CreateMeshPipeline(desc); });
  context.device.DestroyPipeline(amplified);
  context.device.DestroyPipeline(direct);
  for (const auto shader : {mesh, payloadMesh, task, pixel})
    context.device.DestroyShader(shader);
  context.Validate();
  std::cout << "PASS mesh pipeline creation with and without amplification, "
               "and invalid descriptions\n";
}

int main() {
  try {
    {
      Context context;
      DescriptorReuse(context);
      ImageQueueHandoffs(context);
      MeshPipelines(context);
      SemaphoreCreation(context);
      std::cout << "PASS fence-backed semaphore creation, 64-bit initial "
                   "values and lifetime\n";
      AuxiliaryQueues(context);
      std::cout << "PASS dedicated copy/compute queues, GPU dependency chain, "
                   "allocator reuse and retained descriptors\n";
      SwapchainResize(context);
      GraphicsBatches(context);
      std::cout << "PASS independent graphics batches while main records, "
                   "timeline ordering, presentation and frame-slot reuse\n";
      Buffers(context);
      std::cout << "PASS buffer heaps, mapping, initial data, partial update, "
                   "readback, alignment\n";
      MirroredBuffers(context);
      std::cout
          << "PASS CPU-visible writable storage and GPU-to-CPU usage mirrors\n";
      Views(context);
      std::cout << "PASS texture ranges, cube arrays, depth/stencil SRVs, "
                   "reinterpretation, samplers\n";
      SparseImages(context);
      std::cout << "PASS sparse color/depth textures, tile residency, "
                   "remapping, mip/layer addressing and validation\n";
      Graphics(context);
      std::cout << "PASS MRT/blend pixel output, topologies, depth-only, "
                   "layouts, pipeline cache\n";
      UploadContextTransfers(context);
      std::cout << "PASS upload context lifetime, tight textures, staging "
                   "growth and repeated batches\n";
      TextureUpload(context);
      std::cout << "PASS padded RGBA8 sRGB texture upload, exact pixels and "
                   "invalid regions\n";
      Sampling(context);
      std::cout << "PASS sampled texture and sampler pixel output through RHI "
                   "descriptor sets\n";
      UniformAndPush(context);
      std::cout << "PASS reflected layouts, uniform ranges, direct binding, "
                   "graphics push constants and pool lifetime\n";
      ComputeBindings(context);
      std::cout << "PASS compute descriptor tables, raw/structured buffers and "
                   "compute push constants\n";
      DescriptorArrays(context, false);
      DescriptorArrays(context, true);
      std::cout << "PASS partial/variable arrays, descriptor snapshots, "
                   "update-after-bind and in-flight version isolation\n";
      StorageImages(context);
      std::cout << "PASS writable texture descriptors and compute "
                   "update-after-bind retargeting\n";
      RootConstantFallbacks(context);
      StagePushConstants(context);
      StagePushConstants(context, true);
      std::cout << "PASS independent vertex/fragment push constant values at "
                   "overlapping offsets\n";
      VertexInput(context);
      std::cout << "PASS normalized vertex input, U16 index offsets, binding "
                   "order, stride changes\n";
      Presentation(context);
      std::cout << "PASS drawing into borrowed swapchain RTV and presenting\n";
      Depth(context);
      std::cout << "PASS depth-only drawing and equal-depth comparison pixel "
                   "output\n";
    }
    CacheRecovery();
    std::cout << "PASS recovery from a deliberately corrupted pipeline cache\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
