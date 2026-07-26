# Reference test-suite coverage

This maps the test categories in `AmelieHeinrich/agfx/src/agfx/agfx_tests/tests`
to the Velos RHI as of this branch.

## Covered by executable Velos tests

- Buffer creation, initial data, device address, update, and buffer-to-buffer copy
- Texture and texture-view handle creation
- 2D, 2D-array, cube, mip, and array-slice resources
- Buffer/image copies with non-zero offsets, padded rows, mip selection, and
  array-slice selection
- Upload-context buffer and image transfers with byte-for-byte readback
- Sampler creation, filter selection, and repeat/clamp address modes
- Uniform-buffer, storage-buffer, sampled-texture, and storage-texture bindings
- Command recording/submission, viewport, scissor, and explicit barriers
- Render-pass clear of a mip or array slice
- Vertex-input, normalized byte attributes, 16/32-bit indexed drawing,
  triangle strips, and depth test/write behavior with pixel readback
- Fragment discard preserving the existing render-target contents
- Nearest-filtered 2D texture sampling through a combined image sampler
- Scissor clipping and add/reverse-subtract blend operations
- Invalid handle/mip validation and repeated resource lifecycle

The suite now includes byte-for-byte RGBA8 pixel tests using the backend-neutral
image-to-buffer copy and mapped GPU-to-CPU buffer APIs. They cover a solid
render-target clear, an uploaded checkerboard, render-area quadrant clears, and
readback from a selected mip and array layer.

Shader-driven coverage includes runtime GLSL compilation, SPIR-V reflection,
graphics pipeline creation, a vertex-index fullscreen triangle, procedural
fragment output, fragment-stage push constants, compute dispatch, descriptor
binding, uniform/storage-buffer shader access, and storage-image writes.

The following advertised paths have executable tests that currently fail:

- direct `BindUniformBuffer`
- compute-stage push constants
- command-list compute-pipeline state reset between recordings
- generated mip-chain intermediate layout transitions
- cube-array images/views
- multiple render targets
- multiple simultaneous swapchains

## Missing functionality blocking direct AGFX equivalents

- **Golden-image tooling:** image-to-buffer copy and mapped readback now exist,
  but PNG output, golden image storage, and an image-difference metric are not
  implemented yet.
- **Texture copies:** no image-to-image command.
- **3D textures:** `ImageType` only exposes `Image2D` and `Cube`.
- **Cube arrays:** `ImageViewType::CubeArray` exists, but cube image creation
  requires exactly six layers and the Vulkan view conversion rejects the type.
- **Multiple render targets:** Vulkan `BeginRendering` explicitly rejects more
  than one color attachment.
- **Multiple swapchains:** Vulkan device state stores a single swapchain and
  rejects creation of a second one.
- **Indirect execution:** no draw-indirect, draw-indexed-indirect,
  multi-draw-indirect, or dispatch-indirect commands.
- **Primitive/raster state:** no point-list topology, front-only culling, depth
  clamp, or programmable blend constants. Wireframe is exposed but still needs
  a pixel-checked test and feature validation.
- **Sampler features:** no mirrored repeat, comparison samplers, border color, or
  mip-filter selection.
- **Depth sampling/readback:** depth test/write behavior is covered through
  color output, but there is no complete sampled-depth or raw-depth comparison
  path.
- **Advanced compute verification:** storage-image and storage-buffer dispatches
  are covered, but atomics, shared memory, wave ops, and multi-dispatch result
  checks still need dedicated shader cases.
- **Multiple queues:** no distinct copy/compute/render queues, queue ownership
  transfer, semaphores, or queue-chain submission API.
- **Mesh/task shaders:** shader stages and draw commands are absent.
- **Ray tracing:** no BLAS/TLAS, acceleration-structure build/copy/update,
  ray-query/trace commands, or device addresses for acceleration structures.
- **UAV barrier semantics:** generic resource-state barriers exist, but there is
  no backend-neutral explicit UAV/global memory barrier abstraction.
- **Buffer views:** there are descriptor bindings, but no dedicated raw,
  structured, constant, or bindless buffer-view objects/handles.
