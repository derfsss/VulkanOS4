# VulkanOS4

Vulkan 1.3 implementation for AmigaOS 4.

VulkanOS4 brings the Vulkan graphics API to PowerPC Amiga hardware.
It provides a complete Vulkan 1.3 runtime with two installable client drivers
(ICDs), a loader library, and a full SDK for developing Vulkan applications
on AmigaOS 4.

## Components

- **vulkan.library** -- Vulkan loader. Discovers and dispatches to installed
  ICDs automatically, selecting the best available driver.
- **software_vk.library** -- Software ICD. Pure CPU rendering with a SPIR-V
  bytecode interpreter. Works on any AmigaOS 4 system without GPU
  requirements. Useful for development, validation, and systems without
  supported GPUs.
- **ogles2_vk.library** -- GPU ICD. Hardware-accelerated rendering via
  ogles2.library and Warp3D Nova. Uses SPIRV-Cross to transpile SPIR-V
  shaders to GLSL ES 3.1 for GPU execution. Typically 10--300x faster than
  the software renderer.
- **SDK** -- Vulkan headers, inline macros, and link library for compiling
  Vulkan programs on AmigaOS 4.

Both ICDs implement 218 Vulkan functions (100% Vulkan 1.3 core + WSI) and
support 210+ SPIR-V opcodes.

## Requirements

### Runtime (using VulkanOS4)

- AmigaOS 4.1 Final Edition or later
- PowerPC-based Amiga (AmigaOne X5000, X1000, A1222, Sam460, or similar)
- For **software rendering**: no additional requirements
- For **GPU rendering** (ogles2_vk.library):
  - Supported Radeon GPU (RX 500/400 series or HD 7000+ series)
  - RadeonRX or RadeonHD graphics driver
  - Warp3D Nova (Warp3DNova.library)
  - ogles2.library (OpenGL ES 2.0)

### Build (compiling VulkanOS4)

- Docker
- `walkero/amigagccondocker:os4-gcc11` Docker image (GCC 11.5.0 cross-compiler)
- GNU Make
- For the GPU ICD, the following must be downloaded into `external/`:
  - [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) -- SPIR-V to
    GLSL transpiler (compiled as `libspirv-cross.a`)
  - [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
    -- Vulkan Memory Allocator (C++ header-only, requires g++)
  - OGLES2 SDK headers (`external/OGLES2/`)
  - Warp3D Nova SDK headers (`external/Warp3DNova/`)

See `external/*/README.md` for download and setup instructions for each
dependency.

## Installation

### Automated installation

The distribution includes an Autoinstall script. From the VulkanOS4 directory
on your Amiga:

```
Execute Autoinstall
```

This installs:
- `LIBS:vulkan.library` -- Vulkan loader
- `LIBS:Vulkan/software_vk.library` -- Software ICD
- `LIBS:Vulkan/ogles2_vk.library` -- GPU ICD
- `DEVS:Vulkan/icd.d/software_vk.json` -- Software ICD manifest
- `DEVS:Vulkan/icd.d/ogles2_vk.json` -- GPU ICD manifest
- `C:vulkaninfo` -- Device information tool
- `C:VulkanPrefs` -- ICD preferences tool

### Manual installation

Copy the files to the following locations:

```
copy Libs/vulkan.library              LIBS:
copy Libs/Vulkan/software_vk.library  LIBS:Vulkan/
copy Libs/Vulkan/ogles2_vk.library    LIBS:Vulkan/
copy Devs/Vulkan/icd.d/software_vk.json  DEVS:Vulkan/icd.d/
copy Devs/Vulkan/icd.d/ogles2_vk.json    DEVS:Vulkan/icd.d/
copy Tools/VulkanPrefs                C:
copy Tools/vulkaninfo                 C:
```

### SDK installation

SDK files must be copied manually to your AmigaOS SDK:

```
copy SDK/include/include_h/vulkan SDK:include/include_h/vulkan ALL CLONE
copy SDK/include/include_h/interfaces/vulkan.h SDK:include/include_h/interfaces/
copy SDK/include/include_h/inline4/vulkan.h SDK:include/include_h/inline4/
copy SDK/include/include_h/proto/vulkan.h SDK:include/include_h/proto/
copy SDK/include/include_h/clib/vulkan_protos.h SDK:include/include_h/clib/
copy SDK/newlib/lib/libvulkan_loader.a SDK:newlib/lib/
```

## Compiling Vulkan programs

Minimal compile command:

```
gcc -mcrt=newlib -D__USE_INLINE__ -o myapp myapp.c -lvulkan_loader -lauto
```

Required flags:
- `-D__USE_INLINE__` -- enables inline macros (`vkCreateInstance()` etc.)
- `-lvulkan_loader` -- links the auto-open stub for vulkan.library
- `-lauto` -- auto-opens exec.library, intuition.library, etc.

Required includes in your source code:

```c
#include <proto/exec.h>        /* always needed */
#include <proto/intuition.h>   /* if using windows/WSI */
#include <proto/vulkan.h>      /* Vulkan API */
```

A project template is provided in `Template/` with a ready-to-use Makefile
and source file.

## Building from source

All components are cross-compiled via Docker. Pull the Docker image first:

```sh
docker pull walkero/amigagccondocker:os4-gcc11
```

### Build targets

```sh
make test-build    # Verify Docker cross-compilation works
make all           # Build everything (loader + both ICDs + examples + tools)
make loader        # Build vulkan.library + libvulkan_loader.a
make icd           # Build software_vk.library
make gpu-icd       # Build ogles2_vk.library
make examples      # Build all 22 example programs
make tools         # Build vulkaninfo, VulkanPrefs, AmigaMark
make dist          # Create distribution directory in dist/VulkanOS4/
make dist-lha      # Create dist/VulkanOS4.lha archive
make check         # Validate build output (ELF header verification)
make clean         # Remove all build artifacts
```

### Build order

`make all` builds components in the correct order. To build individually,
the dependencies are:

1. `make loader` -- no dependencies
2. `make icd` -- no dependencies
3. `make gpu-icd` -- requires external dependencies (see Requirements)
4. `make examples` -- depends on loader (for libvulkan_loader.a)
5. `make tools` -- depends on loader

## Running examples

22 example programs are included, covering basic rendering through to
3D geometry, texturing, instancing, and glTF model loading. Run them from
the distribution directory on your Amiga:

```
Examples/01_enumerate/01_enumerate
Examples/08_triangle/08_triangle
Examples/09_rotating/09_rotating
Examples/12_wireframe_cube/12_wireframe_cube
Examples/20_torus/20_torus
```

### Example list

| # | Example | Description |
|---|---------|-------------|
| 01 | `01_enumerate` | Device and extension enumeration |
| 02 | `02_clear` | Screen clear with solid colour |
| 03 | `03_gradient` | Gradient via fragment shader |
| 04 | `04_checkerboard` | Procedural checkerboard pattern |
| 05 | `05_plasma` | Animated plasma effect |
| 06 | `06_rings` | Concentric ring pattern |
| 07 | `07_waves` | Animated wave distortion |
| 08 | `08_triangle` | Classic RGB triangle |
| 09 | `09_rotating` | Rotating triangle with push constants |
| 10 | `10_depth` | Depth buffer testing |
| 11 | `11_textured` | Textured quad with sampler |
| 12 | `12_wireframe_cube` | 3D wireframe cube with perspective |
| 13 | `13_solid_cube` | Solid cube with face colouring and lighting |
| 14 | `14_instanced_cubes` | Instanced rendering (3x3 grid) |
| 15 | `15_render_pass` | Legacy render pass API usage |
| 16 | `16_transfer_ops` | Buffer and image transfer commands |
| 17 | `17_events_sync` | Fences, semaphores, and events |
| 18 | `18_query_demo` | Occlusion and timestamp queries |
| 19 | `19_indirect_draw` | Indirect draw commands |
| 20 | `20_torus` | Textured torus with starfield background |
| 21 | `21_image_texture` | Image file loading via stb_image |
| 22 | `22_gltf_viewer` | glTF 2.0 model viewer via cgltf |
| 24 | `24_proc_addr` | `vkGetDeviceProcAddr` ABI regression test — verifies raw `PFN_vk*` resolution works across queue/memory/command/sync/WSI categories |

Examples 21 and 22 require optional third-party headers (stb_image.h and
cgltf.h respectively). They include procedural fallbacks and will still
compile and run without the external headers.

## Tools

### vulkaninfo

Displays Vulkan device information, supported features, memory properties,
and queue families.

```
vulkaninfo
```

### VulkanPrefs

Manages ICD priority and enable/disable state. Reads and writes
`ENVARC:Vulkan/vulkan.prefs`.

```
VulkanPrefs               ; Show current ICD configuration
VulkanPrefs -list          ; List discovered ICDs
VulkanPrefs -disable sw    ; Disable software ICD
VulkanPrefs -enable sw     ; Re-enable software ICD
VulkanPrefs -priority gpu 1 ; Set GPU ICD as highest priority
```

### AmigaMark

Vulkan benchmark tool. Measures rendering performance with timed FPS
reporting.

```
AmigaMark
```

## Third-party software

VulkanOS4 uses the following third-party libraries and resources:

| Library | Version | Author | License | Usage |
|---------|---------|--------|---------|-------|
| [cglm](https://github.com/recp/cglm) | 0.9.6 | recp | MIT | API design inspiration for vk_math.h |
| [stb_image](https://github.com/nothings/stb) | 2.30 | Sean Barrett | Public domain / MIT | Optional image loading in examples |
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.15 | Johannes Kuhlmann | MIT | Optional glTF 2.0 model loading |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 3.2.0 | AMD GPUOpen | MIT | GPU/host memory suballocation |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | latest | Khronos Group | Apache 2.0 | SPIR-V to GLSL ES 3.1 transpiler |

OGLES2 and Warp3D Nova SDK headers are proprietary (A-EON Technology) and must be obtained separately.

See [THIRD_PARTY.md](THIRD_PARTY.md) for full attribution details.

## Changelog

### 1.3 -- 2026-05-11

ABI and compatibility fixes; build hygiene. Many of the underlying
issues were diagnosed by Andrea Palmate' ([afxgroup](https://github.com/afxgroup))
in [PR #1](https://github.com/derfsss/VulkanOS4/pull/1).

ICD changes:
- Fix `vkGetDeviceProcAddr` ABI: the GPU ICD's `LookupRawProcAddr`
  previously fell back to APICALL trampolines for unlisted names,
  which slid every argument by one register slot when called as raw
  `PFN_vk*`. Expanded the `RAW` table to mirror the full `DISPATCH`
  table (~210 entries) and replaced the fallback with `return NULL`.
  Also extended the SW ICD's `RAW` table with the four surface queries
  and five swapchain entry points it was missing.
- Detect SPIR-V endianness from the magic word in the SW ICD, rather
  than unconditionally byte-swapping. Per the Vulkan spec, the magic
  word may be supplied in either endianness. The previous behaviour
  was wrong for callers that already produce host-byte-order SPIR-V
  (e.g. `uint32_t` array literals compiled on a big-endian host such
  as ImGui's `__glsl_shader_*_spv` arrays on PowerPC).
- GPU ICD: force `FORCE_FLATTENED_IO_BLOCKS` in SPIRV-Cross and rename
  vertex/fragment interface-block instances to a common `vary` so
  Warp3D Nova's name-based GLSL ES linker can match varyings across
  stages.
- GPU ICD: add `VK_FORMAT_R8G8B8A8_UNORM` (VkFormat 37) to the vertex
  attribute mapping with `GL_TRUE` for the normalised flag. ImGui
  packs vertex colour in this format.
- GPU ICD: set non-zero defaults for `bufferImageGranularity`,
  `nonCoherentAtomSize`, the various `min*OffsetAlignment` and
  `max*Range` limits. VMA divides by some of these during init and
  on every allocation; zeroes caused `vmaCreateImage` to bail out
  with `VK_ERROR_OUT_OF_DEVICE_MEMORY` before calling
  `vkAllocateMemory`.
- Add a `D(...)` debug-print macro to both ICDs (no-op when `DEBUG`
  is undefined) and convert the existing `IExec->DebugPrintF` call
  sites. Lets release builds skip the trace overhead.

Build / loader:
- Wire `$(DEBUG)` into the Docker build recipes for the loader and
  both ICDs (previously the variable existed but never reached the
  compiler).
- Refactor `software_icd/Makefile.cross` to use pattern rules instead
  of a single 80-line `&&`-chained shell pipeline; supports
  incremental builds and `make -j`.
- Replace `strncpy` + manual null-terminate with `snprintf` in the
  loader ICD path tracking to silence a `-Wstringop-truncation`
  warning.

### 1.0.0 -- 2026-03-20

Initial release.

- Vulkan 1.3 loader (`vulkan.library`) with automatic ICD discovery and
  dispatch
- Software ICD (`software_vk.library`) with SPIR-V bytecode interpreter,
  triangle rasteriser with depth testing, and Bresenham line rasteriser
- GPU ICD (`ogles2_vk.library`) with OGLES2/Warp3D Nova backend and
  SPIRV-Cross transpiler for SPIR-V to GLSL ES 3.1
- 218 Vulkan functions per ICD (100% Vulkan 1.3 core + WSI coverage)
- 210+ SPIR-V opcodes including vertex/fragment shader operations,
  matrix math, texture sampling, control flow, and type conversions
- 22 example programs from basic triangle to glTF model viewer
- SDK headers with AmigaOS interface definitions, inline macros, and
  link library
- VulkanPrefs CLI tool for ICD priority management
- vulkaninfo device enumeration tool
- AmigaMark benchmark tool
- Autoinstall script for AmigaOS 4
- Optimised software rasteriser with incremental edge functions, active
  varying mask, and shader state allocation pooling
- GPU ICD optimisations including VBO caching and batched resource cleanup

## License

See individual source files for license information.
