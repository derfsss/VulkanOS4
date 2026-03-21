VulkanOS4
=========

Vulkan 1.3 implementation for AmigaOS 4.

VulkanOS4 brings the Vulkan graphics API to PowerPC Amiga hardware.
It provides a complete Vulkan 1.3 runtime with two installable client
drivers (ICDs), a loader library, and a full SDK for developing Vulkan
applications on AmigaOS 4.


Components
----------

  vulkan.library       Vulkan loader. Discovers and dispatches to
                       installed ICDs automatically, selecting the best
                       available driver.

  software_vk.library  Software ICD. Pure CPU rendering with a SPIR-V
                       bytecode interpreter. Works on any AmigaOS 4
                       system without GPU requirements. Useful for
                       development, validation, and systems without
                       supported GPUs.

  ogles2_vk.library    GPU ICD. Hardware-accelerated rendering via
                       ogles2.library and Warp3D Nova. Uses SPIRV-Cross
                       to transpile SPIR-V shaders to GLSL ES 3.1 for
                       GPU execution. Typically 10-300x faster than
                       the software renderer.

  SDK                  Vulkan headers, inline macros, and link library
                       for compiling Vulkan programs on AmigaOS 4.

Both ICDs implement 218 Vulkan functions (100% Vulkan 1.3 core + WSI)
and support 210+ SPIR-V opcodes.


Requirements
------------

Runtime (using VulkanOS4):

  - AmigaOS 4.1 Final Edition or later
  - PowerPC-based Amiga (AmigaOne X5000, X1000, A1222, Sam460, etc.)
  - For software rendering: no additional requirements
  - For GPU rendering (ogles2_vk.library):
    - Supported Radeon GPU (RX 500/400 series or HD 7000+ series)
    - RadeonRX or RadeonHD graphics driver (by Hans de Ruiter)
    - Warp3D Nova (Warp3DNova.library by Hans de Ruiter)
    - ogles2.library (OpenGL ES 2.0 by Daniel Muessener)

Build (compiling VulkanOS4 from source):

  - Docker
  - walkero/amigagccondocker:os4-gcc11 image (GCC 11.5.0 cross-compiler)
  - GNU Make
  - For the GPU ICD, the following must be downloaded into external/:
    - SPIRV-Cross (https://github.com/KhronosGroup/SPIRV-Cross)
    - VMA (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
    - OGLES2 SDK headers
    - Warp3D Nova SDK headers


Installation
------------

Automated installation:

  The distribution includes an Autoinstall script. From the VulkanOS4
  directory on your Amiga:

    Execute Autoinstall

  This installs:

    LIBS:vulkan.library                  Vulkan loader
    LIBS:Vulkan/software_vk.library      Software ICD
    LIBS:Vulkan/ogles2_vk.library        GPU ICD
    DEVS:Vulkan/icd.d/software_vk.json   Software ICD manifest
    DEVS:Vulkan/icd.d/ogles2_vk.json     GPU ICD manifest
    C:vulkaninfo                         Device information tool
    C:VulkanPrefs                        ICD preferences tool

Manual installation:

    copy Libs/vulkan.library              LIBS:
    copy Libs/Vulkan/software_vk.library  LIBS:Vulkan/
    copy Libs/Vulkan/ogles2_vk.library    LIBS:Vulkan/
    copy Devs/Vulkan/icd.d/software_vk.json  DEVS:Vulkan/icd.d/
    copy Devs/Vulkan/icd.d/ogles2_vk.json    DEVS:Vulkan/icd.d/
    copy Tools/VulkanPrefs                C:
    copy Tools/vulkaninfo                 C:


SDK Installation
----------------

SDK files must be copied manually to your AmigaOS SDK:

    copy SDK/include/include_h/vulkan SDK:include/include_h/vulkan ALL CLONE
    copy SDK/include/include_h/interfaces/vulkan.h SDK:include/include_h/interfaces/
    copy SDK/include/include_h/inline4/vulkan.h SDK:include/include_h/inline4/
    copy SDK/include/include_h/proto/vulkan.h SDK:include/include_h/proto/
    copy SDK/include/include_h/clib/vulkan_protos.h SDK:include/include_h/clib/
    copy SDK/newlib/lib/libvulkan_loader.a SDK:newlib/lib/


Compiling Vulkan Programs
-------------------------

Minimal compile command:

    gcc -mcrt=newlib -D__USE_INLINE__ -o myapp myapp.c -lvulkan_loader -lauto

Required flags:

    -D__USE_INLINE__     Enables inline macros (vkCreateInstance() etc.)
    -lvulkan_loader      Links the auto-open stub for vulkan.library
    -lauto               Auto-opens exec.library, intuition.library, etc.

Required includes in your source code:

    #include <proto/exec.h>        /* always needed */
    #include <proto/intuition.h>   /* if using windows/WSI */
    #include <proto/vulkan.h>      /* Vulkan API */

A project template is provided in Template/ with a ready-to-use Makefile
and source file.


Building from Source
--------------------

All components are cross-compiled via Docker. Pull the Docker image:

    docker pull walkero/amigagccondocker:os4-gcc11

Build targets:

    make test-build     Verify Docker cross-compilation works
    make all            Build everything (loader + ICDs + examples + tools)
    make loader         Build vulkan.library + libvulkan_loader.a
    make icd            Build software_vk.library
    make gpu-icd        Build ogles2_vk.library
    make examples       Build all 23 example programs
    make tools          Build vulkaninfo, VulkanPrefs, AmigaMark
    make dist           Create distribution directory
    make dist-lha       Create VulkanOS4.lha archive
    make check          Validate build output (ELF header verification)
    make clean          Remove all build artifacts

Build order:

    make all builds components in the correct order. To build
    individually, the dependencies are:

    1. make loader     no dependencies
    2. make icd        no dependencies
    3. make gpu-icd    requires external dependencies (see Requirements)
    4. make examples   depends on loader (for libvulkan_loader.a)
    5. make tools      depends on loader


Running Examples
----------------

23 example programs are included, covering basic rendering through to
3D geometry, texturing, instancing, and glTF model loading. Run them
from the distribution directory on your Amiga:

    Examples/01_enumerate/01_enumerate
    Examples/08_triangle/08_triangle
    Examples/09_rotating/09_rotating
    Examples/12_wireframe_cube/12_wireframe_cube
    Examples/20_torus/20_torus

Example list:

    01_enumerate          Device and extension enumeration
    02_clear              Screen clear with solid colour
    03_gradient           Gradient via fragment shader
    04_checkerboard       Procedural checkerboard pattern
    05_plasma             Animated plasma effect
    06_rings              Concentric ring pattern
    07_waves              Animated wave distortion
    08_triangle           Classic RGB triangle
    09_rotating           Rotating triangle with push constants
    10_depth              Depth buffer testing
    11_textured           Textured quad with sampler
    12_wireframe_cube     3D wireframe cube with perspective
    13_solid_cube         Solid cube with face colouring and lighting
    14_instanced_cubes    Instanced rendering (3x3 grid)
    15_render_pass        Legacy render pass API usage
    16_transfer_ops       Buffer and image transfer commands
    17_events_sync        Fences, semaphores, and events
    18_query_demo         Occlusion and timestamp queries
    19_indirect_draw      Indirect draw commands
    20_torus              Textured torus with starfield background
    21_image_texture      Image file loading via stb_image
    22_gltf_viewer        glTF 2.0 model viewer via cgltf
    23_cow3d              Textured rotating 3D cow (Alain Thellier's Cow3D)

Examples 21 and 22 require optional third-party headers (stb_image.h
and cgltf.h respectively). They include procedural fallbacks and will
still compile and run without the external headers.


Tools
-----

vulkaninfo

    Displays Vulkan device information, supported features, memory
    properties, and queue families.

        vulkaninfo

VulkanPrefs

    Manages ICD priority and enable/disable state. Reads and writes
    ENVARC:Vulkan/vulkan.prefs.

        VulkanPrefs                Show current ICD configuration
        VulkanPrefs -list          List discovered ICDs
        VulkanPrefs -disable sw    Disable software ICD
        VulkanPrefs -enable sw     Re-enable software ICD
        VulkanPrefs -priority gpu 1  Set GPU ICD as highest priority

AmigaMark

    Vulkan benchmark tool. Measures rendering performance with timed
    FPS reporting.

        AmigaMark


Third-party Software
--------------------

VulkanOS4 uses the following third-party libraries and resources:

    cglm 0.9.6
        Author:  recp
        License: MIT
        URL:     https://github.com/recp/cglm
        Usage:   API design inspiration for vk_math.h

    stb_image 2.30
        Author:  Sean Barrett
        License: Public domain / MIT
        URL:     https://github.com/nothings/stb
        Usage:   Optional image loading in examples

    cgltf 1.15
        Author:  Johannes Kuhlmann
        License: MIT
        URL:     https://github.com/jkuhlmann/cgltf
        Usage:   Optional glTF 2.0 model loading

    VMA 3.2.0
        Author:  AMD GPUOpen
        License: MIT
        URL:     https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        Usage:   GPU/host memory suballocation

    SPIRV-Cross
        Author:  Khronos Group
        License: Apache 2.0
        URL:     https://github.com/KhronosGroup/SPIRV-Cross
        Usage:   SPIR-V to GLSL ES 3.1 transpiler

    Cow3D
        Author:  Alain Thellier
        License: Public domain
        Usage:   3D cow model data for example 23_cow3d

ogles2.library (OpenGL ES 2.0) by Daniel Muessener, Warp3DNova.library
by Hans de Ruiter, and RadeonRX/RadeonHD graphics drivers by Hans de
Ruiter are runtime dependencies for GPU rendering. OGLES2 and Warp3D
Nova SDK headers are proprietary (A-EON Technology) and must be obtained
separately.


Changelog
---------

1.1.0 -- 2026-03-21

    SPIRV-Cross GPU ICD fixes and new example.

    - SPIRV-Cross GLSL backend now enabled (was compiled out in 1.0)
    - GPU ICD GLSL post-processing pipeline for Warp3D Nova compatibility:
      - Const array initializers converted to runtime assignments
      - Dynamic gl_VertexID array indexing replaced with if-else chains
      - gl_InstanceID renamed to u_InstanceIndex (gl_ prefix reserved)
      - Integer modulo (%) replaced with (A - (A/B)*B) workaround
      - SPIRV-Cross ARB extension guards stripped
    - SPIRV-Cross static keyword set fix for AmigaOS shared library
      (prevents crash when library persists between program runs)
    - Push constant uniform name fixed (PushConstants, not SPIR-V block name)
    - Shader module transpile results cached across pipelines sharing
      the same shader modules (fixes missing samplers on second pipeline)
    - New example: 23_cow3d -- port of Alain Thellier's Cow3D demo with
      textured rotating 3D cow and cosmos background (23 examples total)
    - Autoinstall runs Avail FLUSH before/after install for clean updates
    - Build timestamp in GPU ICD debug output for version verification
    - Attribution added for ogles2.library (Daniel Muessener),
      Warp3DNova.library, RadeonRX/RadeonHD drivers (Hans de Ruiter)

1.0.0 -- 2026-03-20

    Initial release.

    - Vulkan 1.3 loader (vulkan.library) with automatic ICD discovery
      and dispatch
    - Software ICD (software_vk.library) with SPIR-V bytecode
      interpreter, triangle rasteriser with depth testing, and
      Bresenham line rasteriser
    - GPU ICD (ogles2_vk.library) with OGLES2/Warp3D Nova backend and
      SPIRV-Cross transpiler for SPIR-V to GLSL ES 3.1
    - 218 Vulkan functions per ICD (100% Vulkan 1.3 core + WSI)
    - 210+ SPIR-V opcodes including vertex/fragment shader operations,
      matrix math, texture sampling, control flow, and type conversions
    - 22 example programs from basic triangle to glTF model viewer
    - SDK headers with AmigaOS interface definitions, inline macros,
      and link library
    - VulkanPrefs CLI tool for ICD priority management
    - vulkaninfo device enumeration tool
    - AmigaMark benchmark tool
    - Autoinstall script for AmigaOS 4
    - Optimised software rasteriser with incremental edge functions,
      active varying mask, and shader state allocation pooling
    - GPU ICD optimisations including VBO caching and batched resource
      cleanup


License
-------

Copyright 2026 Richard Gibbs

Licensed under the Apache License, Version 2.0.
See LICENSE file for full terms.

Source code: https://github.com/derfsss/VulkanOS4
