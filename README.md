# velos

velos is a modern Render Hardware Interface (RHI) with a Vulkan backend, designed to provide a clean and minimal abstraction over GPU APIs for real-time rendering.

It serves as a foundation for building rendering engines and graphics systems, such as the Rodan renderer.

---

## Overview

velos abstracts core GPU concepts into a unified interface while preserving explicit control over rendering operations.

It provides:

- Device and swapchain management
- Command list recording and submission
- Buffer and image abstractions
- Pipeline and shader management
- Resource binding (descriptors)
- Synchronization primitives

velos is **not** a rendering engine. It does not implement:

- Scene systems
- Materials or lighting models
- Asset loading
- Rendering techniques (PBR, shadows, etc.)

These are intentionally left to higher-level systems built on top of the RHI.

---

## Design Goals

- **API abstraction without hiding fundamentals**  
  Expose GPU concepts clearly without leaking backend-specific details

- **Explicit control over rendering**  
  No hidden work or implicit synchronization

- **Backend independence**  
  Vulkan is the first backend, with future support for APIs like DirectX 12

- **Minimal but complete**  
  Provide only what is necessary to build a renderer

---

## Architecture

velos is structured into a platform-independent RHI layer and backend implementations.

[Application / Engine]

↓

[velos RHI Interface]

↓

[Backend (Vulkan)]

↓

[GPU]

---

## Core Components
- Device
  - Entry point for all GPU operations
- Swapchain
  - Presentation and backbuffer management
- Command List
  - Recording GPU commands
- Buffers
  - Vertex, index, uniform, and storage buffers
- Images
  - Textures, render targets, depth buffers
- Pipelines
  - Graphics and compute pipeline state
- Resource Bindings
  - Descriptor sets / binding layouts
- Synchronization
  - Fences, semaphores, frame management

---

## Current Status

Early development.

---

## Implemented / in progress:

- Vulkan backend
- Device and swapchain abstraction
- Command list recording
- Pipeline creation
- Buffer support
- Shader compilation and reflection

- ---

## Planned:

- Image/texture system
- Descriptor abstraction improvements
- Resource state tracking
- Better synchronization model
- Multi-backend support
- Example Usage

---

velos includes example applications demonstrating basic functionality:

- Triangle rendering
- Indexed geometry (cube)
- (Planned) Textured rendering

These examples are intended to validate the RHI and serve as a reference for higher-level systems.

---

## Building

velos uses Premake for project generation.

**Setup**

```bash
git clone https://github.com/LipskiDev/velos
cd velos
```

**Generate build files**

```bash
premake5 gmake
make
```
