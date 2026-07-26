#include "rhi/device.h"
#include "shader/shader_compiler.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Velos::RHI;

struct Backend {
  std::string name;
  GraphicsAPI api;
};

struct ImageArtifact {
  std::string label;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

struct Result {
  std::string name;
  std::string kind;
  std::string backend;
  bool passed = false;
  double milliseconds = 0.0;
  std::string description;
  std::string detail;
  std::vector<ImageArtifact> images;
};

static std::string Escape(std::string value) {
  const std::pair<std::string, std::string> replacements[] = {
      {"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}, {"\"", "&quot;"}};
  for (const auto &[from, to] : replacements) {
    for (size_t position = 0; (position = value.find(from, position)) != std::string::npos;
         position += to.size())
      value.replace(position, from.size(), to);
  }
  return value;
}

static std::vector<Backend> Backends() {
  // Add { "dx12", GraphicsAPI::D3D12 } here when the backend lands. All tests
  // below are expressed only through IDevice and will be reused automatically.
  return {{"vulkan", GraphicsAPI::Vulkan}};
}

static std::string DescribeTest(const std::string &name) {
  if (name == "ShaderProceduralPixelCoordinates")
    return "Compiles vertex and fragment GLSL at runtime, draws a fullscreen triangle, and verifies a deterministic per-pixel color generated from gl_FragCoord.";
  if (name == "ShaderPushConstantColor")
    return "Compiles a fragment shader with a reflected push-constant block, pushes an exact RGBA color, draws through a graphics pipeline, and checks every output pixel.";
  if (name == "ShaderComputeStorageImage")
    return "Compiles and dispatches a compute shader through a storage-image descriptor, then reads the written image back and verifies every invocation's pixel.";
  if (name == "DrawIndexedPixelOutput")
    return "Binds explicit vertex and 32-bit index buffers, draws a fullscreen quad as two triangles, and verifies complete exact coverage.";
  if (name == "DrawIndexedU16UNormPixelOutput")
    return "Draws through a 16-bit index buffer and normalized four-byte vertex colors, then verifies exact fullscreen output.";
  if (name == "DepthTestTriangleStripPixelOutput")
    return "Draws near and far fullscreen triangle strips into color and D32 depth attachments and verifies the farther draw fails the depth test.";
  if (name == "FragmentDiscardPixelOutput")
    return "Kills the right half of a fullscreen primitive in the fragment shader and verifies that cleared pixels remain untouched.";
  if (name == "Sample2DNearestPixelOutput")
    return "Samples an uploaded RGBA8 texture through a combined image sampler into a storage image and checks every texel.";
  if (name == "ScissorPixelOutput")
    return "Draws a fullscreen primitive through a smaller scissor rectangle and verifies that only pixels inside the rectangle are modified.";
  if (name == "BlendAddPixelOutput")
    return "Enables blending with one/one factors and the add operation, then verifies the exact source-plus-destination result.";
  if (name == "BlendReverseSubtractPixelOutput")
    return "Enables blending with one/one factors and reverse subtraction, then verifies the exact destination-minus-source result.";
  if (name == "MultipleRenderTargetsPixelOutput")
    return "Binds two color attachments in one rendering pass, clears each to a distinct color, reads both images back, and verifies every pixel.";
  if (name == "CubeArrayImageView")
    return "Creates a 12-layer cube array and a cube-array view. The test fails while the declared cube-array RHI type is unsupported.";
  if (name == "MultipleSwapchains")
    return "Creates two independent hidden-window swapchains and verifies that both receive distinct valid handles.";
  if (name == "UploadContextBufferRoundTrip")
    return "Uploads patterned data into a GPU-only buffer through IUploadContext, copies it back, and compares every value.";
  if (name == "UploadContextImageRoundTrip")
    return "Uploads a patterned image through IUploadContext, reads it back from its requested final layout, and compares every pixel.";
  if (name == "BufferCopyOffsetRange")
    return "Copies a subrange between non-zero buffer offsets and verifies both the copied values and untouched surrounding values.";
  if (name == "BufferTexturePaddedRowsRoundTrip")
    return "Uploads an image from rows with explicit padding and verifies that row length and image height metadata are honored.";
  if (name == "BufferTextureSubrectangleRoundTrip")
    return "Uploads a rectangular patch at a non-zero image offset and verifies the patch plus every untouched surrounding pixel.";
  if (name == "BlitMipPixelOutput")
    return "Linearly blits mip zero into mip one and verifies the exact downsampled 2x2 pixel result.";
  if (name == "GenerateMipmapsPixelOutput")
    return "Generates a complete three-level mip chain and verifies the exact final 1x1 downsample.";
  if (name == "UniformBufferShaderPixelOutput")
    return "Reads a color through a fragment-shader uniform-buffer descriptor and verifies every rendered pixel.";
  if (name == "StorageBufferComputeReadback")
    return "Writes a deterministic uint pattern from a compute shader into a storage buffer and verifies every value after GPU readback.";
  if (name == "ComputePushConstantsReadback")
    return "Pushes a compute-stage constant, uses it to generate storage-buffer values, and verifies the complete GPU result.";
  if (name == "DispatchRequiresCurrentComputePipeline")
    return "Begins a fresh command buffer and verifies Dispatch rejects calls without a compute pipeline bound in that recording.";
  if (name == "PixelAccurateClearReadback")
    return "Clears an RGBA8 render target to an exact color, copies it to a GPU-to-CPU buffer, and compares every channel byte.";
  if (name == "PixelUploadCheckerboardReadback")
    return "Uploads a two-pixel checkerboard through a transfer buffer, reads the image back, and verifies that copy layout and row ordering preserve every pixel.";
  if (name == "PixelRenderAreaQuadrants")
    return "Builds a four-color image using four render-area clears and verifies the exact boundary and color of every quadrant.";
  if (name == "PixelMipLayerReadback")
    return "Clears mip 1 of array layer 1, reads only that subresource back, and verifies that mip and layer selection are correct.";
  if (name == "MappedGpuToCpuBufferAccess")
    return "Copies known bytes into a GPU-to-CPU buffer, maps it, and compares the complete readback against the source.";
  if (name == "BindUniformBufferCommand")
    return "Records a direct uniform-buffer bind. The test fails if the backend rejects or has not implemented this command.";
  if (name == "CommandBufferCopy")
    return "Copies a patterned buffer on the GPU, maps the destination, and compares every copied value.";
  if (name == "CommandBufferUpdate")
    return "Updates a buffer at a non-zero byte offset, maps it, and verifies the written values at that exact offset.";
  if (name == "CopyBufferToTextureMip")
    return "Uploads pixels to a selected mip, copies that mip back to a mapped buffer, and compares every pixel.";
  if (name == "CopyBufferToTextureSlice")
    return "Uploads pixels to a selected array layer, copies that layer back to a mapped buffer, and compares every pixel.";
  if (name == "GpuOnlyBufferMapRejected")
    return "Confirms that mapping device-local GPU-only memory is rejected instead of returning an invalid pointer.";
  if (name.find("Buffer") != std::string::npos)
    return "Exercises buffer creation, usage flags, memory behavior, copying, or validation through the backend-neutral RHI.";
  if (name.find("Image") != std::string::npos || name.find("Texture") != std::string::npos ||
      name.find("Render") != std::string::npos || name.find("Sampler") != std::string::npos)
    return "Exercises image resources, views, sampling, render attachments, subresources, or layout transitions through the RHI.";
  if (name.find("Descriptor") != std::string::npos || name.find("Binding") != std::string::npos)
    return "Creates and updates descriptor bindings and verifies that the requested RHI resource type is accepted.";
  if (name.find("Command") != std::string::npos || name.find("Viewport") != std::string::npos ||
      name.find("Barrier") != std::string::npos)
    return "Records and submits commands while checking command-list state and synchronization behavior.";
  if (name.find("Invalid") != std::string::npos || name.find("Unsupported") != std::string::npos)
    return "Passes an unsupported or invalid request and verifies that the RHI reports it deterministically.";
  if (name == "ResourceLifecycleStress")
    return "Repeatedly creates and destroys resources to catch stale handles and backend resource leaks.";
  return "Verifies this backend-neutral RHI operation completes successfully and leaves the device in a valid state.";
}

static ShaderHandle CompileShader(IDevice &device, const fs::path &path,
                                  ShaderStage stage, const char *debugName) {
  const auto compiled = Velos::ShaderCompiler::CompileFile({
      .path = path.string(), .stage = stage,
      .language = Velos::ShaderSourceLanguage::GLSL});
  return device.CreateShader({
      .stage = stage,
      .bytecode = compiled.spirv.data(),
      .bytecodeSize = compiled.spirv.size() * sizeof(uint32_t),
      .reflection = compiled.reflection,
      .debugName = debugName});
}

static void WritePixelSvg(std::ostream &out, const ImageArtifact &image) {
  constexpr uint32_t scale = 16;
  out << "<figure><figcaption>" << Escape(image.label) << " · "
      << image.width << "×" << image.height << "</figcaption><svg class=\"pixel-image\" "
      << "viewBox=\"0 0 " << image.width * scale << ' ' << image.height * scale
      << "\" role=\"img\" aria-label=\"" << Escape(image.label) << "\">";
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = 0; x < image.width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
      if (offset + 3 >= image.rgba.size())
        continue;
      out << "<rect x=\"" << x * scale << "\" y=\"" << y * scale
          << "\" width=\"" << scale << "\" height=\"" << scale
          << "\" fill=\"rgba(" << static_cast<unsigned>(image.rgba[offset]) << ','
          << static_cast<unsigned>(image.rgba[offset + 1]) << ','
          << static_cast<unsigned>(image.rgba[offset + 2]) << ','
          << std::fixed << std::setprecision(3)
          << static_cast<double>(image.rgba[offset + 3]) / 255.0 << ")\"/>";
    }
  }
  out << "</svg></figure>";
}

static void WriteReport(const fs::path &path, const Backend &backend,
                        const std::vector<Result> &results, double totalMs) {
  const size_t passed = std::count_if(results.begin(), results.end(),
                                      [](const Result &r) { return r.passed; });
  std::ofstream out(path);
  if (!out)
    throw std::runtime_error("Could not write report: " + path.string());

  out << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>velos RHI test report</title><style>
:root{color-scheme:dark;--bg:#11141a;--panel:#171b23;--line:#2a303d;--text:#f2f6ff;--muted:#94a3bd;--green:#43d56b;--red:#ff5252;--blue:#4399ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:13px Inter,Segoe UI,sans-serif}
header{position:sticky;top:0;z-index:2;background:#141820;border-bottom:1px solid var(--line);padding:16px 21px}
.top,.filters{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.top h1{font-size:17px;margin:0}.meta{color:var(--muted);flex:1}
.pill,.button,input{border:1px solid var(--line);background:#1b202a;border-radius:8px;padding:7px 12px}.pass{color:var(--green)}.fail{color:var(--red)}
.filters{margin-top:17px}input{width:240px;color:var(--text);outline:none}.button{cursor:pointer;color:var(--muted)}.button.active{background:var(--blue);color:white}
main{padding:21px;display:grid;grid-template-columns:repeat(auto-fill,minmax(290px,1fr));gap:12px;align-items:start}.card{min-height:112px;background:var(--panel);border:1px solid var(--line);border-left:3px solid var(--green);border-radius:9px;padding:15px;cursor:pointer}
.card.failed{border-left-color:var(--red)}.title{display:flex;align-items:center;gap:8px;font-weight:700}.title span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.tag{font:10px ui-monospace,monospace;color:#9db3d7;border:1px solid var(--line);padding:4px 7px;border-radius:4px;text-transform:uppercase}.time{font:11px ui-monospace;color:#a9bce0}
.bar{height:14px;background:#2b3342;border-radius:2px;margin:19px 0 7px}.card.failed .bar{background:#3e2027}.detail{color:var(--muted);font:11px ui-monospace;white-space:pre-wrap}
.chevron{color:var(--muted);transition:transform .15s}.card.expanded .chevron{transform:rotate(90deg)}.expanded-content{display:none;border-top:1px solid var(--line);margin-top:14px;padding-top:14px}.card.expanded .expanded-content{display:block}.description{color:#c7d2e6;line-height:1.5;margin:0 0 14px}.images{display:flex;gap:14px;flex-wrap:wrap}figure{margin:0}figcaption{color:var(--muted);font:10px ui-monospace;margin-bottom:7px;text-transform:uppercase}.pixel-image{width:128px;height:128px;image-rendering:pixelated;background:#0c0e13;border:1px solid var(--line);border-radius:5px}
</style></head><body><header><div class="top"><h1>velos RHI test report</h1><span class="meta">)HTML";
  out << Escape(backend.name) << " · last run</span><span class=\"pill pass\">" << passed
      << " passed</span><span class=\"pill fail\">" << results.size() - passed
      << " failed</span><span class=\"pill\">" << std::fixed << std::setprecision(1)
      << totalMs << " ms</span></div><div class=\"filters\">"
      << "<input id=\"search\" placeholder=\"Filter by name…\">"
      << "<span class=\"button active\" data-filter=\"all\">all</span>"
      << "<span class=\"button\" data-filter=\"resource\">resource</span>"
      << "<span class=\"button\" data-filter=\"buffer\">buffer</span>"
      << "<span class=\"button\" data-filter=\"texture\">texture</span>"
      << "<span class=\"button\" data-filter=\"shader\">shader</span>"
      << "<span class=\"button\" data-filter=\"command\">command</span>"
      << "<span class=\"button\" data-filter=\"validation\">validation</span>"
      << "<span class=\"button\" data-filter=\"passed\">passed</span>"
      << "<span class=\"button\" data-filter=\"failed\">failed</span>"
      << "<span class=\"button\" id=\"expand-all\">expand all</span></div></header><main>";
  for (const auto &r : results) {
    out << "<article class=\"card " << (r.passed ? "passed" : "failed")
        << "\" data-name=\"" << Escape(r.name) << "\" data-kind=\"" << r.kind
        << "\" data-status=\"" << (r.passed ? "passed" : "failed") << "\">"
        << "<div class=\"title\"><span>" << Escape(r.name) << "</span><span class=\"tag\">"
        << Escape(r.backend) << "</span><span class=\"tag\">" << Escape(r.kind)
        << "</span><span class=\"time\">" << std::fixed << std::setprecision(1)
        << r.milliseconds << " ms</span><span class=\"chevron\">▶</span></div><div class=\"bar\"></div><div class=\"detail\">"
        << Escape(r.detail.empty() ? (r.passed ? "completed" : "failed") : r.detail)
        << "</div><div class=\"expanded-content\"><p class=\"description\">"
        << Escape(r.description) << "</p>";
    if (!r.images.empty()) {
      out << "<div class=\"images\">";
      for (const auto &image : r.images)
        WritePixelSvg(out, image);
      out << "</div>";
    } else {
      out << "<div class=\"detail\">This test does not produce an image.</div>";
    }
    out << "</div></article>";
  }
  out << R"HTML(</main><script>
let filter="all";const cards=[...document.querySelectorAll(".card")],search=document.querySelector("#search");
function apply(){const q=search.value.toLowerCase();cards.forEach(c=>c.hidden=!(c.dataset.name.toLowerCase().includes(q)&&(filter==="all"||c.dataset.kind===filter||c.dataset.status===filter)))}
search.oninput=apply;document.querySelectorAll(".button").forEach(b=>b.onclick=()=>{document.querySelector(".button.active").classList.remove("active");b.classList.add("active");filter=b.dataset.filter;apply()});
cards.forEach(c=>c.onclick=()=>c.classList.toggle("expanded"));
const expandAll=document.querySelector("#expand-all");
expandAll.onclick=e=>{e.stopPropagation();const expand=cards.some(c=>!c.classList.contains("expanded"));cards.forEach(c=>c.classList.toggle("expanded",expand));expandAll.textContent=expand?"collapse all":"expand all"};
</script></body></html>)HTML";
}

int main(int argc, char **argv) {
  std::string requested = "vulkan";
  fs::path report = fs::path(__FILE__).parent_path() / "last-run.html";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--backend" && i + 1 < argc) requested = argv[++i];
    else if (arg == "--report" && i + 1 < argc) report = argv[++i];
    else if (arg == "--list-backends") {
      for (const auto &backend : Backends()) std::cout << backend.name << '\n';
      return 0;
    }
  }
  const auto backends = Backends();
  const auto selected = std::find_if(backends.begin(), backends.end(),
      [&](const Backend &backend) { return backend.name == requested; });
  if (selected == backends.end()) {
    std::cerr << "Unknown backend '" << requested << "'. Use --list-backends.\n";
    return 2;
  }

  std::vector<Result> results;
  std::vector<ImageArtifact> pendingImages;
  auto publishPixelComparison = [&](uint32_t width, uint32_t height,
                                    const std::vector<uint8_t> &expected,
                                    const std::vector<uint8_t> &actual) {
    pendingImages = {
        {.label = "GPU output", .width = width, .height = height, .rgba = actual},
        {.label = "Expected", .width = width, .height = height, .rgba = expected}};
  };
  const auto suiteStart = std::chrono::steady_clock::now();
  std::unique_ptr<IDevice, decltype(&DestroyDevice)> device(nullptr, DestroyDevice);
  struct GlfwScope {
    GlfwScope() {
      if (!glfwInit()) throw std::runtime_error("glfwInit failed");
      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    ~GlfwScope() { glfwTerminate(); }
  };
  auto run = [&](std::string name, std::string kind, const std::function<void()> &test) {
    Result result{.name = std::move(name), .kind = std::move(kind), .backend = selected->name};
    result.description = DescribeTest(result.name);
    pendingImages.clear();
    const auto start = std::chrono::steady_clock::now();
    try { test(); result.passed = true; result.detail = "completed"; }
    catch (const std::exception &error) { result.detail = error.what(); }
    catch (...) { result.detail = "unknown exception"; }
    result.images = std::move(pendingImages);
    result.milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name
              << " (" << result.milliseconds << " ms)\n";
    results.push_back(std::move(result));
  };

  std::unique_ptr<GlfwScope> glfw;
  run("DeviceCreate", "validation", [&] {
    glfw = std::make_unique<GlfwScope>();
    device.reset(CreateDevice({.graphicsAPI = selected->api, .enableValidation = true,
                               .applicationName = "Velos RHI tests"}));
    if (!device) throw std::runtime_error("CreateDevice returned null");
  });
  if (device) {
    auto renderFullscreen = [&](uint32_t width, uint32_t height,
                                const ClearColor &clearColor,
                                const float shaderColor[4],
                                const BlendStateDesc &blend,
                                const Rect2D &scissor,
                                const std::vector<uint8_t> &expected,
                                const char *debugName) {
      const uint64_t byteCount = static_cast<uint64_t>(width) * height * 4;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vs = CompileShader(*device, shaderRoot / "fullscreen.vert",
                              ShaderStage::Vertex, "test.fullscreen.shared.vs");
      auto fsHandle = CompileShader(*device, shaderRoot / "push_color.frag",
                                    ShaderStage::Fragment, "test.fullscreen.shared.fs");
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vs, .fragmentShader = fsHandle,
          .raster = {.cullBackFaces = false}, .blend = blend,
          .colorFormat = Format::RGBA8_UNORM, .debugName = debugName});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store, .clearValue = clearColor};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &attachment, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width), .height = static_cast<float>(height)});
      commands.SetScissor(scissor);
      commands.PushConstants(ShaderStage::Fragment, 0, sizeof(float) * 4, shaderColor);
      commands.Draw(3); commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image, .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback); device->DestroyBuffer(readback);
      device->DestroyImageView(view); device->DestroyImage(image);
      device->DestroyPipeline(pipeline); device->DestroyShader(fsHandle); device->DestroyShader(vs);
      if (actual != expected) throw std::runtime_error(std::string(debugName) + " output mismatch");
    };
    run("BackendIdentity", "validation", [&] {
      if (device->GetBackend() != selected->api) throw std::runtime_error("backend mismatch");
    });
    run("TypedHandleSemantics", "validation", [&] {
      BufferHandle invalid{};
      BufferHandle valid{42};
      if (invalid || invalid.IsValid()) throw std::runtime_error("default handle is valid");
      if (!valid || !valid.IsValid() || valid.id != 42)
        throw std::runtime_error("constructed handle lost its id");
    });
    run("BufferCpuVisibleInitialData", "resource", [&] {
      const uint32_t data[] = {0x12345678, 0xabcdef01, 7, 42};
      auto handle = device->CreateBuffer({.size = sizeof(data),
          .usage = BufferUsage::TransferSrc | BufferUsage::Uniform,
          .memoryUsage = MemoryUsage::CPUToGPU, .initialData = data,
          .debugName = "test.cpu.initial"});
      if (!handle) throw std::runtime_error("invalid buffer handle");
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(handle));
      const bool matches = std::equal(std::begin(data), std::end(data), mapped);
      device->UnmapBuffer(handle);
      device->DestroyBuffer(handle);
      if (!matches) throw std::runtime_error("CPU-visible initial data mismatch");
    });
    run("BufferGpuOnly", "resource", [&] {
      auto handle = device->CreateBuffer({.size = 4096,
          .usage = BufferUsage::TransferDst | BufferUsage::Storage,
          .memoryUsage = MemoryUsage::GPUOnly, .debugName = "test.gpu"});
      if (!handle) throw std::runtime_error("invalid buffer handle");
      device->DestroyBuffer(handle);
    });
    run("BufferDeviceAddress", "resource", [&] {
      auto handle = device->CreateBuffer({.size = 256,
          .usage = BufferUsage::Storage | BufferUsage::ShaderDeviceAddress,
          .memoryUsage = MemoryUsage::GPUOnly, .debugName = "test.device.address"});
      if (!handle) throw std::runtime_error("invalid buffer handle");
      if (device->GetBufferDeviceAddress(handle) == 0)
        throw std::runtime_error("device address is zero");
      device->DestroyBuffer(handle);
    });
    run("MappedGpuToCpuBufferAccess", "buffer", [&] {
      const uint32_t expected[] = {0x10203040, 0x55667788, 17, 99};
      auto source = device->CreateBuffer({.size = sizeof(expected),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = expected});
      auto buffer = device->CreateBuffer({.size = sizeof(expected),
          .usage = BufferUsage::TransferDst,
          .memoryUsage = MemoryUsage::GPUToCPU, .debugName = "test.readback.map"});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.CopyBuffer(source, buffer, {.size = sizeof(expected)});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(buffer));
      if (mapped == nullptr) throw std::runtime_error("MapBuffer returned null");
      const bool matches = std::equal(std::begin(expected), std::end(expected), mapped);
      device->UnmapBuffer(buffer);
      device->DestroyBuffer(buffer);
      device->DestroyBuffer(source);
      if (!matches) throw std::runtime_error("mapped GPU-to-CPU data mismatch");
    });
    run("GpuOnlyBufferMapRejected", "validation", [&] {
      auto buffer = device->CreateBuffer({.size = 64,
          .usage = BufferUsage::Storage, .memoryUsage = MemoryUsage::GPUOnly});
      bool rejected = false;
      try { (void)device->MapBuffer(buffer); }
      catch (const std::exception &) { rejected = true; }
      device->DestroyBuffer(buffer);
      if (!rejected) throw std::runtime_error("GPUOnly buffer was mappable");
    });
    run("Image2DAndView", "resource", [&] {
      auto image = device->CreateImage({.width = 64, .height = 64, .mipLevels = 4,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
          .debugName = "test.image"});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM,
          .mipLevelCount = 4, .debugName = "test.view"});
      if (!image || !view) throw std::runtime_error("invalid image/view handle");
      if (device->GetImageLayout(image, 0) != ImageLayout::Undefined)
        throw std::runtime_error("unexpected initial image layout");
      device->DestroyImageView(view); device->DestroyImage(image);
    });
    run("SamplerModes", "resource", [&] {
      auto sampler = device->CreateSampler({.minFilter = Filter::Nearest,
          .magFilter = Filter::Linear, .addressU = SamplerAddressMode::ClampToEdge,
          .addressV = SamplerAddressMode::Repeat, .debugName = "test.sampler"});
      if (!sampler) throw std::runtime_error("invalid sampler handle");
      device->DestroySampler(sampler);
    });
    run("CubeImageAndView", "resource", [&] {
      auto image = device->CreateImage({.width = 16, .height = 16, .arrayLayers = 6,
          .format = Format::RGBA16_FLOAT, .type = ImageType::Cube,
          .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
          .debugName = "test.cube"});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA16_FLOAT,
          .type = ImageViewType::Cube, .arrayLayerCount = 6, .debugName = "test.cube.view"});
      if (!image || !view) throw std::runtime_error("invalid cube handles");
      device->DestroyImageView(view); device->DestroyImage(image);
    });
    run("CubeArrayImageView", "texture", [&] {
      ImageHandle image{};
      ImageViewHandle view{};
      std::exception_ptr creationFailure;
      try {
        image = device->CreateImage({.width = 16, .height = 16,
            .arrayLayers = 12, .format = Format::RGBA8_UNORM,
            .type = ImageType::Cube,
            .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
            .debugName = "test.cube.array"});
        view = device->CreateImageView({.image = image,
            .format = Format::RGBA8_UNORM, .type = ImageViewType::CubeArray,
            .arrayLayerCount = 12, .debugName = "test.cube.array.view"});
      } catch (...) {
        creationFailure = std::current_exception();
      }
      if (view) device->DestroyImageView(view);
      if (image) device->DestroyImage(image);
      if (creationFailure) std::rethrow_exception(creationFailure);
    });
    run("DescriptorUniformBuffer", "resource", [&] {
      const BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer,
          .count = 1, .visibility = ShaderStage::Vertex};
      auto layout = device->CreateBindingLayout({.bindings = &binding, .bindingCount = 1,
          .debugName = "test.uniform.layout"});
      const BindingPoolSize poolSize{.type = BindingType::UniformBuffer, .count = 1};
      auto pool = device->CreateBindingPool({.poolSizes = &poolSize, .poolSizeCount = 1,
          .maxSets = 1, .debugName = "test.uniform.pool"});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout,
          .debugName = "test.uniform.set"});
      auto buffer = device->CreateBuffer({.size = 256, .usage = BufferUsage::Uniform,
          .memoryUsage = MemoryUsage::CPUToGPU, .debugName = "test.uniform.buffer"});
      const BindingBufferInfo info{.buffer = buffer, .offset = 0, .range = 256};
      device->UpdateBindingSet({.dstSet = set, .binding = 0,
          .type = BindingType::UniformBuffer, .bufferInfo = &info});
      device->DestroyBuffer(buffer);
      device->DestroyBindingPool(pool);
      device->DestroyBindingLayout(layout);
    });
    run("DescriptorStorageBufferArray", "resource", [&] {
      const BindingDesc binding{.binding = 3, .type = BindingType::StorageBuffer,
          .count = 2, .visibility = ShaderStage::Compute,
          .flags = BindingFlags::PartiallyBound};
      auto layout = device->CreateBindingLayout({.bindings = &binding, .bindingCount = 1,
          .debugName = "test.storage.layout"});
      const BindingPoolSize poolSize{.type = BindingType::StorageBuffer, .count = 2};
      auto pool = device->CreateBindingPool({.poolSizes = &poolSize, .poolSizeCount = 1,
          .maxSets = 1, .debugName = "test.storage.pool"});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout,
          .debugName = "test.storage.set"});
      auto a = device->CreateBuffer({.size = 128, .usage = BufferUsage::Storage,
          .memoryUsage = MemoryUsage::GPUOnly, .debugName = "test.storage.a"});
      auto b = device->CreateBuffer({.size = 128, .usage = BufferUsage::Storage,
          .memoryUsage = MemoryUsage::GPUOnly, .debugName = "test.storage.b"});
      const BindingBufferInfo infos[] = {{.buffer = a, .range = 128},
                                         {.buffer = b, .range = 128}};
      device->UpdateBindingSet({.dstSet = set, .binding = 3,
          .type = BindingType::StorageBuffer, .bufferInfo = infos, .descriptorCount = 2});
      device->DestroyBuffer(b); device->DestroyBuffer(a);
      device->DestroyBindingPool(pool); device->DestroyBindingLayout(layout);
    });
    run("TextureView2DArrayHandle", "validation", [&] {
      auto image = device->CreateImage({.width = 16, .height = 16, .arrayLayers = 4,
          .format = Format::RGBA8_UNORM, .usage = ImageUsage::Sampled,
          .debugName = "test.array"});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM,
          .type = ImageViewType::View2DArray, .arrayLayerCount = 4,
          .debugName = "test.array.view"});
      if (!view) throw std::runtime_error("2D array view handle is null");
      device->DestroyImageView(view); device->DestroyImage(image);
    });
    run("SampledTextureBinding", "resource", [&] {
      auto image = device->CreateImage({.width = 8, .height = 8,
          .format = Format::RGBA8_UNORM, .usage = ImageUsage::Sampled,
          .debugName = "test.sampled.image"});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM,
          .debugName = "test.sampled.view"});
      auto sampler = device->CreateSampler({.minFilter = Filter::Linear,
          .magFilter = Filter::Nearest, .addressU = SamplerAddressMode::ClampToEdge,
          .debugName = "test.sampled.sampler"});
      const BindingDesc binding{.binding = 0, .type = BindingType::CombinedImageSampler,
          .count = 1, .visibility = ShaderStage::Fragment};
      auto layout = device->CreateBindingLayout({.bindings = &binding, .bindingCount = 1});
      const BindingPoolSize size{.type = BindingType::CombinedImageSampler, .count = 1};
      auto pool = device->CreateBindingPool({.poolSizes = &size, .poolSizeCount = 1, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      const BindingImageInfo info{.sampler = sampler, .imageView = view,
          .imageLayout = ImageLayout::ShaderReadOnly};
      device->UpdateBindingSet({.dstSet = set, .type = BindingType::CombinedImageSampler,
          .imageInfo = &info});
      device->DestroyBindingPool(pool); device->DestroyBindingLayout(layout);
      device->DestroySampler(sampler); device->DestroyImageView(view); device->DestroyImage(image);
    });
    run("StorageTextureBinding", "resource", [&] {
      auto image = device->CreateImage({.width = 8, .height = 8,
          .format = Format::RGBA8_UNORM, .usage = ImageUsage::Storage,
          .debugName = "test.storage.image"});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM});
      const BindingDesc binding{.binding = 2, .type = BindingType::StorageImage,
          .count = 1, .visibility = ShaderStage::Compute};
      auto layout = device->CreateBindingLayout({.bindings = &binding, .bindingCount = 1});
      const BindingPoolSize size{.type = BindingType::StorageImage, .count = 1};
      auto pool = device->CreateBindingPool({.poolSizes = &size, .poolSizeCount = 1, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      const BindingImageInfo info{.imageView = view, .imageLayout = ImageLayout::General};
      device->UpdateBindingSet({.dstSet = set, .binding = 2, .type = BindingType::StorageImage,
          .imageInfo = &info});
      device->DestroyBindingPool(pool); device->DestroyBindingLayout(layout);
      device->DestroyImageView(view); device->DestroyImage(image);
    });
    run("CommandBufferBeginEnd", "command", [&] {
      auto &commands = device->GetCommandList();
      commands.Begin(); commands.End(); device->Submit(); device->WaitIdle();
    });
    run("BindUniformBufferCommand", "command", [&] {
      auto buffer = device->CreateBuffer({.size = 256,
          .usage = BufferUsage::Uniform, .memoryUsage = MemoryUsage::CPUToGPU,
          .debugName = "test.bind.uniform"});
      auto &commands = device->GetCommandList();
      commands.Begin();
      std::exception_ptr bindingFailure;
      try {
        commands.BindUniformBuffer(0, buffer, 0, 256);
      } catch (...) {
        bindingFailure = std::current_exception();
      }
      commands.End();
      device->Submit();
      device->WaitIdle();
      device->DestroyBuffer(buffer);
      if (bindingFailure) std::rethrow_exception(bindingFailure);
    });
    run("CommandBufferCopy", "command", [&] {
      uint32_t data[16];
      for (uint32_t i = 0; i < 16; ++i) data[i] = i * 101u + 7u;
      auto source = device->CreateBuffer({.size = sizeof(data),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = data, .debugName = "test.copy.source"});
      auto destination = device->CreateBuffer({.size = sizeof(data),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.copy.destination"});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.CopyBuffer(source, destination, {.srcOffset = 0, .dstOffset = 0, .size = sizeof(data)});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(destination));
      const bool matches = std::equal(std::begin(data), std::end(data), mapped);
      device->UnmapBuffer(destination);
      device->DestroyBuffer(destination); device->DestroyBuffer(source);
      if (!matches) throw std::runtime_error("buffer copy contents mismatch");
    });
    run("CommandBufferUpdate", "command", [&] {
      auto buffer = device->CreateBuffer({.size = 64,
          .usage = BufferUsage::Uniform | BufferUsage::TransferSrc,
          .memoryUsage = MemoryUsage::CPUToGPU, .debugName = "test.update"});
      const uint32_t values[] = {1, 2, 3, 4};
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.UpdateBuffer({.buffer = buffer, .offset = 16,
          .data = values, .size = sizeof(values)});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(buffer));
      const bool matches = std::equal(std::begin(values), std::end(values), mapped + 4);
      device->UnmapBuffer(buffer);
      device->DestroyBuffer(buffer);
      if (!matches) throw std::runtime_error("buffer update contents mismatch");
    });
    run("CopyBufferToTextureMip", "command", [&] {
      std::vector<uint32_t> pixels(8 * 8, 0xff33aa77u);
      auto buffer = device->CreateBuffer({.size = pixels.size() * sizeof(uint32_t),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = pixels.data(), .debugName = "test.texture.upload"});
      auto image = device->CreateImage({.width = 16, .height = 16, .mipLevels = 2,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc | ImageUsage::Sampled,
          .debugName = "test.texture.upload.dst"});
      auto readback = device->CreateBuffer({.size = pixels.size() * sizeof(uint32_t),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::TransferDst,
          .aspect = ImageAspect::Color, .baseMipLevel = 1, .mipLevelCount = 1});
      commands.CopyBufferToImage(buffer, image, {.mipLevel = 1, .imageExtent = {8, 8, 1}});
      commands.Barrier(ImageBarrier{.image = image, .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color,
          .baseMipLevel = 1, .mipLevelCount = 1});
      commands.CopyImageToBuffer(image, readback,
          {.mipLevel = 1, .imageExtent = {8, 8, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(readback));
      const bool matches = std::equal(pixels.begin(), pixels.end(), mapped);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback); device->DestroyImage(image); device->DestroyBuffer(buffer);
      if (!matches) throw std::runtime_error("uploaded mip contents mismatch");
    });
    run("CopyBufferToTextureSlice", "command", [&] {
      std::vector<uint32_t> pixels(8 * 8, 0xff884422u);
      auto buffer = device->CreateBuffer({.size = pixels.size() * sizeof(uint32_t),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = pixels.data()});
      auto image = device->CreateImage({.width = 8, .height = 8, .arrayLayers = 3,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc | ImageUsage::Sampled});
      auto readback = device->CreateBuffer({.size = pixels.size() * sizeof(uint32_t),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::TransferDst,
          .aspect = ImageAspect::Color, .baseArrayLayer = 2, .layerCount = 1});
      commands.CopyBufferToImage(buffer, image, {.baseArrayLayer = 2, .layerCount = 1,
          .imageExtent = {8, 8, 1}});
      commands.Barrier(ImageBarrier{.image = image, .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color,
          .baseArrayLayer = 2, .layerCount = 1});
      commands.CopyImageToBuffer(image, readback, {.baseArrayLayer = 2, .layerCount = 1,
          .imageExtent = {8, 8, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint32_t *>(device->MapBuffer(readback));
      const bool matches = std::equal(pixels.begin(), pixels.end(), mapped);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback); device->DestroyImage(image); device->DestroyBuffer(buffer);
      if (!matches) throw std::runtime_error("uploaded array-slice contents mismatch");
    });
    run("BufferCopyOffsetRange", "buffer", [&] {
      std::array<uint32_t, 12> sourceValues{};
      for (uint32_t i = 0; i < sourceValues.size(); ++i)
        sourceValues[i] = 0x1000u + i * 19u;
      std::array<uint32_t, 12> initialDestination{};
      initialDestination.fill(0xdeadbeefu);
      auto source = device->CreateBuffer({.size = sizeof(sourceValues),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = sourceValues.data()});
      auto destination = device->CreateBuffer({.size = sizeof(initialDestination),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .initialData = initialDestination.data()});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.CopyBuffer(source, destination,
          {.srcOffset = 3 * sizeof(uint32_t), .dstOffset = 5 * sizeof(uint32_t),
           .size = 4 * sizeof(uint32_t)});
      commands.End();
      device->Submit();
      device->WaitIdle();
      auto expected = initialDestination;
      std::copy_n(sourceValues.begin() + 3, 4, expected.begin() + 5);
      const auto *mapped =
          static_cast<const uint32_t *>(device->MapBuffer(destination));
      const bool matches = std::equal(expected.begin(), expected.end(), mapped);
      device->UnmapBuffer(destination);
      device->DestroyBuffer(destination);
      device->DestroyBuffer(source);
      if (!matches)
        throw std::runtime_error("buffer copy modified bytes outside the requested range");
    });
    run("UploadContextBufferRoundTrip", "buffer", [&] {
      std::array<uint32_t, 16> expected{};
      for (uint32_t i = 0; i < expected.size(); ++i)
        expected[i] = i * i + 31u;
      auto destination = device->CreateBuffer({.size = sizeof(expected),
          .usage = BufferUsage::TransferDst | BufferUsage::TransferSrc,
          .memoryUsage = MemoryUsage::GPUOnly});
      auto readback = device->CreateBuffer({.size = sizeof(expected),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto upload = device->CreateUploadContext(4096);
      upload->Begin();
      upload->UploadBuffer({.dstBuffer = destination, .size = sizeof(expected),
          .data = expected.data()});
      upload->Flush();
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(BufferBarrier{.buffer = destination,
          .oldState = ResourceState::TransferDst,
          .newState = ResourceState::TransferSrc});
      commands.CopyBuffer(destination, readback, {.size = sizeof(expected)});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint32_t *>(device->MapBuffer(readback));
      const bool matches = std::equal(expected.begin(), expected.end(), mapped);
      device->UnmapBuffer(readback);
      upload.reset();
      device->DestroyBuffer(readback);
      device->DestroyBuffer(destination);
      if (!matches)
        throw std::runtime_error("upload-context buffer round-trip mismatch");
    });
    run("UploadContextImageRoundTrip", "texture", [&] {
      constexpr uint32_t width = 6, height = 5;
      std::vector<uint8_t> expected(width * height * 4);
      for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
          const size_t offset = (y * width + x) * 4;
          expected[offset + 0] = static_cast<uint8_t>(x * 29u + 3u);
          expected[offset + 1] = static_cast<uint8_t>(y * 41u + 7u);
          expected[offset + 2] = static_cast<uint8_t>((x + y) * 17u + 5u);
          expected[offset + 3] = 255;
        }
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc});
      auto readback = device->CreateBuffer({.size = expected.size(),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto upload = device->CreateUploadContext(4096);
      upload->Begin();
      upload->UploadImage({.dstImage = image, .finalLayout = ImageLayout::TransferSrc,
          .width = width, .height = height}, expected.data(), expected.size());
      upload->Flush();
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + expected.size());
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      upload.reset();
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      if (actual != expected)
        throw std::runtime_error("upload-context image round-trip mismatch");
    });
    run("BufferTexturePaddedRowsRoundTrip", "texture", [&] {
      constexpr uint32_t width = 4, height = 3, rowLength = 7;
      std::vector<uint8_t> padded(rowLength * height * 4, 0x5a);
      std::vector<uint8_t> expected(width * height * 4);
      for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t pixel[] = {static_cast<uint8_t>(x * 47u + 9u),
              static_cast<uint8_t>(y * 71u + 13u),
              static_cast<uint8_t>((x + y) * 23u + 4u), 255};
          std::copy(pixel, pixel + 4,
              padded.begin() + (y * rowLength + x) * 4);
          std::copy(pixel, pixel + 4,
              expected.begin() + (y * width + x) * 4);
        }
      auto source = device->CreateBuffer({.size = padded.size(),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = padded.data()});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc});
      auto readback = device->CreateBuffer({.size = expected.size(),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::TransferDst});
      commands.CopyBufferToImage(source, image,
          {.bufferRowLength = rowLength, .bufferImageHeight = height,
           .imageExtent = {width, height, 1}});
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback,
          {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + expected.size());
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      device->DestroyBuffer(source);
      if (actual != expected)
        throw std::runtime_error("buffer row length was not honored");
    });
    run("BufferTextureSubrectangleRoundTrip", "texture", [&] {
      constexpr uint32_t width = 6, height = 5;
      std::vector<uint8_t> base(width * height * 4);
      for (size_t i = 0; i < base.size(); i += 4) {
        base[i + 0] = 7; base[i + 1] = 19; base[i + 2] = 41; base[i + 3] = 255;
      }
      constexpr uint32_t patchWidth = 3, patchHeight = 2;
      std::vector<uint8_t> patch(patchWidth * patchHeight * 4);
      for (uint32_t y = 0; y < patchHeight; ++y)
        for (uint32_t x = 0; x < patchWidth; ++x) {
          const uint8_t pixel[] = {static_cast<uint8_t>(180 + x * 20),
              static_cast<uint8_t>(40 + y * 80), 93, 255};
          std::copy(pixel, pixel + 4,
              patch.begin() + (y * patchWidth + x) * 4);
          std::copy(pixel, pixel + 4,
              base.begin() + ((y + 2) * width + x + 1) * 4);
        }
      std::vector<uint8_t> initial(width * height * 4);
      for (size_t i = 0; i < initial.size(); i += 4) {
        initial[i + 0] = 7; initial[i + 1] = 19;
        initial[i + 2] = 41; initial[i + 3] = 255;
      }
      auto initialBuffer = device->CreateBuffer({.size = initial.size(),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = initial.data()});
      auto patchBuffer = device->CreateBuffer({.size = patch.size(),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = patch.data()});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc});
      auto readback = device->CreateBuffer({.size = base.size(),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::TransferDst});
      commands.CopyBufferToImage(initialBuffer, image,
          {.imageExtent = {width, height, 1}});
      commands.CopyBufferToImage(patchBuffer, image,
          {.imageOffset = {1, 2, 0},
           .imageExtent = {patchWidth, patchHeight, 1}});
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback,
          {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + base.size());
      publishPixelComparison(width, height, base, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      device->DestroyBuffer(patchBuffer);
      device->DestroyBuffer(initialBuffer);
      if (actual != base)
        throw std::runtime_error("subrectangle upload changed pixels outside its region");
    });
    run("BlitMipPixelOutput", "texture", [&] {
      constexpr uint32_t width = 4, height = 4;
      std::vector<uint8_t> source(width * height * 4);
      for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t value = static_cast<uint8_t>(x * 48u + y * 16u);
          const size_t offset = (y * width + x) * 4;
          source[offset + 0] = value; source[offset + 1] = value;
          source[offset + 2] = value; source[offset + 3] = 255;
        }
      const std::vector<uint8_t> expected = {
          32,32,32,255, 128,128,128,255,
          64,64,64,255, 160,160,160,255};
      auto upload = device->CreateBuffer({.size = source.size(),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = source.data()});
      auto image = device->CreateImage({.width = width, .height = height,
          .mipLevels = 2, .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferSrc | ImageUsage::TransferDst});
      auto readback = device->CreateBuffer({.size = expected.size(),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::TransferDst, .mipLevelCount = 2});
      commands.CopyBufferToImage(upload, image,
          {.imageExtent = {width, height, 1}});
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc,
          .baseMipLevel = 0, .mipLevelCount = 1});
      commands.BlitMip(image, width, height, 0, 1, 1);
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc,
          .baseMipLevel = 1, .mipLevelCount = 1});
      commands.CopyImageToBuffer(image, readback,
          {.mipLevel = 1, .imageExtent = {2, 2, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + expected.size());
      publishPixelComparison(2, 2, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      device->DestroyBuffer(upload);
      if (actual != expected)
        throw std::runtime_error("linear mip blit output mismatch");
    });
    run("GenerateMipmapsPixelOutput", "texture", [&] {
      constexpr uint32_t width = 4, height = 4;
      std::vector<uint8_t> source(width * height * 4);
      for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
          const uint8_t value = static_cast<uint8_t>(x * 48u + y * 16u);
          const size_t offset = (y * width + x) * 4;
          source[offset + 0] = value; source[offset + 1] = value;
          source[offset + 2] = value; source[offset + 3] = 255;
        }
      const std::vector<uint8_t> expected = {96, 96, 96, 255};
      auto upload = device->CreateBuffer({.size = source.size(),
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = source.data()});
      auto image = device->CreateImage({.width = width, .height = height,
          .mipLevels = 3, .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferSrc | ImageUsage::TransferDst});
      auto readback = device->CreateBuffer({.size = expected.size(),
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::TransferDst, .mipLevelCount = 3});
      commands.CopyBufferToImage(upload, image,
          {.imageExtent = {width, height, 1}});
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc,
          .baseMipLevel = 0, .mipLevelCount = 1});
      commands.GenerateMipmaps(image, width, height, 3, 1);
      const bool intermediateMipTransitioned =
          device->GetImageLayout(image, 1) == ImageLayout::TransferSrc;
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc,
          .baseMipLevel = 2, .mipLevelCount = 1});
      commands.CopyImageToBuffer(image, readback,
          {.mipLevel = 2, .imageExtent = {1, 1, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + expected.size());
      publishPixelComparison(1, 1, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      device->DestroyBuffer(upload);
      if (!intermediateMipTransitioned)
        throw std::runtime_error(
            "GenerateMipmaps reused mip 1 as a source without transitioning it from TransferDst to TransferSrc");
      if (actual != expected)
        throw std::runtime_error("generated mip chain output mismatch");
    });
    run("PixelAccurateClearReadback", "texture", [&] {
      constexpr uint32_t width = 4;
      constexpr uint32_t height = 4;
      constexpr uint64_t byteCount = width * height * 4;
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
          .debugName = "test.pixel.clear"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM, .debugName = "test.pixel.clear.view"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.pixel.readback"});

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment, .aspect = ImageAspect::Color});
      const ColorAttachmentDesc color{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store,
          .clearValue = {64.0f / 255.0f, 128.0f / 255.0f,
                         191.0f / 255.0f, 1.0f}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &color, .colorAttachmentCount = 1});
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback,
          {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      const uint8_t expected[] = {64, 128, 191, 255};
      std::vector<uint8_t> expectedImage(byteCount);
      for (size_t i = 0; i < byteCount; ++i)
        expectedImage[i] = expected[i % 4];
      const std::vector<uint8_t> actualImage(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expectedImage, actualImage);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (pixels[i] != expected[i % 4]) { mismatch = i; break; }
      }
      const uint8_t actual = mismatch < byteCount ? pixels[mismatch] : 0;
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      if (mismatch < byteCount) {
        std::ostringstream error;
        error << "pixel byte " << mismatch << ": expected "
              << static_cast<unsigned>(expected[mismatch % 4]) << ", got "
              << static_cast<unsigned>(actual);
        throw std::runtime_error(error.str());
      }
    });
    run("PixelUploadCheckerboardReadback", "texture", [&] {
      constexpr uint32_t width = 8;
      constexpr uint32_t height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      std::vector<uint8_t> expected(byteCount);
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          const bool bright = ((x / 2) + (y / 2)) % 2 == 0;
          const size_t offset = (y * width + x) * 4;
          expected[offset + 0] = bright ? 255 : 17;
          expected[offset + 1] = bright ? 73 : 211;
          expected[offset + 2] = bright ? 31 : 149;
          expected[offset + 3] = 255;
        }
      }

      auto upload = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = expected.data(), .debugName = "test.pixel.checker.upload"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::TransferSrc,
          .debugName = "test.pixel.checker.image"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.pixel.checker.readback"});

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::TransferDst, .aspect = ImageAspect::Color});
      commands.CopyBufferToImage(upload, image, {.imageExtent = {width, height, 1}});
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      const std::vector<uint8_t> actualImage(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expected, actualImage);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (pixels[i] != expected[i]) { mismatch = i; break; }
      }
      const uint8_t actual = mismatch < byteCount ? pixels[mismatch] : 0;
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImage(image);
      device->DestroyBuffer(upload);
      if (mismatch < byteCount) {
        const size_t pixel = mismatch / 4;
        std::ostringstream error;
        error << "pixel (" << pixel % width << ", " << pixel / width
              << ") channel " << mismatch % 4 << ": expected "
              << static_cast<unsigned>(expected[mismatch]) << ", got "
              << static_cast<unsigned>(actual);
        throw std::runtime_error(error.str());
      }
    });
    run("PixelRenderAreaQuadrants", "texture", [&] {
      constexpr uint32_t width = 8;
      constexpr uint32_t height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
          .debugName = "test.pixel.quadrants"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM, .debugName = "test.pixel.quadrants.view"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.pixel.quadrants.readback"});

      struct Region {
        Rect2D area;
        ClearColor color;
      };
      const Region regions[] = {
          {{{0, 0}, {4, 4}}, {1, 0, 0, 1}},
          {{{4, 0}, {4, 4}}, {0, 1, 0, 1}},
          {{{0, 4}, {4, 4}}, {0, 0, 1, 1}},
          {{{4, 4}, {4, 4}}, {1, 1, 0, 1}},
      };

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment, .aspect = ImageAspect::Color});
      for (const auto &region : regions) {
        const ColorAttachmentDesc color{.view = view, .loadOp = LoadOp::Clear,
            .storeOp = StoreOp::Store, .clearValue = region.color};
        commands.BeginRendering({.renderArea = region.area,
            .colorAttachments = &color, .colorAttachmentCount = 1});
        commands.EndRendering();
      }
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      const uint8_t colors[][4] = {
          {255, 0, 0, 255}, {0, 255, 0, 255},
          {0, 0, 255, 255}, {255, 255, 0, 255}};
      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> expectedImage(byteCount);
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          const auto &expected = colors[(y >= 4 ? 2 : 0) + (x >= 4 ? 1 : 0)];
          const size_t offset = (y * width + x) * 4;
          std::copy(expected, expected + 4, expectedImage.begin() + offset);
        }
      }
      const std::vector<uint8_t> actualImage(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expectedImage, actualImage);
      size_t mismatch = byteCount;
      uint8_t expectedByte = 0;
      for (uint32_t y = 0; y < height && mismatch == byteCount; ++y) {
        for (uint32_t x = 0; x < width && mismatch == byteCount; ++x) {
          const auto &expected = colors[(y >= 4 ? 2 : 0) + (x >= 4 ? 1 : 0)];
          for (size_t channel = 0; channel < 4; ++channel) {
            const size_t offset = (y * width + x) * 4 + channel;
            if (pixels[offset] != expected[channel]) {
              mismatch = offset;
              expectedByte = expected[channel];
              break;
            }
          }
        }
      }
      const uint8_t actual = mismatch < byteCount ? pixels[mismatch] : 0;
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      if (mismatch < byteCount) {
        const size_t pixel = mismatch / 4;
        std::ostringstream error;
        error << "pixel (" << pixel % width << ", " << pixel / width
              << ") channel " << mismatch % 4 << ": expected "
              << static_cast<unsigned>(expectedByte) << ", got "
              << static_cast<unsigned>(actual);
        throw std::runtime_error(error.str());
      }
    });
    run("PixelMipLayerReadback", "texture", [&] {
      constexpr uint32_t width = 4;
      constexpr uint32_t height = 4;
      constexpr uint64_t byteCount = width * height * 4;
      auto image = device->CreateImage({.width = 8, .height = 8, .mipLevels = 2,
          .arrayLayers = 2, .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
          .debugName = "test.pixel.subresource"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM, .type = ImageViewType::View2DArray,
          .baseMipLevel = 1, .mipLevelCount = 1,
          .baseArrayLayer = 1, .arrayLayerCount = 1,
          .debugName = "test.pixel.subresource.view"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.pixel.subresource.readback"});

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment, .aspect = ImageAspect::Color,
          .baseMipLevel = 1, .mipLevelCount = 1,
          .baseArrayLayer = 1, .layerCount = 1});
      const ColorAttachmentDesc color{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store,
          .clearValue = {23.0f / 255.0f, 101.0f / 255.0f,
                         219.0f / 255.0f, 1.0f}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &color, .colorAttachmentCount = 1});
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color,
          .baseMipLevel = 1, .mipLevelCount = 1,
          .baseArrayLayer = 1, .layerCount = 1});
      commands.CopyImageToBuffer(image, readback, {.mipLevel = 1,
          .baseArrayLayer = 1, .layerCount = 1,
          .imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      const uint8_t expected[] = {23, 101, 219, 255};
      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> expectedImage(byteCount);
      for (size_t i = 0; i < byteCount; ++i)
        expectedImage[i] = expected[i % 4];
      const std::vector<uint8_t> actualImage(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expectedImage, actualImage);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (pixels[i] != expected[i % 4]) { mismatch = i; break; }
      }
      const uint8_t actual = mismatch < byteCount ? pixels[mismatch] : 0;
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      if (mismatch < byteCount) {
        std::ostringstream error;
        error << "subresource byte " << mismatch << ": expected "
              << static_cast<unsigned>(expected[mismatch % 4]) << ", got "
              << static_cast<unsigned>(actual);
        throw std::runtime_error(error.str());
      }
    });
    run("UniformBufferShaderPixelOutput", "shader", [&] {
      constexpr uint32_t width = 4, height = 4;
      constexpr uint64_t byteCount = width * height * 4;
      const float color[] = {33.0f / 255.0f, 117.0f / 255.0f,
                             201.0f / 255.0f, 1.0f};
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vertexShader = CompileShader(*device, shaderRoot / "fullscreen.vert",
          ShaderStage::Vertex, "test.uniform.shader.vs");
      auto fragmentShader = CompileShader(*device, shaderRoot / "uniform_color.frag",
          ShaderStage::Fragment, "test.uniform.shader.fs");
      const BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer,
          .count = 1, .visibility = ShaderStage::Fragment};
      auto layout = device->CreateBindingLayout(
          {.bindings = &binding, .bindingCount = 1});
      const BindingPoolSize poolSize{.type = BindingType::UniformBuffer, .count = 1};
      auto pool = device->CreateBindingPool(
          {.poolSizes = &poolSize, .poolSizeCount = 1, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      auto uniformBuffer = device->CreateBuffer({.size = sizeof(color),
          .usage = BufferUsage::Uniform, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = color});
      const BindingBufferInfo bufferInfo{
          .buffer = uniformBuffer, .offset = 0, .range = sizeof(color)};
      device->UpdateBindingSet({.dstSet = set, .binding = 0,
          .type = BindingType::UniformBuffer, .bufferInfo = &bufferInfo});
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vertexShader, .fragmentShader = fragmentShader,
          .layout = {.descriptorSetLayouts = &layout,
                     .descriptorSetLayoutCount = 1},
          .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto view = device->CreateImageView(
          {.image = image, .format = Format::RGBA8_UNORM});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view,
          .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &attachment, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetBindings(pipeline, 0, set);
      commands.SetViewport({.width = static_cast<float>(width),
          .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.Draw(3);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback,
          {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const uint8_t pixel[] = {33, 117, 201, 255};
      std::vector<uint8_t> expected(byteCount);
      for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = pixel[i % 4];
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      device->DestroyPipeline(pipeline);
      device->DestroyBuffer(uniformBuffer);
      device->DestroyBindingPool(pool);
      device->DestroyBindingLayout(layout);
      device->DestroyShader(fragmentShader);
      device->DestroyShader(vertexShader);
      if (actual != expected)
        throw std::runtime_error("uniform-buffer shader color mismatch");
    });
    run("StorageBufferComputeReadback", "shader", [&] {
      constexpr uint32_t valueCount = 64;
      constexpr uint64_t byteCount = valueCount * sizeof(uint32_t);
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto shader = CompileShader(*device, shaderRoot / "storage_buffer.comp",
          ShaderStage::Compute, "test.storage.buffer.cs");
      const BindingDesc binding{.binding = 0, .type = BindingType::StorageBuffer,
          .count = 1, .visibility = ShaderStage::Compute};
      auto layout = device->CreateBindingLayout(
          {.bindings = &binding, .bindingCount = 1});
      const BindingPoolSize poolSize{.type = BindingType::StorageBuffer, .count = 1};
      auto pool = device->CreateBindingPool(
          {.poolSizes = &poolSize, .poolSizeCount = 1, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      auto output = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::Storage | BufferUsage::TransferSrc,
          .memoryUsage = MemoryUsage::GPUOnly});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      const BindingBufferInfo info{.buffer = output, .range = byteCount};
      device->UpdateBindingSet({.dstSet = set, .binding = 0,
          .type = BindingType::StorageBuffer, .bufferInfo = &info});
      auto pipeline = device->CreateComputePipeline({.computeShader = shader,
          .layout = {.descriptorSetLayouts = &layout,
                     .descriptorSetLayoutCount = 1}});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.BindComputePipeline(pipeline);
      commands.SetComputeBindings(pipeline, 0, set);
      commands.Dispatch(valueCount / 8, 1, 1);
      commands.Barrier(BufferBarrier{.buffer = output,
          .oldState = ResourceState::ShaderWrite,
          .newState = ResourceState::TransferSrc});
      commands.CopyBuffer(output, readback, {.size = byteCount});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const auto *mapped =
          static_cast<const uint32_t *>(device->MapBuffer(readback));
      uint32_t mismatch = valueCount;
      for (uint32_t i = 0; i < valueCount; ++i)
        if (mapped[i] != i * 37u + 11u) { mismatch = i; break; }
      const uint32_t actual = mismatch < valueCount ? mapped[mismatch] : 0;
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyBuffer(output);
      device->DestroyPipeline(pipeline);
      device->DestroyBindingPool(pool);
      device->DestroyBindingLayout(layout);
      device->DestroyShader(shader);
      if (mismatch < valueCount) {
        std::ostringstream error;
        error << "storage buffer value " << mismatch << ": expected "
              << mismatch * 37u + 11u << ", got " << actual;
        throw std::runtime_error(error.str());
      }
    });
    run("ComputePushConstantsReadback", "shader", [&] {
      constexpr uint32_t valueCount = 32;
      constexpr uint64_t byteCount = valueCount * sizeof(uint32_t);
      constexpr uint32_t baseValue = 9001;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto shader = CompileShader(*device,
          shaderRoot / "compute_push_constants.comp", ShaderStage::Compute,
          "test.compute.push.cs");
      const BindingDesc binding{.binding = 0, .type = BindingType::StorageBuffer,
          .count = 1, .visibility = ShaderStage::Compute};
      auto layout = device->CreateBindingLayout(
          {.bindings = &binding, .bindingCount = 1});
      const BindingPoolSize poolSize{.type = BindingType::StorageBuffer, .count = 1};
      auto pool = device->CreateBindingPool(
          {.poolSizes = &poolSize, .poolSizeCount = 1, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      auto output = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::Storage | BufferUsage::TransferSrc,
          .memoryUsage = MemoryUsage::GPUOnly});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      const BindingBufferInfo info{.buffer = output, .range = byteCount};
      device->UpdateBindingSet({.dstSet = set, .binding = 0,
          .type = BindingType::StorageBuffer, .bufferInfo = &info});
      auto pipeline = device->CreateComputePipeline({.computeShader = shader,
          .layout = {.descriptorSetLayouts = &layout,
                     .descriptorSetLayoutCount = 1}});
      auto &commands = device->GetCommandList();
      commands.Begin();
      std::exception_ptr recordingFailure;
      try {
        commands.BindComputePipeline(pipeline);
        commands.SetComputeBindings(pipeline, 0, set);
        commands.PushConstants(ShaderStage::Compute, 0, sizeof(baseValue),
                               &baseValue);
        commands.Dispatch(valueCount / 8, 1, 1);
        commands.Barrier(BufferBarrier{.buffer = output,
            .oldState = ResourceState::ShaderWrite,
            .newState = ResourceState::TransferSrc});
        commands.CopyBuffer(output, readback, {.size = byteCount});
      } catch (...) {
        recordingFailure = std::current_exception();
      }
      commands.End();
      device->Submit();
      device->WaitIdle();
      uint32_t mismatch = valueCount;
      uint32_t actual = 0;
      if (!recordingFailure) {
        const auto *mapped =
            static_cast<const uint32_t *>(device->MapBuffer(readback));
        for (uint32_t i = 0; i < valueCount; ++i)
          if (mapped[i] != baseValue + i) { mismatch = i; break; }
        actual = mismatch < valueCount ? mapped[mismatch] : 0;
        device->UnmapBuffer(readback);
      }
      device->DestroyBuffer(readback);
      device->DestroyBuffer(output);
      device->DestroyPipeline(pipeline);
      device->DestroyBindingPool(pool);
      device->DestroyBindingLayout(layout);
      device->DestroyShader(shader);
      if (recordingFailure) std::rethrow_exception(recordingFailure);
      if (mismatch < valueCount) {
        std::ostringstream error;
        error << "compute push-constant value " << mismatch << ": expected "
              << baseValue + mismatch << ", got " << actual;
        throw std::runtime_error(error.str());
      }
    });
    run("DispatchRequiresCurrentComputePipeline", "validation", [&] {
      auto &commands = device->GetCommandList();
      commands.Begin();
      bool rejected = false;
      try {
        commands.Dispatch(0, 1, 1);
      } catch (const std::exception &) {
        rejected = true;
      }
      commands.End();
      if (!rejected)
        throw std::runtime_error(
            "Dispatch accepted a fresh command buffer with no compute pipeline bound");
    });
    run("ShaderProceduralPixelCoordinates", "shader", [&] {
      constexpr uint32_t width = 8;
      constexpr uint32_t height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vertexShader = CompileShader(*device, shaderRoot / "fullscreen.vert",
                                        ShaderStage::Vertex, "test.shader.fullscreen");
      auto fragmentShader = CompileShader(*device,
          shaderRoot / "pixel_coordinates.frag", ShaderStage::Fragment,
          "test.shader.pixel_coordinates");
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vertexShader, .fragmentShader = fragmentShader,
          .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM,
          .debugName = "test.pipeline.pixel_coordinates"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
          .debugName = "test.shader.pixel_coordinates.image"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM,
          .debugName = "test.shader.pixel_coordinates.view"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.shader.pixel_coordinates.readback"});

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment, .aspect = ImageAspect::Color});
      const ColorAttachmentDesc color{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store, .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &color, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width),
          .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.Draw(3);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      std::vector<uint8_t> expected(byteCount);
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          const size_t offset = (y * width + x) * 4;
          expected[offset + 0] = static_cast<uint8_t>(std::min(x * 29u + 11u, 255u));
          expected[offset + 1] = static_cast<uint8_t>(std::min(y * 37u + 7u, 255u));
          expected[offset + 2] =
              static_cast<uint8_t>(std::min((x ^ y) * 41u + 3u, 255u));
          expected[offset + 3] = 255;
        }
      }
      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      const std::vector<uint8_t> actual(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expected, actual);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (actual[i] != expected[i]) { mismatch = i; break; }
      }
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      device->DestroyPipeline(pipeline);
      device->DestroyShader(fragmentShader);
      device->DestroyShader(vertexShader);
      if (mismatch < byteCount) {
        const size_t pixel = mismatch / 4;
        std::ostringstream error;
        error << "shader pixel (" << pixel % width << ", " << pixel / width
              << ") channel " << mismatch % 4 << ": expected "
              << static_cast<unsigned>(expected[mismatch]) << ", got "
              << static_cast<unsigned>(actual[mismatch]);
        throw std::runtime_error(error.str());
      }
    });
    run("ShaderPushConstantColor", "shader", [&] {
      constexpr uint32_t width = 6;
      constexpr uint32_t height = 6;
      constexpr uint64_t byteCount = width * height * 4;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vertexShader = CompileShader(*device, shaderRoot / "fullscreen.vert",
                                        ShaderStage::Vertex, "test.shader.fullscreen.push");
      auto fragmentShader = CompileShader(*device, shaderRoot / "push_color.frag",
                                          ShaderStage::Fragment,
                                          "test.shader.push_color");
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vertexShader, .fragmentShader = fragmentShader,
          .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM,
          .debugName = "test.pipeline.push_color"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
          .debugName = "test.shader.push_color.image"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM, .debugName = "test.shader.push_color.view"});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.shader.push_color.readback"});
      const float pushedColor[4] = {
          37.0f / 255.0f, 113.0f / 255.0f, 227.0f / 255.0f, 1.0f};

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment, .aspect = ImageAspect::Color});
      const ColorAttachmentDesc color{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store, .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &color, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width),
          .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.PushConstants(ShaderStage::Fragment, 0, sizeof(pushedColor), pushedColor);
      commands.Draw(3);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      const uint8_t colorBytes[] = {37, 113, 227, 255};
      std::vector<uint8_t> expected(byteCount);
      for (size_t i = 0; i < byteCount; ++i)
        expected[i] = colorBytes[i % 4];
      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      const std::vector<uint8_t> actual(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expected, actual);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (actual[i] != expected[i]) { mismatch = i; break; }
      }
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      device->DestroyPipeline(pipeline);
      device->DestroyShader(fragmentShader);
      device->DestroyShader(vertexShader);
      if (mismatch < byteCount) {
        const size_t pixel = mismatch / 4;
        std::ostringstream error;
        error << "push-constant pixel (" << pixel % width << ", "
              << pixel / width << ") channel " << mismatch % 4
              << ": expected " << static_cast<unsigned>(expected[mismatch])
              << ", got " << static_cast<unsigned>(actual[mismatch]);
        throw std::runtime_error(error.str());
      }
    });
    run("ShaderComputeStorageImage", "shader", [&] {
      constexpr uint32_t width = 8;
      constexpr uint32_t height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto computeShader = CompileShader(*device, shaderRoot / "storage_image.comp",
                                         ShaderStage::Compute,
                                         "test.shader.storage_image");
      const BindingDesc binding{.binding = 0, .type = BindingType::StorageImage,
          .count = 1, .visibility = ShaderStage::Compute};
      auto bindingLayout = device->CreateBindingLayout({
          .bindings = &binding, .bindingCount = 1,
          .debugName = "test.shader.storage_image.layout"});
      const BindingPoolSize poolSize{.type = BindingType::StorageImage, .count = 1};
      auto bindingPool = device->CreateBindingPool({
          .poolSizes = &poolSize, .poolSizeCount = 1, .maxSets = 1,
          .debugName = "test.shader.storage_image.pool"});
      auto bindingSet = device->AllocateBindingSet({
          .pool = bindingPool, .layout = bindingLayout,
          .debugName = "test.shader.storage_image.set"});
      auto pipeline = device->CreateComputePipeline({
          .computeShader = computeShader,
          .layout = {.descriptorSetLayouts = &bindingLayout,
                     .descriptorSetLayoutCount = 1},
          .debugName = "test.pipeline.storage_image"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::Storage | ImageUsage::TransferSrc,
          .debugName = "test.shader.storage_image.output"});
      auto view = device->CreateImageView({.image = image,
          .format = Format::RGBA8_UNORM,
          .debugName = "test.shader.storage_image.output.view"});
      const BindingImageInfo imageInfo{.imageView = view,
          .imageLayout = ImageLayout::General};
      device->UpdateBindingSet({.dstSet = bindingSet, .binding = 0,
          .type = BindingType::StorageImage, .imageInfo = &imageInfo});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU,
          .debugName = "test.shader.storage_image.readback"});

      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::General, .aspect = ImageAspect::Color});
      commands.BindComputePipeline(pipeline);
      commands.SetComputeBindings(pipeline, 0, bindingSet);
      commands.Dispatch(width / 4, height / 4, 1);
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::General,
          .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();

      std::vector<uint8_t> expected(byteCount);
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          const size_t offset = (y * width + x) * 4;
          expected[offset + 0] = static_cast<uint8_t>(x * 31u + 5u);
          expected[offset + 1] =
              static_cast<uint8_t>(std::min(y * 43u + 9u, 255u));
          expected[offset + 2] =
              static_cast<uint8_t>(std::min((x + y) * 17u + 13u, 255u));
          expected[offset + 3] = 255;
        }
      }
      const auto *pixels = static_cast<const uint8_t *>(device->MapBuffer(readback));
      const std::vector<uint8_t> actual(pixels, pixels + byteCount);
      publishPixelComparison(width, height, expected, actual);
      size_t mismatch = byteCount;
      for (size_t i = 0; i < byteCount; ++i) {
        if (actual[i] != expected[i]) { mismatch = i; break; }
      }
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      device->DestroyPipeline(pipeline);
      device->DestroyBindingPool(bindingPool);
      device->DestroyBindingLayout(bindingLayout);
      device->DestroyShader(computeShader);
      if (mismatch < byteCount) {
        const size_t pixel = mismatch / 4;
        std::ostringstream error;
        error << "compute pixel (" << pixel % width << ", " << pixel / width
              << ") channel " << mismatch % 4 << ": expected "
              << static_cast<unsigned>(expected[mismatch]) << ", got "
              << static_cast<unsigned>(actual[mismatch]);
        throw std::runtime_error(error.str());
      }
    });
    run("DrawIndexedPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      struct Vertex { float position[2]; float color[4]; };
      const Vertex vertices[] = {
          {{-1, -1}, {0.125490196f, 0.752941176f, 0.250980392f, 1}},
          {{ 1, -1}, {0.125490196f, 0.752941176f, 0.250980392f, 1}},
          {{ 1,  1}, {0.125490196f, 0.752941176f, 0.250980392f, 1}},
          {{-1,  1}, {0.125490196f, 0.752941176f, 0.250980392f, 1}}};
      const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vs = CompileShader(*device, shaderRoot / "vertex_input.vert",
                              ShaderStage::Vertex, "test.indexed.vs");
      auto fsHandle = CompileShader(*device, shaderRoot / "vertex_color.frag",
                                    ShaderStage::Fragment, "test.indexed.fs");
      VertexBufferLayoutDesc vertexLayout{
          .stride = sizeof(Vertex), .inputRate = VertexInputRate::PerVertex,
          .attributes = {
              {.location = 0, .binding = 0, .format = VertexFormat::Float32x2,
               .offset = offsetof(Vertex, position)},
              {.location = 1, .binding = 0, .format = VertexFormat::Float32x4,
               .offset = offsetof(Vertex, color)}}};
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vs, .fragmentShader = fsHandle,
          .vertexLayouts = {vertexLayout}, .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM, .debugName = "test.indexed.pipeline"});
      auto vertexBuffer = device->CreateBuffer({.size = sizeof(vertices),
          .usage = BufferUsage::Vertex, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = vertices, .debugName = "test.indexed.vertices"});
      auto indexBuffer = device->CreateBuffer({.size = sizeof(indices),
          .usage = BufferUsage::Index, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = indices, .debugName = "test.indexed.indices"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store, .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &attachment, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width), .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.BindVertexBuffer(0, vertexBuffer);
      commands.BindIndexBuffer(indexBuffer, IndexType::U32);
      commands.DrawIndexed(6);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image, .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      const uint8_t color[] = {32, 192, 64, 255};
      std::vector<uint8_t> expected(byteCount);
      for (size_t i = 0; i < byteCount; ++i) expected[i] = color[i % 4];
      const auto *mapped = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback); device->DestroyImageView(view); device->DestroyImage(image);
      device->DestroyBuffer(indexBuffer); device->DestroyBuffer(vertexBuffer);
      device->DestroyPipeline(pipeline); device->DestroyShader(fsHandle); device->DestroyShader(vs);
      if (actual != expected) throw std::runtime_error("indexed draw output differs from expected fullscreen quad");
    });
    run("DrawIndexedU16UNormPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      struct Vertex {
        float position[2];
        uint8_t color[4];
      };
      const Vertex vertices[] = {
          {{-1, -1}, {17, 149, 233, 255}},
          {{ 1, -1}, {17, 149, 233, 255}},
          {{ 1,  1}, {17, 149, 233, 255}},
          {{-1,  1}, {17, 149, 233, 255}}};
      const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vertexShader = CompileShader(*device, shaderRoot / "vertex_input.vert",
          ShaderStage::Vertex, "test.u16.unorm.vs");
      auto fragmentShader = CompileShader(*device, shaderRoot / "vertex_color.frag",
          ShaderStage::Fragment, "test.u16.unorm.fs");
      VertexBufferLayoutDesc vertexLayout{.stride = sizeof(Vertex),
          .attributes = {
              {.location = 0, .format = VertexFormat::Float32x2,
               .offset = offsetof(Vertex, position)},
              {.location = 1, .format = VertexFormat::UNorm8x4,
               .offset = offsetof(Vertex, color)}}};
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vertexShader, .fragmentShader = fragmentShader,
          .vertexLayouts = {vertexLayout}, .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM});
      auto vertexBuffer = device->CreateBuffer({.size = sizeof(vertices),
          .usage = BufferUsage::Vertex, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = vertices});
      auto indexBuffer = device->CreateBuffer({.size = sizeof(indices),
          .usage = BufferUsage::Index, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = indices});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto view = device->CreateImageView(
          {.image = image, .format = Format::RGBA8_UNORM});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = image,
          .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view,
          .clearValue = {0, 0, 0, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &attachment, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width),
          .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.BindVertexBuffer(0, vertexBuffer);
      commands.BindIndexBuffer(indexBuffer, IndexType::U16);
      commands.DrawIndexed(6);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback,
          {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const uint8_t pixel[] = {17, 149, 233, 255};
      std::vector<uint8_t> expected(byteCount);
      for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = pixel[i % 4];
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(view);
      device->DestroyImage(image);
      device->DestroyBuffer(indexBuffer);
      device->DestroyBuffer(vertexBuffer);
      device->DestroyPipeline(pipeline);
      device->DestroyShader(fragmentShader);
      device->DestroyShader(vertexShader);
      if (actual != expected)
        throw std::runtime_error(
            "16-bit indexed UNorm8 vertex output mismatch");
    });
    run("DepthTestTriangleStripPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      struct Vertex {
        float position[3];
        float color[4];
      };
      const Vertex vertices[] = {
          {{-1, -1, 0.25f}, {1, 0, 0, 1}},
          {{ 1, -1, 0.25f}, {1, 0, 0, 1}},
          {{-1,  1, 0.25f}, {1, 0, 0, 1}},
          {{ 1,  1, 0.25f}, {1, 0, 0, 1}},
          {{-1, -1, 0.75f}, {0, 1, 0, 1}},
          {{ 1, -1, 0.75f}, {0, 1, 0, 1}},
          {{-1,  1, 0.75f}, {0, 1, 0, 1}},
          {{ 1,  1, 0.75f}, {0, 1, 0, 1}}};
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vertexShader = CompileShader(*device, shaderRoot / "vertex_depth.vert",
          ShaderStage::Vertex, "test.depth.vs");
      auto fragmentShader = CompileShader(*device, shaderRoot / "vertex_color.frag",
          ShaderStage::Fragment, "test.depth.fs");
      VertexBufferLayoutDesc vertexLayout{.stride = sizeof(Vertex),
          .attributes = {
              {.location = 0, .format = VertexFormat::Float32x3,
               .offset = offsetof(Vertex, position)},
              {.location = 1, .format = VertexFormat::Float32x4,
               .offset = offsetof(Vertex, color)}}};
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vertexShader, .fragmentShader = fragmentShader,
          .vertexLayouts = {vertexLayout},
          .topology = PrimitiveTopology::TriangleStrip,
          .raster = {.cullBackFaces = false},
          .depth = {.depthTestEnable = true, .depthWriteEnable = true,
                    .depthFormat = Format::D32_FLOAT},
          .colorFormat = Format::RGBA8_UNORM});
      auto vertexBuffer = device->CreateBuffer({.size = sizeof(vertices),
          .usage = BufferUsage::Vertex, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = vertices});
      auto colorImage = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto depthImage = device->CreateImage({.width = width, .height = height,
          .format = Format::D32_FLOAT, .usage = ImageUsage::DepthStencil});
      auto colorView = device->CreateImageView(
          {.image = colorImage, .format = Format::RGBA8_UNORM});
      auto depthView = device->CreateImageView({.image = depthImage,
          .format = Format::D32_FLOAT, .aspect = ImageAspect::Depth});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList();
      commands.Begin();
      commands.Barrier(ImageBarrier{.image = colorImage,
          .newLayout = ImageLayout::ColorAttachment});
      commands.Barrier(ImageBarrier{.image = depthImage,
          .newLayout = ImageLayout::DepthAttachment,
          .aspect = ImageAspect::Depth});
      const ColorAttachmentDesc colorAttachment{.view = colorView,
          .clearValue = {0, 0, 0, 1}};
      const DepthAttachmentDesc depthAttachment{.view = depthView,
          .clearDepth = 1.0f};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &colorAttachment, .colorAttachmentCount = 1,
          .depthAttachment = &depthAttachment});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width),
          .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}});
      commands.BindVertexBuffer(0, vertexBuffer);
      commands.Draw(4, 1, 0);
      commands.Draw(4, 1, 4);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = colorImage,
          .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(colorImage, readback,
          {.imageExtent = {width, height, 1}});
      commands.End();
      device->Submit();
      device->WaitIdle();
      const uint8_t pixel[] = {255, 0, 0, 255};
      std::vector<uint8_t> expected(byteCount);
      for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = pixel[i % 4];
      const auto *mapped =
          static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback);
      device->DestroyBuffer(readback);
      device->DestroyImageView(depthView);
      device->DestroyImageView(colorView);
      device->DestroyImage(depthImage);
      device->DestroyImage(colorImage);
      device->DestroyBuffer(vertexBuffer);
      device->DestroyPipeline(pipeline);
      device->DestroyShader(fragmentShader);
      device->DestroyShader(vertexShader);
      if (actual != expected)
        throw std::runtime_error(
            "depth test/write did not preserve the nearer triangle strip");
    });
    run("FragmentDiscardPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto vs = CompileShader(*device, shaderRoot / "fullscreen.vert",
                              ShaderStage::Vertex, "test.discard.vs");
      auto fsHandle = CompileShader(*device, shaderRoot / "fragment_discard.frag",
                                    ShaderStage::Fragment, "test.discard.fs");
      auto pipeline = device->CreateGraphicsPipeline({
          .vertexShader = vs, .fragmentShader = fsHandle, .raster = {.cullBackFaces = false},
          .colorFormat = Format::RGBA8_UNORM, .debugName = "test.discard.pipeline"});
      auto image = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
      auto view = device->CreateImageView({.image = image, .format = Format::RGBA8_UNORM});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view, .loadOp = LoadOp::Clear,
          .storeOp = StoreOp::Store, .clearValue = {0, 0, 1, 1}};
      commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
          .colorAttachments = &attachment, .colorAttachmentCount = 1});
      commands.BindPipeline(pipeline);
      commands.SetViewport({.width = static_cast<float>(width), .height = static_cast<float>(height)});
      commands.SetScissor({.extent = {width, height}}); commands.Draw(3);
      commands.EndRendering();
      commands.Barrier(ImageBarrier{.image = image, .oldLayout = ImageLayout::ColorAttachment,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(image, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      std::vector<uint8_t> expected(byteCount);
      for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const uint8_t pixel[] = {x < 4 ? uint8_t{255} : uint8_t{0},
            x < 4 ? uint8_t{32} : uint8_t{0},
            x < 4 ? uint8_t{128} : uint8_t{255}, 255};
        std::copy(pixel, pixel + 4, expected.begin() + (y * width + x) * 4);
      }
      const auto *mapped = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback); device->DestroyBuffer(readback);
      device->DestroyImageView(view); device->DestroyImage(image);
      device->DestroyPipeline(pipeline); device->DestroyShader(fsHandle); device->DestroyShader(vs);
      if (actual != expected) throw std::runtime_error("fragment discard did not preserve the cleared half");
    });
    run("Sample2DNearestPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t byteCount = width * height * 4;
      std::vector<uint8_t> expected(byteCount);
      for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const size_t offset = (y * width + x) * 4;
        expected[offset] = static_cast<uint8_t>(x * 31 + 7);
        expected[offset + 1] = static_cast<uint8_t>(y * 29 + 11);
        expected[offset + 2] = ((x + y) & 1) ? 233 : 19;
        expected[offset + 3] = 255;
      }
      const fs::path shaderRoot = fs::path(__FILE__).parent_path() / "shaders";
      auto shader = CompileShader(*device, shaderRoot / "sample_texture.comp",
                                  ShaderStage::Compute, "test.sample2d.cs");
      const BindingDesc bindings[] = {
          {.binding = 0, .type = BindingType::CombinedImageSampler,
           .count = 1, .visibility = ShaderStage::Compute},
          {.binding = 1, .type = BindingType::StorageImage,
           .count = 1, .visibility = ShaderStage::Compute}};
      auto layout = device->CreateBindingLayout({.bindings = bindings, .bindingCount = 2});
      const BindingPoolSize sizes[] = {
          {.type = BindingType::CombinedImageSampler, .count = 1},
          {.type = BindingType::StorageImage, .count = 1}};
      auto pool = device->CreateBindingPool({
          .poolSizes = sizes, .poolSizeCount = 2, .maxSets = 1});
      auto set = device->AllocateBindingSet({.pool = pool, .layout = layout});
      auto pipeline = device->CreateComputePipeline({
          .computeShader = shader,
          .layout = {.descriptorSetLayouts = &layout, .descriptorSetLayoutCount = 1}});
      auto source = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::TransferDst | ImageUsage::Sampled});
      auto destination = device->CreateImage({.width = width, .height = height,
          .format = Format::RGBA8_UNORM,
          .usage = ImageUsage::Storage | ImageUsage::TransferSrc});
      auto sourceView = device->CreateImageView({.image = source, .format = Format::RGBA8_UNORM});
      auto destinationView =
          device->CreateImageView({.image = destination, .format = Format::RGBA8_UNORM});
      auto sampler = device->CreateSampler({.minFilter = Filter::Nearest,
          .magFilter = Filter::Nearest, .addressU = SamplerAddressMode::ClampToEdge,
          .addressV = SamplerAddressMode::ClampToEdge});
      const BindingImageInfo sourceInfo{.sampler = sampler, .imageView = sourceView,
          .imageLayout = ImageLayout::ShaderReadOnly};
      const BindingImageInfo destinationInfo{.imageView = destinationView,
          .imageLayout = ImageLayout::General};
      device->UpdateBindingSet({.dstSet = set, .binding = 0,
          .type = BindingType::CombinedImageSampler, .imageInfo = &sourceInfo});
      device->UpdateBindingSet({.dstSet = set, .binding = 1,
          .type = BindingType::StorageImage, .imageInfo = &destinationInfo});
      auto upload = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferSrc, .memoryUsage = MemoryUsage::CPUToGPU,
          .initialData = expected.data()});
      auto readback = device->CreateBuffer({.size = byteCount,
          .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      auto &commands = device->GetCommandList(); commands.Begin();
      commands.Barrier(ImageBarrier{.image = source, .newLayout = ImageLayout::TransferDst});
      commands.CopyBufferToImage(upload, source, {.imageExtent = {width, height, 1}});
      commands.Barrier(ImageBarrier{.image = source, .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::ShaderReadOnly});
      commands.Barrier(ImageBarrier{.image = destination, .newLayout = ImageLayout::General});
      commands.BindComputePipeline(pipeline); commands.SetComputeBindings(pipeline, 0, set);
      commands.Dispatch(width / 4, height / 4, 1);
      commands.Barrier(ImageBarrier{.image = destination, .oldLayout = ImageLayout::General,
          .newLayout = ImageLayout::TransferSrc});
      commands.CopyImageToBuffer(destination, readback, {.imageExtent = {width, height, 1}});
      commands.End(); device->Submit(); device->WaitIdle();
      const auto *mapped = static_cast<const uint8_t *>(device->MapBuffer(readback));
      std::vector<uint8_t> actual(mapped, mapped + byteCount);
      publishPixelComparison(width, height, expected, actual);
      device->UnmapBuffer(readback); device->DestroyBuffer(readback); device->DestroyBuffer(upload);
      device->DestroySampler(sampler); device->DestroyImageView(destinationView);
      device->DestroyImageView(sourceView); device->DestroyImage(destination);
      device->DestroyImage(source); device->DestroyPipeline(pipeline);
      device->DestroyBindingPool(pool); device->DestroyBindingLayout(layout);
      device->DestroyShader(shader);
      if (actual != expected) throw std::runtime_error("nearest sampled texture differs from uploaded source");
    });
    run("ScissorPixelOutput", "shader", [&] {
      constexpr uint32_t width = 8, height = 8;
      std::vector<uint8_t> expected(width * height * 4);
      for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const bool inside = x >= 2 && x < 6 && y >= 2 && y < 6;
        const uint8_t pixel[] = {inside ? uint8_t{240} : uint8_t{8},
            inside ? uint8_t{24} : uint8_t{16},
            inside ? uint8_t{48} : uint8_t{200}, 255};
        std::copy(pixel, pixel + 4, expected.begin() + (y * width + x) * 4);
      }
      const float shaderColor[] = {240.0f / 255.0f, 24.0f / 255.0f,
                                   48.0f / 255.0f, 1.0f};
      renderFullscreen(width, height, {8.0f / 255.0f, 16.0f / 255.0f,
          200.0f / 255.0f, 1.0f}, shaderColor, {},
          {.offset = {2, 2}, .extent = {4, 4}}, expected, "test.scissor.pixel");
    });
    run("BlendAddPixelOutput", "shader", [&] {
      constexpr uint32_t width = 4, height = 4;
      const uint8_t pixel[] = {30, 60, 120, 255};
      std::vector<uint8_t> expected(width * height * 4);
      for (size_t i = 0; i < expected.size(); ++i) expected[i] = pixel[i % 4];
      const float shaderColor[] = {10.0f / 255.0f, 20.0f / 255.0f,
                                   40.0f / 255.0f, 1.0f};
      BlendStateDesc blend{.enable = true, .srcColor = BlendFactor::One,
          .dstColor = BlendFactor::One, .colorOp = BlendOp::Add,
          .srcAlpha = BlendFactor::One, .dstAlpha = BlendFactor::Zero,
          .alphaOp = BlendOp::Add};
      renderFullscreen(width, height, {20.0f / 255.0f, 40.0f / 255.0f,
          80.0f / 255.0f, 1.0f}, shaderColor, blend,
          {.extent = {width, height}}, expected, "test.blend.add");
    });
    run("BlendReverseSubtractPixelOutput", "shader", [&] {
      constexpr uint32_t width = 4, height = 4;
      const uint8_t pixel[] = {90, 100, 160, 255};
      std::vector<uint8_t> expected(width * height * 4);
      for (size_t i = 0; i < expected.size(); ++i) expected[i] = pixel[i % 4];
      const float shaderColor[] = {10.0f / 255.0f, 20.0f / 255.0f,
                                   40.0f / 255.0f, 1.0f};
      BlendStateDesc blend{.enable = true, .srcColor = BlendFactor::One,
          .dstColor = BlendFactor::One, .colorOp = BlendOp::ReverseSubtract,
          .srcAlpha = BlendFactor::One, .dstAlpha = BlendFactor::Zero,
          .alphaOp = BlendOp::Add};
      renderFullscreen(width, height, {100.0f / 255.0f, 120.0f / 255.0f,
          200.0f / 255.0f, 1.0f}, shaderColor, blend,
          {.extent = {width, height}}, expected, "test.blend.reverse_subtract");
    });
    run("MultipleRenderTargetsPixelOutput", "texture", [&] {
      constexpr uint32_t width = 8, height = 8;
      constexpr uint64_t imageByteCount = width * height * 4;
      ImageHandle images[2]; ImageViewHandle views[2]; BufferHandle readbacks[2];
      for (int i = 0; i < 2; ++i) {
        images[i] = device->CreateImage({.width = width, .height = height,
            .format = Format::RGBA8_UNORM,
            .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
        views[i] = device->CreateImageView({.image = images[i], .format = Format::RGBA8_UNORM});
        readbacks[i] = device->CreateBuffer({.size = imageByteCount,
            .usage = BufferUsage::TransferDst, .memoryUsage = MemoryUsage::GPUToCPU});
      }
      auto &commands = device->GetCommandList(); commands.Begin();
      for (auto image : images)
        commands.Barrier(ImageBarrier{.image = image, .newLayout = ImageLayout::ColorAttachment,
            .aspect = ImageAspect::Color});
      const ColorAttachmentDesc colors[] = {
          {.view = views[0], .loadOp = LoadOp::Clear, .storeOp = StoreOp::Store,
           .clearValue = {1, 0, 0, 1}},
          {.view = views[1], .loadOp = LoadOp::Clear, .storeOp = StoreOp::Store,
           .clearValue = {0, 1, 0, 1}}};
      std::exception_ptr recordingFailure;
      try {
        commands.BeginRendering({.renderArea = {{0, 0}, {width, height}},
            .colorAttachments = colors, .colorAttachmentCount = 2});
        commands.EndRendering();
        for (int i = 0; i < 2; ++i) {
          commands.Barrier(ImageBarrier{.image = images[i],
              .oldLayout = ImageLayout::ColorAttachment,
              .newLayout = ImageLayout::TransferSrc, .aspect = ImageAspect::Color});
          commands.CopyImageToBuffer(images[i], readbacks[i],
              {.imageExtent = {width, height, 1}});
        }
      } catch (...) {
        recordingFailure = std::current_exception();
      }
      commands.End(); device->Submit(); device->WaitIdle();

      if (!recordingFailure) {
        std::vector<uint8_t> expected(width * 2 * height * 4);
        std::vector<uint8_t> actual(width * 2 * height * 4);
        const uint8_t targetColors[][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}};
        for (int target = 0; target < 2; ++target) {
          const auto *mapped =
              static_cast<const uint8_t *>(device->MapBuffer(readbacks[target]));
          for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
              const size_t sourceOffset = (y * width + x) * 4;
              const size_t combinedOffset = (y * width * 2 + target * width + x) * 4;
              std::copy(mapped + sourceOffset, mapped + sourceOffset + 4,
                        actual.begin() + combinedOffset);
              std::copy(targetColors[target], targetColors[target] + 4,
                        expected.begin() + combinedOffset);
            }
          }
          device->UnmapBuffer(readbacks[target]);
        }
        publishPixelComparison(width * 2, height, expected, actual);
        if (actual != expected)
          recordingFailure = std::make_exception_ptr(
              std::runtime_error("multiple render-target clear output mismatch"));
      }
      for (int i = 1; i >= 0; --i) {
        device->DestroyBuffer(readbacks[i]);
        device->DestroyImageView(views[i]); device->DestroyImage(images[i]);
      }
      if (recordingFailure) std::rethrow_exception(recordingFailure);
    });
    run("MultipleSwapchains", "validation", [&] {
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
      GLFWwindow *firstWindow =
          glfwCreateWindow(64, 64, "Velos RHI swapchain A", nullptr, nullptr);
      GLFWwindow *secondWindow =
          glfwCreateWindow(64, 64, "Velos RHI swapchain B", nullptr, nullptr);
      if (!firstWindow || !secondWindow) {
        if (secondWindow) glfwDestroyWindow(secondWindow);
        if (firstWindow) glfwDestroyWindow(firstWindow);
        throw std::runtime_error("could not create hidden swapchain test windows");
      }
      SwapchainHandle first{};
      SwapchainHandle second{};
      std::exception_ptr creationFailure;
      try {
        first = device->CreateSwapchain({.windowHandle = firstWindow,
            .width = 64, .height = 64, .bufferCount = 2, .vsync = true});
        second = device->CreateSwapchain({.windowHandle = secondWindow,
            .width = 64, .height = 64, .bufferCount = 2, .vsync = true});
        if (!first || !second || first.id == second.id)
          throw std::runtime_error("multiple swapchains did not return distinct handles");
      } catch (...) {
        creationFailure = std::current_exception();
      }
      if (second) device->DestroySwapchain(second);
      if (first) device->DestroySwapchain(first);
      glfwDestroyWindow(secondWindow);
      glfwDestroyWindow(firstWindow);
      if (creationFailure) std::rethrow_exception(creationFailure);
    });
    run("InvalidImageLayoutQuery", "validation", [&] {
      bool rejected = false;
      try { (void)device->GetImageLayout({}, 0); }
      catch (const std::exception &) { rejected = true; }
      if (!rejected) throw std::runtime_error("invalid handle was accepted");
    });
    run("InvalidMipLayoutQuery", "validation", [&] {
      auto image = device->CreateImage({.width = 8, .height = 8, .mipLevels = 2,
          .format = Format::RGBA8_UNORM, .usage = ImageUsage::Sampled,
          .debugName = "test.invalid.mip"});
      bool rejected = false;
      try { (void)device->GetImageLayout(image, 2); }
      catch (const std::exception &) { rejected = true; }
      device->DestroyImage(image);
      if (!rejected) throw std::runtime_error("out-of-range mip was accepted");
    });
    run("ResourceLifecycleStress", "resource", [&] {
      std::vector<BufferHandle> buffers;
      buffers.reserve(64);
      for (uint32_t i = 0; i < 64; ++i)
        buffers.push_back(device->CreateBuffer({.size = 64 + i * 16,
            .usage = BufferUsage::Storage, .memoryUsage = MemoryUsage::GPUOnly,
            .debugName = "test.stress"}));
      if (std::any_of(buffers.begin(), buffers.end(),
                      [](BufferHandle handle) { return !handle; }))
        throw std::runtime_error("stress allocation returned invalid handle");
      for (auto it = buffers.rbegin(); it != buffers.rend(); ++it)
        device->DestroyBuffer(*it);
      device->CollectGarbage();
    });
    device->WaitIdle();
  }
  const double totalMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - suiteStart).count();
  WriteReport(report, *selected, results, totalMs);
  std::cout << "Report: " << fs::absolute(report).string() << '\n';
  return std::all_of(results.begin(), results.end(),
                     [](const Result &result) { return result.passed; }) ? 0 : 1;
}
