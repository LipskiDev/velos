<div align="center">

# velos
### Render Hardware Interface · Modern C++ · Vulkan

![C++](https://img.shields.io/badge/C%2B%2B-23-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan-red)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-green)

</div>

---

velos is a modern Render Hardware Interface (RHI) with a Vulkan backend, designed to provide a clean abstraction over GPU APIs for real-time rendering. It serves as the foundation for [Rodan](https://github.com/LipskiDev/Rodan).

velos is **not** a rendering engine - scene systems, materials, lighting, and rendering techniques are intentionally left to higher-level systems built on top.

---

## Features

- Device and swapchain management with resize support
- Dynamic rendering — no render passes, no framebuffers
- Command list recording and submission
- Vertex, index, uniform, and storage buffers
- Image, image view, and sampler abstractions
- Graphics and compute pipeline state
- Descriptor pools, layouts, and sets
- Push constants
- Buffer-to-image copies
- Explicit image layout transitions and resource barriers
- Deferred resource deletion
- Type-safe handle system
- Shader compilation from GLSL, HLSL, SPIR-V binary, and Slang
- Automatic shader reflection via SPIRV-Reflect — extracts bindings and push constant ranges
- CPU profiling via Tracy
- GPU profiling via Tracy Vulkan integration

---

## Architecture

```
Application / Engine
        ↓
  velos RHI Interface
        ↓
  Backend (Vulkan)
        ↓
       GPU
```

---

## Design Goals

- **API abstraction without hiding fundamentals** — expose GPU concepts clearly without leaking backend-specific details
- **Explicit control** — no hidden work or implicit synchronization
- **Backend independence** — Vulkan is the first backend, with future support for DirectX 12
- **Minimal but complete** — provide only what is necessary to build a renderer

---

## Backends

| Backend | Status |
|---------|--------|
| Vulkan | ✅ Implemented |
| DirectX 12 | 🔲 Planned |
| WebGPU | 🔲 Planned |

---

## Building

```bash
git clone https://github.com/LipskiDev/velos
cd velos
premake5 gmake2
make -j$(nproc)
```

Requires a C++23 compiler, Vulkan SDK, and Premake5.

---

## Roadmap

- [x] Vulkan backend
- [x] Device and swapchain abstraction
- [x] Command list recording
- [x] Graphics and compute pipelines
- [x] Buffer support (vertex, index, uniform, storage)
- [x] Image, image view, and sampler abstractions
- [x] Descriptor pools, layouts, and sets
- [x] Push constants
- [x] Explicit resource barriers and image transitions
- [x] Dynamic rendering
- [x] Shader compilation (GLSL, HLSL, SPIR-V, Slang)
- [x] SPIRV-Reflect shader reflection
- [x] CPU & GPU profiling (Tracy)
- [x] Deferred resource deletion
- [ ] Resource state tracking
- [ ] DirectX 12 backend
- [ ] Bindless resource model
- [ ] Multi-queue submission

---

## Example Usage

A minimal example showing device creation, resource setup, and a render loop.

### 1. Create a device and swapchain

```cpp
#include "velos/rhi/rhi_device.h"

Velos::RHI::IDevice* device = Velos::RHI::CreateDevice({
    .backend         = Velos::RHI::BackendAPI::Vulkan,
    .enableValidation = true,
    .applicationName = "MyApp",
});

Velos::RHI::SwapchainHandle swapchain = device->CreateSwapchain({
    .windowHandle = glfwGetWin32Window(window), // or equivalent
    .width        = 1280,
    .height       = 720,
    .format       = Velos::RHI::Format::BGRA8_UNORM,
    .bufferCount  = 2,
    .vsync        = true,
});
```

### 2. Compile shaders and create a pipeline

```cpp
#include "velos/shader/shader_compiler.h"

// Compile and reflect shaders from source
auto vertOutput = Velos::ShaderCompiler::CompileFile({
    .path       = "shaders/mesh.vert",
    .stage      = Velos::RHI::ShaderStage::Vertex,
    .language   = Velos::ShaderSourceLanguage::GLSL,
});

auto fragOutput = Velos::ShaderCompiler::CompileFile({
    .path       = "shaders/mesh.frag",
    .stage      = Velos::RHI::ShaderStage::Fragment,
    .language   = Velos::ShaderSourceLanguage::GLSL,
});

Velos::RHI::ShaderHandle vertShader = device->CreateShader({
    .stage        = Velos::RHI::ShaderStage::Vertex,
    .bytecode     = vertOutput.spirv.data(),
    .bytecodeSize = vertOutput.spirv.size() * sizeof(uint32_t),
    .reflection   = vertOutput.reflection,
});

Velos::RHI::ShaderHandle fragShader = device->CreateShader({
    .stage        = Velos::RHI::ShaderStage::Fragment,
    .bytecode     = fragOutput.spirv.data(),
    .bytecodeSize = fragOutput.spirv.size() * sizeof(uint32_t),
    .reflection   = fragOutput.reflection,
});

Velos::RHI::PipelineHandle pipeline = device->CreateGraphicsPipeline({
    .vertexShader   = vertShader,
    .fragmentShader = fragShader,
    .vertexLayouts  = {{
        .stride    = sizeof(Vertex),
        .inputRate = Velos::RHI::VertexInputRate::PerVertex,
        .attributes = {
            { .location = 0, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x3, .offset = 0  },
            { .location = 1, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x2, .offset = 12 },
        },
    }},
    .topology    = Velos::RHI::PrimitiveTopology::TriangleList,
    .raster      = { .cullBackFaces = true, .frontFaceCCW = true },
    .depth       = { .depthTestEnable = true, .depthWriteEnable = true, .depthFormat = Velos::RHI::Format::D32_FLOAT },
    .colorFormat = Velos::RHI::Format::BGRA8_UNORM,
});
```

### 3. Upload geometry

```cpp
Velos::RHI::BufferHandle vertexBuffer = device->CreateBuffer({
    .size        = sizeof(vertices),
    .usage       = Velos::RHI::BufferUsage::Vertex,
    .memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
    .initialData = vertices,
});

Velos::RHI::BufferHandle indexBuffer = device->CreateBuffer({
    .size        = sizeof(indices),
    .usage       = Velos::RHI::BufferUsage::Index,
    .memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
    .initialData = indices,
});
```

### 4. Render loop

```cpp
while (!glfwWindowShouldClose(window))
{
    auto frame = device->BeginFrame(swapchain);
    if (!frame.success) continue;

    auto& cmd = device->GetCommandList(frame.commandList);
    cmd.Begin();

    // Transition backbuffer to color attachment
    cmd.Barrier({
        .image     = frame.backbufferImage,
        .newLayout = Velos::RHI::ImageLayout::ColorAttachment,
        .aspect    = Velos::RHI::ImageAspect::Color,
    });

    // Begin rendering
    Velos::RHI::ColorAttachmentDesc color{
        .view       = frame.backbuffer,
        .loadOp     = Velos::RHI::LoadOp::Clear,
        .storeOp    = Velos::RHI::StoreOp::Store,
        .clearValue = { 0.1f, 0.1f, 0.1f, 1.0f },
    };

    cmd.BeginRendering({
        .renderArea           = { {0, 0}, {1280, 720} },
        .colorAttachments     = &color,
        .colorAttachmentCount = 1,
    });

    cmd.SetViewport({ 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f });
    cmd.SetScissor({ {0, 0}, {1280, 720} });

    cmd.BindPipeline(pipeline);
    cmd.BindVertexBuffer(0, vertexBuffer);
    cmd.BindIndexBuffer(indexBuffer, Velos::RHI::IndexType::U32);
    cmd.DrawIndexed(indexCount);

    cmd.EndRendering();

    // Transition backbuffer to present
    cmd.Barrier({
        .image     = frame.backbufferImage,
        .newLayout = Velos::RHI::ImageLayout::Present,
        .aspect    = Velos::RHI::ImageAspect::Color,
    });

    cmd.End();

    device->SubmitAndPresent(frame.commandList, swapchain);
    device->CollectGarbage();
}
```

### 5. Cleanup

```cpp
device->WaitIdle();
device->DestroyBuffer(vertexBuffer);
device->DestroyBuffer(indexBuffer);
device->DestroyPipeline(pipeline);
device->DestroyShader(vertShader);
device->DestroyShader(fragShader);
device->DestroySwapchain(swapchain);
Velos::RHI::DestroyDevice(device);
```
