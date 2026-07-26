# Velos RHI tests

The executable runs the same backend-neutral RHI checks against a selected
backend and overwrites `last-run.html` after every run.

```powershell
.\tests\run-tests.ps1 -Backend vulkan -Configuration Debug
```

The suite currently covers device creation and identity, typed handles,
CPU-visible and GPU-only buffers, device addresses, 2D and cube images/views,
samplers, uniform and storage descriptor bindings, command
recording/submission, buffer copies and updates, mip/handle validation, and
repeated resource lifecycle stress. It also covers mapped GPU-to-CPU buffers
and an exact RGBA8 image readback path: clear an image, copy it into a readback
buffer, map it, and compare every pixel byte-for-byte.

The pixel suite currently verifies a solid render-target clear, an uploaded
checkerboard round-trip, four render-area clears forming an exact quadrant
pattern, readback from a selected mip and array layer, padded-row and
subrectangle transfers, upload-context round trips, explicit mip blits, and
generated mip chains. Mismatches report the first failing pixel coordinate,
channel, expected byte, and actual byte.

Shader pixel tests compile GLSL to SPIR-V at runtime and verify a procedural
`gl_FragCoord` image plus reflected fragment push constants through real
graphics pipelines and fullscreen draws. A compute case also binds and writes
an RGBA8 storage image through a descriptor before exact readback.

Additional shader cases cover uniform and storage-buffer descriptors,
compute-stage push constants, explicit vertex attributes, 16-bit and 32-bit
indexed drawing, normalized byte attributes, depth testing/writes,
triangle-strip topology, per-fragment discard, and nearest-filtered 2D texture
sampling into a storage image. Pixel-checked raster coverage also includes
scissor clipping, additive blending, and reverse-subtract blending.

Unsupported or broken advertised paths remain ordinary failing tests. The
report therefore doubles as a live implementation-gap list instead of treating
rejected functionality as a successful result.

See `REFERENCE_COVERAGE.md` for the detailed coverage map and the RHI
functionality still required for additional test categories.

To add DX12, add its enum value to `GraphicsAPI`, implement `CreateDevice`, then
add `{ "dx12", GraphicsAPI::D3D12 }` to `Backends()` in `main.cpp`. The existing
test cases will then run against it with `--backend dx12`.

`tests/premake5.lua` is intentionally self-contained because the repository's
root Premake file currently contains unresolved merge markers.
