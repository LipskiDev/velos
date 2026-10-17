<div align="center">

# velos
### Render Hardware Interface · Modern C++ · Vulkan

![C++](https://img.shields.io/badge/C%2B%2B-23-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan-red)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-green)

</div>

---

velos is a modern Render Hardware Interface (RHI) with Vulkan and D3D12 backends, designed to provide a clean abstraction over GPU APIs for real-time rendering. It serves as the foundation for [Iryven](https://github.com/LipskiDev/Iryven).

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
- [x] D3D12 per-subresource resource state tracking
- [x] DirectX 12 core rendering backend (see remaining API gaps below)
- [x] Descriptor arrays and partially bound resources
- [x] Graphics, compute, and transfer queue timeline submissions

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
    .window = {
        .type = Velos::RHI::WindowHandleType::GLFW,
        .value = window,
    },
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

### D3D12 queues

The device owns separate DIRECT (graphics/presentation), COMPUTE, and COPY
(transfer) queues. `GetComputeQueue()` and `GetTransferQueue()` expose the
native queues. `AcquireCommandList(QueueType::Compute/Transfer)` provides a
matching command list; call `Begin()`, record commands, `End()`, then
`Submit(queueType, commands)`. Each auxiliary queue currently reuses one
allocator/list, waiting for its previous submission before reuse. Descriptors,
pipelines, and staging allocations remain alive until that submission finishes.
`WaitIdle()` waits for all three queues.

`GetCommandList(Graphics)` returns the main frame list for UI/presentation.
`AcquireCommandList(Graphics)` instead leases an independent DIRECT list from
the current frame slot's batch pool, even while the main list is recording.
End and submit every batch before submitting the main list. Batch submissions
keep the main list recording and retain their own descriptors/staging objects.
Pool entries are reused only after the frame-slot completion fence has passed.

Queue queries report distinct queues without family ownership transfers.
RHI `SubmitDesc` timeline waits/signals are supported on queue submissions and
on the main list's `SubmitAndPresent` call.
COPY queues accept only copy-compatible states; textures must pass through
COMMON when crossing between COPY and DIRECT/COMPUTE. The existing synchronous
upload context continues using DIRECT; explicit transfer command lists use COPY.

### Sparse images on D3D12

`CreateSparseImage(desc, memoryBudgetBytes)` creates a reserved 2D texture or
cube array and a default-heap tile pool. `GetSparseImageMemoryInfo` reports the
adapter's tile width/height, 64 KiB page size, and physical page count. The
budget is rounded down to whole pages and capped at the texture's virtual tile
count.

`BindSparseImagePages(image, pages)` supplies the complete desired residency
set, matching the Vulkan backend. Omitted blocks are unmapped; an empty set
unmaps everything. Unchanged sets do no GPU work. Changes wait for prior GPU
work, update tile mappings, and complete before returning. Clear or copy newly
mapped render-target/depth regions before using their contents.

The current per-block API requires tiled resources tier 2 or higher and rejects
packed mip tails and combined depth/stencil formats. D32 depth cubemaps are
supported. The caller must finish GPU use and destroy image views before
destroying the image; its reserved resource and page pool are released together.

### Timeline semaphores

Vulkan device selection requires timeline semaphore support and enables it.
Create a timeline, then pass optional waits/signals to a queue submission:

~~~cpp
using namespace Velos::RHI;
auto timeline = device->CreateSemaphore(SemaphoreType::Timeline, 0);
device->SignalSemaphore(timeline, 1); // CPU signal
const TimelineSemaphorePoint waits[] = {{timeline, 1}};
const TimelineSemaphorePoint signals[] = {{timeline, 2}};
auto &compute = device->AcquireCommandList(QueueType::Compute);
compute.Begin();
// Record compute work.
compute.End();
device->Submit(QueueType::Compute, compute,
               {.waits = waits, .signals = signals});
device->WaitSemaphore(timeline, 2); // CPU wait for GPU completion
auto completed = device->GetSemaphoreValue(timeline);
device->WaitIdle();
device->DestroySemaphore(timeline);
~~~

The caller chooses a logical `QueueType`; Velos selects the best available physical
queue and falls back to an alias of the graphics queue when necessary.
`GetQueueInfo` reports whether the selected queue is dedicated or aliases graphics.
`AcquireCommandList` may be called repeatedly for the same logical queue in one
frame. Each acquired list must be submitted explicitly, which permits sequences such
as compute, graphics, then compute again. The pool waits and recycles those command
lists when their frame-in-flight slot is reused.
`SubmitAndPresent(swapchain, desc)` remains graphics-only, accepts the same
descriptor, and retains the swapchain's binary acquire/present semaphores. Existing
graphics calls without an explicit queue type remain valid.

Signal values must strictly increase, including relative to pending signals.
The caller must synchronize CPU/GPU signals and keep semaphores alive until all
uses complete. Descriptor spans need remain valid only during the submission
call. CPU waits retain the existing exception behavior on timeout.

### D3D12 mesh drawing

Mesh pipelines support both direct `DrawMeshTasks` and `DrawMeshTasksIndirect`,
with or without a task/amplification shader. Vertex and indexed draw calls require
a conventional graphics pipeline; mesh draws require a mesh pipeline and an
active rendering scope on the graphics queue.

Indirect records contain three `u32` group counts (X, Y, Z). The stride must be
at least 12 bytes and a multiple of four; the byte offset must also be a multiple
of four. Command signatures are cached per stride on the device and reused across
pipelines. The argument buffer needs `BufferUsage::Indirect`, sufficient space
for `drawCount * stride` bytes after the offset, and indirect-readable state.
Default-heap buffers in COMMON may implicitly promote; GPU-written buffers must
be transitioned from shader-write access to `ResourceState::IndirectArgument`
before rendering. Upload buffers in GENERIC_READ are also supported.

Zero direct group counts and zero indirect draw counts are no-ops. Direct counts
are checked against the baseline D3D12 mesh group limits; callers generating
indirect commands on the GPU must ensure the generated counts satisfy those limits.

### D3D12 texture queue handoffs

Texture barriers track native state separately for each mip, array layer and
depth/stencil plane. Their source state comes from native tracking, while the
requested destination state is restricted to stages supported by the recording
queue (compute shader reads exclude pixel-shader access).

At command-list end, DIRECT and COMPUTE lists explicitly release transitioned
textures to COMMON on the producer queue. This conservative policy adds release
and acquire transitions even between lists on the same queue, but allows either
queue to perform the next transition legally. Buffer submission decay remains
unchanged. The next image-layout query reports Undefined for released textures
so a new acquire barrier is recorded; contents are preserved. This is a native
D3D12 COMMON state, not a Vulkan discard operation.

Queue submissions must still wait on the producer's completion semaphore/fence.
Releasing a texture does not synchronize queues by itself. Per-mip barriers only
affect the requested range, and same-state UAV barriers preserve write ordering.

### D3D12 descriptor table reuse

Repeated binds of an unchanged binding set reuse its shader-visible descriptor
tables within the current command-list recording, including across pipelines
whose table layouts have identical descriptor formats and counts. Binding-set
writes create a new version. Cached snapshots are not shared across recordings,
so update-after-bind is resolved independently at each submission. Frame resources
retain descriptor allocations until the associated completion fence is retired.

Sampler heaps remain limited to 2,048 descriptors. Applications should use a
small shared sampler table independently of large bindless texture arrays rather
than reserve one sampler slot for every possible texture.

### D3D12 push constants

Push blocks at `b0, space2147483647` use native 32-bit root constants, with
independent values for each shader stage and support for partial writes. The
root signature accounts for descriptor tables and the 64-DWORD limit. If it
cannot fit all stage blocks, the largest blocks spill to root CBVs until it fits.
Blocks containing arrays also use CBVs because root constants do not support
HLSL constant-buffer arrays.

Fallback writes suballocate 256-byte-aligned slices from persistently mapped
1 MiB upload pages. Each frame or auxiliary command context retains its pages
until GPU completion, then reuses them. Push updates do not create a committed
resource per write. The public `PushConstants` API and HLSL declarations remain
the same for Vulkan and D3D12.

### Remaining D3D12 API gaps

Core graphics/compute/mesh rendering, uploads, descriptors, sparse images,
resizing, and GPU timeline submissions are implemented. Full RHI parity is not
complete: timestamp queries, generic image-to-buffer copies, mip generation and
blitting, indexed indirect/count drawing, and indirect dispatch are not implemented.
The public FenceHandle API and semaphore host wait/signal methods also remain
stubs; internal frame/queue fences and GPU submission waits/signals are implemented.
Use tests/d3d12/README.md for supported paths and validation instructions.

D3D12 uses committed allocations and conservative queue handoffs. Some resource
destruction and host-mirror operations wait for GPU completion. These are current
implementation tradeoffs rather than guarantees of Vulkan-equivalent performance.
