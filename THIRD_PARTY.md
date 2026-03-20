# Third-party Software

VulkanOS4 uses the following third-party libraries and resources.
Header-only libraries are placed in the `external/` directory.

## cglm -- OpenGL Mathematics for C

- **Version:** 0.9.6
- **Author:** recp
- **License:** MIT
- **URL:** https://github.com/recp/cglm
- **Usage:** API design inspiration for `examples/common/vk_math.h`.
  The actual implementation is original code. cglm itself is not
  directly included due to its multi-header architecture.

## stb_image.h -- Image Loader

- **Version:** 2.30
- **Author:** Sean Barrett
- **License:** Public domain / MIT (dual-licensed)
- **URL:** https://github.com/nothings/stb
- **Usage:** Optional image loading in examples (PNG, JPG, BMP, TGA).
  Must be downloaded separately into `external/stb/`.

## cgltf -- glTF 2.0 Loader

- **Version:** 1.15
- **Author:** Johannes Kuhlmann (jkuhlmann)
- **License:** MIT
- **URL:** https://github.com/jkuhlmann/cgltf
- **Usage:** Optional glTF 2.0 model loading in examples.
  Must be downloaded separately into `external/cgltf/`.

## VMA -- Vulkan Memory Allocator

- **Version:** 3.2.0
- **Author:** AMD GPUOpen
- **License:** MIT
- **URL:** https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- **Usage:** GPU and host memory suballocation for Vulkan resources.
  C++ single-header library, compiled with ppc-amigaos-g++ (C++14).
  Must be downloaded separately into `external/vma/`.
  Requires `loader/gthread_stubs.c` and `-latomic` at link time.

## SPIRV-Cross -- SPIR-V Shader Reflection and Cross-Compilation

- **Version:** latest main branch
- **Author:** Khronos Group / Hans-Kristian Arntzen
- **License:** Apache 2.0
- **URL:** https://github.com/KhronosGroup/SPIRV-Cross
- **Usage:** GPU ICD (`ogles2_vk.library`) SPIR-V to GLSL ES 3.1 transpiler.
  Compiled as a static library (`libspirv-cross.a`) using the C API
  (`spirv_cross_c.h`). Replaces the hand-rolled transpiler with full
  SPIR-V spec coverage. Must be downloaded separately into
  `external/spirv-cross/`.

## OGLES2 SDK Headers

- **Source:** AmigaOS 4 SDK (ogles2.library)
- **License:** Proprietary (A-EON Technology)
- **Usage:** GPU ICD backend interface headers in `external/OGLES2/`.

## Warp3D Nova SDK Headers

- **Source:** AmigaOS 4 SDK (Warp3DNova.library)
- **License:** Proprietary (A-EON Technology)
- **Usage:** GPU hardware access headers in `external/Warp3DNova/`.
