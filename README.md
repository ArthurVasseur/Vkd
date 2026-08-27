# Vkd

[![Build and test](https://github.com/ArthurVasseur/Vkd/actions/workflows/build.yml/badge.svg)](https://github.com/ArthurVasseur/Vkd/actions/workflows/build.yml)
[![Language](https://img.shields.io/badge/language-C%2B%2B20-blue)](https://github.com/ArthurVasseur/Vkd)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

**Vkd** is an experimental [Vulkan](https://www.khronos.org/vulkan/) Installable Client Driver (ICD) written in C++20. Rasterization, shader execution, and device memory all run on the CPU. The Vulkan loader loads the shared library `vkd-Software`. Shaders go through See, a SPIR-V interpreter; drawing goes through `CpuContext`.

It is a learning and prototyping platform for driver internals (object lifetime, ICD negotiation, command recording, memory allocation, and shader execution), not a production renderer.

Vulkan 1.0 only, no instance or device extensions. Many entry points still exist as warn-and-no-op stubs so the loader and CTS do not crash on missing function pointers.

## What works today

The bundled Hello Triangle (`vkd-test-app`) renders a real RGB triangle to `triangle.ppm`. That path exercises:

- ICD handshake with the Vulkan loader (interface version 7)
- Instance, physical device, logical device, and queue creation
- Images, image views, device memory, and host mapping
- Render passes, framebuffers, and `vkCmdClearColorImage`
- Graphics pipeline state (viewport, scissor, cull, front face, color blend, shaders)
- Command pool / command buffer record and `vkQueueSubmit`
- Uniform buffers through descriptor sets
- Combined image samplers (nearest, 2D, RGBA8)
- Vertex and fragment shader execution through [See](#see-shader-execution-engine)
- Triangle-list rasterization with perspective-correct interpolation

```
xmake
xmake run vkd-test-app
```

## Architecture

Vkd is split so a future hardware backend can reuse the common Vulkan object layer:

```
+---------------------------------------------------------+
|                   Vulkan application                    |
+--------------------------+------------------------------+
                           |
                      Vulkan loader
                  vk_icdGetInstanceProcAddr
                           |
+--------------------------v------------------------------+
|  Vkd                                                    |
|  ICD entry points, Vulkan objects, command recording    |
+--------------------------+------------------------------+
                           |
+--------------------------v------------------------------+
|  VkdSoftware                                            |
|  CPU device, command dispatch, rasterizer (CpuContext)  |
+--------------+-----------------------+------------------+
               |                       |
       +-------v--------+      +-------v--------+
       |  See           |      |  VkdUtils      |
       |  SPIR-V to IR  |      |  TLSF allocator|
       |  scalar exec   |      |  system utils  |
       +----------------+      +----------------+
```

| Layer | Target | Role |
| --- | --- | --- |
| **Vkd** | `vkd` (static) | Shared ICD and Vulkan objects (`VkInstance`, `VkDevice`, buffers, images, pipelines, descriptors, command buffers, ...) |
| **VkdSoftware** | `vkd-Software` (shared) | CPU backend: physical device, queue submit, `CpuContext` rasterizer, command dispatcher |
| **See** | `see` (static) | Shader Execution Engine: SPIR-V parse, SSA IR, scalar interpreter |
| **VkdUtils** | `vkd-Utils` (static) | TLSF device-memory allocator and platform helpers |

Host allocations go through [mimalloc](https://github.com/microsoft/mimalloc) (or the application's `VkAllocationCallbacks`). Device-visible memory is managed by a Two-Level Segregated Fit (TLSF) allocator with O(1) alloc/free.

The software physical device reports as `VK_PHYSICAL_DEVICE_TYPE_CPU`, named `Vkd software device`, with three queue families (graphics+compute+transfer, graphics-only, transfer-only).

## See: Shader Execution Engine

See is a C++ SPIR-V interpreter used by the software backend. Shaders are not compiled to native code; they are validated, lowered, and executed one invocation at a time.

1. **Parse**: [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) decodes the binary; See builds a CFG from `OpFunction` / `OpLabel` / terminators.
2. **IR**: ids stay SSA names. Types cover scalars, vectors, matrices, pointers, structs, images, and sampled images.
3. **Execute**: a scalar interpreter runs the IR. Vertex and fragment wrappers inject `VertexIndex` / `InstanceIndex` / `Position` and `Location(N)` varyings.
4. **Memory**: uniform / storage buffers and sampled images are bound as external views over mapped `VkDeviceMemory`.

Supported today: arithmetic, compares, select, composites, struct access, uniform buffers, and nearest 2D RGBA8 `OpImageSample*Lod`. Arrays, `OpConstantComposite` initializers, mip/LOD, and a SIMD (warp) backend are not implemented yet.

## Status

| Area | State | Notes |
| --- | --- | --- |
| ICD / loader | Done | Negotiate, `GetInstanceProcAddr`, Windows adapter enumeration |
| Instance / device / queues | Done | Software CPU device, submit + `WaitIdle` |
| Images / views / memory | Partial | Linear host-visible memory; mip and array sizes counted; no tiling model |
| Render pass / framebuffer | Partial | Color attachments, `CLEAR` / `LOAD`; no depth/stencil |
| Graphics pipeline | Partial | State captured; triangle lists; one viewport/scissor |
| Rasterizer | Partial | Edge-function fill, cull, perspective-correct varyings; no depth test |
| Shaders (See) | Partial | Scalar SPIR-V interpreter; UBO + RGBA8 nearest sampling |
| Descriptors / samplers | Partial | Buffer and combined-image-sampler writes; no copies, no dynamic offsets |
| Transfers | Partial | Buffer fill/copy/update, image copy, buffer <-> image, color clear |
| Compute | Stub | `vkCmdDispatch` logs a warning |
| Indexed / indirect draw | Stub | Recorded as ops, not executed |
| Depth / stencil | Missing | |
| Validation layers | Missing | Unknown entry points return a trap trampoline instead of `nullptr` |

Unknown `vkGetDeviceProcAddr` names return a per-name trampoline that logs and returns, so CTS and apps fail the test instead of jumping to `0x0`.

`vkCmdPipelineBarrier` is a deliberate no-op: the software backend stores a linear blob and has no image layouts to transition.

## Building

Vkd uses [xmake](https://xmake.io).

### Prerequisites

- A C++20 compiler (MSVC, Clang, or GCC)
- [xmake](https://xmake.io/#/getting_started)
- Network access for packages (`vulkan-headers`, SPIRV-Tools, Catch2, NZSL, ...)
- Git, with submodule support

### Configure and build

```bash
git clone --recursive https://github.com/ArthurVasseur/Vkd.git
cd Vkd

# Debug (debug checks on by default in this mode)
xmake config -m debug

# Or a faster build with symbols
xmake config -m releasedbg

xmake
```

If the clone was not recursive:

```bash
git submodule update --init --recursive
```

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--debug-checks=y` | on in debug | Extra asserts and `cpptrace` stack traces (`VKD_DEBUG_CHECK`) |
| `--profiling=y` | off | Tracy profiler (`VKD_PROFILING`) |
| `--tests=y` | on | Catch2 unit tests and the Hello Triangle app |
| `--cts=y` | off | Khronos Vulkan CTS runner (`vk-cts-Software`) |
| `--installer=y` | off | NSIS / zip / tar packages via xpack |

### Tests and demo

```bash
xmake run vkd-tests       # Catch2: allocator + See parser/IR/executor
xmake run vkd-test-app    # Hello Triangle writes triangle.ppm
```

CI builds Windows (x86, x64, ARM64), Linux (x86_64, including ASan/LSan/TSan), and macOS (ARM64).

Visual Studio project files can be generated with `xmake project -k vsxmake`.

## Using the ICD

A successful build writes `vkd-Software.json` next to the project, with `library_path` pointing at the built shared library (`vkd-Software.dll` / `libvkd-Software.so` / `libvkd-Software.dylib`). Point the loader at that manifest:

```bash
# Linux / macOS
export VK_DRIVER_FILES="$PWD/vkd-Software.json"
export VK_LOADER_LAYERS_DISABLE="~implicit~"

# Windows (PowerShell)
$env:VK_DRIVER_FILES = "$PWD\vkd-Software.json"
$env:VK_LOADER_LAYERS_DISABLE = "~implicit~"
```

`vulkaninfo` should then list **Vkd software device**. Disable implicit layers (`VK_LOADER_LAYERS_DISABLE=~implicit~`) while developing; validation layers will disagree with this driver.

On Windows the NSIS installer (`xmake config --installer=y` then `xmake pack`) can register the manifest under `HKLM\Software\Khronos\Vulkan\Drivers`.

`vkd-test-app` does not need `VK_DRIVER_FILES`: it loads `vkd-Software` directly through `VK_LUNARG_direct_driver_loading`.

## Vulkan CTS

CTS is optional and not expected to pass. With `--cts=y`, xmake fetches a prebuilt `deqp-vk` and runs the `vk-default.txt` caselist against the software ICD:

```bash
xmake config -m releasedbg --cts=y --debug-checks=y
xmake build vkd-Software
xmake run vk-cts-Software
```

Results land in `Software-cts-results/` (log, XML, HTML). The GitHub Actions workflow `cts.yml` runs this on Windows x64.

## Layout

```
Src/
  Vkd/            Common ICD and Vulkan objects
  VkdSoftware/    CPU backend (rasterizer, dispatch, ICD exports)
  See/            SPIR-V parser, IR, scalar executor
  VkdUtils/       TLSF allocator and system helpers
  Tests/          Catch2 unit tests
  TestApp/        Hello Triangle (NZSL to SPIR-V)
xmake/            CTS runner and installer packaging
```

Each Vulkan object lives in its own folder (`Device/`, `Image/`, `Pipeline/`, ...), with a software specialization under `VkdSoftware/` when the CPU backend needs extra state.

## Design goals

- A readable, from-scratch software Vulkan implementation
- A common object layer that a hardware backend could plug into
- A SPIR-V interpreter (See) that can grow a SIMD backend without changing the ICD
- Driver-level debugging: object tracking, explicit command ops, warn-on-stub entry points

## Contributing

The project is early and incomplete. Issues and pull requests are welcome, especially around shader coverage, rasterizer correctness, and CTS failures.

## License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2025-2026 Arthur Vasseur
