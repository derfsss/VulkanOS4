# Vulkan 1.3 API for AmigaOS 4 - Feasibility Analysis & Design

## 1. Feasibility Assessment

### Verdict: FEASIBLE

A Vulkan 1.3 API can be implemented for AmigaOS 4. The SDK's interface system is
architecturally compatible with Vulkan's design, and the hardware path exists through
existing Radeon driver infrastructure.

### Why It Works

| Aspect | AmigaOS 4 SDK | Vulkan 1.3 | Compatibility |
|--------|--------------|------------|---------------|
| Language | C with struct-based interfaces | Pure C API | Direct match |
| Function dispatch | vtable of function pointers (`IFace->Method()`) | Function pointer dispatch via `vkGetProcAddr` | Equivalent models |
| Handle model | Opaque pointers (Library*, Context*) | Opaque handles (VkInstance, VkDevice) | Direct match |
| Parameter passing | TagItem lists + structs | pNext-chained structs with sType | Compatible (Vulkan's is more structured) |
| Extension model | Interface versioning | Extension/layer enumeration | Adaptable |
| Memory model | Explicit (AllocMem/FreeMem) | Explicit (vkAllocateMemory/vkFreeMemory) | Philosophical match |

### Technical Challenges & Mitigations

**1. Endianness (PowerPC = big-endian, GPU = little-endian)**
- Already solved: existing Radeon Warp3D drivers handle byte-swapping for command
  buffers, texture uploads, and DMA transfers
- The GPU's command processor accepts big-endian submissions when configured via
  the MC_SWAP register on GCN hardware
- SPIR-V bytecode is little-endian by spec; the driver swaps at shader load time

**2. SPIR-V Shader Compilation**
- SPIR-V is an intermediate representation; the driver must compile it to GCN ISA
- Approach: port the Mesa ACO (AMD Compiler) backend for GCN, which is
  well-documented open source (MIT license)
- Offline compilation: include `glslangValidator` and `spirv-tools` in the SDK
  as cross-compiled PPC tools
- Runtime: the ICD performs SPIR-V -> GCN translation at pipeline creation time

**3. GPU Memory Management**
- RX580 has dedicated VRAM (up to 8GB) accessed via PCIe BAR
- AmigaOS 4 on X5000/A1222 has PCIe with MMIO support through expansion.library
- The ICD manages VRAM allocation internally; host-visible memory uses the PCIe
  aperture (BAR1 on GCN)
- AmigaOS's flat memory model simplifies host pointer mapping vs. segmented OSes

**4. PCIe DMA & Command Submission**
- GCN uses ring buffers for command submission (GFX, compute, DMA rings)
- The ICD writes command packets to system memory, then signals the GPU via
  doorbell writes to MMIO registers
- Interrupts handled through AmigaOS's interrupt system (AddIntServer)

**5. Interface Struct Size**
- Vulkan 1.3 core has ~400 functions
- The AmigaOS interface struct will be large (~3200 bytes for function pointers)
- No technical limit on interface struct size; exec handles this fine
- Warp3D already has ~80 function pointers in its interface

**6. WSI (Window System Integration)**
- Vulkan needs platform-specific surface creation
- AmigaOS surfaces created from Intuition Screen/Window handles
- Custom extension: `VK_AMIGA_surface`

---

## 2. Architecture Overview

```
+-----------------------------------------------------------------+
|  Application        Warp3D Nova wrapper    ogles2 wrapper  ...  |
|  (direct Vulkan)    (Section 15.2)         (Section 15.3)       |
|  #include           +--------------------------------------+    |
|  <proto/vulkan.h>   | Compatibility Layers (Section 15)    |    |
|                     | Translate legacy APIs to Vulkan calls|    |
|                     +---------------+----------------------+    |
|                                     |                           |
+-------------------------------------v---------------------------+
|              vulkan.library (Loader)                            |
|                                                                 |
|  +-------------+  +--------------+  +------------------------+  |
|  | VulkanIFace |  |  Layer       |  |  ICD Dispatch          |  |
|  | (main)      |--|  Dispatch    |--|  (routes to ICD below) |  |
|  |             |  |  (optional)  |  |                        |  |
|  +-------------+  +--------------+  +------------------------+  |
+---------------------------+-------------------------------------+
|   radeon_vk.library       |    software_vk.library              |
|   (Hardware ICD)          |    (Software ICD - Section 16)      |
|                           |                                     |
|  +----------+ +--------+  |  +------------+ +----------------+  |
|  | SPIR-V   | | Cmd    |  |  | SPIR-V     | | Software       |  |
|  | Compiler | | Buffer |  |  | Interpreter| | Rasteriser     |  |
|  | (ACO)    | | Builder|  |  | (or JIT)   | | (CPU-based)    |  |
|  +----------+ +--------+  |  +------------+ +----------------+  |
+---------------------------+                                     |
|  AmigaOS 4 Kernel         |  Renders to system memory,          |
|  expansion.library        |  blits to Intuition window          |
|  PCIe MMIO / DMA / IRQs   |                                     |
+---------------------------+                                     |
|  AMD Radeon RX 580        |  No GPU hardware required           |
|  (GCN 4.0)                |  (validation / fallback)            |
+---------------------------+-------------------------------------+
```

### Component Breakdown

**vulkan.library** - The Vulkan Loader
- AmigaOS 4 shared library following all SDK conventions
- Exposes `VulkanIFace` with all Vulkan 1.3 core functions
- Discovers and loads ICDs from `DEVS:Vulkan/icd.d/` directory
- Loads validation layers from `DEVS:Vulkan/layers.d/`
- Dispatches calls to the appropriate ICD
- Handles instance-level vs device-level dispatch

**radeon_vk.library** - Installable Client Driver (ICD)
- GPU-specific Vulkan implementation for Radeon GCN hardware
- Contains the SPIR-V -> GCN shader compiler
- Manages GPU hardware directly via PCIe MMIO
- Implements all Vulkan 1.3 core device functionality
- Registered via JSON manifest in `DEVS:Vulkan/icd.d/`

**software_vk.library** - Software Fallback ICD (Section 16)
- CPU-only Vulkan implementation for testing and validation
- SPIR-V interpreter (with optional JIT acceleration, Section 16.11)
- Software rasteriser rendering to system memory
- Presents via `WritePixelArray` blit to Intuition window
- Reports as `VK_PHYSICAL_DEVICE_TYPE_CPU`
- No GPU hardware required -- runs on any AmigaOS 4 system

**Validation Layers** (optional, development only)
- Separate AmigaOS libraries in `DEVS:Vulkan/layers.d/`
- Intercept API calls for validation, logging, debugging
- Not loaded in production for zero overhead

---

## 3. SDK File Structure

Following AmigaOS 4 SDK conventions exactly:

```
include/
+-- include_h/
|   +-- vulkan/
|   |   +-- vulkan.h              # Master include (includes all below)
|   |   +-- vulkan_core.h         # Core Vulkan 1.3 types, enums, structs
|   |   +-- vulkan_amiga.h        # VK_AMIGA_surface WSI extension
|   |   +-- vk_platform.h         # Platform-specific type definitions
|   |   +-- vk_icd.h              # ICD interface for driver authors
|   +-- interfaces/
|   |   +-- vulkan.h              # Generated: VulkanIFace struct
|   +-- inline4/
|   |   +-- vulkan.h              # Generated: inline macros
|   +-- proto/
|   |   +-- vulkan.h              # Proto header (ties it all together)
|   +-- clib/
|       +-- vulkan_protos.h       # C function prototypes (compatibility)
+-- interfaces/
|   +-- vulkan.xml                # XML interface definition (source of truth)
|
Documentation/
+-- AutoDocs/
|   +-- vulkan.doc                # AmigaOS-style autodoc
+-- Vulkan/
|   +-- vulkan_guide.txt          # AmigaOS-specific Vulkan guide
|   +-- wsi_amiga.txt             # WSI extension documentation
|   +-- porting_from_warp3d.txt   # Migration guide from Warp3D
|
Examples/
+-- Vulkan/
    +-- triangle/                 # Basic triangle rendering
    +-- texture/                  # Textured quad
    +-- compute/                  # Compute shader example
    +-- wsi/                      # Window/surface creation

newlib/lib/
+-- libvulkan_loader.a            # Static loader stub (optional)

local/newlib/lib/
+-- libvulkan_loader.a            # Compiled loader stub
```

**Runtime installation layout** (not part of SDK, installed on target system):

```
LIBS:
+-- vulkan.library                # Vulkan loader (always installed)
+-- Vulkan/
    +-- radeon_vk.library         # Hardware ICD (GPU-specific)
    +-- software_vk.library       # Software ICD (fallback/testing)

DEVS:
+-- Vulkan/
    +-- icd.d/
    |   +-- radeon_vk.json        # Hardware ICD manifest
    |   +-- software_vk.json      # Software ICD manifest
    +-- layers.d/
        +-- VkLayer_khronos_validation.json  # Validation layer manifest

ENVARC:
+-- Vulkan/
    +-- shadercache/              # System-wide compiled shader cache
```

---

## 4. Core Header Design

### 4.1 vk_platform.h - Platform Definitions

```c
#ifndef VK_PLATFORM_H
#define VK_PLATFORM_H

/*
** Vulkan platform definitions for AmigaOS 4
** Defines types and calling conventions for the Vulkan API
*/

#include <exec/types.h>

/* AmigaOS 4 uses APICALL for all library interface functions.
** For Vulkan API functions called through the interface, we use VKAPI_ATTR
** and VKAPI_CALL which map to nothing (handled by the interface dispatch).
** For function pointers (PFN_vk*), VKAPI_PTR maps to nothing on AmigaOS.
*/
#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR

/* Vulkan requires these exact-width types */
#include <stdint.h>
#include <stddef.h>

#endif /* VK_PLATFORM_H */
```

### 4.2 vulkan_core.h - Core Types & Structures (excerpt)

```c
#ifndef VULKAN_CORE_H
#define VULKAN_CORE_H

/*
** Vulkan 1.3 Core API - Types, Enumerations, and Structures
** Conformant with Vulkan Specification v1.3
**
** This file defines all Vulkan core data types. Function declarations
** are in the interface header (interfaces/vulkan.h) and accessed through
** the VulkanIFace interface or inline4 macros.
*/

#include <vulkan/vk_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** NOTE: Do NOT use #pragma pack(2) or #pragma amiga-align here.
** Vulkan structures must use standard C alignment (natural packing)
** to match the Vulkan specification ABI. AmigaOS 2-byte packing would
** change struct sizes and field offsets, breaking compatibility.
** The #pragma pack(2) is only used in interfaces/vulkan.h for the
** AmigaOS VulkanIFace struct itself, not for Vulkan data types.
*/

/*------------------------------------------------------------------------
** Vulkan version macros
**----------------------------------------------------------------------*/
#define VK_API_VERSION_1_0  VK_MAKE_API_VERSION(0, 1, 0, 0)
#define VK_API_VERSION_1_1  VK_MAKE_API_VERSION(0, 1, 1, 0)
#define VK_API_VERSION_1_2  VK_MAKE_API_VERSION(0, 1, 2, 0)
#define VK_API_VERSION_1_3  VK_MAKE_API_VERSION(0, 1, 3, 0)

#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | \
     (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

#define VK_API_VERSION_VARIANT(version) ((uint32_t)(version) >> 29U)
#define VK_API_VERSION_MAJOR(version)   (((uint32_t)(version) >> 22U) & 0x7FU)
#define VK_API_VERSION_MINOR(version)   (((uint32_t)(version) >> 12U) & 0x3FFU)
#define VK_API_VERSION_PATCH(version)   ((uint32_t)(version) & 0xFFFU)

#define VK_HEADER_VERSION        275
#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION)

/*------------------------------------------------------------------------
** Fundamental type definitions
**----------------------------------------------------------------------*/
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;

#define VK_NULL_HANDLE 0

typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef uint64_t VkFlags64;
typedef uint64_t VkDeviceAddress;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleMask;

/*------------------------------------------------------------------------
** Dispatchable handles (pointers - contain dispatch table)
**----------------------------------------------------------------------*/
VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkCommandBuffer)

/*------------------------------------------------------------------------
** Non-dispatchable handles (uint64_t - opaque identifiers)
**----------------------------------------------------------------------*/
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkEvent)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkQueryPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBufferView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)
/* Vulkan 1.1 */
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSamplerYcbcrConversion)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorUpdateTemplate)
/* Vulkan 1.3 */
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPrivateDataSlot)
/* KHR extensions promoted to core */
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR)

/*------------------------------------------------------------------------
** Result codes
**----------------------------------------------------------------------*/
typedef enum VkResult {
    VK_SUCCESS                        = 0,
    VK_NOT_READY                      = 1,
    VK_TIMEOUT                        = 2,
    VK_EVENT_SET                      = 3,
    VK_EVENT_RESET                    = 4,
    VK_INCOMPLETE                     = 5,
    VK_SUBOPTIMAL_KHR                 = 1000001003,
    VK_ERROR_OUT_OF_HOST_MEMORY       = -1,
    VK_ERROR_OUT_OF_DEVICE_MEMORY     = -2,
    VK_ERROR_INITIALIZATION_FAILED    = -3,
    VK_ERROR_DEVICE_LOST              = -4,
    VK_ERROR_MEMORY_MAP_FAILED        = -5,
    VK_ERROR_LAYER_NOT_PRESENT        = -6,
    VK_ERROR_EXTENSION_NOT_PRESENT    = -7,
    VK_ERROR_FEATURE_NOT_PRESENT      = -8,
    VK_ERROR_INCOMPATIBLE_DRIVER      = -9,
    VK_ERROR_TOO_MANY_OBJECTS         = -10,
    VK_ERROR_FORMAT_NOT_SUPPORTED     = -11,
    VK_ERROR_FRAGMENTED_POOL          = -12,
    VK_ERROR_UNKNOWN                  = -13,
    /* Vulkan 1.1 */
    VK_ERROR_OUT_OF_POOL_MEMORY       = -1000069000,
    VK_ERROR_INVALID_EXTERNAL_HANDLE  = -1000072003,
    /* Vulkan 1.2 */
    VK_ERROR_FRAGMENTATION            = -1000161000,
    VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS = -1000257000,
    /* Vulkan 1.3 */
    VK_PIPELINE_COMPILE_REQUIRED      = 1000297000,
    /* KHR */
    VK_ERROR_SURFACE_LOST_KHR         = -1000000000,
    VK_ERROR_NATIVE_WINDOW_IN_USE_KHR = -1000000001,
    VK_ERROR_OUT_OF_DATE_KHR          = -1000001004,
    VK_RESULT_MAX_ENUM                = 0x7FFFFFFF
} VkResult;

/*------------------------------------------------------------------------
** Structure type enum (sType field in all Vulkan structs)
** Only a representative subset shown - full enum has ~1000 entries
**----------------------------------------------------------------------*/
typedef enum VkStructureType {
    VK_STRUCTURE_TYPE_APPLICATION_INFO                    = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO            = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO                  = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO                         = 4,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO                = 5,
    VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE                 = 6,
    VK_STRUCTURE_TYPE_BIND_SPARSE_INFO                    = 7,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO                   = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO               = 9,
    VK_STRUCTURE_TYPE_EVENT_CREATE_INFO                   = 10,
    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO              = 11,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO                  = 12,
    VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO             = 13,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO                   = 14,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO              = 15,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO           = 16,
    VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO          = 17,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO   = 18,
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 19,
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 20,
    VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO = 21,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO = 25,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26,
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO  = 27,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO         = 28,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO             = 29,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO            = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO        = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO            = 42,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO             = 37,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO   = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO         = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO        = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET                = 35,
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO                 = 31,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR           = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR                    = 1000001001,
    /* Vulkan 1.3 core (promoted from extensions) */
    VK_STRUCTURE_TYPE_RENDERING_INFO                      = 1000044000,
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO           = 1000044001,
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO      = 1000044002,
    /* AmigaOS WSI extension
    ** Value follows Vulkan vendor extension convention:
    ** 1000000000 + (ext_number * 1000) + offset
    ** Using ext_number 400 (private/unregistered range) + offset 0
    ** Register with Khronos for an official extension number before release.
    */
    VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO           = 1000400000,
    /* ... hundreds more entries ... */
    VK_STRUCTURE_TYPE_MAX_ENUM                            = 0x7FFFFFFF
} VkStructureType;

/*------------------------------------------------------------------------
** Key structures (representative subset)
**----------------------------------------------------------------------*/

typedef struct VkApplicationInfo {
    VkStructureType    sType;        /* Must be VK_STRUCTURE_TYPE_APPLICATION_INFO */
    const void*        pNext;
    const char*        pApplicationName;
    uint32_t           applicationVersion;
    const char*        pEngineName;
    uint32_t           engineVersion;
    uint32_t           apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType            sType;
    const void*                pNext;
    VkInstanceCreateFlags      flags;
    const VkApplicationInfo*   pApplicationInfo;
    uint32_t                   enabledLayerCount;
    const char* const*         ppEnabledLayerNames;
    uint32_t                   enabledExtensionCount;
    const char* const*         ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkPhysicalDeviceProperties {
    uint32_t           apiVersion;
    uint32_t           driverVersion;
    uint32_t           vendorID;
    uint32_t           deviceID;
    VkPhysicalDeviceType deviceType;
    char               deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t            pipelineCacheUUID[VK_UUID_SIZE];
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType            sType;
    const void*                pNext;
    VkDeviceQueueCreateFlags   flags;
    uint32_t                   queueFamilyIndex;
    uint32_t                   queueCount;
    const float*               pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType                sType;
    const void*                    pNext;
    VkDeviceCreateFlags            flags;
    uint32_t                       queueCreateInfoCount;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t                       enabledLayerCount;
    const char* const*             ppEnabledLayerNames;
    uint32_t                       enabledExtensionCount;
    const char* const*             ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkCommandBufferBeginInfo {
    VkStructureType                     sType;
    const void*                         pNext;
    VkCommandBufferUsageFlags           flags;
    const VkCommandBufferInheritanceInfo* pInheritanceInfo;
} VkCommandBufferBeginInfo;

/* Vulkan 1.3 dynamic rendering (no render pass objects needed) */
typedef struct VkRenderingAttachmentInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkImageView              imageView;
    VkImageLayout            imageLayout;
    VkResolveModeFlagBits    resolveMode;
    VkImageView              resolveImageView;
    VkImageLayout            resolveImageLayout;
    VkAttachmentLoadOp       loadOp;
    VkAttachmentStoreOp      storeOp;
    VkClearValue             clearValue;
} VkRenderingAttachmentInfo;

typedef struct VkRenderingInfo {
    VkStructureType                 sType;
    const void*                     pNext;
    VkRenderingFlags                flags;
    VkRect2D                        renderArea;
    uint32_t                        layerCount;
    uint32_t                        viewMask;
    uint32_t                        colorAttachmentCount;
    const VkRenderingAttachmentInfo* pColorAttachments;
    const VkRenderingAttachmentInfo* pDepthAttachment;
    const VkRenderingAttachmentInfo* pStencilAttachment;
} VkRenderingInfo;

/* ... Full Vulkan 1.3 spec structures would continue here
** (approximately 500+ structure definitions)
** The complete header would be generated from the Vulkan XML registry
*/

/*------------------------------------------------------------------------
** Function pointer typedefs (PFN_vk* types)
** Used for vkGetInstanceProcAddr / vkGetDeviceProcAddr return values
**----------------------------------------------------------------------*/
typedef void     (VKAPI_PTR *PFN_vkVoidFunction)(void);
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(
    const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef void     (VKAPI_PTR *PFN_vkDestroyInstance)(
    VkInstance, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(
    VkInstance, uint32_t*, VkPhysicalDevice*);
/* ... PFN types for all ~400 core functions ... */

/* No #pragma pack() needed -- standard C alignment used throughout */

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_CORE_H */
```

### 4.3 vulkan_amiga.h - AmigaOS WSI Extension

```c
#ifndef VULKAN_AMIGA_H
#define VULKAN_AMIGA_H

/*
** VK_AMIGA_surface - Vulkan Window System Integration for AmigaOS 4
**
** This extension provides the ability to create VkSurfaceKHR objects
** from AmigaOS Intuition Screens and Windows.
**
** Extension name: VK_AMIGA_surface
** Extension number: 400 (private/unregistered; register with Khronos before release)
** Required extensions: VK_KHR_surface
*/

#include <vulkan/vulkan_core.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_AMIGA_SURFACE_SPEC_VERSION     1
#define VK_AMIGA_SURFACE_EXTENSION_NAME   "VK_AMIGA_surface"

typedef VkFlags VkAmigaSurfaceCreateFlagsAMIGA;

/*
** VkAmigaSurfaceCreateInfoAMIGA
**
** Create a Vulkan surface from an AmigaOS Intuition Window.
** The surface will render into the window's visible area.
**
** If pScreen is provided and pWindow is NULL, a fullscreen surface
** is created that covers the entire screen.
*/
typedef struct VkAmigaSurfaceCreateInfoAMIGA {
    VkStructureType                sType;    /* VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO */
    const void*                    pNext;
    VkAmigaSurfaceCreateFlagsAMIGA flags;
    struct Screen*                 pScreen;  /* Screen to present on (required) */
    struct Window*                 pWindow;  /* Window to render into (NULL = fullscreen) */
} VkAmigaSurfaceCreateInfoAMIGA;

/*
** Function prototypes for WSI extension
** These are part of the VulkanIFace interface.
*/
typedef VkResult (VKAPI_PTR *PFN_vkCreateAmigaSurfaceAMIGA)(
    VkInstance                             instance,
    const VkAmigaSurfaceCreateInfoAMIGA*   pCreateInfo,
    const VkAllocationCallbacks*           pAllocator,
    VkSurfaceKHR*                          pSurface);

typedef VkBool32 (VKAPI_PTR *PFN_vkGetPhysicalDeviceAmigaPresentationSupportAMIGA)(
    VkPhysicalDevice    physicalDevice,
    uint32_t            queueFamilyIndex,
    struct Screen*      screen);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_AMIGA_H */
```

### 4.4 vulkan.h - Master Include

```c
#ifndef VULKAN_VULKAN_H
#define VULKAN_VULKAN_H

/*
** Master Vulkan include for AmigaOS 4
** Include this single file to get all Vulkan definitions.
*/

#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

/* AmigaOS WSI extension - always available on this platform */
#include <vulkan/vulkan_amiga.h>

#endif /* VULKAN_VULKAN_H */
```

---

## 5. AmigaOS Interface Definition

### 5.1 vulkan.xml - Interface Source Definition

```xml
<?xml version="1.0" encoding="iso-8859-1"?>
<!DOCTYPE library SYSTEM "library.dtd">
<library name="vulkan" basename="VulkanBase" openname="vulkan.library">
    <include>exec/types.h</include>
    <include>vulkan/vulkan.h</include>

    <interface name="main" version="1.0" struct="VulkanIFace"
               prefix="_Vulkan_" asmprefix="IVulkan" global="IVulkan">

        <!-- Standard AmigaOS interface methods -->
        <method name="Obtain" result="ULONG"/>
        <method name="Release" result="ULONG"/>
        <method name="Expunge" result="void" status="unimplemented"/>
        <method name="Clone" result="struct Interface *" status="unimplemented"/>

        <!--=============================================================
            Vulkan Loader Functions (not part of Vulkan spec, AmigaOS-specific)
            These allow the loader to be configured on AmigaOS.
        =============================================================-->
        <method name="VkAmigaGetLoaderVersion" result="uint32"/>
        <method name="VkAmigaSetICDSearchPath" result="VkResult">
            <arg name="path" type="const char *"/>
        </method>
        <method name="VkAmigaSetLayerSearchPath" result="VkResult">
            <arg name="path" type="const char *"/>
        </method>

        <!--=============================================================
            Vulkan 1.0 Core - Instance Functions
        =============================================================-->
        <method name="vkCreateInstance" result="VkResult">
            <arg name="pCreateInfo" type="const VkInstanceCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pInstance" type="VkInstance *"/>
        </method>
        <method name="vkDestroyInstance" result="void">
            <arg name="instance" type="VkInstance"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkEnumeratePhysicalDevices" result="VkResult">
            <arg name="instance" type="VkInstance"/>
            <arg name="pPhysicalDeviceCount" type="uint32_t *"/>
            <arg name="pPhysicalDevices" type="VkPhysicalDevice *"/>
        </method>
        <method name="vkGetPhysicalDeviceFeatures" result="void">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pFeatures" type="VkPhysicalDeviceFeatures *"/>
        </method>
        <method name="vkGetPhysicalDeviceFormatProperties" result="void">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="format" type="VkFormat"/>
            <arg name="pFormatProperties" type="VkFormatProperties *"/>
        </method>
        <method name="vkGetPhysicalDeviceImageFormatProperties" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="format" type="VkFormat"/>
            <arg name="type" type="VkImageType"/>
            <arg name="tiling" type="VkImageTiling"/>
            <arg name="usage" type="VkImageUsageFlags"/>
            <arg name="flags" type="VkImageCreateFlags"/>
            <arg name="pImageFormatProperties" type="VkImageFormatProperties *"/>
        </method>
        <method name="vkGetPhysicalDeviceProperties" result="void">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pProperties" type="VkPhysicalDeviceProperties *"/>
        </method>
        <method name="vkGetPhysicalDeviceQueueFamilyProperties" result="void">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pQueueFamilyPropertyCount" type="uint32_t *"/>
            <arg name="pQueueFamilyProperties" type="VkQueueFamilyProperties *"/>
        </method>
        <method name="vkGetPhysicalDeviceMemoryProperties" result="void">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pMemoryProperties" type="VkPhysicalDeviceMemoryProperties *"/>
        </method>
        <method name="vkGetInstanceProcAddr" result="PFN_vkVoidFunction">
            <arg name="instance" type="VkInstance"/>
            <arg name="pName" type="const char *"/>
        </method>
        <method name="vkGetDeviceProcAddr" result="PFN_vkVoidFunction">
            <arg name="device" type="VkDevice"/>
            <arg name="pName" type="const char *"/>
        </method>
        <method name="vkEnumerateInstanceExtensionProperties" result="VkResult">
            <arg name="pLayerName" type="const char *"/>
            <arg name="pPropertyCount" type="uint32_t *"/>
            <arg name="pProperties" type="VkExtensionProperties *"/>
        </method>
        <method name="vkEnumerateInstanceLayerProperties" result="VkResult">
            <arg name="pPropertyCount" type="uint32_t *"/>
            <arg name="pProperties" type="VkLayerProperties *"/>
        </method>
        <method name="vkEnumerateInstanceVersion" result="VkResult">
            <arg name="pApiVersion" type="uint32_t *"/>
        </method>

        <!--=============================================================
            Vulkan 1.0 Core - Device Functions
        =============================================================-->
        <method name="vkCreateDevice" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pCreateInfo" type="const VkDeviceCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pDevice" type="VkDevice *"/>
        </method>
        <method name="vkDestroyDevice" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkEnumerateDeviceExtensionProperties" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="pLayerName" type="const char *"/>
            <arg name="pPropertyCount" type="uint32_t *"/>
            <arg name="pProperties" type="VkExtensionProperties *"/>
        </method>
        <method name="vkGetDeviceQueue" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="queueFamilyIndex" type="uint32_t"/>
            <arg name="queueIndex" type="uint32_t"/>
            <arg name="pQueue" type="VkQueue *"/>
        </method>
        <method name="vkQueueSubmit" result="VkResult">
            <arg name="queue" type="VkQueue"/>
            <arg name="submitCount" type="uint32_t"/>
            <arg name="pSubmits" type="const VkSubmitInfo *"/>
            <arg name="fence" type="VkFence"/>
        </method>
        <method name="vkQueueWaitIdle" result="VkResult">
            <arg name="queue" type="VkQueue"/>
        </method>
        <method name="vkDeviceWaitIdle" result="VkResult">
            <arg name="device" type="VkDevice"/>
        </method>

        <!--=============================================================
            Memory Management
        =============================================================-->
        <method name="vkAllocateMemory" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pAllocateInfo" type="const VkMemoryAllocateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pMemory" type="VkDeviceMemory *"/>
        </method>
        <method name="vkFreeMemory" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="memory" type="VkDeviceMemory"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkMapMemory" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="memory" type="VkDeviceMemory"/>
            <arg name="offset" type="VkDeviceSize"/>
            <arg name="size" type="VkDeviceSize"/>
            <arg name="flags" type="VkMemoryMapFlags"/>
            <arg name="ppData" type="void **"/>
        </method>
        <method name="vkUnmapMemory" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="memory" type="VkDeviceMemory"/>
        </method>
        <method name="vkFlushMappedMemoryRanges" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="memoryRangeCount" type="uint32_t"/>
            <arg name="pMemoryRanges" type="const VkMappedMemoryRange *"/>
        </method>
        <method name="vkInvalidateMappedMemoryRanges" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="memoryRangeCount" type="uint32_t"/>
            <arg name="pMemoryRanges" type="const VkMappedMemoryRange *"/>
        </method>
        <method name="vkBindBufferMemory" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="memory" type="VkDeviceMemory"/>
            <arg name="memoryOffset" type="VkDeviceSize"/>
        </method>
        <method name="vkBindImageMemory" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="image" type="VkImage"/>
            <arg name="memory" type="VkDeviceMemory"/>
            <arg name="memoryOffset" type="VkDeviceSize"/>
        </method>
        <method name="vkGetBufferMemoryRequirements" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="pMemoryRequirements" type="VkMemoryRequirements *"/>
        </method>
        <method name="vkGetImageMemoryRequirements" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="image" type="VkImage"/>
            <arg name="pMemoryRequirements" type="VkMemoryRequirements *"/>
        </method>

        <!--=============================================================
            Buffer & Image Creation
        =============================================================-->
        <method name="vkCreateBuffer" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkBufferCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pBuffer" type="VkBuffer *"/>
        </method>
        <method name="vkDestroyBuffer" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreateImage" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkImageCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pImage" type="VkImage *"/>
        </method>
        <method name="vkDestroyImage" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="image" type="VkImage"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreateImageView" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkImageViewCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pView" type="VkImageView *"/>
        </method>
        <method name="vkDestroyImageView" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="view" type="VkImageView"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>

        <!--=============================================================
            Shader & Pipeline
        =============================================================-->
        <method name="vkCreateShaderModule" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkShaderModuleCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pShaderModule" type="VkShaderModule *"/>
        </method>
        <method name="vkDestroyShaderModule" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="shaderModule" type="VkShaderModule"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreateGraphicsPipelines" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pipelineCache" type="VkPipelineCache"/>
            <arg name="createInfoCount" type="uint32_t"/>
            <arg name="pCreateInfos" type="const VkGraphicsPipelineCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pPipelines" type="VkPipeline *"/>
        </method>
        <method name="vkCreateComputePipelines" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pipelineCache" type="VkPipelineCache"/>
            <arg name="createInfoCount" type="uint32_t"/>
            <arg name="pCreateInfos" type="const VkComputePipelineCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pPipelines" type="VkPipeline *"/>
        </method>
        <method name="vkDestroyPipeline" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pipeline" type="VkPipeline"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreatePipelineLayout" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkPipelineLayoutCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pPipelineLayout" type="VkPipelineLayout *"/>
        </method>
        <method name="vkDestroyPipelineLayout" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pipelineLayout" type="VkPipelineLayout"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreatePipelineCache" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkPipelineCacheCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pPipelineCache" type="VkPipelineCache *"/>
        </method>
        <method name="vkDestroyPipelineCache" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pipelineCache" type="VkPipelineCache"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>

        <!--=============================================================
            Render Pass (legacy, still supported in 1.3)
        =============================================================-->
        <method name="vkCreateRenderPass" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkRenderPassCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pRenderPass" type="VkRenderPass *"/>
        </method>
        <method name="vkDestroyRenderPass" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="renderPass" type="VkRenderPass"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreateFramebuffer" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkFramebufferCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pFramebuffer" type="VkFramebuffer *"/>
        </method>
        <method name="vkDestroyFramebuffer" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="framebuffer" type="VkFramebuffer"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>

        <!--=============================================================
            Descriptor Sets
        =============================================================-->
        <method name="vkCreateDescriptorSetLayout" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkDescriptorSetLayoutCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pSetLayout" type="VkDescriptorSetLayout *"/>
        </method>
        <method name="vkDestroyDescriptorSetLayout" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="descriptorSetLayout" type="VkDescriptorSetLayout"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkCreateDescriptorPool" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkDescriptorPoolCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pDescriptorPool" type="VkDescriptorPool *"/>
        </method>
        <method name="vkDestroyDescriptorPool" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="descriptorPool" type="VkDescriptorPool"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkAllocateDescriptorSets" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pAllocateInfo" type="const VkDescriptorSetAllocateInfo *"/>
            <arg name="pDescriptorSets" type="VkDescriptorSet *"/>
        </method>
        <method name="vkUpdateDescriptorSets" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="descriptorWriteCount" type="uint32_t"/>
            <arg name="pDescriptorWrites" type="const VkWriteDescriptorSet *"/>
            <arg name="descriptorCopyCount" type="uint32_t"/>
            <arg name="pDescriptorCopies" type="const VkCopyDescriptorSet *"/>
        </method>

        <!--=============================================================
            Sampler
        =============================================================-->
        <method name="vkCreateSampler" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkSamplerCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pSampler" type="VkSampler *"/>
        </method>
        <method name="vkDestroySampler" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="sampler" type="VkSampler"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>

        <!--=============================================================
            Command Pool & Command Buffer
        =============================================================-->
        <method name="vkCreateCommandPool" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkCommandPoolCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pCommandPool" type="VkCommandPool *"/>
        </method>
        <method name="vkDestroyCommandPool" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="commandPool" type="VkCommandPool"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkResetCommandPool" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="commandPool" type="VkCommandPool"/>
            <arg name="flags" type="VkCommandPoolResetFlags"/>
        </method>
        <method name="vkAllocateCommandBuffers" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pAllocateInfo" type="const VkCommandBufferAllocateInfo *"/>
            <arg name="pCommandBuffers" type="VkCommandBuffer *"/>
        </method>
        <method name="vkFreeCommandBuffers" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="commandPool" type="VkCommandPool"/>
            <arg name="commandBufferCount" type="uint32_t"/>
            <arg name="pCommandBuffers" type="const VkCommandBuffer *"/>
        </method>
        <method name="vkBeginCommandBuffer" result="VkResult">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pBeginInfo" type="const VkCommandBufferBeginInfo *"/>
        </method>
        <method name="vkEndCommandBuffer" result="VkResult">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
        </method>
        <method name="vkResetCommandBuffer" result="VkResult">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="flags" type="VkCommandBufferResetFlags"/>
        </method>

        <!--=============================================================
            Command Buffer Recording - Draw Commands
        =============================================================-->
        <method name="vkCmdBindPipeline" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pipelineBindPoint" type="VkPipelineBindPoint"/>
            <arg name="pipeline" type="VkPipeline"/>
        </method>
        <method name="vkCmdSetViewport" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="firstViewport" type="uint32_t"/>
            <arg name="viewportCount" type="uint32_t"/>
            <arg name="pViewports" type="const VkViewport *"/>
        </method>
        <method name="vkCmdSetScissor" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="firstScissor" type="uint32_t"/>
            <arg name="scissorCount" type="uint32_t"/>
            <arg name="pScissors" type="const VkRect2D *"/>
        </method>
        <method name="vkCmdBindVertexBuffers" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="firstBinding" type="uint32_t"/>
            <arg name="bindingCount" type="uint32_t"/>
            <arg name="pBuffers" type="const VkBuffer *"/>
            <arg name="pOffsets" type="const VkDeviceSize *"/>
        </method>
        <method name="vkCmdBindIndexBuffer" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="offset" type="VkDeviceSize"/>
            <arg name="indexType" type="VkIndexType"/>
        </method>
        <method name="vkCmdBindDescriptorSets" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pipelineBindPoint" type="VkPipelineBindPoint"/>
            <arg name="layout" type="VkPipelineLayout"/>
            <arg name="firstSet" type="uint32_t"/>
            <arg name="descriptorSetCount" type="uint32_t"/>
            <arg name="pDescriptorSets" type="const VkDescriptorSet *"/>
            <arg name="dynamicOffsetCount" type="uint32_t"/>
            <arg name="pDynamicOffsets" type="const uint32_t *"/>
        </method>
        <method name="vkCmdDraw" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="vertexCount" type="uint32_t"/>
            <arg name="instanceCount" type="uint32_t"/>
            <arg name="firstVertex" type="uint32_t"/>
            <arg name="firstInstance" type="uint32_t"/>
        </method>
        <method name="vkCmdDrawIndexed" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="indexCount" type="uint32_t"/>
            <arg name="instanceCount" type="uint32_t"/>
            <arg name="firstIndex" type="uint32_t"/>
            <arg name="vertexOffset" type="int32_t"/>
            <arg name="firstInstance" type="uint32_t"/>
        </method>
        <method name="vkCmdDrawIndirect" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="offset" type="VkDeviceSize"/>
            <arg name="drawCount" type="uint32_t"/>
            <arg name="stride" type="uint32_t"/>
        </method>
        <method name="vkCmdDrawIndexedIndirect" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="offset" type="VkDeviceSize"/>
            <arg name="drawCount" type="uint32_t"/>
            <arg name="stride" type="uint32_t"/>
        </method>
        <method name="vkCmdDispatch" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="groupCountX" type="uint32_t"/>
            <arg name="groupCountY" type="uint32_t"/>
            <arg name="groupCountZ" type="uint32_t"/>
        </method>
        <method name="vkCmdDispatchIndirect" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="buffer" type="VkBuffer"/>
            <arg name="offset" type="VkDeviceSize"/>
        </method>

        <!--=============================================================
            Command Buffer Recording - Transfer Commands
        =============================================================-->
        <method name="vkCmdCopyBuffer" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcBuffer" type="VkBuffer"/>
            <arg name="dstBuffer" type="VkBuffer"/>
            <arg name="regionCount" type="uint32_t"/>
            <arg name="pRegions" type="const VkBufferCopy *"/>
        </method>
        <method name="vkCmdCopyImage" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcImage" type="VkImage"/>
            <arg name="srcImageLayout" type="VkImageLayout"/>
            <arg name="dstImage" type="VkImage"/>
            <arg name="dstImageLayout" type="VkImageLayout"/>
            <arg name="regionCount" type="uint32_t"/>
            <arg name="pRegions" type="const VkImageCopy *"/>
        </method>
        <method name="vkCmdBlitImage" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcImage" type="VkImage"/>
            <arg name="srcImageLayout" type="VkImageLayout"/>
            <arg name="dstImage" type="VkImage"/>
            <arg name="dstImageLayout" type="VkImageLayout"/>
            <arg name="regionCount" type="uint32_t"/>
            <arg name="pRegions" type="const VkImageBlit *"/>
            <arg name="filter" type="VkFilter"/>
        </method>
        <method name="vkCmdCopyBufferToImage" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcBuffer" type="VkBuffer"/>
            <arg name="dstImage" type="VkImage"/>
            <arg name="dstImageLayout" type="VkImageLayout"/>
            <arg name="regionCount" type="uint32_t"/>
            <arg name="pRegions" type="const VkBufferImageCopy *"/>
        </method>
        <method name="vkCmdCopyImageToBuffer" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcImage" type="VkImage"/>
            <arg name="srcImageLayout" type="VkImageLayout"/>
            <arg name="dstBuffer" type="VkBuffer"/>
            <arg name="regionCount" type="uint32_t"/>
            <arg name="pRegions" type="const VkBufferImageCopy *"/>
        </method>
        <method name="vkCmdPipelineBarrier" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="srcStageMask" type="VkPipelineStageFlags"/>
            <arg name="dstStageMask" type="VkPipelineStageFlags"/>
            <arg name="dependencyFlags" type="VkDependencyFlags"/>
            <arg name="memoryBarrierCount" type="uint32_t"/>
            <arg name="pMemoryBarriers" type="const VkMemoryBarrier *"/>
            <arg name="bufferMemoryBarrierCount" type="uint32_t"/>
            <arg name="pBufferMemoryBarriers" type="const VkBufferMemoryBarrier *"/>
            <arg name="imageMemoryBarrierCount" type="uint32_t"/>
            <arg name="pImageMemoryBarriers" type="const VkImageMemoryBarrier *"/>
        </method>

        <!--=============================================================
            Command Buffer Recording - Render Pass
        =============================================================-->
        <method name="vkCmdBeginRenderPass" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pRenderPassBegin" type="const VkRenderPassBeginInfo *"/>
            <arg name="contents" type="VkSubpassContents"/>
        </method>
        <method name="vkCmdEndRenderPass" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
        </method>
        <method name="vkCmdNextSubpass" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="contents" type="VkSubpassContents"/>
        </method>
        <method name="vkCmdPushConstants" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="layout" type="VkPipelineLayout"/>
            <arg name="stageFlags" type="VkShaderStageFlags"/>
            <arg name="offset" type="uint32_t"/>
            <arg name="size" type="uint32_t"/>
            <arg name="pValues" type="const void *"/>
        </method>

        <!--=============================================================
            Synchronization
        =============================================================-->
        <method name="vkCreateFence" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkFenceCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pFence" type="VkFence *"/>
        </method>
        <method name="vkDestroyFence" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="fence" type="VkFence"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkResetFences" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="fenceCount" type="uint32_t"/>
            <arg name="pFences" type="const VkFence *"/>
        </method>
        <method name="vkGetFenceStatus" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="fence" type="VkFence"/>
        </method>
        <method name="vkWaitForFences" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="fenceCount" type="uint32_t"/>
            <arg name="pFences" type="const VkFence *"/>
            <arg name="waitAll" type="VkBool32"/>
            <arg name="timeout" type="uint64_t"/>
        </method>
        <method name="vkCreateSemaphore" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkSemaphoreCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pSemaphore" type="VkSemaphore *"/>
        </method>
        <method name="vkDestroySemaphore" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="semaphore" type="VkSemaphore"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>

        <!--=============================================================
            Vulkan 1.3 Core - Dynamic Rendering (promoted from VK_KHR_dynamic_rendering)
        =============================================================-->
        <method name="vkCmdBeginRendering" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pRenderingInfo" type="const VkRenderingInfo *"/>
        </method>
        <method name="vkCmdEndRendering" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
        </method>

        <!--=============================================================
            Vulkan 1.3 Core - Synchronization2 (promoted from VK_KHR_synchronization2)
        =============================================================-->
        <method name="vkCmdPipelineBarrier2" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="pDependencyInfo" type="const VkDependencyInfo *"/>
        </method>
        <method name="vkCmdSetEvent2" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="event" type="VkEvent"/>
            <arg name="pDependencyInfo" type="const VkDependencyInfo *"/>
        </method>
        <method name="vkCmdResetEvent2" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="event" type="VkEvent"/>
            <arg name="stageMask" type="VkPipelineStageFlags2"/>
        </method>
        <method name="vkCmdWaitEvents2" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="eventCount" type="uint32_t"/>
            <arg name="pEvents" type="const VkEvent *"/>
            <arg name="pDependencyInfos" type="const VkDependencyInfo *"/>
        </method>
        <method name="vkQueueSubmit2" result="VkResult">
            <arg name="queue" type="VkQueue"/>
            <arg name="submitCount" type="uint32_t"/>
            <arg name="pSubmits" type="const VkSubmitInfo2 *"/>
            <arg name="fence" type="VkFence"/>
        </method>
        <method name="vkCmdWriteTimestamp2" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="stage" type="VkPipelineStageFlags2"/>
            <arg name="queryPool" type="VkQueryPool"/>
            <arg name="query" type="uint32_t"/>
        </method>

        <!--=============================================================
            Vulkan 1.3 Core - Dynamic State (promoted from VK_EXT_extended_dynamic_state)
        =============================================================-->
        <method name="vkCmdSetCullMode" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="cullMode" type="VkCullModeFlags"/>
        </method>
        <method name="vkCmdSetFrontFace" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="frontFace" type="VkFrontFace"/>
        </method>
        <method name="vkCmdSetPrimitiveTopology" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="primitiveTopology" type="VkPrimitiveTopology"/>
        </method>
        <method name="vkCmdSetViewportWithCount" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="viewportCount" type="uint32_t"/>
            <arg name="pViewports" type="const VkViewport *"/>
        </method>
        <method name="vkCmdSetScissorWithCount" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="scissorCount" type="uint32_t"/>
            <arg name="pScissors" type="const VkRect2D *"/>
        </method>
        <method name="vkCmdSetDepthTestEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="depthTestEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetDepthWriteEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="depthWriteEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetDepthCompareOp" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="depthCompareOp" type="VkCompareOp"/>
        </method>
        <method name="vkCmdSetDepthBoundsTestEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="depthBoundsTestEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetStencilTestEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="stencilTestEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetStencilOp" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="faceMask" type="VkStencilFaceFlags"/>
            <arg name="failOp" type="VkStencilOp"/>
            <arg name="passOp" type="VkStencilOp"/>
            <arg name="depthFailOp" type="VkStencilOp"/>
            <arg name="compareOp" type="VkCompareOp"/>
        </method>
        <method name="vkCmdSetRasterizerDiscardEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="rasterizerDiscardEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetDepthBiasEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="depthBiasEnable" type="VkBool32"/>
        </method>
        <method name="vkCmdSetPrimitiveRestartEnable" result="void">
            <arg name="commandBuffer" type="VkCommandBuffer"/>
            <arg name="primitiveRestartEnable" type="VkBool32"/>
        </method>

        <!--=============================================================
            Vulkan 1.3 Core - Misc promoted features
        =============================================================-->
        <method name="vkGetDeviceBufferMemoryRequirements" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pInfo" type="const VkDeviceBufferMemoryRequirements *"/>
            <arg name="pMemoryRequirements" type="VkMemoryRequirements2 *"/>
        </method>
        <method name="vkGetDeviceImageMemoryRequirements" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="pInfo" type="const VkDeviceImageMemoryRequirements *"/>
            <arg name="pMemoryRequirements" type="VkMemoryRequirements2 *"/>
        </method>
        <method name="vkCreatePrivateDataSlot" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkPrivateDataSlotCreateInfo *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pPrivateDataSlot" type="VkPrivateDataSlot *"/>
        </method>
        <method name="vkDestroyPrivateDataSlot" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="privateDataSlot" type="VkPrivateDataSlot"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkSetPrivateData" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="objectType" type="VkObjectType"/>
            <arg name="objectHandle" type="uint64_t"/>
            <arg name="privateDataSlot" type="VkPrivateDataSlot"/>
            <arg name="data" type="uint64_t"/>
        </method>
        <method name="vkGetPrivateData" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="objectType" type="VkObjectType"/>
            <arg name="objectHandle" type="uint64_t"/>
            <arg name="privateDataSlot" type="VkPrivateDataSlot"/>
            <arg name="pData" type="uint64_t *"/>
        </method>

        <!--=============================================================
            KHR Surface / Swapchain (required for WSI)
        =============================================================-->
        <method name="vkDestroySurfaceKHR" result="void">
            <arg name="instance" type="VkInstance"/>
            <arg name="surface" type="VkSurfaceKHR"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkGetPhysicalDeviceSurfaceSupportKHR" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="queueFamilyIndex" type="uint32_t"/>
            <arg name="surface" type="VkSurfaceKHR"/>
            <arg name="pSupported" type="VkBool32 *"/>
        </method>
        <method name="vkGetPhysicalDeviceSurfaceCapabilitiesKHR" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="surface" type="VkSurfaceKHR"/>
            <arg name="pSurfaceCapabilities" type="VkSurfaceCapabilitiesKHR *"/>
        </method>
        <method name="vkGetPhysicalDeviceSurfaceFormatsKHR" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="surface" type="VkSurfaceKHR"/>
            <arg name="pSurfaceFormatCount" type="uint32_t *"/>
            <arg name="pSurfaceFormats" type="VkSurfaceFormatKHR *"/>
        </method>
        <method name="vkGetPhysicalDeviceSurfacePresentModesKHR" result="VkResult">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="surface" type="VkSurfaceKHR"/>
            <arg name="pPresentModeCount" type="uint32_t *"/>
            <arg name="pPresentModes" type="VkPresentModeKHR *"/>
        </method>
        <method name="vkCreateSwapchainKHR" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="pCreateInfo" type="const VkSwapchainCreateInfoKHR *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pSwapchain" type="VkSwapchainKHR *"/>
        </method>
        <method name="vkDestroySwapchainKHR" result="void">
            <arg name="device" type="VkDevice"/>
            <arg name="swapchain" type="VkSwapchainKHR"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
        </method>
        <method name="vkGetSwapchainImagesKHR" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="swapchain" type="VkSwapchainKHR"/>
            <arg name="pSwapchainImageCount" type="uint32_t *"/>
            <arg name="pSwapchainImages" type="VkImage *"/>
        </method>
        <method name="vkAcquireNextImageKHR" result="VkResult">
            <arg name="device" type="VkDevice"/>
            <arg name="swapchain" type="VkSwapchainKHR"/>
            <arg name="timeout" type="uint64_t"/>
            <arg name="semaphore" type="VkSemaphore"/>
            <arg name="fence" type="VkFence"/>
            <arg name="pImageIndex" type="uint32_t *"/>
        </method>
        <method name="vkQueuePresentKHR" result="VkResult">
            <arg name="queue" type="VkQueue"/>
            <arg name="pPresentInfo" type="const VkPresentInfoKHR *"/>
        </method>

        <!--=============================================================
            AmigaOS WSI Extension
        =============================================================-->
        <method name="vkCreateAmigaSurfaceAMIGA" result="VkResult">
            <arg name="instance" type="VkInstance"/>
            <arg name="pCreateInfo" type="const VkAmigaSurfaceCreateInfoAMIGA *"/>
            <arg name="pAllocator" type="const VkAllocationCallbacks *"/>
            <arg name="pSurface" type="VkSurfaceKHR *"/>
        </method>
        <method name="vkGetPhysicalDeviceAmigaPresentationSupportAMIGA" result="VkBool32">
            <arg name="physicalDevice" type="VkPhysicalDevice"/>
            <arg name="queueFamilyIndex" type="uint32_t"/>
            <arg name="screen" type="struct Screen *"/>
        </method>

        <!--=============================================================
            NOTE: The complete XML would include all remaining ~250 Vulkan 1.3
            core functions. The full list is generated from the Vulkan XML
            registry (vk.xml) maintained by Khronos. The functions shown above
            cover the most critical categories. Additional categories include:

            - Query pools (vkCreateQueryPool, vkGetQueryPoolResults, etc.)
            - Events (vkCreateEvent, vkSetEvent, vkResetEvent, etc.)
            - Sparse resources (vkGetImageSparseMemoryRequirements, etc.)
            - Additional command buffer state commands
            - Vulkan 1.1 promoted functions (descriptor update templates,
              external memory/semaphore/fence, device groups, etc.)
            - Vulkan 1.2 promoted functions (timeline semaphores,
              buffer device address, etc.)
            - Vulkan 1.3 remaining promoted functions (copy commands 2,
              format feature flags 2, etc.)
        =============================================================-->

    </interface>
</library>
```

### 5.2 proto/vulkan.h - Proto Header

```c
#ifndef PROTO_VULKAN_H
#define PROTO_VULKAN_H

/*
** Proto header for vulkan.library
** Auto-generated by idltool
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef VULKAN_VULKAN_H
#include <vulkan/vulkan.h>
#endif

/*
** Library base - vulkan.library
*/
#ifndef __NOLIBBASE__
 #if defined(__cplusplus) && defined(__USE_AMIGAOS_NAMESPACE__)
  extern struct AmigaOS::Library * VulkanBase;
 #else
  extern struct Library * VulkanBase;
 #endif
#endif

#ifdef __amigaos4__
 #include <interfaces/vulkan.h>
 #ifdef __USE_INLINE__
  #include <inline4/vulkan.h>
 #endif
 #ifndef CLIB_VULKAN_PROTOS_H
  #define CLIB_VULKAN_PROTOS_H 1
 #endif
 #ifndef __NOGLOBALIFACE__
  #if defined(__cplusplus) && defined(__USE_AMIGAOS_NAMESPACE__)
   extern struct AmigaOS::VulkanIFace * IVulkan;
  #else
   extern struct VulkanIFace * IVulkan;
  #endif
 #endif
#endif /* __amigaos4__ */

#endif /* PROTO_VULKAN_H */
```

### 5.3 inline4/vulkan.h - Inline Macros (excerpt)

```c
#ifndef INLINE4_VULKAN_H
#define INLINE4_VULKAN_H

/* Auto-generated by idltool 53.1 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_EXEC_H
#include <exec/exec.h>
#endif
#ifndef EXEC_INTERFACES_H
#include <exec/interfaces.h>
#endif
#ifndef VULKAN_VULKAN_H
#include <vulkan/vulkan.h>
#endif
#include <interfaces/vulkan.h>

/* Inline macros for Interface "main" */

/* AmigaOS-specific loader functions */
#define VkAmigaGetLoaderVersion() IVulkan->VkAmigaGetLoaderVersion()
#define VkAmigaSetICDSearchPath(path) IVulkan->VkAmigaSetICDSearchPath((path))
#define VkAmigaSetLayerSearchPath(path) IVulkan->VkAmigaSetLayerSearchPath((path))

/* Vulkan 1.0 Core */
#define vkCreateInstance(pCreateInfo, pAllocator, pInstance) \
    IVulkan->vkCreateInstance((pCreateInfo), (pAllocator), (pInstance))
#define vkDestroyInstance(instance, pAllocator) \
    IVulkan->vkDestroyInstance((instance), (pAllocator))
#define vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices) \
    IVulkan->vkEnumeratePhysicalDevices((instance), (pPhysicalDeviceCount), (pPhysicalDevices))
#define vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures) \
    IVulkan->vkGetPhysicalDeviceFeatures((physicalDevice), (pFeatures))
#define vkGetPhysicalDeviceProperties(physicalDevice, pProperties) \
    IVulkan->vkGetPhysicalDeviceProperties((physicalDevice), (pProperties))
#define vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pCount, pProps) \
    IVulkan->vkGetPhysicalDeviceQueueFamilyProperties((physicalDevice), (pCount), (pProps))
#define vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties) \
    IVulkan->vkGetPhysicalDeviceMemoryProperties((physicalDevice), (pMemoryProperties))
#define vkGetInstanceProcAddr(instance, pName) \
    IVulkan->vkGetInstanceProcAddr((instance), (pName))
#define vkGetDeviceProcAddr(device, pName) \
    IVulkan->vkGetDeviceProcAddr((device), (pName))
#define vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties) \
    IVulkan->vkEnumerateInstanceExtensionProperties((pLayerName), (pPropertyCount), (pProperties))
#define vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties) \
    IVulkan->vkEnumerateInstanceLayerProperties((pPropertyCount), (pProperties))

/* Device */
#define vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice) \
    IVulkan->vkCreateDevice((physicalDevice), (pCreateInfo), (pAllocator), (pDevice))
#define vkDestroyDevice(device, pAllocator) \
    IVulkan->vkDestroyDevice((device), (pAllocator))
#define vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue) \
    IVulkan->vkGetDeviceQueue((device), (queueFamilyIndex), (queueIndex), (pQueue))
#define vkQueueSubmit(queue, submitCount, pSubmits, fence) \
    IVulkan->vkQueueSubmit((queue), (submitCount), (pSubmits), (fence))
#define vkQueueWaitIdle(queue) IVulkan->vkQueueWaitIdle((queue))
#define vkDeviceWaitIdle(device) IVulkan->vkDeviceWaitIdle((device))

/* Memory */
#define vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory) \
    IVulkan->vkAllocateMemory((device), (pAllocateInfo), (pAllocator), (pMemory))
#define vkFreeMemory(device, memory, pAllocator) \
    IVulkan->vkFreeMemory((device), (memory), (pAllocator))
#define vkMapMemory(device, memory, offset, size, flags, ppData) \
    IVulkan->vkMapMemory((device), (memory), (offset), (size), (flags), (ppData))
#define vkUnmapMemory(device, memory) \
    IVulkan->vkUnmapMemory((device), (memory))

/* Command Buffer Recording */
#define vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline) \
    IVulkan->vkCmdBindPipeline((commandBuffer), (pipelineBindPoint), (pipeline))
#define vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports) \
    IVulkan->vkCmdSetViewport((commandBuffer), (firstViewport), (viewportCount), (pViewports))
#define vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors) \
    IVulkan->vkCmdSetScissor((commandBuffer), (firstScissor), (scissorCount), (pScissors))
#define vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance) \
    IVulkan->vkCmdDraw((commandBuffer), (vertexCount), (instanceCount), (firstVertex), (firstInstance))
#define vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance) \
    IVulkan->vkCmdDrawIndexed((commandBuffer), (indexCount), (instanceCount), (firstIndex), (vertexOffset), (firstInstance))
#define vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ) \
    IVulkan->vkCmdDispatch((commandBuffer), (groupCountX), (groupCountY), (groupCountZ))

/* Vulkan 1.3 Dynamic Rendering */
#define vkCmdBeginRendering(commandBuffer, pRenderingInfo) \
    IVulkan->vkCmdBeginRendering((commandBuffer), (pRenderingInfo))
#define vkCmdEndRendering(commandBuffer) \
    IVulkan->vkCmdEndRendering((commandBuffer))

/* Swapchain / WSI */
#define vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain) \
    IVulkan->vkCreateSwapchainKHR((device), (pCreateInfo), (pAllocator), (pSwapchain))
#define vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex) \
    IVulkan->vkAcquireNextImageKHR((device), (swapchain), (timeout), (semaphore), (fence), (pImageIndex))
#define vkQueuePresentKHR(queue, pPresentInfo) \
    IVulkan->vkQueuePresentKHR((queue), (pPresentInfo))

/* AmigaOS WSI Extension */
#define vkCreateAmigaSurfaceAMIGA(instance, pCreateInfo, pAllocator, pSurface) \
    IVulkan->vkCreateAmigaSurfaceAMIGA((instance), (pCreateInfo), (pAllocator), (pSurface))
#define vkGetPhysicalDeviceAmigaPresentationSupportAMIGA(physicalDevice, queueFamilyIndex, screen) \
    IVulkan->vkGetPhysicalDeviceAmigaPresentationSupportAMIGA((physicalDevice), (queueFamilyIndex), (screen))

/* ... all remaining ~350 functions follow the same pattern ... */

#endif /* INLINE4_VULKAN_H */
```

---

## 6. ICD Interface (Driver Authors)

### 6.1 vk_icd.h - ICD Registration

```c
#ifndef VK_ICD_H
#define VK_ICD_H

/*
** Vulkan ICD (Installable Client Driver) interface for AmigaOS 4
**
** GPU driver authors implement this interface in their driver library
** (e.g., radeon_vk.library) and register it via a JSON manifest file
** in DEVS:Vulkan/icd.d/
*/

#include <vulkan/vulkan_core.h>
#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_ICD_VERSION  1

/*
** ICD Manifest JSON format (placed in DEVS:Vulkan/icd.d/radeon_vk.json):
**
** {
**     "file_format_version": "1.0.0",
**     "ICD": {
**         "library_path": "LIBS:Vulkan/radeon_vk.library",
**         "api_version": "1.3.0",
**         "library_version": 1
**     }
** }
*/

/*
** The ICD library must export a "main" interface (following AmigaOS conventions)
** that contains at minimum:
**
** - Obtain / Release / Expunge / Clone (standard)
** - vk_icdGetInstanceProcAddr: Entry point for the loader to resolve all
**   Vulkan functions. This is the ONLY function the loader calls directly.
** - vk_icdNegotiateLoaderICDInterfaceVersion: Version negotiation.
** - vk_icdGetPhysicalDeviceProcAddr: (optional) For physical-device-level
**   dispatch optimization.
*/

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vk_icdGetInstanceProcAddr)(
    VkInstance instance, const char *pName);

typedef VkResult (VKAPI_PTR *PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(
    uint32_t *pSupportedVersion);

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vk_icdGetPhysicalDeviceProcAddr)(
    VkInstance instance, const char *pName);

#ifdef __cplusplus
}
#endif

#endif /* VK_ICD_H */
```

### 6.2 ICD Discovery

The loader searches for ICDs in this order:
1. `DEVS:Vulkan/icd.d/*.json` - System-installed drivers
2. `ENV:VK_ICD_FILENAMES` - Override (development/debugging)

Each JSON manifest points to an AmigaOS shared library that implements
the ICD interface. The loader opens the library, obtains the interface,
and calls `vk_icdGetInstanceProcAddr` to resolve all Vulkan functions.

---

## 7. Runtime Flow

### Opening the Library (Application)

```c
#include <proto/exec.h>
#include <proto/vulkan.h>

struct Library *VulkanBase = NULL;
struct VulkanIFace *IVulkan = NULL;

int main(void)
{
    /* Open vulkan.library - standard AmigaOS pattern */
    VulkanBase = IExec->OpenLibrary("vulkan.library", 1);
    if (!VulkanBase) {
        /* Vulkan not available */
        return 1;
    }

    IVulkan = (struct VulkanIFace *)
        IExec->GetInterface(VulkanBase, "main", 1, NULL);
    if (!IVulkan) {
        IExec->CloseLibrary(VulkanBase);
        return 1;
    }

    /* Now use Vulkan normally */
    VkInstance instance;
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "AmigaOS Vulkan App",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    const char *extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_AMIGA_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &instance);
    /* or equivalently: IVulkan->vkCreateInstance(&createInfo, NULL, &instance) */

    /* ... use Vulkan ... */

    vkDestroyInstance(instance, NULL);

    IExec->DropInterface((struct Interface *)IVulkan);
    IExec->CloseLibrary(VulkanBase);
    return 0;
}
```

### Creating a Surface (AmigaOS WSI)

```c
#include <proto/intuition.h>
#include <proto/vulkan.h>

VkSurfaceKHR createAmigaSurface(VkInstance instance,
                                 struct Screen *screen,
                                 struct Window *window)
{
    VkAmigaSurfaceCreateInfoAMIGA surfaceInfo = {
        .sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pScreen = screen,
        .pWindow = window   /* NULL for fullscreen */
    };

    VkSurfaceKHR surface;
    VkResult result = vkCreateAmigaSurfaceAMIGA(
        instance, &surfaceInfo, NULL, &surface);

    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return surface;
}
```

---

## 8. Complete Triangle Example

```c
/*
** Vulkan Triangle - AmigaOS 4
**
** Minimal example that renders a colored triangle using Vulkan 1.3
** with dynamic rendering (no render pass objects).
**
** Compile: ppc-amigaos-gcc -o triangle triangle.c -lvulkan_loader
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *VulkanBase = NULL;
struct VulkanIFace *IVulkan = NULL;

/* SPIR-V bytecode for vertex shader (pre-compiled)
** Equivalent GLSL:
**   #version 450
**   layout(location = 0) out vec3 fragColor;
**   vec2 positions[3] = vec2[](vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
**   vec3 colors[3] = vec3[](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
**   void main() {
**       gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
**       fragColor = colors[gl_VertexIndex];
**   }
*/
static const uint32_t vertShaderCode[] = { /* ... SPIR-V bytecode ... */ };

/* SPIR-V bytecode for fragment shader (pre-compiled)
** Equivalent GLSL:
**   #version 450
**   layout(location = 0) in vec3 fragColor;
**   layout(location = 0) out vec4 outColor;
**   void main() { outColor = vec4(fragColor, 1.0); }
*/
static const uint32_t fragShaderCode[] = { /* ... SPIR-V bytecode ... */ };

int main(int argc, char **argv)
{
    /* 1. Open libraries */
    VulkanBase = IExec->OpenLibrary("vulkan.library", 1);
    if (!VulkanBase) { printf("No vulkan.library\n"); return 1; }

    IVulkan = (struct VulkanIFace *)
        IExec->GetInterface(VulkanBase, "main", 1, NULL);
    if (!IVulkan) { printf("No Vulkan interface\n"); goto cleanup; }

    /* 2. Create Vulkan instance */
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Triangle",
        .apiVersion = VK_API_VERSION_1_3
    };

    const char *instExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_AMIGA_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo instanceCI = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instExts
    };

    VkInstance instance;
    if (vkCreateInstance(&instanceCI, NULL, &instance) != VK_SUCCESS) {
        printf("Failed to create Vulkan instance\n");
        goto cleanup;
    }

    /* 3. Select physical device */
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        printf("No Vulkan-capable GPU found\n");
        goto cleanup_instance;
    }

    VkPhysicalDevice physicalDevice;
    vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    printf("GPU: %s (Vulkan %u.%u.%u)\n", props.deviceName,
        VK_API_VERSION_MAJOR(props.apiVersion),
        VK_API_VERSION_MINOR(props.apiVersion),
        VK_API_VERSION_PATCH(props.apiVersion));

    /* 4. Find graphics queue family */
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *queueFamilies = malloc(
        queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, queueFamilies);

    uint32_t graphicsFamily = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            break;
        }
    }
    free(queueFamilies);

    /* 5. Create logical device */
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    /* Enable Vulkan 1.3 features including dynamic rendering */
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE
    };

    VkDeviceCreateInfo deviceCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = devExts
    };

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCI, NULL, &device) != VK_SUCCESS) {
        printf("Failed to create device\n");
        goto cleanup_instance;
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);

    /* 6. Open an Intuition screen and window, create Vulkan surface */
    struct Screen *screen = IIntuition->LockPubScreen(NULL);
    struct Window *window = IIntuition->OpenWindowTags(NULL,
        WA_Title,       "Vulkan Triangle",
        WA_Width,       800,
        WA_Height,      600,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   screen,
        TAG_DONE);

    VkSurfaceKHR surface;
    VkAmigaSurfaceCreateInfoAMIGA surfaceCI = {
        .sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO,
        .pScreen = screen,
        .pWindow = window
    };
    vkCreateAmigaSurfaceAMIGA(instance, &surfaceCI, NULL, &surface);

    /* 7. Create swapchain, pipeline, command buffers, and render loop
    **    (abbreviated - follows standard Vulkan 1.3 patterns)
    */

    /* ... swapchain creation ... */
    /* ... shader module creation from SPIR-V ... */
    /* ... graphics pipeline creation with VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO ... */
    /* ... command buffer recording with vkCmdBeginRendering/vkCmdEndRendering ... */
    /* ... main loop: acquire image, record commands, submit, present ... */

    /* 8. Cleanup (reverse order) */
    vkDeviceWaitIdle(device);
    /* ... destroy all Vulkan objects ... */
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);

    IIntuition->CloseWindow(window);
    IIntuition->UnlockPubScreen(NULL, screen);

cleanup_instance:
    vkDestroyInstance(instance, NULL);

cleanup:
    if (IVulkan) IExec->DropInterface((struct Interface *)IVulkan);
    if (VulkanBase) IExec->CloseLibrary(VulkanBase);
    return 0;
}
```

---

## 9. Comparison: Warp3D vs Vulkan API

> For the full compatibility wrapper architecture enabling Warp3D, Warp3D Nova,
> OpenGL ES 2.0, MiniGL, and other APIs to run on top of Vulkan, see
> **Section 15: Compatibility Layer Architecture**.

| Feature | Warp3D | Vulkan 1.3 for AmigaOS |
|---------|--------|----------------------|
| API Level | Fixed-function (OpenGL 1.x era) | Programmable shaders, compute |
| Shaders | None (fixed pipeline) | SPIR-V (vertex, fragment, compute, geometry, tessellation) |
| Pipeline | Implicit state machine | Explicit pipeline state objects |
| Command Buffers | Immediate mode | Recorded, reusable command buffers |
| Memory Management | Automatic texture management | Explicit memory allocation/binding |
| Multithreading | Single-threaded | Multi-queue, multi-threaded recording |
| Synchronization | W3D_LockHardware/W3D_WaitIdle | Fences, semaphores, events, barriers |
| Render Targets | Single bitmap/texture | Framebuffers, dynamic rendering, MRT |
| Texture Formats | 14 formats | 100+ formats including compressed (BC, ASTC) |
| Compute | Not supported | Full compute shaders with shared memory |
| Draw Calls | Per-primitive or vertex arrays | Indexed, instanced, indirect, multi-draw |
| Validation | Runtime checks in driver | Separable validation layers |
| Extensions | Version-based interface | Named extensions with feature bits |
| Functions | ~80 | ~400 core |

---

## 10. Implementation Roadmap

Three parallel work streams feed into a unified timeline. The software ICD
(Stream A) enables early validation and wrapper development, while the
hardware ICD (Stream B) targets GPU-accelerated rendering. Wrappers
(Stream C) provide backward compatibility with the existing API ecosystem.

### Stream A: Software ICD (`software_vk.library`) -- see Section 16

**Phase A0: Minimum viable (triangle test)**
- Implement ~50-60 Vulkan functions (Section 16.8 Phase A)
- SPIR-V interpreter with ~35 opcodes (Section 16.4)
- Software rasteriser: triangle-only (Section 16.5)
- WSI presentation via `WritePixelArray` to Intuition window (Section 16.6)
- Goal: solid-colour triangle rendered through the full Vulkan API path

**Phase A1: Textured rendering**
- Add texture sampling, image views, samplers
- Add indexed drawing (`vkCmdDrawIndexed`)
- Expand SPIR-V interpreter to ~60 opcodes
- Goal: render the triangle example from Section 8

**Phase A2: JIT acceleration (optional)**
- Integrate SLJIT or GNU Lightning (Section 16.11)
- JIT-compile vertex and fragment shaders to native PPC code
- 10-50x speedup over interpreter
- AltiVec vec4 path for X1000 (optional, Section 16.11 Stage 3)

### Stream B: Hardware ICD (`radeon_vk.library`)

**Phase B0: Foundation (loader + headers + SDK) -- see Section 17**
- Generate complete Vulkan 1.3 SDK headers (Section 17.2 - 17.5)
- Build `libvulkan_loader.a` link library (Section 17.6)
- Implement `vulkan.library` loader
- Implement ICD discovery and loading mechanism (JSON manifest parsing)
- Implement `VK_AMIGA_surface` extension in loader (Section 14.4)
- Bundle a minimal JSON parser (e.g., cJSON) in the loader
- Build `vulkaninfo` utility (Section 17.7)
- Create working example programs with Makefiles (Section 17.8)
- Validate loader against `software_vk.library` from Stream A

**Phase B1: Radeon ICD bring-up**
- PCIe device enumeration and GPU initialisation via `expansion.library`
  (Section 14.5)
- VRAM memory manager (buddy allocator for device-local memory)
- Command ring buffer setup (GFX ring for graphics, compute ring)
- Basic command buffer building (PM4 packet format for GCN)
- PPC cache coherency for DMA (Section 13.5)
- Write-combining for BAR access (Section 13.6)
- Basic presentation: zero-copy GPU scanout (Section 13.4)

**Phase B2: Radeon ICD rendering pipeline**
- SPIR-V to GCN ISA compiler (port ACO backend)
- Pipeline cache with disk serialisation (Section 13.7)
- Graphics pipeline creation (VS + PS, rasteriser state)
- Vertex buffer binding and index buffer support
- Basic 2D/3D rendering (triangles, indexed draws)
- Texture sampling (image views, samplers)
- Descriptor sets and push constants
- Endianness handling: MC_SWAP + lwbrx/stwbrx (Section 14.6)

**Phase B3: Radeon ICD completeness**
- Compute pipelines and dispatch
- All remaining Vulkan 1.3 core functions
- Synchronisation primitives (timeline semaphores, etc.)
- Multi-threaded command buffer recording (Section 13.8)
- Synchronisation: memory-mapped fences, hybrid poll/interrupt (Section 13.10)
- Performance optimisation and testing
- Validation layer port (`DEVS:Vulkan/layers.d/`)

### Stream C: Compatibility Wrappers -- see Section 15

**Phase C0: Warp3D Nova -> Vulkan (P1 priority)**
- SPIR-V pass-through wrapper (Section 15.2)
- State hashing + pipeline caching for mutable render state
- Goal: existing W3DN applications (Quake III, OpenJK) run on Vulkan

**Phase C1: OpenGL ES 2.0 -> Vulkan (P1 priority)**
- Retarget ogles2.library to emit Vulkan commands (Section 15.3)
- Reuse existing GLSL-to-SPIR-V compiler from ogles2
- Goal: existing ogles2 applications run on Vulkan

**Phase C2: Legacy + 2D wrappers (P2-P3)**
- Warp3D Classic -> Vulkan via fixed-function shader generation (Section 15.4)
- MiniGL -> Vulkan (Section 15.5)
- Warp2D -> Vulkan (Section 15.6)
- SDL2 Vulkan backend (Section 15.7)

**Phase C3: Extended ecosystem (P4-P5)**
- Cairo Vulkan backend (Section 15.7)
- NanoVG / Dear ImGui with AmigaOS platform backends (Section 15.7)
- Offline AOT shader compilation SDK tool (Section 13.7)

### Stream D: SDK and Ecosystem -- see Section 17

- Complete SDK header generation pipeline (Section 17.1 - 17.5)
- Link library: `libvulkan_loader.a` (Section 17.6)
- Developer utility: `vulkaninfo` (Section 17.7)
- Working example programs with Makefiles (Section 17.8)
- AmigaOS-style AutoDocs for all Vulkan functions
- Port Vulkan Memory Allocator (VMA) helper library
- Port SPIRV-Cross for shader reflection
- Include glslang/spirv-tools in SDK for shader compilation
- SPIR-V toolchain: spirv-opt, spirv-val, spirv-dis (Section 15.8)
- Vulkan CTS conformance testing (Section 14.9)
- Project Makefile template for new Vulkan applications

### Timeline Dependencies

```
Stream A (Software ICD)     Stream B (Hardware ICD)     Stream C (Wrappers)
-------------------------   --------------------------  --------------------
Phase A0 --------------+
  (triangle test)      |
                       +--> Phase B0 (loader)
Phase A1 --------------+      |
  (textures)           |      v
                       |    Phase B1 (GPU bring-up)
                       |      |
                       |      v
                       +--> Phase B2 (rendering) ------> Phase C0 (W3DN)
Phase A2 --------------+      |                          Phase C1 (ogles2)
  (JIT, optional)      |      v
                       |    Phase B3 (complete) -------> Phase C2 (legacy)
                       |                                 Phase C3 (ecosystem)
                       |
                       +--> Stream D (SDK tools, at any time)
```

Software ICD (Stream A) can begin immediately and validates the loader
before any GPU hardware work starts. Wrapper development (Stream C) can
begin against `software_vk.library` and switch to `radeon_vk.library`
once it is ready.

---

## 11. RX580 (Polaris/GCN 4.0) Vulkan Feature Support

The RX580 hardware can support these Vulkan 1.3 features:

**Fully Supported:**
- All shader stages (vertex, fragment, geometry, tessellation, compute)
- 36 compute units, 2304 stream processors
- 64-wide wavefronts (GCN native; maps to Vulkan subgroup size of 64)
- Up to 8GB VRAM
- 256-bit memory bus
- PCIe 3.0 x16
- Multiple hardware queues (graphics, compute, DMA/copy)
- Full 32-bit and 16-bit floating point
- All standard texture formats including BC compression
- Geometry and tessellation shaders
- Indirect rendering and multi-draw-indirect
- Sparse resources (virtual textures)
- Timeline semaphores (implemented in driver)

**Hardware Limits (representative):**
- Max image dimension: 16384
- Max framebuffer size: 16384 x 16384
- Max viewports: 16
- Max bound descriptor sets: 32
- Max push constant size: 256 bytes
- Max compute workgroup size: 1024
- Max compute shared memory: 64KB per workgroup
- Max color attachments: 8

These capabilities fully satisfy Vulkan 1.3 core requirements.

---

## 12. Key Design Decisions & Rationale

**1. Why a separate vulkan.library instead of extending Warp3D?**
- Vulkan's API design is fundamentally different (explicit vs implicit)
- Warp3D's interface would need to be completely rewritten
- Clean separation allows both to coexist for backward compatibility
- Vulkan's loader/ICD model doesn't fit Warp3D's monolithic design

**2. Why include WSI functions in the main interface?**
- AmigaOS convention: all library functions in one interface struct
- Avoids complexity of separate extension interface libraries
- vkGetInstanceProcAddr still works for additional/future extensions
- Platform has exactly one WSI implementation (Intuition)
- Note: surface creation (`vkCreateAmigaSurfaceAMIGA`) is implemented by the
  **loader**, not forwarded to the ICD. Swapchain functions are device-level
  and forwarded to the ICD. See Section 14.4 for the full WSI split.

**3. Why use JSON manifests for ICD discovery?**
- Consistent with Vulkan loader conventions on all platforms
- Allows multiple ICDs (future: Intel, Nvidia if ever on AmigaOS)
- Simple, no binary format to maintain
- Development override via environment variable

**4. Why target Vulkan 1.3 specifically?**
- 1.3 promoted many critical extensions to core (dynamic rendering,
  synchronization2, extended dynamic state) reducing extension burden
- RX580 hardware supports all required features
- 1.3 is the current practical baseline for modern Vulkan applications
- Easier to port existing Vulkan code targeting 1.3

**5. Non-dispatchable handles as uint64_t**
- Standard Vulkan convention for 64-bit-capable platforms
- AmigaOS 4 on PPC is 32-bit, but uint64_t handles are fine
- Avoids ABI issues if AmigaOS ever moves to 64-bit

**6. JSON parsing for ICD/layer discovery**
- The loader must parse JSON manifest files in `DEVS:Vulkan/icd.d/`
- AmigaOS has no built-in JSON parser
- Bundle a minimal JSON parser (cJSON, MIT license, ~1500 lines of C)
  directly in the loader -- no external dependency
- JSON is only parsed once at `vkCreateInstance` time; not performance-critical

**7. VkAllocationCallbacks support**
- Vulkan allows applications to provide custom allocators via the
  `VkAllocationCallbacks` parameter on create/destroy functions
- The loader and ICD SHOULD honour these callbacks when provided, using
  `pfnAllocation` / `pfnFree` instead of `AllocVecTags` / `FreeVec`
- If NULL is passed (the common case), use AmigaOS default allocation
- Custom allocators are important for memory tracking, leak detection,
  and applications that manage their own memory pools

**8. Error handling and graceful degradation**
- If no ICD is found: `vkCreateInstance` returns `VK_ERROR_INCOMPATIBLE_DRIVER`
  and logs "No Vulkan ICD found in DEVS:Vulkan/icd.d/" via serial debug
- If ICD loads but GPU init fails: `vkEnumeratePhysicalDevices` returns 0
  devices; application sees no GPU but does not crash
- If GPU hangs during rendering: ICD returns `VK_ERROR_DEVICE_LOST`; the
  device becomes unusable but the instance and other devices remain valid
- On systems without PCIe (Sam440): only `software_vk.library` is available;
  `vkEnumeratePhysicalDevices` returns 1 CPU device

**9. VulkanIFace versioning for future Vulkan versions**
- The VulkanIFace struct is versioned using the AmigaOS interface versioning
  system: `GetInterface(VulkanBase, "main", version, NULL)`
- Version 1: Vulkan 1.3 core (~400 functions)
- Version 2 (future): would add Vulkan 1.4+ functions at the end of the struct
- Applications requesting version 1 always work, even if version 2 is installed
- New functions are always appended to the end of the interface struct -- never
  inserted or reordered

**10. Loader thread safety**
- The loader must be thread-safe for instance-level operations
- Multiple threads may call `vkEnumeratePhysicalDevices` concurrently
- Use AmigaOS `IExec->ObtainSemaphore()` / `ReleaseSemaphore()` to protect
  loader-internal data structures (ICD list, physical device wrappers)
- Device-level dispatch is inherently thread-safe (dispatch table pointer
  in the handle is immutable after creation)

---

## 13. Performance Architecture

### 13.1 Design Principles

The Vulkan API is inherently GPU-first: pre-compiled pipelines, recorded command
buffers, explicit memory management, and separable validation layers. This design
preserves those properties and adds AmigaOS-specific optimisations to minimise
CPU overhead on PowerPC hardware, which runs at 1.2-2.0 GHz across AmigaOS 4
platforms -- an order of magnitude slower per-core than modern x86.

Every design decision in this section follows one rule: **if the GPU can do it,
the CPU must not**.

### 13.2 Target Hardware Landscape

The ICD and loader must account for the full range of AmigaOS 4 PPC systems.
Not all are realistic Vulkan targets, but the loader must run everywhere and
fail gracefully where GPU hardware is absent.

| Platform | CPU | Clock | Cores | AltiVec | PCIe | Vulkan Viable |
|----------|-----|-------|-------|---------|------|---------------|
| Sam440ep | PPC 440EP | 667 MHz | 1 | No | None (PCI) | No |
| Sam460ex | PPC 460EX | 1.15 GHz | 1 | No | 1x PCIe 1.0 | Marginal |
| AmigaOne X1000 | PA6T-1682M | 1.8 GHz | 2 | Yes (VMX) | x16 PCIe 1.1 | Yes |
| AmigaOne X5000/20 | P5020 (e5500) | 2.0 GHz | 2 | No | x4 PCIe 2.0 | Yes (primary target) |
| AmigaOne X5000/40 | P5040 (e5500) | 2.0 GHz | 4 | No | x4 PCIe 2.0 | Yes (best overall) |
| A1222 (Tabor) | P1022 (e500v2) | 1.2 GHz | 2 | No | x1 PCIe 1.0 | Limited |

**PCIe bandwidth implications:**

| Config | Theoretical Max | Practical Impact |
|--------|----------------|------------------|
| PCIe 1.0 x1 (A1222) | 250 MB/s | Texture uploads and readbacks severely constrained. VRAM-resident workloads only. |
| PCIe 2.0 x4 (X5000) | 2 GB/s | Adequate for most workloads. Streaming large textures may bottleneck. |
| PCIe 1.1 x16 (X1000) | 4 GB/s | No practical bandwidth constraint for Vulkan workloads. |

The ICD should detect the PCIe link speed/width at init time and adjust
behaviour accordingly (e.g., prefer smaller texture formats on bandwidth-
constrained links, warn applications via validation layers).

### 13.3 Loader Dispatch -- Zero Overhead for Single ICD

The standard AmigaOS call path through inline macros is:

```
App -> inline macro -> IVulkan vtable -> loader function -> ICD function
```

On PPC, each indirect branch can cost 10-20 cycles due to branch misprediction
and instruction cache miss. With ~2000 `vkCmd*` calls per frame, a loader
trampoline adds 20,000-40,000 wasted cycles per frame.

**Required optimisation: direct vtable population.**

When only one ICD is loaded (the only realistic AmigaOS scenario), the loader
MUST populate the `VulkanIFace` vtable with **direct pointers to the ICD's
device-level functions**. The call path becomes:

```
App -> inline macro -> IVulkan vtable -> ICD function (direct, single indirection)
```

This is identical to the overhead of any AmigaOS library call. The loader adds
zero dispatch cost.

Implementation in the loader:

```c
/* During ICD loading, after obtaining ICD function pointers: */
static void PopulateDirectDispatch(struct VulkanIFace *iface,
                                   PFN_vk_icdGetInstanceProcAddr icdGetProc,
                                   VkInstance icdInstance)
{
    /* Device-level hot-path functions: point directly to ICD */
    iface->vkCmdDraw = (void *)icdGetProc(icdInstance, "vkCmdDraw");
    iface->vkCmdDrawIndexed = (void *)icdGetProc(icdInstance, "vkCmdDrawIndexed");
    iface->vkCmdBindPipeline = (void *)icdGetProc(icdInstance, "vkCmdBindPipeline");
    iface->vkCmdSetViewport = (void *)icdGetProc(icdInstance, "vkCmdSetViewport");
    iface->vkCmdSetScissor = (void *)icdGetProc(icdInstance, "vkCmdSetScissor");
    iface->vkCmdBindVertexBuffers = (void *)icdGetProc(icdInstance, "vkCmdBindVertexBuffers");
    iface->vkCmdBindIndexBuffer = (void *)icdGetProc(icdInstance, "vkCmdBindIndexBuffer");
    iface->vkCmdBindDescriptorSets = (void *)icdGetProc(icdInstance, "vkCmdBindDescriptorSets");
    iface->vkCmdPushConstants = (void *)icdGetProc(icdInstance, "vkCmdPushConstants");
    iface->vkCmdDispatch = (void *)icdGetProc(icdInstance, "vkCmdDispatch");
    iface->vkCmdPipelineBarrier = (void *)icdGetProc(icdInstance, "vkCmdPipelineBarrier");
    iface->vkCmdBeginRendering = (void *)icdGetProc(icdInstance, "vkCmdBeginRendering");
    iface->vkCmdEndRendering = (void *)icdGetProc(icdInstance, "vkCmdEndRendering");
    iface->vkCmdCopyBuffer = (void *)icdGetProc(icdInstance, "vkCmdCopyBuffer");
    iface->vkCmdCopyImage = (void *)icdGetProc(icdInstance, "vkCmdCopyImage");
    iface->vkCmdBlitImage = (void *)icdGetProc(icdInstance, "vkCmdBlitImage");
    /* ... all ~150 vkCmd* and device-level functions ... */

    /* Instance-level functions remain as loader trampolines */
    /* iface->vkEnumeratePhysicalDevices = LoaderTrampoline_EnumPhysDevs; */
    /* iface->vkCreateDevice = LoaderTrampoline_CreateDevice; */
}
```

If a future multi-ICD scenario arises, the loader falls back to trampoline
functions that read the dispatch table from the handle's first pointer. But
this path is never taken on current hardware.

**High-performance application path:**

For maximum speed in tight render loops, applications should use
`vkGetDeviceProcAddr` to obtain function pointers that bypass the VulkanIFace
vtable entirely:

```c
/* One-time setup after device creation */
PFN_vkCmdDraw pfnCmdDraw =
    (PFN_vkCmdDraw)vkGetDeviceProcAddr(device, "vkCmdDraw");
PFN_vkCmdBindPipeline pfnCmdBindPipeline =
    (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(device, "vkCmdBindPipeline");

/* In render loop -- direct call, zero loader overhead */
pfnCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
pfnCmdDraw(cmdBuf, vertexCount, 1, 0, 0);
```

The loader's `vkGetDeviceProcAddr` implementation MUST return the ICD's
function pointer directly, not a loader wrapper.

### 13.4 Zero-Copy Presentation

**This is the single most important performance requirement.**

The ICD MUST program the GPU's display controller (CRTC / display engine) to
scan out directly from the swapchain image in VRAM. The presentation path must
be:

```
GPU renders to VRAM surface
  -> GPU display controller scans VRAM surface to DAC/TMDS
    -> Signal appears on monitor

CPU involvement: ZERO (after initial display mode setup)
```

The ICD MUST NOT:
- Copy rendered pixels from VRAM to system memory
- Copy pixels into an Intuition bitmap via the CPU
- Use the CPU to composite or convert the final image
- Rely on graphics.library's software rendering path for presentation

At 800x600x32bpp @ 60fps, CPU-side copying requires 115 MB/s of memory
bandwidth. On the A1222 (P1022 at 1.2 GHz), this alone would consume a
significant fraction of available memory bandwidth and CPU time. At 1920x1080,
it rises to 497 MB/s -- which exceeds the A1222's PCIe link speed entirely.

**Implementation approach:**

The ICD manages the display output directly:
1. At swapchain creation, allocate N framebuffers in VRAM
2. GPU renders to the current back buffer
3. On `vkQueuePresentKHR`, program the display controller's primary plane to
   scan from the completed buffer's VRAM address (page flip)
4. The display controller reads pixels from VRAM autonomously via the GPU's
   internal memory bus -- no PCIe or CPU involvement

For windowed mode (rendering into an Intuition window), the ICD has two options:
- **Overlay plane**: Use the GPU's hardware overlay/sprite to display the Vulkan
  surface over the Intuition screen. No CPU compositing. Preferred.
- **GPU blit to screen buffer**: If the display hardware supports it, use the
  GPU's 2D blit engine to composite the Vulkan surface into the screen's
  framebuffer. Still GPU-only, no CPU.

CPU-based compositing (ReadPixels from VRAM to system RAM, write to Intuition
bitmap) is acceptable ONLY as a debug/fallback path and must be clearly flagged
as slow.

### 13.5 PPC Cache Coherency for DMA

PowerPC CPUs do not maintain hardware coherency between the CPU's data cache
and DMA transfers. On x86, the CPU cache is DMA-coherent and no explicit
management is needed. On PPC, the ICD MUST manage cache coherency manually.
Getting this wrong causes either data corruption or forces unnecessarily slow
uncached memory mappings.

**CPU writes data for GPU to read (command buffers, vertex data uploads):**

```c
/* After CPU writes command buffer data to system memory: */
void FlushCacheForGPU(void *addr, uint32_t size)
{
    uint8_t *p = (uint8_t *)((uintptr_t)addr & ~31);  /* align to cache line */
    uint8_t *end = (uint8_t *)addr + size;
    while (p < end) {
        asm volatile("dcbst 0,%0" : : "r"(p));  /* write dirty line to memory */
        p += 32;  /* cache line size -- varies by CPU, 32 is safe minimum */
    }
    asm volatile("sync");  /* ensure all flushes complete before GPU reads */
}

/* Usage: */
BuildCommandBuffer(cmdBufMem, ...);
FlushCacheForGPU(cmdBufMem, cmdBufSize);
RingDoorbell();  /* now safe for GPU to read */
```

**GPU writes data for CPU to read (query results, timestamps, fences):**

```c
/* Before CPU reads data that GPU has written via DMA: */
void InvalidateCacheFromGPU(void *addr, uint32_t size)
{
    uint8_t *p = (uint8_t *)((uintptr_t)addr & ~31);
    uint8_t *end = (uint8_t *)addr + size;
    asm volatile("sync");  /* ensure prior GPU writes are visible */
    while (p < end) {
        asm volatile("dcbi 0,%0" : : "r"(p));  /* invalidate cache line */
        p += 32;
    }
    asm volatile("sync");
}

/* Usage: */
WaitForGPUFence();
InvalidateCacheFromGPU(queryResultMem, resultSize);
uint64_t result = *(volatile uint64_t *)queryResultMem;  /* now reads fresh data */
```

**Cache line sizes by platform:**

| CPU | L1 D-cache line | Safe to use |
|-----|----------------|-------------|
| PPC 440EP (Sam440) | 32 bytes | 32 bytes |
| PPC 460EX (Sam460) | 32 bytes | 32 bytes |
| PA6T (X1000) | 64 bytes | 32 bytes (conservative) |
| e5500 (X5000) | 64 bytes | 32 bytes (conservative) |
| e500v2 (A1222) | 32 bytes | 32 bytes |

The ICD should query the cache line size at init time (from the PVR or device
tree) and use the actual value for optimal performance. Using 32 bytes as a
minimum is safe but may issue twice the necessary cache management instructions
on 64-byte-line CPUs.

**Critical rule:** Never use uncached (I=1) mappings for command buffer memory
or staging buffers as a shortcut to avoid cache management. Uncached reads on
PPC are 10-50x slower than cached reads. Correct cache management with cacheable
memory is always faster.

### 13.6 Write-Combining for PCIe BAR Access

When the CPU writes to GPU registers or VRAM through the PCIe BAR, each store
instruction generates a separate PCIe write transaction. PCIe write transactions
have ~100ns of per-transaction overhead due to packet framing, credit flow
control, and bus arbitration.

Writing a 64-byte command packet as 16 individual 4-byte stores:
  16 transactions x ~100ns = ~1600ns

With write-combining, the CPU's write buffer coalesces consecutive stores to
the same cache line into a single PCIe burst transaction:
  1 transaction x ~100ns = ~100ns (**16x faster**)

**PPC WIMG bit configuration for BAR mappings:**

| Memory Region | W | I | M | G | Purpose |
|--------------|---|---|---|---|---------|
| GPU MMIO registers | 0 | 1 | 0 | 1 | Caching-inhibited, guarded. Each store goes immediately to device. Required for register reads that have side effects. |
| Doorbell registers | 0 | 1 | 0 | 1 | Same as MMIO. Single 4-byte write triggers GPU action. |
| BAR aperture (VRAM write) | 1 | 0 | 0 | 0 | Write-through, cacheable. Allows burst writes. Used for command buffer submission and data uploads through BAR. |
| BAR aperture (VRAM read) | 0 | 1 | 0 | 0 | Caching-inhibited. Reads from VRAM through BAR should not be cached (data may change via GPU writes). |

**Note:** Write-combining behaviour on PPC is achieved differently from x86.
PPC does not have a WC memory type; instead, write-through (W=1) combined with
cacheable (I=0) allows the cache to coalesce stores. The exact behaviour depends
on the CPU implementation:
- **PA6T (X1000):** Supports store gathering in write-through mode
- **e5500 (X5000):** Supports store gathering in the L2 cache
- **e500v2 (A1222):** Limited store gathering; may require explicit use of
  `dcbz` to allocate full cache lines before writing

The ICD must configure BAR mappings via `expansion.library` with the appropriate
memory attributes. If the platform's MMU setup does not expose WIMG control, the
ICD should fall back to caching-inhibited (safe but slower) and document the
performance impact.

### 13.7 SPIR-V Compilation Strategy

Porting the Mesa ACO compiler to PPC means running a complex optimising compiler
on a 1.2-2.0 GHz CPU. Compilation times per shader stage will be 5-10x longer
than on x86. A typical game shader pipeline (vertex + fragment) may take
200-1000ms to compile. Applications with hundreds of pipeline permutations face
multi-minute load times.

**Required mitigations (ICD must implement all of these):**

**1. Pipeline cache serialisation to disk**

`vkCreatePipelineCache` / `vkGetPipelineCacheData` must be fully implemented.
The ICD must store compiled GCN/RDNA ISA binaries in the cache, keyed by
SPIR-V hash + pipeline state hash. Applications that serialise the cache to
disk (standard Vulkan practice) will only compile each shader once, ever.

```c
/* Application pattern the ICD must support efficiently: */
/* Load cache from disk at startup */
FILE *f = fopen("PROGDIR:pipeline.cache", "rb");
size_t cacheSize = fread(cacheData, 1, maxSize, f);
VkPipelineCacheCreateInfo cacheCI = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    .initialDataSize = cacheSize,
    .pInitialData = cacheData
};
vkCreatePipelineCache(device, &cacheCI, NULL, &cache);
/* All subsequent pipeline creates check cache first -- cache hit = ~0ms */
```

**2. Background compilation**

The ICD should compile shaders on a background thread when possible. On
multi-core systems (X1000: 2 cores, X5000/40: 4 cores), shader compilation
should not block the main thread. `vkCreateGraphicsPipelines` may return
`VK_PIPELINE_COMPILE_REQUIRED` (Vulkan 1.3) to indicate deferred compilation
when `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` is set.

**3. Offline / AOT compilation SDK tool**

Include a cross-compilation tool in the SDK that runs on the developer's x86
host:

```bash
# On developer's x86 machine (fast):
amivk-compile --target=gcn4 --spirv=shader.vert.spv --output=shader.vert.gcn
amivk-compile --target=gcn4 --spirv=shader.frag.spv --output=shader.frag.gcn

# Ship pre-compiled .gcn files with the application
# ICD detects pre-compiled binary in pipeline cache -- zero compilation on PPC
```

This moves the expensive compilation entirely off the PPC CPU. The SDK tool
runs the same ACO backend but on an x86 host at 5 GHz instead of a PPC at
1.8 GHz.

**4. Shader cache directory**

The ICD should maintain a system-wide shader cache at `ENVARC:Vulkan/shadercache/`
that persists across application runs and across applications. Identical shaders
used by different programs are compiled only once.

### 13.8 Multi-Threaded Command Recording

AmigaOS 4 platforms have 1-4 CPU cores. Vulkan's design allows recording command
buffers from multiple threads simultaneously -- this is one of Vulkan's key
advantages over OpenGL's single-threaded model.

**ICD threading requirements:**

1. `vkAllocateCommandBuffers` from different `VkCommandPool` objects must be
   safe to call concurrently from different threads. Each pool is single-threaded
   (per Vulkan spec), but different pools on different threads must not contend.

2. `vkBeginCommandBuffer` / `vkCmdDraw` / `vkEndCommandBuffer` on command
   buffers from different pools must be fully parallel with no shared locks.

3. `vkQueueSubmit` requires external synchronisation per the Vulkan spec (the
   application must not submit to the same queue from multiple threads). The ICD
   does not need to make queue submission thread-safe, but the submit path
   itself must be fast (write ring buffer entries + doorbell write).

**ICD implementation guidance:**

- Command buffer memory should be allocated from per-pool arenas, not a global
  allocator with a mutex. This eliminates contention between threads recording
  to different pools.
- PM4 packet building should be stateless with respect to other command buffers.
  No global state that requires locking during recording.
- Descriptor set binding state should be stored per-command-buffer, not globally.

**Performance impact by platform:**

| Platform | Cores | Expected Benefit |
|----------|-------|-----------------|
| Sam460ex | 1 | None -- single core, threads add overhead |
| X1000 | 2 | Up to 1.8x speedup for CPU-bound recording |
| X5000/20 | 2 | Up to 1.8x speedup |
| X5000/40 | 4 | Up to 3.2x speedup for heavily CPU-bound scenes |
| A1222 | 2 | Up to 1.6x speedup (slower cores, less headroom) |

### 13.9 GPU-Offloaded Operations

The following operations MUST be implemented as GPU commands, never as CPU
fallbacks. On PPC at 1.2-2.0 GHz, CPU fallbacks for graphics operations are
orders of magnitude slower than GPU execution.

| Operation | GPU Path (required) | CPU Fallback (forbidden in production) |
|-----------|-------------------|--------------------------------------|
| Buffer-to-image copies | DMA/copy engine (`vkCmdCopyBufferToImage`) | memcpy through CPU |
| Image layout transitions | Pipeline barriers with GPU-side cache flushes | CPU-side pixel reformatting |
| Image blits / scaling | GPU 2D blit engine or compute shader | CPU-side bilinear filter |
| Mipmap generation | GPU blit chain or compute shader | CPU-side downsampling |
| Buffer clears | GPU DMA fill (`vkCmdFillBuffer`) | CPU memset |
| Image clears | GPU fast-clear or render target clear | CPU pixel fill |
| Timestamp queries | GPU-side timestamp writes | Reading CPU clock |
| Occlusion queries | GPU rasteriser pixel counting | CPU-side (impossible to replicate) |

The GPU's DMA/copy engine can operate concurrently with the 3D engine on GCN
hardware (separate GFX, compute, and DMA rings). The ICD should use the DMA
engine for transfer operations when the graphics engine is busy rendering.

### 13.10 Synchronisation Without CPU Stalls

Vulkan provides multiple synchronisation primitives. The ICD must implement
them to minimise CPU idle time:

**GPU-to-GPU synchronisation (fences within command buffers):**
- Use GPU-side semaphores and pipeline barriers
- CPU is not involved at all
- No performance concern

**GPU-to-CPU synchronisation (application waits for GPU):**

`vkWaitForFences` is the primary GPU-to-CPU sync point. Implementation options,
in order of performance:

1. **Memory-mapped fence register** (best): The GPU writes a monotonically
   increasing value to a system memory location when work completes. The CPU
   polls this memory location (with cache invalidation). No kernel/interrupt
   involvement for short waits.

2. **Hybrid poll-then-interrupt**: Poll the fence memory location for a short
   duration (1-10 us). If the fence is not signalled, fall back to waiting on
   an AmigaOS signal via `AddIntServer` + `IExec->Wait()`. This avoids
   interrupt overhead for the common case where the GPU finishes quickly.

3. **Interrupt-only** (acceptable): Use `AddIntServer` to register a GPU
   completion interrupt handler that signals an AmigaOS task. The application
   thread calls `IExec->Wait()`. Higher latency than polling but does not
   waste CPU cycles.

The ICD MUST NOT busy-wait in a tight loop without yielding, as this wastes
CPU time on a system with only 1-4 cores.

---

## 14. ICD Developer Guide

This section is for GPU driver developers writing a Vulkan ICD for AmigaOS 4.
It documents everything needed to create a conformant, high-performance ICD
for any Vulkan 1.3-capable GPU, independent of GPU vendor.

### 14.1 ICD Library Structure

The ICD is a standard AmigaOS shared library with a "main" interface. The
library is installed to `LIBS:Vulkan/` and registered via a JSON manifest in
`DEVS:Vulkan/icd.d/`.

**Required exports (via the "main" interface):**

| Method | Purpose |
|--------|---------|
| `Obtain` / `Release` | Standard AmigaOS reference counting |
| `Expunge` / `Clone` | Standard (may be unimplemented stubs) |
| `vk_icdGetInstanceProcAddr` | Loader calls this to resolve ALL Vulkan functions |
| `vk_icdNegotiateLoaderICDInterfaceVersion` | Negotiate loader protocol version |
| `vk_icdGetPhysicalDeviceProcAddr` | (Optional) Physical-device-level dispatch |

The ICD's `vulkan.xml` interface definition:

```xml
<library name="<vendor>_vk" basename="<Vendor>VkBase"
         openname="<vendor>_vk.library">
    <include>exec/types.h</include>
    <include>vulkan/vulkan.h</include>
    <include>vulkan/vk_icd.h</include>

    <interface name="main" version="1.0" struct="<Vendor>VkICDIFace"
               prefix="_<Vendor>Vk_" asmprefix="I<Vendor>Vk"
               global="I<Vendor>Vk">

        <method name="Obtain" result="ULONG"/>
        <method name="Release" result="ULONG"/>
        <method name="Expunge" result="void" status="unimplemented"/>
        <method name="Clone" result="struct Interface *" status="unimplemented"/>

        <method name="vk_icdGetInstanceProcAddr" result="PFN_vkVoidFunction">
            <arg name="instance" type="VkInstance"/>
            <arg name="pName" type="const char *"/>
        </method>
        <method name="vk_icdNegotiateLoaderICDInterfaceVersion" result="VkResult">
            <arg name="pSupportedVersion" type="uint32_t *"/>
        </method>
        <method name="vk_icdGetPhysicalDeviceProcAddr" result="PFN_vkVoidFunction">
            <arg name="instance" type="VkInstance"/>
            <arg name="pName" type="const char *"/>
        </method>
    </interface>
</library>
```

### 14.2 Dispatchable Handle Layout (Critical for Performance)

For the loader to route device-level calls to the correct ICD with zero
overhead, the ICD MUST store a pointer to its internal dispatch table as the
**first `sizeof(void*)` bytes** of every dispatchable object it creates:

- `VkDevice`
- `VkQueue`
- `VkCommandBuffer`

The loader reads this pointer to jump directly to the ICD's function without
any lookup or hash table.

```c
/*
** Internal ICD structure for a VkDevice.
** The dispatch pointer MUST be the first field.
*/
struct ICD_Device {
    const struct ICD_DispatchTable *dispatch;  /* MUST be first */
    /* ... ICD-private fields follow ... */
    struct PCIeDevice *gpu;
    struct MemoryManager *memMgr;
    struct RingBuffer *gfxRing;
    struct RingBuffer *computeRing;
    struct RingBuffer *dmaRing;
    /* ... */
};

/*
** ICD dispatch table -- contains pointers to all device-level functions.
** The loader dereferences the first pointer in a VkDevice/VkQueue/VkCommandBuffer
** and indexes into this table.
*/
struct ICD_DispatchTable {
    PFN_vkDestroyDevice                 DestroyDevice;
    PFN_vkGetDeviceQueue                GetDeviceQueue;
    PFN_vkQueueSubmit                   QueueSubmit;
    PFN_vkQueueWaitIdle                 QueueWaitIdle;
    PFN_vkDeviceWaitIdle                DeviceWaitIdle;
    PFN_vkAllocateMemory                AllocateMemory;
    PFN_vkFreeMemory                    FreeMemory;
    /* ... all ~300 device-level functions ... */
};

/*
** When the loader calls vkCmdDraw(commandBuffer, ...):
** 1. Read dispatch = *(void **)commandBuffer
** 2. Call dispatch->CmdDraw(commandBuffer, ...)
** This is a single pointer chase -- essentially free.
*/
```

For `VkInstance` and `VkPhysicalDevice`, the loader wraps these in its own
objects (the ICD does not need a dispatch pointer in them). The loader tracks
which ICD owns each physical device internally.

### 14.3 Functions the ICD Must Implement

The ICD returns function pointers for all supported functions via
`vk_icdGetInstanceProcAddr`. These are grouped by level:

**Global functions** (called with `instance = NULL`):

| Function | Notes |
|----------|-------|
| `vkEnumerateInstanceExtensionProperties` | Return ICD's supported instance extensions |
| `vkEnumerateInstanceLayerProperties` | May return empty (layers are loader-managed) |
| `vkEnumerateInstanceVersion` | Return `VK_API_VERSION_1_3` |
| `vkCreateInstance` | Create ICD-internal instance state; enumerate GPU hardware |

**Instance-level functions** (first arg is `VkInstance` or `VkPhysicalDevice`):

| Category | Key Functions |
|----------|---------------|
| Physical device enumeration | `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures`, `vkGetPhysicalDeviceQueueFamilyProperties`, `vkGetPhysicalDeviceMemoryProperties` |
| Format queries | `vkGetPhysicalDeviceFormatProperties`, `vkGetPhysicalDeviceImageFormatProperties` |
| Device creation | `vkCreateDevice` |
| WSI surface queries | `vkGetPhysicalDeviceSurfaceSupportKHR`, `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, `vkGetPhysicalDeviceSurfaceFormatsKHR`, `vkGetPhysicalDeviceSurfacePresentModesKHR` |

**Device-level functions** (~300 functions, first arg is `VkDevice` / `VkQueue` / `VkCommandBuffer`):

All remaining Vulkan 1.3 core functions: memory management, buffer/image
creation, pipeline creation, descriptor sets, command buffer recording, draw
and dispatch commands, synchronisation, queries, and swapchain operations.

The ICD does not need to implement instance-level WSI surface creation
(`vkCreateAmigaSurfaceAMIGA`) -- that is the loader's responsibility.

### 14.4 WSI Responsibilities: Loader vs ICD

The WSI division ensures surfaces work across ICDs while swapchains are
GPU-specific:

**Loader owns (instance-level):**

| Function | Loader Action |
|----------|---------------|
| `vkCreateAmigaSurfaceAMIGA` | Creates a `VkSurfaceKHR` that wraps the Intuition `Screen*` / `Window*` handles. This is a loader-managed object. |
| `vkDestroySurfaceKHR` | Frees the loader's surface wrapper. |
| `vkGetPhysicalDeviceSurfaceSupportKHR` | Unwraps the physical device, calls into the owning ICD's implementation. |
| `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` | Same -- loader terminates into ICD after unwrapping. |
| `vkGetPhysicalDeviceSurfaceFormatsKHR` | Same. |
| `vkGetPhysicalDeviceSurfacePresentModesKHR` | Same. |

**ICD owns (device-level):**

| Function | ICD Action |
|----------|-----------|
| `vkCreateSwapchainKHR` | Allocate VRAM framebuffers, configure display controller for page-flipping. Extract `Screen*`/`Window*` from the loader's `VkSurfaceKHR`. |
| `vkDestroySwapchainKHR` | Free VRAM framebuffers, tear down display state. |
| `vkGetSwapchainImagesKHR` | Return handles to the VRAM framebuffer images. |
| `vkAcquireNextImageKHR` | Return the index of the next available back buffer. Synchronise with display refresh if `VK_PRESENT_MODE_FIFO_KHR`. |
| `vkQueuePresentKHR` | Program GPU display controller to scan from the completed buffer (page flip). See Section 13.4. |

**Surface handle interop:**

The loader stores platform-specific data inside `VkSurfaceKHR`:

```c
/* Loader-internal structure (opaque to ICD) */
struct LoaderSurface {
    struct Screen *screen;
    struct Window *window;   /* NULL for fullscreen */
    uint32_t      width;
    uint32_t      height;
};
```

The loader provides a helper function for ICDs to extract this data:

```c
/* In vk_icd.h -- ICD calls this to get platform surface info */
typedef VkResult (VKAPI_PTR *PFN_vk_icdGetAmigaSurfaceInfo)(
    VkSurfaceKHR surface,
    struct Screen **ppScreen,
    struct Window **ppWindow);
```

### 14.5 PCIe Hardware Access

All GPU hardware access goes through AmigaOS `expansion.library`. The ICD must:

**1. Find the GPU on the PCI bus:**

```c
struct PCIIFace *IPCI = /* obtain from expansion.library */;
struct PCIDevice *dev = IPCI->FindDeviceByClass(
    PCI_CLASS_DISPLAY_VGA,  /* or PCI_CLASS_DISPLAY_3D */
    0);                      /* index -- 0 for first GPU */

/* Check vendor/device ID to confirm this is our GPU */
uint16_t vendorID = IPCI->ReadConfigWord(dev, PCI_VENDOR_ID);
uint16_t deviceID = IPCI->ReadConfigWord(dev, PCI_DEVICE_ID);
```

**2. Map BAR regions:**

```c
/* Map BAR0 (MMIO registers) with caching-inhibited, guarded attributes */
void *mmioBase = IPCI->MapBarRegion(dev, 0,
    PCI_MAP_FLAG_IO | PCI_MAP_FLAG_NOCACHE);

/* Map BAR1 (VRAM aperture) -- see Section 13.6 for WIMG guidance */
void *vramAperture = IPCI->MapBarRegion(dev, 2,
    PCI_MAP_FLAG_MEM);
```

**3. Set up interrupts:**

```c
/* Register interrupt handler for GPU completion signals */
struct Interrupt *gpuInt = IExec->AllocSysObjectTags(ASOT_INTERRUPT,
    ASOINTR_Code, GPUInterruptHandler,
    ASOINTR_Data, icdPrivateData,
    TAG_DONE);

IPCI->AddIntServer(dev, gpuInt);
```

**4. DMA considerations:**

- AmigaOS 4 uses a flat physical address space visible to PCI devices
- System memory allocated with `IExec->AllocMem(size, MEMF_PUBLIC)` is
  DMA-accessible -- no IOMMU translation needed
- The ICD must flush CPU caches before GPU DMA reads (Section 13.5)
- The ICD must invalidate CPU caches before reading GPU DMA writes
- Allocate DMA-able memory in physically contiguous blocks for ring buffers
  and large transfers

### 14.6 Endianness Handling

Endianness byte-swapping is **entirely the ICD's responsibility**. The loader
does not perform any swapping. Different GPU architectures handle this
differently -- there is no universal solution.

**What needs swapping (PPC is big-endian, all modern GPUs are little-endian):**

| Data | Direction | Notes |
|------|-----------|-------|
| GPU register reads/writes | CPU <-> GPU | MMIO registers are little-endian |
| Command buffer packets | CPU -> GPU | GPU command processor reads LE |
| SPIR-V bytecode | CPU -> GPU | SPIR-V spec is LE by definition |
| Vertex/index data | CPU -> GPU | Format-dependent |
| Texture data | CPU -> GPU | Format-dependent |
| Query results | GPU -> CPU | 32/64-bit LE values |
| Fence values | GPU -> CPU | 32/64-bit LE values |

**Strategy options (ICD chooses based on GPU hardware):**

**Option A: Hardware byte-swap (preferred when available)**

Some GPU architectures (AMD GCN/RDNA) support configuring the memory controller
to automatically swap bytes on reads/writes. On GCN, the MC_SWAP register can
be set to swap 32-bit words on the command buffer read path:

```c
/* GCN-specific: configure GPU memory controller for big-endian command reads */
WREG32(mmMC_SWAP_CNTL, MC_SWAP_32BIT);
```

This eliminates ALL CPU-side swapping for command buffer data. The GPU hardware
performs the swap at memory-controller speed (free from the CPU's perspective).

**Option B: Software byte-swap (fallback for GPUs without HW swap support)**

If the GPU has no hardware swap support, the ICD must swap in software:

```c
static inline uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

/* PPC has a byte-reverse load instruction (lwbrx) -- use it */
static inline uint32_t ppc_load_le32(const volatile uint32_t *addr) {
    uint32_t val;
    asm volatile("lwbrx %0, 0, %1" : "=r"(val) : "r"(addr));
    return val;
}

static inline void ppc_store_le32(volatile uint32_t *addr, uint32_t val) {
    asm volatile("stwbrx %0, 0, %1" : : "r"(val), "r"(addr));
}
```

PPC's `lwbrx` / `stwbrx` instructions perform byte-reversed loads/stores in a
single instruction -- much faster than manual shifting. The ICD should use these
for all register access and command buffer writes.

**Option C: Hybrid (common in practice)**

Use hardware swap for bulk data paths (command buffers, DMA) and `lwbrx`/`stwbrx`
for individual register accesses. This is what the existing Radeon Warp3D
driver does.

### 14.7 ICD JSON Manifest

Place the manifest file in `DEVS:Vulkan/icd.d/`:

```json
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "LIBS:Vulkan/<vendor>_vk.library",
        "api_version": "1.3.0",
        "library_version": 1
    }
}
```

The loader reads all `.json` files in `DEVS:Vulkan/icd.d/` at startup and
attempts to open each listed library. If a library fails to open (e.g., the
corresponding GPU is not present), the loader silently skips it.

Development override: set `ENV:VK_ICD_FILENAMES` to a comma-separated list
of manifest paths to force the loader to use specific ICDs.

### 14.8 ICD Checklist for New GPU Vendors

A complete ICD implementation requires these subsystems. Each is entirely
GPU-specific -- nothing is shared between ICDs.

| # | Subsystem | Description | Difficulty |
|---|-----------|-------------|-----------|
| 1 | **PCIe enumeration** | Find GPU on PCI bus, map BARs, configure interrupts via `expansion.library` | Low |
| 2 | **GPU initialisation** | Program GPU power/clock management, enable engines, configure memory controller | High (requires GPU docs) |
| 3 | **Memory manager** | VRAM allocator (buddy/slab), GTT/GART setup, host-visible aperture mapping, memory type reporting | High |
| 4 | **Command buffer builder** | Encode GPU-native commands (PM4 for AMD, pushbuffers for NVIDIA, batch buffers for Intel) | High (GPU-specific format) |
| 5 | **Ring buffer / submission** | Set up hardware command rings, doorbell registers, submission queue management | Medium |
| 6 | **SPIR-V compiler** | Translate SPIR-V intermediate representation to GPU-native ISA. Typically ported from Mesa (ACO for AMD, NIR+codegen for others) | Very High |
| 7 | **Pipeline state** | Compile rasteriser, blend, depth/stencil, and vertex input state into GPU-native state objects | High |
| 8 | **Descriptor management** | Map Vulkan descriptor sets/bindings to GPU-native resource binding model | Medium |
| 9 | **Synchronisation** | Implement fences (GPU writes to memory), semaphores (GPU-to-GPU), events. Handle PPC cache coherency per Section 13.5 | Medium |
| 10 | **Display / scanout** | Program GPU's display controller (CRTC, planes) for zero-copy presentation per Section 13.4 | High (GPU-specific display engine) |
| 11 | **Endianness** | Byte-swap strategy per Section 14.6 | Medium |
| 12 | **Cache coherency** | PPC dcbst/dcbi/sync management per Section 13.5 | Medium (easy to get wrong) |

### 14.9 Testing and Validation

**Vulkan Conformance Test Suite (CTS):**

The Khronos Vulkan CTS (`dEQP-VK`) is the definitive conformance test.
Porting it to AmigaOS requires:
- Cross-compiling dEQP for PPC/AmigaOS
- Adapting the WSI test harness for `VK_AMIGA_surface`
- Running the full test suite (~500,000 tests)

Full CTS conformance is the ultimate goal but not required for initial
bring-up. Prioritise in this order:
1. Instance creation and device enumeration
2. Memory allocation and mapping
3. Command buffer recording and submission
4. Basic rendering (triangle test)
5. Swapchain and presentation
6. Full CTS pass

**Validation layers:**

During development, port `VK_LAYER_KHRONOS_validation` (from the Vulkan SDK)
as an AmigaOS library in `DEVS:Vulkan/layers.d/`. This catches API usage
errors before they become GPU hangs. Validation layers are loader-managed
and have zero overhead when not loaded.

**GPU hang debugging:**

When the GPU hangs (stops responding), the ICD should:
1. Read the GPU's status registers to determine which engine stalled
2. Dump the last submitted command buffer contents to serial debug
3. Report the hang via `VK_ERROR_DEVICE_LOST` (do not crash)
4. Attempt GPU reset if the hardware supports it

---

## 15. Compatibility Layer Architecture

Vulkan as the lowest-level GPU API enables a unified driver stack where all
existing and future graphics APIs are implemented as translation layers on top
of Vulkan. This eliminates the need for each API to have its own GPU-specific
driver and consolidates all hardware-specific code into a single ICD.

### 15.1 Unified Graphics Stack

**Current stack** (each API has its own driver path):

```
ogles2.library    MiniGL    Warp3D (classic)         Warp2D
     |               |           |                      |
     v               v           v                      |
Warp3D Nova  <--- NovaBridge    (legacy driver)         |
     |                                                  |
     v                                                  v
W3DN_SI / W3DN_GCN  (GPU-specific 3D drivers)   Warp3D Nova
     |                                                  |
     v                                                  v
RadeonHD.chip / RadeonRX.chip  (2D driver)     W3DN_SI / W3DN_GCN
     |                                                  |
     v                                                  v
GPU Hardware                                    GPU Hardware
```

**Vulkan-based stack** (single driver, all APIs translate to Vulkan):

```
+----------+ +----------+ +----------+ +--------+ +----------+
| ogles2   | |  MiniGL  | | Warp3D   | | Warp2D | |  SDL2    |
| (GL ES2) | | (OpenGL) | |  Nova    | |  (2D)  | |  Cairo   |
+----+-----+ +----+-----+ +----+-----+ +---+----+ +----+-----+
     |             |            |            |           |
     v             v            v            v           v
+----------+ +----------+ +----------+ +--------+ +----------+
|ogles2_vk | |minigl_vk | |ogles2_vk| |warp2d  | |  sdl2_vk |
| wrapper  | | wrapper  | | wrapper  | |_vk     | | cairo_vk |
+----+-----+ +----+-----+ +----+-----+ +---+----+ +----+-----+
     |             |            |            |           |
     +------+------+-----+------+-----+------+-----------+
            |            |            |
            v            v            v
+---------------------------------------------------------+
|              vulkan.library (Loader)                    |
|                                                         |
|  All API calls translated to Vulkan command buffers     |
|  SPIR-V shaders passed through or generated             |
+------------------------+--------------------------------+
                         |
                         v
+---------------------------------------------------------+
|              radeon_vk.library (ICD)                    |
|  Single GPU driver handles ALL rendering                |
+------------------------+--------------------------------+
                         |
                         v
                    GPU Hardware
```

**Benefits of the unified stack:**

- **One GPU driver instead of many.** Currently, each GPU needs a W3DN_SI
  driver, a W3DN_GCN driver, and a RadeonHD/RadeonRX 2D driver. With Vulkan,
  only the ICD needs to be GPU-specific. All wrappers are GPU-independent.
- **New GPUs get all APIs instantly.** Writing a new Vulkan ICD for a new GPU
  automatically provides Warp3D Nova, OpenGL ES 2.0, MiniGL, Warp2D, SDL2,
  Cairo, and every other wrapper -- for free.
- **Shared shader pipeline.** Warp3D Nova already uses SPIR-V. Vulkan uses
  SPIR-V. No shader format translation is needed between them.
- **Bug fixes in one place.** A rendering bug fixed in the ICD fixes it for
  all APIs simultaneously.

### 15.2 Warp3D Nova -> Vulkan Wrapper

**Feasibility: HIGH. Both APIs use SPIR-V shaders. Direct concept mapping.**

Warp3D Nova is the closest API to Vulkan in the AmigaOS ecosystem. It is a
modern shader-based API with VBOs, DBOs, framebuffers, and programmable
shaders in SPIR-V format. The wrapper translates W3DN concepts to Vulkan
equivalents.

**Concept mapping:**

| Warp3D Nova | Vulkan | Notes |
|-------------|--------|-------|
| `W3DN_Context` | `VkDevice` + `VkQueue` + internal state | W3DN context encapsulates all state |
| `W3DN_FrameBuffer` | `VkFramebuffer` + `VkRenderPass` or dynamic rendering | W3DN FBOs map directly |
| `W3DN_RenderState` (RSO) | `VkPipeline` (graphics) | RSO captures blend, depth, stencil state -> baked into Vulkan pipeline |
| `W3DN_Texture` | `VkImage` + `VkImageView` | Texture formats map with minor translation |
| `W3DN_TexSampler` | `VkSampler` | Direct mapping |
| VBO (`CreateVertexBufferObject`) | `VkBuffer` (vertex) | Direct mapping; endianness handled identically |
| DBO (`CreateDataBufferObject`) | `VkBuffer` (uniform) | Maps to uniform buffer |
| `W3DN_ShaderPipeline` | `VkPipeline` (with `VkShaderModule`) | SPIR-V passes straight through |
| `CompileShader` (SPIR-V -> ISA) | `vkCreateShaderModule` + pipeline creation | **Key advantage: SPIR-V is the input for both** |
| `Submit` | `vkQueueSubmit` | Command buffer built from queued operations |
| `DrawArrays` / `DrawElements` | `vkCmdDraw` / `vkCmdDrawIndexed` | Direct mapping |
| `SetBlendMode` / `SetDepthCompareFunc` | Pipeline state at creation | W3DN sets state per-call; wrapper batches into pipeline objects |
| `W3DN_Q_ENDIANNESS` | ICD handles internally | Vulkan ICD manages endianness; wrapper does not need to |

**SPIR-V pass-through:**

```c
/* W3DN wrapper: CompileShader just wraps SPIR-V in a VkShaderModule */
W3DN_ErrorCode ogles2_vk_CompileShader(W3DN_Context *ctx,
                                        W3DN_Shader *shader,
                                        W3DN_ShaderType type,
                                        const uint32 *spirvCode,
                                        uint32 spirvSize)
{
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirvSize,
        .pCode = spirvCode   /* Pass SPIR-V directly -- no translation */
    };

    VkResult result = vkCreateShaderModule(ctx->vkDevice, &ci, NULL,
                                           &shader->vkModule);

    return (result == VK_SUCCESS) ? W3DNEC_SUCCESS : W3DNEC_SHADERERRORS;
}
```

**State management challenge:**

Warp3D Nova allows setting render state (blend mode, depth test, stencil ops)
at any time between draw calls -- similar to OpenGL's immediate-mode state.
Vulkan requires all this state to be baked into a `VkPipeline` object at
creation time.

The wrapper must:
1. Track all mutable state in a shadow state structure
2. Hash the current state combination before each draw call
3. Look up a cached `VkPipeline` matching that state, or create one on demand
4. Cache pipelines aggressively -- most applications use a small number of
   state combinations (typically 10-50)

This is the same approach used by DXVK, Zink, and every other translation
layer that maps immediate-mode state to Vulkan pipelines.

### 15.3 OpenGL ES 2.0 -> Vulkan Wrapper

**Feasibility: HIGH. Well-understood translation (ANGLE project does this).**

The existing `ogles2.library` translates GL ES 2.0 to Warp3D Nova. A Vulkan-
backed version (`ogles2_vk.library`) would translate directly to Vulkan,
bypassing Warp3D Nova entirely.

**Concept mapping:**

| OpenGL ES 2.0 | Vulkan | Notes |
|---------------|--------|-------|
| GL context (`aglCreateContext`) | `VkDevice` + `VkQueue` + state tracker | |
| `glCreateShader` / `glCompileShader` | Compile GLSL -> SPIR-V -> `VkShaderModule` | ogles2 already includes a GLSL compiler that outputs SPIR-V |
| `glCreateProgram` / `glLinkProgram` | `VkPipeline` creation | Program linking = pipeline creation |
| `glGenBuffers` / `glBufferData` | `VkBuffer` + `vkAllocateMemory` | |
| `glGenTextures` / `glTexImage2D` | `VkImage` + `VkImageView` + staging upload | |
| `glDrawArrays` / `glDrawElements` | `vkCmdDraw` / `vkCmdDrawIndexed` | |
| `glEnable(GL_BLEND)` etc. | Pipeline state | Same state-hashing approach as W3DN wrapper |
| `glViewport` / `glScissor` | `vkCmdSetViewport` / `vkCmdSetScissor` | Dynamic state in Vulkan 1.3 |
| `aglSwapBuffers` | `vkQueuePresentKHR` | |
| FBOs (`glGenFramebuffers`) | `VkFramebuffer` or dynamic rendering | |

**GLSL compilation path:**

The existing ogles2.library already contains a GLSL-to-SPIR-V compiler
(a patched glslangValidator). The Vulkan wrapper reuses this:

```
Application GLSL source
    -> ogles2_vk's built-in GLSL compiler
    -> SPIR-V bytecode
    -> vkCreateShaderModule (pass-through to ICD)
    -> ICD compiles SPIR-V to GPU ISA
```

No new shader compiler is needed. The existing one from ogles2 is reused.

### 15.4 Warp3D Classic -> Vulkan Wrapper

**Feasibility: MEDIUM. Fixed-function pipeline requires shader generation.**

Warp3D (classic) is a fixed-function API from the OpenGL 1.x era. It has no
programmable shaders -- all rendering uses a fixed pipeline with configurable
texture environment modes, fog, alpha test, etc.

The existing `W3D_NovaBridge` already translates Warp3D to Warp3D Nova. A
Vulkan-backed version follows the same approach but targets Vulkan directly.

**The key challenge: generating shaders for fixed-function state.**

Since Vulkan has no fixed-function pipeline, the wrapper must generate SPIR-V
shaders that replicate the fixed-function behaviour:

```c
/* Example: generate fragment shader for W3D texture environment mode */
static VkShaderModule GenerateFixedFunctionFragShader(
    VkDevice device,
    W3D_TexEnvMode texEnvMode,
    BOOL fogEnabled,
    W3D_FogMode fogMode,
    BOOL alphaTestEnabled)
{
    /* Build SPIR-V bytecode for the required fixed-function combination.
    ** Pre-generate common combinations at init time.
    ** Typical W3D apps use only 5-15 combinations total. */

    /* Common combinations:
    **   MODULATE + no fog + no alpha test  (most geometry)
    **   MODULATE + linear fog              (outdoor scenes)
    **   DECAL + no fog                     (UI overlays)
    **   REPLACE + no fog                   (skybox)
    */

    uint32_t spirv[] = { /* pre-compiled SPIR-V for this combination */ };
    /* ... */
}
```

**Concept mapping:**

| Warp3D Classic | Vulkan | Notes |
|----------------|--------|-------|
| `W3D_Context` | `VkDevice` + state tracker | |
| `W3D_Texture` / `W3D_AllocTexObj` | `VkImage` + `VkImageView` | Format conversion needed for some W3D formats |
| `W3D_DrawTriangle` / `W3D_DrawTriFan` etc. | `vkCmdDraw` with dynamically built vertex buffer | W3D passes vertices inline; wrapper must batch into VkBuffer |
| `W3D_DrawArray` / `W3D_DrawElements` | `vkCmdDraw` / `vkCmdDrawIndexed` | More direct mapping |
| `W3D_SetTexEnv` | Fragment shader selection | Select from pre-generated shader variants |
| `W3D_SetFogParams` | Uniform buffer + shader variant | Fog parameters as push constants |
| `W3D_SetAlphaMode` / `W3D_SetBlendMode` | Pipeline state | |
| `W3D_LockHardware` / `W3D_UnLockHardware` | No-op or begin/end command buffer | Vulkan doesn't need hardware locking |
| `W3D_SetDrawRegion` | Render target setup | |
| `W3D_AllocZBuffer` / `W3D_ClearZBuffer` | `VkImage` (depth) + `vkCmdClearDepthStencilImage` | |

### 15.5 MiniGL -> Vulkan Wrapper

**Feasibility: MEDIUM. Same approach as Zink (Mesa's OpenGL-over-Vulkan).**

MiniGL is a subset of OpenGL 1.x-2.x. It currently sits on top of Warp3D
(classic). A Vulkan wrapper would follow the same architecture as Mesa's Zink
driver, which translates OpenGL to Vulkan.

Since MiniGL is a small subset (not full OpenGL), the wrapper is much simpler
than Zink. Key areas:

- **Fixed-function pipeline:** Same shader generation approach as the Warp3D
  classic wrapper (Section 15.4)
- **Immediate mode (`glBegin`/`glEnd`):** Buffer vertices CPU-side, flush
  as a single `vkCmdDraw` on `glEnd` or buffer full
- **Display lists:** Record Vulkan commands into secondary command buffers
- **Matrix stack:** Maintained CPU-side (same as current MiniGL), passed
  to shaders as uniform matrices
- **glut (mglut.library):** Window management wrapper, creates Vulkan
  surface from Intuition window instead of W3D context

### 15.6 Warp2D -> Vulkan Wrapper

**Feasibility: HIGH. Warp2D is already built on Warp3D Nova.**

Warp2D is a 2D vector graphics API that currently uses Warp3D Nova for
hardware acceleration. Retargeting to Vulkan is straightforward because the
rendering primitives (filled rectangles, blits, stroked paths) map to
simple Vulkan draw calls.

**Implementation approach:**

- Filled rectangles and blits: textured quads rendered with `vkCmdDraw`
- Stroked paths: tessellated to triangle strips CPU-side (same as current
  Warp2D), rendered with `vkCmdDraw`
- Surface management: Vulkan images for render targets
- Batch operations: Warp2D already queues operations; the wrapper submits
  them as a single Vulkan command buffer
- Scaling and rotation: handled by vertex shader with a 2D transformation
  matrix as a push constant

### 15.7 Future 2D and Multimedia API Wrappers

Beyond the existing AmigaOS APIs, Vulkan enables wrapping popular cross-
platform 2D and multimedia libraries. These would dramatically expand the
software that can be ported to AmigaOS.

#### SDL2 (Simple DirectMedia Layer)

**Impact: VERY HIGH. Enables hundreds of games and applications.**

SDL2 is the most widely used cross-platform multimedia library. It provides
2D rendering, input handling, audio, and window management. An SDL2 port with
a Vulkan backend would enable porting the vast catalogue of SDL2-based games
and applications to AmigaOS.

SDL2 and SDL3 have full AmigaOS 4 ports at https://github.com/AmigaPorts/SDL
(branches SDL2 and main). As of 2026-03-19, neither port has any Vulkan
support -- `SDL_VIDEO_VULKAN` is undefined, no `SDL_os4vulkan.c` exists,
and the render driver options are compositing/opengles2/software only.
Adding a Vulkan backend requires ~200-300 lines of new C code implementing
4 functions (SDL2) or 6 functions (SDL3) that open `vulkan.library` via
AmigaOS `OpenLibrary`/`GetInterface` and create surfaces via
`VK_AMIGA_surface`. VulkanOS4's WSI already provides everything needed.
See `docs/batch9-progress.md` Phase W3 for full implementation details.

Adding a Vulkan rendering backend:

| SDL2 Component | AmigaOS + Vulkan Implementation |
|----------------|-------------------------------|
| `SDL_CreateWindow` | Open Intuition window |
| `SDL_CreateRenderer` (accelerated) | Create Vulkan swapchain + command pool |
| `SDL_CreateTexture` | `VkImage` + `VkImageView` |
| `SDL_RenderCopy` / `SDL_RenderCopyEx` | Textured quad via `vkCmdDraw` with transform |
| `SDL_RenderFillRect` | Solid-colour quad via `vkCmdDraw` |
| `SDL_RenderDrawLine` / `SDL_RenderDrawPoint` | Line/point rendering via `vkCmdDraw` |
| `SDL_RenderPresent` | `vkQueuePresentKHR` |
| `SDL_GL_CreateContext` (for SDL+OpenGL apps) | Use ogles2_vk or minigl_vk wrapper |

SDL2's 2D renderer is simple (textured quads, filled rects, lines, points).
A Vulkan backend needs only one vertex shader and a few fragment shader
variants (textured, solid colour, colour-modulated texture).

**SDL2 also supports Vulkan natively** via `SDL_Vulkan_CreateSurface`. With
`vulkan.library` available, SDL2 applications that use Vulkan directly (many
modern games) would work with minimal porting effort.

#### Cairo

**Impact: HIGH (theoretical). Enables GTK+ applications and general vector graphics.**

**2026-03-19 status:** Cairo has minimal presence on AmigaOS 4. salass00's
cairo_lib (GitHub) is an incomplete static library port. AmiCygnix includes
Cairo but only under X11. GTK-MUI bypasses Cairo entirely. No native
AmigaOS4 applications depend on Cairo. A Vulkan Cairo backend is deferred
indefinitely until a stable native `cairo.library` exists with applications
using it.

Cairo is a 2D vector graphics library used by GTK+, Firefox, LibreOffice,
and many Linux applications. A Vulkan-accelerated Cairo backend would enable
porting these applications with hardware-accelerated rendering.

| Cairo Operation | Vulkan Implementation |
|----------------|----------------------|
| Path fill | Tessellate to triangles, `vkCmdDraw` |
| Path stroke | Generate stroke geometry CPU-side, `vkCmdDraw` |
| Image compositing | Textured quad with blend mode |
| Gradient fill | Fragment shader with gradient computation |
| Text rendering (via FreeType) | Glyph atlas as `VkImage`, textured quads per glyph |
| Clipping | Vulkan scissor rect or stencil buffer |
| Surface creation | `VkImage` as render target |

Cairo's rendering model is mostly CPU-side geometry generation with GPU-
accelerated rasterisation and compositing. The Vulkan backend would handle
the rasterisation/compositing, while tessellation and stroke generation
remain CPU-side (same as Cairo's existing GL and Vulkan backends on Linux).

#### NanoVG

**Impact: MEDIUM. Popular for game UIs and lightweight vector graphics.**

NanoVG is a small antialiased 2D vector graphics library modelled after the
HTML5 Canvas API. It is widely used for game UI rendering. NanoVG already has
OpenGL and Vulkan backends, so porting to AmigaOS Vulkan is straightforward --
essentially just adapting the existing `nanovg_vk` backend to use the
AmigaOS Vulkan headers and WSI.

Features: antialiased paths, text rendering, gradients, image patterns,
scissor clipping. All rendered with a small set of Vulkan shaders.

#### Dear ImGui

**Impact: MEDIUM. Standard for developer tools and debug UIs.**

Dear ImGui is the de facto standard immediate-mode GUI library for games and
developer tools. It already has a Vulkan rendering backend (`imgui_impl_vulkan`).
Porting to AmigaOS requires:

1. Adapting `imgui_impl_vulkan` to use AmigaOS Vulkan headers
2. Writing an `imgui_impl_amiga` platform backend (input from Intuition
   IDCMP messages, window management)

The Vulkan rendering backend is GPU-efficient: all UI geometry is batched
into a single vertex buffer and drawn with one or two draw calls per frame.

#### Skia

**Impact: HIGH (long-term). Powers Chrome, Android, Flutter.**

Skia is Google's 2D graphics engine, used in Chrome (for web page rendering),
Android (for UI), and Flutter (for cross-platform apps). Skia already has a
Vulkan backend (`GrVkGpu`). Porting Skia to AmigaOS with Vulkan would be a
large effort but would unlock:

- Web browser rendering (if a Chromium port is ever attempted)
- Flutter application support
- High-quality 2D rendering with GPU acceleration

Skia is significantly more complex than Cairo or NanoVG, so this is a long-
term goal rather than an immediate target.

#### Raylib

**Impact: MEDIUM. Simple game programming library, growing community.**

Raylib is a simple, easy-to-use library for game programming. It supports
OpenGL backends and has community Vulkan support. An AmigaOS port using
the ogles2_vk wrapper (for its OpenGL path) or a direct Vulkan backend
would enable the growing catalogue of raylib games.

#### Allegro 5

**Impact: MEDIUM. Game programming library with active community.**

Allegro 5 is a cross-platform game programming library similar to SDL2. It
has OpenGL rendering internally. An AmigaOS port using ogles2_vk as the GL
backend would provide hardware-accelerated rendering.

### 15.8 Shared SPIR-V Pipeline

A critical advantage of the Vulkan-based stack is that SPIR-V is the shared
shader format across multiple APIs:

```
GLSL (OpenGL / GL ES)          HLSL               Any shader language
        |                        |                        |
        v                        v                        v
   glslangValidator          dxc compiler           custom compiler
        |                        |                        |
        +----------+-------------+------------------------+
                   |
                   v
              SPIR-V bytecode (universal intermediate format)
                   |
       +-----------+-----------+
       |           |           |
       v           v           v
   Warp3D Nova   Vulkan     ogles2_vk
   (existing)    (direct)   (wrapper)
       |           |           |
       +-----+-----+-----------+
             |
             v
       vkCreateShaderModule
             |
             v
       ICD SPIR-V compiler (ACO / vendor-specific)
             |
             v
       GPU native ISA (GCN, RDNA, etc.)
```

**Shared tooling:**

The SDK should include these SPIR-V tools (cross-compiled for PPC and
available as x86 host tools for offline use):

| Tool | Purpose | Source |
|------|---------|--------|
| `glslangValidator` | GLSL -> SPIR-V compiler | Already shipped with Warp3D Nova / ogles2 |
| `spirv-opt` | SPIR-V optimiser (reduces shader size, improves perf) | Khronos SPIRV-Tools |
| `spirv-val` | SPIR-V validator (catches invalid shaders) | Khronos SPIRV-Tools |
| `spirv-cross` | SPIR-V reflection and cross-compilation | Khronos SPIRV-Cross |
| `spirv-dis` / `spirv-as` | SPIR-V disassembler / assembler (debugging) | Khronos SPIRV-Tools |

Since `glslangValidator` is already ported to AmigaOS PPC (shipped with
Warp3D Nova since version 1.16), the SPIR-V toolchain foundation already
exists.

### 15.9 Performance Considerations for Wrappers

Translation layers inherently add CPU overhead. On PPC at 1.2-2.0 GHz,
minimising this overhead is critical.

**State hashing and pipeline caching:**

The dominant cost in all wrappers is translating mutable render state
(blend mode, depth test, etc.) into immutable Vulkan pipeline objects.
The wrapper must:

1. Hash the current state into a 64-bit key (fast -- a few XORs and shifts)
2. Look up the hash in a pipeline cache (hash map -- O(1) average)
3. On cache miss, create a new `VkPipeline` (expensive -- 1-10ms on PPC)
4. On cache hit, bind the cached pipeline (fast -- single `vkCmdBindPipeline`)

Most applications converge to a small working set of state combinations within
the first few frames. After warm-up, the cache hit rate approaches 100% and
the per-draw-call overhead is minimal (hash + lookup + bind).

**Batching:**

Wrappers should batch multiple draw calls into a single Vulkan command buffer
before submitting. This amortises the cost of command buffer submission and
allows the GPU to process draws efficiently. Warp3D Nova's `Submit()` already
uses this pattern (queue operations, then submit all at once).

**Vertex data:**

APIs like Warp3D classic pass vertices inline per draw call. The wrapper must
batch these into a GPU-visible `VkBuffer`. Options:

1. **Ring buffer:** Pre-allocate a large vertex buffer, write sequentially,
   wrap around. No per-frame allocation overhead. Best for streaming geometry.
2. **Persistent mapping:** Use `vkMapMemory` once and keep the pointer. Write
   vertices directly. Flush with `vkFlushMappedMemoryRanges`. Avoids map/unmap
   overhead.

**Endianness:**

With Vulkan, endianness is handled entirely within the ICD (Section 14.6).
Wrappers do not need to perform any byte-swapping -- they pass data to Vulkan
in host byte order, and the ICD handles conversion to GPU byte order. This
eliminates the endianness conversion code that currently exists in
`W3DN_SI`/`W3DN_GCN` (and the associated CPU overhead).

**Overhead summary by wrapper:**

| Wrapper | Overhead vs Direct Vulkan | Main Cost |
|---------|--------------------------|-----------|
| Warp3D Nova -> Vulkan | Low (~5-10%) | State hashing + pipeline cache lookup |
| OpenGL ES 2.0 -> Vulkan | Low-Medium (~10-15%) | State hashing + GLSL compile on first use |
| Warp3D Classic -> Vulkan | Medium (~15-25%) | Fixed-function shader generation + inline vertex batching |
| MiniGL -> Vulkan | Medium (~15-25%) | Same as Warp3D classic + display list management |
| Warp2D -> Vulkan | Very Low (~2-5%) | Warp2D operations are already batched |
| SDL2 2D Renderer -> Vulkan | Very Low (~2-5%) | Simple textured quads, minimal state |
| Cairo -> Vulkan | Low (~5-10%) | Tessellation is CPU-side regardless of backend |

### 15.10 Wrapper Implementation Priority

Based on impact, feasibility, and the existing AmigaOS software ecosystem:

| Priority | Wrapper | Rationale |
|----------|---------|-----------|
| **P0** (with Vulkan) | vulkan.library + ICD | Foundation -- everything depends on this |
| **P1** | Warp3D Nova -> Vulkan | Highest immediate impact. Existing W3DN apps (Quake III, OpenJK, etc.) work immediately. SPIR-V pass-through makes this the easiest wrapper. |
| **P1** | OpenGL ES 2.0 -> Vulkan | Second-highest impact. All ogles2 apps work. GLSL compiler already exists. |
| **P2** | Warp3D Classic -> Vulkan | Backward compatibility. NovaBridge already proves the translation is possible. Many legacy games. |
| **P2** | SDL2 Vulkan backend | Unlocks massive game catalogue. SDL2 already partially ported to AmigaOS. |
| **P3** | Warp2D -> Vulkan | Low effort, enables GPU-accelerated 2D. |
| **P3** | MiniGL -> Vulkan | Legacy OpenGL compatibility. |
| **P3** | NanoVG / Dear ImGui | Existing Vulkan backends, minimal porting work. |
| **P4** | Cairo | Large effort, but enables GTK+ application porting. |
| **P4** | Raylib / Allegro 5 | Can use ogles2_vk as GL backend instead of direct Vulkan. |
| **P5** | Skia | Very large effort. Long-term goal for browser/Flutter support. |

### 15.11 Backward Compatibility Strategy

The wrapper libraries should be **drop-in replacements** for the existing
libraries where possible:

- `ogles2_vk.library` installs as `ogles2.library` -- existing GL ES 2.0
  applications run without recompilation
- `warp3dnova_vk.library` installs as `Warp3DNova.library` -- existing W3DN
  applications run without recompilation
- `W3D_NovaBridge_vk` replaces `W3D_NovaBridge` for Warp3D classic apps
- `Warp2D_vk.library` installs as `Warp2D.library`

The original non-Vulkan libraries remain available for systems without Vulkan
hardware (e.g., Sam440/Sam460 with PCI-only GPUs).

**Version numbering:** Vulkan-backed wrappers should report a higher library
version than the originals, so that `OpenLibrary` with a minimum version
requirement selects the Vulkan version when both are available.

**Feature parity:** The wrappers must pass the existing DDK test suites for
their respective APIs. For Warp3D Nova, this means passing all W3DN DDK unit
tests. For ogles2, this means passing the GL ES 2.0 conformance tests that
the current ogles2.library passes.

---

## 16. Software Fallback ICD (`software_vk.library`)

### 16.1 Purpose

A software-only Vulkan ICD that renders entirely on the CPU, with no GPU
hardware required. This serves three critical roles:

1. **Loader validation.** Test `vulkan.library` (the loader), the VulkanIFace
   dispatch, ICD discovery, and WSI surface/swapchain path end-to-end before
   any hardware ICD exists.
2. **API conformance testing.** Run the Vulkan CTS and application tests on
   any AmigaOS system, including those without a Vulkan-capable GPU (Sam440,
   Sam460, machines with older Radeon cards).
3. **Development reference.** Wrapper authors (Section 15) can develop and
   test their translation layers against the software ICD without needing
   GPU hardware. This parallelises development -- the loader team, wrapper
   teams, and hardware ICD team can all work simultaneously.

Performance will be extremely slow (single-digit FPS for nontrivial scenes).
This is acceptable -- it is a test harness, not a product.

### 16.2 Why Not Port SwiftShader or Lavapipe

The two existing open-source software Vulkan implementations were evaluated
and both are impractical to port to AmigaOS PPC:

| Factor | SwiftShader (Google) | Lavapipe (Mesa) |
|--------|---------------------|-----------------|
| License | Apache 2.0 (OK) | MIT (OK) |
| Vulkan version | 1.3 | 1.4 |
| **JIT requirement** | **Mandatory** (Reactor/Subzero) | **Mandatory** (LLVM) |
| PPC backend | None (x86/ARM/MIPS only) | LLVM has PPC, but only ppc64le tested |
| Big-endian support | None | Not tested on Vulkan path |
| SIMD | SSE/NEON hardcoded | SSE primary |
| Codebase | ~200K lines C++ | Millions (Mesa + LLVM) |
| OS deps | Windows/Linux/macOS/Android | Full POSIX (pthreads, mmap) |

**The fundamental blocker is JIT compilation.** Both SwiftShader and lavapipe
require runtime code generation (JIT) to execute shaders. This requires
either:
- A JIT backend that emits PPC32 big-endian machine code (does not exist
  in either project), or
- LLVM running on AmigaOS 4 (LLVM has never been built for AmigaOS; it
  requires ~1-2GB RAM just to build, plus C++17, pthreads, mmap, dynamic
  linking)

Porting either implementation would exceed the effort of writing a new one
from scratch.

### 16.3 Architecture

The software ICD is a standard AmigaOS shared library (`software_vk.library`)
that registers as an ICD via JSON manifest. It implements Vulkan by
interpreting SPIR-V shaders and rasterising triangles entirely on the CPU.

```
+-----------------------------------------------------+
|              software_vk.library                    |
|                                                     |
|  +----------------------------------------------+   |
|  |        Vulkan Entrypoints (~400 functions)   |   |
|  |  Instance, Device, Memory, Buffer, Image,    |   |
|  |  Pipeline, CmdBuffer, Sync, WSI              |   |
|  +------------------+---------------------------+   |
|                     |                               |
|  +------------------v---------------------------+   |
|  |          SPIR-V Interpreter                  |   |
|  |  Loads SPIR-V, byte-swaps (LE->BE),          |   |
|  |  executes vertex + fragment shaders on CPU   |   |
|  +------------------+---------------------------+   |
|                     |                               |
|  +------------------v---------------------------+   |
|  |        Software Rasteriser                   |   |
|  |  Triangle setup, edge walking, interpolation |   |
|  |  Depth test, blend, write to framebuffer     |   |
|  +------------------+---------------------------+   |
|                     |                               |
|  +------------------v---------------------------+   |
|  |         Memory Framebuffer                   |   |
|  |  System RAM, ARGB32 pixel format             |   |
|  +------------------+---------------------------+   |
|                     |                               |
|  +------------------v---------------------------+   |
|  |      WSI Presentation (Blit to Intuition)    |   |
|  |  BltBitMapRastPort from framebuffer to       |   |
|  |  Intuition window -- CPU copy is fine here   |   |
|  +----------------------------------------------+   |
+-----------------------------------------------------+
```

Unlike the hardware ICD, the software ICD **does** copy the framebuffer to
the Intuition window via `BltBitMapRastPort`. This is acceptable because
everything is CPU-rendered anyway -- there is no GPU scanout to bypass.

### 16.4 SPIR-V Interpreter

The SPIR-V interpreter is the core of the software ICD. It executes shader
programs on the CPU without any JIT compilation.

**Design:**

```c
/*
** SPIR-V interpreter -- walks instruction stream, executes each op.
** No JIT, no LLVM, no architecture dependencies. Pure portable C.
*/

typedef struct {
    float    values[4];    /* vec4 register */
} SpvRegister;

typedef struct {
    SpvRegister *registers;     /* register file */
    uint32_t     regCount;
    const uint32_t *code;       /* SPIR-V bytecode (already byte-swapped) */
    uint32_t     codeLen;
    /* Uniform/input/output bindings */
    const void  *uniformData;
    const void  *vertexInputs;
    void        *outputs;
} SpvInterpreterState;

/*
** Execute a SPIR-V shader.
** Called once per vertex (vertex shader) or once per fragment (fragment shader).
*/
void SpvInterpret(SpvInterpreterState *state)
{
    uint32_t pc = 0;  /* program counter -- word offset into code */

    while (pc < state->codeLen) {
        uint32_t word0 = state->code[pc];
        uint16_t opcode = word0 & 0xFFFF;
        uint16_t wordCount = word0 >> 16;

        switch (opcode) {
        case SpvOpLoad:
            /* Load from variable to register */
            InterpLoad(state, &state->code[pc]);
            break;
        case SpvOpStore:
            /* Store register to variable */
            InterpStore(state, &state->code[pc]);
            break;
        case SpvOpFAdd:
            InterpFAdd(state, &state->code[pc]);
            break;
        case SpvOpFMul:
            InterpFMul(state, &state->code[pc]);
            break;
        case SpvOpVectorShuffle:
            InterpVectorShuffle(state, &state->code[pc]);
            break;
        case SpvOpCompositeConstruct:
            InterpCompositeConstruct(state, &state->code[pc]);
            break;
        /* ... ~100 opcodes for basic shaders ... */

        case SpvOpReturn:
        case SpvOpFunctionEnd:
            return;

        default:
            /* Unimplemented opcode -- skip */
            break;
        }

        pc += wordCount;
    }
}
```

**SPIR-V byte-swapping at load time:**

SPIR-V is little-endian by specification. On big-endian PPC, all 32-bit words
must be byte-swapped when the shader module is loaded. After swapping, the
interpreter operates on native-endian data:

```c
VkResult software_vk_CreateShaderModule(VkDevice device,
                                        const VkShaderModuleCreateInfo *ci,
                                        const VkAllocationCallbacks *alloc,
                                        VkShaderModule *pModule)
{
    uint32_t wordCount = ci->codeSize / 4;
    uint32_t *nativeCode = AllocMem(ci->codeSize, MEMF_PUBLIC);

    /* Byte-swap all SPIR-V words from LE to BE */
    const uint32_t *leCode = ci->pCode;
    for (uint32_t i = 0; i < wordCount; i++) {
        nativeCode[i] = __builtin_bswap32(leCode[i]);
    }

    /* Validate SPIR-V magic number (after swap) */
    if (nativeCode[0] != 0x07230203) {
        FreeMem(nativeCode, ci->codeSize);
        return VK_ERROR_INVALID_SHADER_NV;
    }

    /* Store for later use in pipeline creation */
    /* ... */
}
```

**SPIR-V opcodes required for basic rendering:**

A minimal triangle shader uses ~30-40 SPIR-V opcodes. The interpreter does
not need to support the full ~500+ SPIR-V opcode set initially:

| Category | Opcodes | Count |
|----------|---------|-------|
| Decoration/type | OpDecorate, OpTypeFloat, OpTypeVector, OpTypePointer, OpConstant, OpVariable | ~15 |
| Memory | OpLoad, OpStore, OpAccessChain | 3 |
| Arithmetic | OpFAdd, OpFSub, OpFMul, OpFDiv, OpFNegate, OpDot | 6 |
| Vector | OpVectorShuffle, OpCompositeConstruct, OpCompositeExtract | 3 |
| Control flow | OpLabel, OpBranch, OpBranchConditional, OpReturn, OpFunctionCall | 5 |
| Comparison | OpFOrdLessThan, OpFOrdGreaterThan, OpSelect | 3 |
| Built-in | OpAccessChain for gl_Position, gl_FragCoord, gl_VertexIndex | via OpLoad |
| **Total** | | **~35** |

Additional opcodes can be added incrementally as more complex shaders are
tested.

### 16.5 Software Rasteriser

The rasteriser converts transformed vertices (output of the vertex shader
interpreter) into pixels (input to the fragment shader interpreter).

**Minimum implementation:**

```
1. Triangle setup
   - Compute edge equations from 3 screen-space vertices
   - Compute bounding box (clamp to viewport/scissor)

2. Rasterisation (scanline or half-space)
   - For each pixel in the bounding box:
     a. Test if pixel is inside the triangle (edge function test)
     b. Compute barycentric coordinates
     c. Interpolate vertex outputs (color, texcoords, etc.)
     d. Execute fragment shader interpreter with interpolated inputs
     e. Depth test (if enabled)
     f. Blend (if enabled)
     g. Write pixel to framebuffer

3. Framebuffer
   - Simple ARGB32 pixel array in system memory
   - Depth buffer: float32 array (same dimensions)
```

**Half-space rasterisation** is simpler to implement than scanline and
naturally handles all triangle orientations:

```c
void RasteriseTriangle(Framebuffer *fb, Vertex v0, Vertex v1, Vertex v2,
                       SpvInterpreterState *fragShader)
{
    /* Compute bounding box */
    int minX = max(0, min3(v0.x, v1.x, v2.x));
    int maxX = min(fb->width - 1, max3(v0.x, v1.x, v2.x));
    int minY = max(0, min3(v0.y, v1.y, v2.y));
    int maxY = min(fb->height - 1, max3(v0.y, v1.y, v2.y));

    /* Edge function coefficients */
    float e01_a = v0.y - v1.y, e01_b = v1.x - v0.x;
    float e12_a = v1.y - v2.y, e12_b = v2.x - v1.x;
    float e20_a = v2.y - v0.y, e20_b = v0.x - v2.x;

    float area = EdgeFunc(v0, v1, v2);
    if (area <= 0.0f) return;  /* backface or degenerate */
    float invArea = 1.0f / area;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float w0 = EdgeFunc2(e12_a, e12_b, v1, x, y);
            float w1 = EdgeFunc2(e20_a, e20_b, v2, x, y);
            float w2 = EdgeFunc2(e01_a, e01_b, v0, x, y);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                /* Inside triangle -- compute barycentrics */
                w0 *= invArea; w1 *= invArea; w2 *= invArea;

                /* Interpolate vertex outputs */
                InterpolateVaryings(fragShader, v0, v1, v2, w0, w1, w2);

                /* Execute fragment shader */
                SpvInterpret(fragShader);

                /* Depth test + write pixel */
                float z = w0 * v0.z + w1 * v1.z + w2 * v2.z;
                WritePixel(fb, x, y, z, fragShader->outputs);
            }
        }
    }
}
```

### 16.6 WSI Presentation

The software ICD presents to screen by blitting the CPU-rendered framebuffer
to the Intuition window:

```c
VkResult software_vk_QueuePresentKHR(VkQueue queue,
                                     const VkPresentInfoKHR *presentInfo)
{
    SwapchainState *sc = GetSwapchain(presentInfo->pSwapchains[0]);
    uint32_t imageIdx = presentInfo->pImageIndices[0];
    Framebuffer *fb = &sc->framebuffers[imageIdx];

    /* Blit CPU framebuffer to Intuition window */
    /* WritePixelArray handles format conversion if needed */
    IGraphics->WritePixelArray(
        fb->pixels,             /* source: our ARGB32 framebuffer */
        0, 0,                   /* source x, y */
        fb->width * 4,          /* source bytes per row */
        sc->rastPort,           /* destination: window's RastPort */
        0, 0,                   /* dest x, y */
        fb->width, fb->height,  /* size */
        RECTFMT_ARGB            /* pixel format */
    );

    return VK_SUCCESS;
}
```

This is a CPU-to-CPU copy from the software framebuffer to the Intuition
display buffer. It is slow but correct, and it exercises the full Vulkan
WSI path (surface creation, swapchain, acquire, present).

### 16.7 Physical Device Reporting

The software ICD must report itself as a `VK_PHYSICAL_DEVICE_TYPE_CPU`
device with conservative limits:

```c
void software_vk_GetPhysicalDeviceProperties(VkPhysicalDevice pd,
                                             VkPhysicalDeviceProperties *props)
{
    memset(props, 0, sizeof(*props));
    props->apiVersion = VK_API_VERSION_1_3;
    props->driverVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    props->vendorID = 0;        /* No vendor */
    props->deviceID = 0;
    props->deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    strncpy(props->deviceName, "AmigaOS Software Renderer",
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

    /* Conservative limits for CPU rendering */
    props->limits.maxImageDimension2D = 4096;
    props->limits.maxFramebufferWidth = 4096;
    props->limits.maxFramebufferHeight = 4096;
    props->limits.maxViewports = 1;
    props->limits.maxBoundDescriptorSets = 4;
    props->limits.maxPushConstantsSize = 128;
    props->limits.maxComputeWorkGroupSize[0] = 128;
    props->limits.maxComputeSharedMemorySize = 16384;
    props->limits.maxColorAttachments = 4;
    /* ... */
}
```

When both `software_vk.library` and a hardware ICD are available, applications
call `vkEnumeratePhysicalDevices` and see both. The hardware device reports
`VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU` and will be preferred by any
application that checks device type.

### 16.8 Implementation Scope

**Phase A: Minimum viable (triangle test)**

~50-60 Vulkan functions, ~35 SPIR-V opcodes, triangle-only rasteriser.
Goal: render a solid-colour triangle to an Intuition window via the full
Vulkan API path (loader -> software ICD -> framebuffer -> blit to window).

Estimated size: ~8,000-12,000 lines of C.

**Phase B: Textured rendering**

Add texture sampling (VkImage, VkSampler, VkImageView), texture SPIR-V
opcodes (OpImageSampleImplicitLod), and indexed drawing (vkCmdDrawIndexed).
Goal: render the Vulkan triangle example from Section 8 of this document.

Estimated size: +5,000 lines.

**Phase C: Full Vulkan 1.3 stubs**

Stub all ~400 Vulkan 1.3 core functions (unimplemented ones return
`VK_ERROR_FEATURE_NOT_PRESENT` or are no-ops). Implement the most common
functions needed by the wrapper layers (Section 15). Goal: the Warp3D Nova
wrapper can run simple W3DN examples against the software ICD.

Estimated size: +5,000-8,000 lines.

**Total: ~18,000-25,000 lines of portable C** with no external dependencies
beyond the AmigaOS SDK.

### 16.9 Constraints and Limitations

- **Performance:** Expect 0.5-5 FPS for simple scenes, <1 FPS for anything
  with textures or complex shaders. This is inherent to CPU rendering on a
  1.2-2.0 GHz PPC without JIT.
- **No compute shaders initially.** SPIR-V compute kernels can be added
  later but are not needed for graphics validation.
- **No sparse resources, no multi-sampling, no tessellation/geometry shaders
  initially.** These can be stubbed as unsupported features.
- **Single-threaded.** Unlike lavapipe (which uses up to 32 threads), the
  minimal software ICD can start single-threaded. Multi-threaded tile-based
  rasterisation can be added later for a modest speedup on 2-4 core systems.
- **AltiVec optimisation (optional).** On the X1000 (PA6T, which has VMX),
  the SPIR-V interpreter's vec4 operations could be accelerated with AltiVec
  intrinsics. This is an optimisation, not a requirement.

### 16.10 JSON Manifest

```json
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "LIBS:Vulkan/software_vk.library",
        "api_version": "1.3.0",
        "library_version": 1
    }
}
```

Place in `DEVS:Vulkan/icd.d/software_vk.json`. The loader discovers it
alongside any hardware ICDs. Applications that enumerate physical devices
will see both the software CPU device and any hardware GPU.

### 16.11 JIT Compilation Path (Future Optimisation)

The SPIR-V interpreter (Section 16.4) executes shader instructions one at a
time via a switch/case dispatch loop. Each SPIR-V opcode incurs:
- Switch dispatch overhead (~10-20 cycles on PPC)
- Indirect branch misprediction penalty (~15-25 cycles)
- No instruction-level optimisation (redundant loads, missed FMA opportunities)

A JIT compiler eliminates this overhead by translating SPIR-V to native PPC
machine code once at pipeline creation time (`vkCreateGraphicsPipelines`).
Subsequent shader executions run native code at full CPU speed.

**Expected speedup: 10-50x over the interpreter** for typical shader workloads.
This transforms the software ICD from "barely functional validation tool" to
"usable for lightweight 2D rendering and simple 3D scenes at low resolutions."

#### JIT Options for PPC32 Big-Endian

Three lightweight JIT libraries support PPC32 big-endian and are portable to
AmigaOS. All three avoid the LLVM dependency that makes SwiftShader and
lavapipe impractical.

| Option | PPC32 BE | Float | SIMD | License | Size | AmigaOS Port |
|--------|----------|-------|------|---------|------|-------------|
| **SLJIT** | Yes | F32/F64 | Partial | BSD 2-clause | ~10-15K LOC | Very good |
| **GNU Lightning** | Yes | F32/F64 + FMA | No | LGPL 2.1+ | ~50K LOC | Good |
| **DynASM** | Yes | Raw PPC asm | Full (AltiVec) | MIT | ~500 LOC runtime | Excellent |

**Option 1: SLJIT (recommended first choice)**

SLJIT is a stack-less JIT compiler extracted from PCRE2. It provides a
platform-independent low-level IR (LIR) that maps to native instructions.
PPC32 is an explicitly supported backend. It has been used in kernel contexts
(NetBSD) so it works without POSIX.

```c
/*
** Example: JIT-compile a simple SPIR-V fragment shader operation
** fragColor.rgb = vertexColor.rgb * 0.5 + 0.5
** using SLJIT to emit native PPC code.
*/
#include <sljit_src/sljitLir.h>

struct sljit_compiler *compiler = sljit_create_compiler(NULL);

/* Emit: load vertex colour R component from memory */
sljit_emit_fop1(compiler, SLJIT_MOV_F32,
    SLJIT_FR0, 0,                           /* dest: float reg 0 */
    SLJIT_MEM1(SLJIT_R0), offsetof(Varyings, colorR));  /* src: memory */

/* Emit: multiply by 0.5 */
sljit_emit_fop2(compiler, SLJIT_MUL_F32,
    SLJIT_FR0, 0,           /* dest */
    SLJIT_FR0, 0,           /* src1: vertex colour */
    SLJIT_FR4, 0);          /* src2: pre-loaded constant 0.5 */

/* Emit: add 0.5 */
sljit_emit_fop2(compiler, SLJIT_ADD_F32,
    SLJIT_FR0, 0,           /* dest */
    SLJIT_FR0, 0,           /* src1 */
    SLJIT_FR4, 0);          /* src2: same constant 0.5 */

/* Emit: store result to output */
sljit_emit_fop1(compiler, SLJIT_MOV_F32,
    SLJIT_MEM1(SLJIT_R1), offsetof(Outputs, colorR),  /* dest: memory */
    SLJIT_FR0, 0);                                      /* src: float reg 0 */

/* Generate native PPC code */
void *code = sljit_generate_code(compiler, 0, NULL);

/* Call the generated function */
typedef void (*ShaderFunc)(const Varyings *, Outputs *);
((ShaderFunc)code)(varyings, outputs);
```

AmigaOS integration requires replacing SLJIT's default memory allocator:

```c
/* Override SLJIT's executable memory allocation for AmigaOS */
#define SLJIT_MALLOC_EXEC(size, exec_offset)  \
    IExec->AllocVecTags((size), AVT_Type, MEMF_SHARED, \
                        AVT_Flags, MEMF_EXECUTABLE, TAG_DONE)
#define SLJIT_FREE_EXEC(ptr, exec_offset)  \
    IExec->FreeVec(ptr)
```

**SPIR-V to SLJIT translation:**

The SPIR-V translator walks the shader instruction stream and emits SLJIT
LIR calls for each operation:

| SPIR-V Opcode | SLJIT Emission |
|---------------|----------------|
| `OpFAdd` | `sljit_emit_fop2(SLJIT_ADD_F32, ...)` |
| `OpFSub` | `sljit_emit_fop2(SLJIT_SUB_F32, ...)` |
| `OpFMul` | `sljit_emit_fop2(SLJIT_MUL_F32, ...)` |
| `OpFDiv` | `sljit_emit_fop2(SLJIT_DIV_F32, ...)` |
| `OpFNegate` | `sljit_emit_fop1(SLJIT_NEG_F32, ...)` |
| `OpLoad` | `sljit_emit_fop1(SLJIT_MOV_F32, ..., SLJIT_MEM, ...)` |
| `OpStore` | `sljit_emit_fop1(SLJIT_MOV_F32, SLJIT_MEM, ..., ...)` |
| `OpBranch` | `sljit_emit_jump(SLJIT_JUMP)` |
| `OpBranchConditional` | `sljit_emit_fcmp(...)` + `sljit_emit_jump(SLJIT_LESS_F32)` |
| `OpDot` (vec4) | 4x MUL + 3x ADD (expanded to scalar) |
| `OpVectorShuffle` | Series of MOV operations |

**Option 2: GNU Lightning (alternative)**

GNU Lightning provides a similar portable IR to SLJIT but is more mature and
has explicit FMA (fused multiply-add) support on PPC. FMA is important for
shader math -- `a * b + c` in a single instruction instead of two.

```c
#include <lightning.h>

jit_state_t *_jit = jit_new_state();

/* Override memory allocator for AmigaOS */
jit_set_memory_functions(amiga_alloc, amiga_realloc, amiga_free);

jit_prolog();
jit_arg_f();  /* input: vertex colour */

/* fmadd: result = colour * 0.5 + 0.5 */
jit_movi_f(JIT_F1, 0.5f);
jit_fmar_f(JIT_F0, JIT_F0, JIT_F1, JIT_F1);  /* F0 = F0 * F1 + F1 */

jit_retr_f(JIT_F0);

typedef float (*ShaderFunc)(float);
ShaderFunc fn = jit_emit();
jit_clear_state();

float result = fn(vertexColour);  /* Executes native PPC fmadd */
```

GNU Lightning's `jit_fmar_f` (fused multiply-add) maps directly to PPC's
`fmadds` instruction -- a single hardware instruction for `a * b + c`.
This is a significant advantage for shader math where multiply-add chains
are extremely common.

**Option 3: DynASM (maximum performance)**

DynASM provides no abstraction -- you write actual PPC assembly. This gives
full access to every PPC instruction including AltiVec/VMX vector operations
for native vec4 shader math.

```
// DynASM .dasc file -- PPC assembly for vec4 multiply-add
// fragColor = vertexColor * scale + bias  (4 floats at once)
|  lvx    v0, 0, r_varyings    // v0 = load vec4 vertexColor
|  lvx    v1, 0, r_scale       // v1 = load vec4 scale
|  lvx    v2, 0, r_bias        // v2 = load vec4 bias
|  vmaddfp v0, v0, v1, v2      // v0 = v0 * v1 + v2 (4x float FMA)
|  stvx   v0, 0, r_output      // store vec4 result
```

Five PPC instructions replace ~20 scalar operations. On the X1000 (PA6T with
AltiVec), this is a 4x throughput improvement for vec4-heavy shaders.

However, DynASM requires:
- Writing PPC assembly directly (no portable IR)
- Lua 5.1 as a build-time dependency (for the `.dasc` preprocessor)
- Deep knowledge of PPC calling conventions and register usage
- Separate code paths for CPUs with and without AltiVec

DynASM is recommended only after SLJIT or GNU Lightning proves insufficient
for performance-critical shaders.

#### JIT Integration Architecture

The JIT is integrated into the software ICD's pipeline creation path:

```
vkCreateGraphicsPipelines
    |
    v
Parse SPIR-V bytecode (byte-swap LE->BE)
    |
    v
[JIT available?] --NO--> Store SPIR-V for interpreter (Section 16.4)
    |
   YES
    |
    v
Translate SPIR-V ops to JIT IR (SLJIT LIR / GNU Lightning / DynASM)
    |
    v
JIT emits native PPC32 machine code to executable memory
    |
    v
Store function pointer in VkPipeline object
    |
    v
vkCmdDraw calls the native function pointer directly
    (no interpreter switch/case overhead)
```

The interpreter remains as a fallback for:
- SPIR-V opcodes not yet implemented in the JIT
- Debugging (interpreter is easier to step through)
- Platforms where executable memory allocation is unavailable

#### Staged JIT Implementation

**Stage 1: Interpreter only (Section 16.4)**
- Ship first. Works on all platforms. Validates the full API.
- 0.5-5 FPS for simple scenes.

**Stage 2: SLJIT scalar JIT**
- Add SLJIT backend. JIT-compile vertex and fragment shaders to native PPC
  scalar float code.
- Expected: 5-50 FPS for simple scenes (10-50x speedup over interpreter).
- Effort: ~3,000-5,000 lines of SPIR-V-to-SLJIT translator.

**Stage 3: AltiVec vec4 JIT (X1000 only)**
- Add DynASM backend targeting AltiVec for vec4 operations.
- 4x throughput for vec4-heavy shaders on AltiVec-capable CPUs.
- Falls back to SLJIT scalar on non-AltiVec systems (X5000, A1222).
- Effort: ~2,000-4,000 lines of PPC assembly in `.dasc` format.

**Stage 4: Multi-threaded tile rasteriser**
- Split the framebuffer into tiles. Assign tiles to different CPU cores.
- 1.5-3x speedup on 2-4 core systems.
- Effort: ~2,000 lines of threading code using AmigaOS task/signal primitives.

#### Performance Projections

Rough estimates for rendering a textured 800x600 scene with a simple vertex
+ fragment shader (one texture sample, one multiply-add):

| Stage | Sam460 (1.15 GHz, 1 core) | A1222 (1.2 GHz, 2 cores) | X1000 (1.8 GHz, 2 cores, AltiVec) | X5000/40 (2.0 GHz, 4 cores) |
|-------|---------------------------|--------------------------|-----------------------------------|----------------------------|
| Interpreter | <1 FPS | ~1 FPS | ~2 FPS | ~2 FPS |
| SLJIT scalar | ~3 FPS | ~5 FPS | ~15 FPS | ~20 FPS |
| + AltiVec | N/A | N/A | ~40 FPS | N/A (no AltiVec) |
| + Multi-thread | ~3 FPS | ~8 FPS | ~65 FPS | ~60 FPS |

These are rough order-of-magnitude estimates. Actual performance depends
heavily on shader complexity, resolution, overdraw, and memory bandwidth.
The key takeaway: JIT compilation transforms the software ICD from a
validation tool into something approaching usable for lightweight rendering.

---

## 17. SDK Deliverables and Header Generation

This section details every file a developer needs to create a Vulkan
application on AmigaOS 4, how each file is produced, and the generation
pipeline that transforms the Khronos Vulkan XML registry into AmigaOS-
specific headers.

### 17.1 Complete SDK File Inventory

Every file listed below must be produced and included in the SDK. Files
marked **generated** are produced by tooling from source definitions.
Files marked **hand-written** are authored manually.

```
SDK/
+-- include/
|   +-- include_h/
|       +-- vulkan/
|       |   +-- vulkan.h                    [hand-written]  ~20 lines
|       |   +-- vulkan_core.h               [GENERATED]     ~15,000 lines
|       |   +-- vulkan_amiga.h              [hand-written]  ~70 lines
|       |   +-- vk_platform.h               [hand-written]  ~30 lines
|       |   +-- vk_icd.h                    [hand-written]  ~90 lines
|       +-- interfaces/
|       |   +-- vulkan.h                    [GENERATED by idltool]
|       +-- inline4/
|       |   +-- vulkan.h                    [GENERATED by idltool]
|       +-- proto/
|       |   +-- vulkan.h                    [hand-written]  ~40 lines
|       +-- clib/
|           +-- vulkan_protos.h             [GENERATED by idltool]
+-- include/
|   +-- interfaces/
|       +-- vulkan.xml                      [GENERATED + hand-edited]
+-- newlib/
|   +-- lib/
|       +-- libvulkan_loader.a              [GENERATED - compiled stub]
+-- local/
|   +-- newlib/
|       +-- lib/
|           +-- libvulkan_loader.a          [GENERATED - compiled stub]
+-- Documentation/
|   +-- AutoDocs/
|   |   +-- vulkan.doc                      [GENERATED from source comments]
|   +-- Vulkan/
|       +-- vulkan_guide.txt                [hand-written]
|       +-- wsi_amiga.txt                   [hand-written]
|       +-- porting_from_warp3d.txt         [hand-written]
|       +-- icd_developer_guide.txt         [hand-written]
+-- Examples/
|   +-- Vulkan/
|       +-- triangle/
|       |   +-- triangle.c                  [hand-written]
|       |   +-- vert.spv                    [GENERATED from GLSL]
|       |   +-- frag.spv                    [GENERATED from GLSL]
|       |   +-- Makefile                    [hand-written]
|       +-- texture/
|       |   +-- texture.c                   [hand-written]
|       |   +-- vert.spv / frag.spv         [GENERATED from GLSL]
|       |   +-- Makefile                    [hand-written]
|       +-- compute/
|       |   +-- compute.c                   [hand-written]
|       |   +-- comp.spv                    [GENERATED from GLSL]
|       |   +-- Makefile                    [hand-written]
|       +-- wsi/
|           +-- wsi_demo.c                  [hand-written]
|           +-- Makefile                    [hand-written]
+-- Tools/
|   +-- vulkaninfo                          [GENERATED - compiled utility]
|   +-- glslangValidator                    [cross-compiled or ported]
|   +-- spirv-opt                           [cross-compiled or ported]
|   +-- spirv-val                           [cross-compiled or ported]
|   +-- spirv-dis                           [cross-compiled or ported]
+-- Templates/
    +-- Makefile.template                   [hand-written]
    +-- .vscode/                            [hand-written]
        +-- c_cpp_properties.json
        +-- tasks.json
```

**Total hand-written files:** ~15 (small, already drafted in Sections 4-6, 8)
**Total generated files:** ~10 (produced by the pipeline below)

### 17.2 Header Generation Pipeline

The Khronos Group maintains the Vulkan XML registry (`vk.xml`) as the
single source of truth for all Vulkan types, enums, structures, constants,
and function signatures. All official Vulkan headers on every platform are
generated from this file. The AmigaOS headers follow the same approach
with an additional AmigaOS-specific generation step.

```
Khronos vk.xml (source of truth, ~45,000 lines XML)
    |
    +--> [Step 1] genvk_amiga.py (modified Khronos generator)
    |        |
    |        +--> vulkan_core.h        (~15,000 lines: all types, enums, structs)
    |        +--> function_list.txt    (all ~400 function signatures, for Step 2)
    |
    +--> [Step 2] gen_vulkan_xml.py (custom AmigaOS script)
    |        |
    |        +--> vulkan.xml           (AmigaOS interface definition, ~1500 lines)
    |
    +--> [Step 3] idltool (standard AmigaOS SDK tool)
             |
             +--> interfaces/vulkan.h  (VulkanIFace struct definition)
             +--> inline4/vulkan.h     (inline macros: #define vkFoo IVulkan->vkFoo)
             +--> clib/vulkan_protos.h (C function prototypes)

Hand-written files (not generated):
    vk_platform.h         (Section 4.1 -- complete, ~30 lines)
    vulkan_amiga.h        (Section 4.3 -- complete, ~70 lines)
    vulkan.h (master)     (Section 4.4 -- complete, ~15 lines)
    vk_icd.h              (Section 6.1 -- complete, ~90 lines)
    proto/vulkan.h        (Section 5.2 -- complete, ~40 lines)
```

### 17.3 Step 1: Generating vulkan_core.h

The Khronos Vulkan SDK includes `genvk.py`, a Python script that reads
`vk.xml` and outputs platform-specific headers. A modified version
(`genvk_amiga.py`) produces the AmigaOS `vulkan_core.h`.

**Modifications needed vs stock Khronos generator:**

1. **Remove function declarations.** On AmigaOS, functions are declared in
   the interface header (`interfaces/vulkan.h`), not in `vulkan_core.h`.
   The core header contains only types, enums, structs, and constants.

2. **Remove `#pragma pack` directives.** Standard Khronos headers don't use
   them, but verify the generator doesn't insert platform-specific packing.
   Vulkan structs use natural alignment (Section 4.2).

3. **PFN_ typedefs remain.** Function pointer typedefs (`PFN_vkCreateInstance`
   etc.) are needed for `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`
   return values. These stay in `vulkan_core.h`.

4. **Add AmigaOS platform guard.** Wrap the file with `#ifdef __amigaos4__`
   sections where AmigaOS-specific choices are made (e.g., VKAPI_ATTR
   definitions come from `vk_platform.h`).

**Running the generator:**

```bash
# On the developer's host machine (Linux/macOS/WSL):
# Download Khronos Vulkan-Headers repository
git clone https://github.com/KhronosGroup/Vulkan-Headers.git
cd Vulkan-Headers

# Run the AmigaOS-modified generator
python3 scripts/genvk_amiga.py -registry registry/vk.xml \
    -o output/ vulkan_core.h

# Output: output/vulkan_core.h (~15,000 lines)
```

The generator also outputs `function_list.txt` containing every Vulkan
function signature, which feeds into Step 2.

**What vulkan_core.h contains (generated):**

| Content | Approximate Lines | Example |
|---------|------------------|---------|
| Version macros | ~20 | `VK_API_VERSION_1_3`, `VK_MAKE_API_VERSION` |
| Handle definitions | ~25 | `VK_DEFINE_HANDLE(VkInstance)`, `VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)` |
| Constants | ~50 | `VK_MAX_PHYSICAL_DEVICE_NAME_SIZE`, `VK_UUID_SIZE`, `VK_NULL_HANDLE` |
| Enumerations | ~3,000 | `VkResult`, `VkStructureType` (~1000 entries), `VkFormat` (~200 entries), `VkImageLayout`, etc. |
| Flag types | ~200 | `VkBufferUsageFlags`, `VkMemoryPropertyFlags`, etc. |
| Structures | ~10,000 | `VkInstanceCreateInfo`, `VkPhysicalDeviceProperties`, all ~500+ structures |
| Function pointer typedefs (PFN_) | ~1,500 | `PFN_vkCreateInstance`, `PFN_vkCmdDraw`, all ~400 functions |
| **Total** | **~15,000** | |

### 17.4 Step 2: Generating vulkan.xml (AmigaOS Interface Definition)

The AmigaOS interface XML file defines the VulkanIFace struct -- the
function pointer vtable that applications call through. This file is the
input to `idltool` which generates the C headers.

A custom Python script (`gen_vulkan_xml.py`) reads the function list from
Step 1 and generates the XML:

```bash
python3 tools/gen_vulkan_xml.py \
    --functions function_list.txt \
    --template vulkan_xml_template.xml \
    --output vulkan.xml
```

The script:
1. Reads all ~400 Vulkan function signatures
2. Classifies each as global, instance-level, or device-level
3. Adds the standard AmigaOS methods (Obtain, Release, Expunge, Clone)
4. Adds the AmigaOS-specific loader functions (VkAmigaGetLoaderVersion,
   VkAmigaSetICDSearchPath, VkAmigaSetLayerSearchPath)
5. Adds the WSI extension functions (vkCreateAmigaSurfaceAMIGA,
   vkGetPhysicalDeviceAmigaPresentationSupportAMIGA)
6. Outputs the complete XML in `idltool` format

The generated `vulkan.xml` is reviewed and committed to source control.
Subsequent Vulkan version updates re-run the generator and append new
functions to the end of the interface (per Section 12 decision #9).

Section 5.1 shows a representative subset of this file. The full generated
file will contain ~400 `<method>` entries totalling ~1,500 lines of XML.

### 17.5 Step 3: Running idltool

`idltool` is the standard AmigaOS SDK tool that generates C headers from
XML interface definitions. It produces three files:

```bash
# Run on AmigaOS or via cross-idltool on the host
idltool -a -h -i vulkan.xml

# Outputs:
#   interfaces/vulkan.h    - VulkanIFace struct with function pointers
#   inline4/vulkan.h       - #define macros mapping vkFoo() to IVulkan->vkFoo()
#   clib/vulkan_protos.h   - Traditional C function prototypes
```

**interfaces/vulkan.h** (generated):
```c
/* Auto-generated by idltool -- DO NOT EDIT */
struct VulkanIFace {
    struct InterfaceData Data;

    uint32 APICALL (*Obtain)(struct VulkanIFace *Self);
    uint32 APICALL (*Release)(struct VulkanIFace *Self);
    void APICALL (*Expunge)(struct VulkanIFace *Self);
    struct Interface * APICALL (*Clone)(struct VulkanIFace *Self);

    /* AmigaOS-specific loader functions */
    uint32 APICALL (*VkAmigaGetLoaderVersion)(struct VulkanIFace *Self);
    VkResult APICALL (*VkAmigaSetICDSearchPath)(struct VulkanIFace *Self,
        const char *path);
    /* ... */

    /* Vulkan 1.0 - 1.3 core (~400 function pointers) */
    VkResult APICALL (*vkCreateInstance)(struct VulkanIFace *Self,
        const VkInstanceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkInstance *pInstance);
    /* ... all ~400 functions ... */

    /* WSI extension */
    VkResult APICALL (*vkCreateAmigaSurfaceAMIGA)(struct VulkanIFace *Self,
        VkInstance instance,
        const VkAmigaSurfaceCreateInfoAMIGA *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkSurfaceKHR *pSurface);
    /* ... */
};
```

Note: `idltool` adds `struct VulkanIFace *Self` as the first parameter to
every method. This is the standard AmigaOS interface calling convention.
The inline macros in `inline4/vulkan.h` hide this parameter so application
code matches the Vulkan spec signatures exactly.

**inline4/vulkan.h** (generated):
```c
/* Auto-generated by idltool -- DO NOT EDIT */
#define vkCreateInstance(pCreateInfo, pAllocator, pInstance) \
    IVulkan->vkCreateInstance((pCreateInfo), (pAllocator), (pInstance))
/* ... all ~400 functions ... */
```

**clib/vulkan_protos.h** (generated):
```c
/* Auto-generated by idltool -- DO NOT EDIT */
VkResult vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
/* ... all ~400 functions ... */
```

### 17.6 Link Library (`libvulkan_loader.a`)

Applications link against `libvulkan_loader.a` at compile time. This static
library provides auto-open stubs that automatically open `vulkan.library`
and obtain the VulkanIFace when the application starts.

**Generation:**

```bash
# idltool generates the auto-open stub source
idltool -a vulkan.xml -o vulkan_autoinit.c

# Cross-compile the stub to a static library
ppc-amigaos-gcc -mcrt=newlib -c vulkan_autoinit.c -o vulkan_autoinit.o
ppc-amigaos-ar rcs libvulkan_loader.a vulkan_autoinit.o
```

**What the auto-open stub provides:**

```c
/* Auto-generated: opens vulkan.library at program startup */
struct Library *VulkanBase = NULL;
struct VulkanIFace *IVulkan = NULL;

/* Called automatically by newlib CRT before main() */
void __init_vulkan(void) __attribute__((constructor));
void __init_vulkan(void)
{
    VulkanBase = IExec->OpenLibrary("vulkan.library", 1);
    if (VulkanBase) {
        IVulkan = (struct VulkanIFace *)
            IExec->GetInterface(VulkanBase, "main", 1, NULL);
    }
}

/* Called automatically by newlib CRT after main() returns */
void __exit_vulkan(void) __attribute__((destructor));
void __exit_vulkan(void)
{
    if (IVulkan) IExec->DropInterface((struct Interface *)IVulkan);
    if (VulkanBase) IExec->CloseLibrary(VulkanBase);
}
```

With this link library, a minimal Vulkan application becomes:

```c
/* Developer does NOT need to manually OpenLibrary / GetInterface */
#include <proto/vulkan.h>

int main(void)
{
    if (!IVulkan) {
        printf("Vulkan not available\n");
        return 1;
    }

    VkInstance instance;
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkCreateInstance(&ci, NULL, &instance);
    /* ... */
}
```

**Compile command:**

```bash
ppc-amigaos-gcc -mcrt=newlib -o myapp myapp.c \
    -Iinclude/include_h \
    -lvulkan_loader -lauto
```

### 17.7 vulkaninfo Utility

`vulkaninfo` is a standard Vulkan SDK tool that queries and displays all
physical device properties, features, and limits. It is invaluable for
developers and users to verify their Vulkan installation works.

**Output example:**

```
vulkaninfo - AmigaOS 4 Vulkan 1.3

Instance Extensions:
  VK_KHR_surface (v25)
  VK_AMIGA_surface (v1)

Physical Device 0: AMD Radeon RX 580 (GCN 4.0)
  Type:            DISCRETE_GPU
  API Version:     1.3.275
  Driver Version:  1.0.0
  Vendor ID:       0x1002 (AMD)
  Device ID:       0x67DF
  Queue Families:
    [0] Graphics + Compute + Transfer  (count: 1)
    [1] Compute + Transfer             (count: 4)
    [2] Transfer (DMA)                 (count: 1)
  Memory Heaps:
    Heap 0: 8192 MiB (DEVICE_LOCAL)
    Heap 1: 2048 MiB (HOST_VISIBLE | HOST_COHERENT)
  Limits:
    maxImageDimension2D:        16384
    maxFramebufferWidth:        16384
    maxFramebufferHeight:       16384
    maxViewports:               16
    maxBoundDescriptorSets:     32
    maxPushConstantsSize:       256
    maxComputeWorkGroupSize:    1024, 1024, 1024
    maxComputeSharedMemorySize: 65536
  Features:
    geometryShader:             YES
    tessellationShader:         YES
    multiDrawIndirect:          YES
    sparseBinding:              YES
    dynamicRendering:           YES
    synchronization2:           YES
  ...

Physical Device 1: AmigaOS Software Renderer
  Type:            CPU
  API Version:     1.3.0
  ...
```

The source for `vulkaninfo` is available from the Khronos Vulkan-Tools
repository and needs minimal modification for AmigaOS (replace WSI-specific
surface creation with `VK_AMIGA_surface`, adapt command-line parsing).

### 17.8 Example Programs

Each example is a complete, compilable project with its own Makefile,
pre-compiled SPIR-V shaders, and comments explaining every Vulkan call.

**triangle/** -- Minimal Vulkan 1.3 triangle (Section 8 expanded into a
full compilable project):

```
triangle/
+-- triangle.c              # Full source (~200 lines, Section 8)
+-- shaders/
|   +-- triangle.vert       # GLSL vertex shader source
|   +-- triangle.frag       # GLSL fragment shader source
|   +-- vert.spv            # Pre-compiled SPIR-V (ship with example)
|   +-- frag.spv            # Pre-compiled SPIR-V
+-- Makefile                # Build: ppc-amigaos-gcc + Docker
+-- README                  # Build/run instructions
```

**Makefile for examples:**

```makefile
PROJECT = triangle
SOURCES = triangle.c

# Note: ppc-amigaos-gcc predefined __AMIGAOS4__ and __amigaos4__ automatically
# Note: SDK/include/include_h is in the compiler's default search path
CFLAGS = -mcrt=newlib -O2 -Wall

LDFLAGS = -mcrt=newlib
LIBS = -lvulkan_loader -lauto

# Docker cross-compilation (same pattern as project Makefile)
DOCKER_IMAGE = walkero/amigagccondocker:os4-gcc11
DOCKER_CMD = docker run --rm -v "$(shell pwd):/work" -w /work \
    $(DOCKER_IMAGE) ppc-amigaos-gcc

all: $(PROJECT)

$(PROJECT): $(SOURCES)
	$(DOCKER_CMD) -o $@ $< $(CFLAGS) $(LDFLAGS) $(LIBS)

shaders: shaders/triangle.vert shaders/triangle.frag
	glslangValidator -V shaders/triangle.vert -o shaders/vert.spv
	glslangValidator -V shaders/triangle.frag -o shaders/frag.spv

clean:
	rm -f $(PROJECT)
```

**texture/** -- Textured quad with image loading:
- Demonstrates `VkImage`, `VkImageView`, `VkSampler`
- Shows staging buffer upload (host -> device memory transfer)
- Uses `vkCmdCopyBufferToImage`

**compute/** -- Compute shader example:
- Demonstrates `vkCmdDispatch`
- Shows buffer readback (device -> host)
- Simple parallel computation (e.g., vector addition)

**wsi/** -- Window management and surface creation:
- Demonstrates `vkCreateAmigaSurfaceAMIGA`
- Shows Intuition window creation with IDCMP message handling
- Swapchain creation and presentation loop
- Proper cleanup on window close

**NOTE:** The actual implemented examples use numbered naming
(01_enumerate through 22_gltf_viewer). See `docs/implementation-guide.md`
Section 10.2 for the full list of 22 completed examples.

**Shared utility headers** are in `examples/common/`:
- `vk_helpers.h` -- Vulkan boilerplate (init/cleanup)
- `vk_math.h` -- Column-major mat4 math (Batch 9 X1, inspired by cglm)

**Third-party libraries** (optional, see `THIRD_PARTY.md`):
- stb_image.h v2.30 (Sean Barrett, public domain) -- image loading
- cgltf v1.15 (Johannes Kuhlmann, MIT) -- glTF 2.0 model loading

### 17.9 Project Template for New Applications

The SDK includes a template that developers copy to start new Vulkan
projects. This provides the same structure as the VulkanOS4 project itself:

```
Templates/VulkanProject/
+-- src/
|   +-- main.c              # Minimal Vulkan app (open library, create
|   |                       # instance, enumerate devices, clean up)
|   +-- include/            # Project-specific headers
+-- shaders/
|   +-- basic.vert          # Simple passthrough vertex shader
|   +-- basic.frag          # Simple solid-colour fragment shader
|   +-- compile_shaders.sh  # Script to compile GLSL -> SPIR-V
+-- build/                  # Build output directory
+-- .vscode/
|   +-- c_cpp_properties.json  # IntelliSense with Vulkan + SDK paths
|   +-- settings.json          # File associations
|   +-- tasks.json             # Docker build task
|   +-- launch.json            # Debug config
+-- Makefile                # Docker cross-compilation
+-- .gitignore
+-- README.md               # Setup and build instructions
```

### 17.10 Generation Pipeline Summary

The complete build-from-source pipeline, from Khronos registry to
installable SDK:

```
[Developer's host machine (x86 Linux/WSL)]

1. Download Khronos Vulkan-Headers (contains vk.xml)
       |
2. Run genvk_amiga.py --> vulkan_core.h + function_list.txt
       |
3. Run gen_vulkan_xml.py --> vulkan.xml
       |
4. Run idltool (cross) --> interfaces/vulkan.h
       |                    inline4/vulkan.h
       |                    clib/vulkan_protos.h
       |
5. Combine with hand-written files:
       vk_platform.h, vulkan_amiga.h, vulkan.h, vk_icd.h, proto/vulkan.h
       |
6. Cross-compile libvulkan_loader.a
       |
7. Cross-compile vulkaninfo
       |
8. Compile example SPIR-V shaders (glslangValidator)
       |
9. Package into SDK installer
       |
       v
   [AmigaOS 4 target machine]

   install_vulkan_sdk --> copies to SDK: assign
       |
       v
   Developer includes <proto/vulkan.h> and links -lvulkan_loader
```

**Automation:** Steps 1-8 should be automated in a single `make sdk`
target that produces a ready-to-install SDK archive. The entire pipeline
runs on the developer's x86 host -- no AmigaOS machine is needed to
build the SDK.

---

## 18. Application Developer Guide

This section is for programmers writing Vulkan applications on AmigaOS 4.
It covers the practical workflow from first compile to shipping, including
porting existing Vulkan code, shader development, debugging, memory
management, and known limitations.

### 18.1 Quick Start

**Minimum to compile and run a Vulkan program:**

1. Install the Vulkan SDK (headers + `libvulkan_loader.a`) into your SDK
   assign
2. Install `vulkan.library` (the loader) to `LIBS:`
3. Install at least one ICD: `software_vk.library` (for any system) or
   `radeon_vk.library` (for systems with a supported GPU)
4. Install the ICD's JSON manifest to `DEVS:Vulkan/icd.d/`

**Compile command (cross-compilation via Docker):**

```bash
# ppc-amigaos-gcc predefines __AMIGAOS4__ and __amigaos4__ automatically.
# SDK/include/include_h is in the default search path -- no -I needed.
ppc-amigaos-gcc -mcrt=newlib -O2 -o myapp src/main.c -lvulkan_loader -lauto
```

**Compile command (native on AmigaOS):**

```bash
# On native AmigaOS, the SDK: assign points to the SDK root.
# Headers in SDK:include/include_h/ are found via the compiler's default paths.
gcc -mcrt=newlib -O2 -o myapp src/main.c -lvulkan_loader -lauto
```

**Minimal application:**

```c
#include <proto/vulkan.h>
#include <stdio.h>

int main(void)
{
    /* IVulkan is auto-opened by libvulkan_loader.a */
    if (!IVulkan) {
        printf("Vulkan not available\n");
        return 1;
    }

    /* Query Vulkan version */
    uint32_t version;
    vkEnumerateInstanceVersion(&version);
    printf("Vulkan %u.%u.%u available\n",
        VK_API_VERSION_MAJOR(version),
        VK_API_VERSION_MINOR(version),
        VK_API_VERSION_PATCH(version));

    /* Create instance */
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "My App",
        .apiVersion = VK_API_VERSION_1_3
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo
    };
    VkInstance instance;
    VkResult result = vkCreateInstance(&ci, NULL, &instance);
    if (result != VK_SUCCESS) {
        printf("vkCreateInstance failed: %d\n", result);
        return 1;
    }

    /* Enumerate GPUs */
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, NULL);
    printf("Found %u Vulkan device(s)\n", count);

    VkPhysicalDevice *devices = malloc(count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &count, devices);
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        printf("  [%u] %s (%s)\n", i, props.deviceName,
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ?
                "GPU" : "CPU");
    }
    free(devices);

    /* Cleanup */
    vkDestroyInstance(instance, NULL);
    return 0;
}
```

### 18.2 Porting Existing Vulkan Applications

If you have a Vulkan application that runs on Linux or Windows, porting to
AmigaOS requires these changes:

**1. WSI (Window System Integration) -- mandatory change**

Replace platform-specific surface creation with `VK_AMIGA_surface`:

```c
/* BEFORE (Linux/X11): */
#include <vulkan/vulkan_xlib.h>
VkXlibSurfaceCreateInfoKHR ci = {
    .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
    .dpy = display,
    .window = window
};
vkCreateXlibSurfaceKHR(instance, &ci, NULL, &surface);

/* AFTER (AmigaOS): */
#include <vulkan/vulkan_amiga.h>
VkAmigaSurfaceCreateInfoAMIGA ci = {
    .sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO,
    .pScreen = screen,
    .pWindow = window
};
vkCreateAmigaSurfaceAMIGA(instance, &ci, NULL, &surface);
```

Replace the instance extension:
```c
/* BEFORE: */ "VK_KHR_xlib_surface"   or "VK_KHR_win32_surface"
/* AFTER:  */ VK_AMIGA_SURFACE_EXTENSION_NAME   /* "VK_AMIGA_surface" */
```

**2. Window management -- mandatory change**

Replace platform windowing (X11, Win32, GLFW, SDL) with Intuition:

```c
#include <proto/intuition.h>

struct Screen *screen = IIntuition->LockPubScreen(NULL);
struct Window *window = IIntuition->OpenWindowTags(NULL,
    WA_Title,       "My Vulkan App",
    WA_Width,       800,
    WA_Height,      600,
    WA_DragBar,     TRUE,
    WA_CloseGadget, TRUE,
    WA_DepthGadget, TRUE,
    WA_SizeGadget,  TRUE,
    WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE,
    WA_PubScreen,   screen,
    TAG_DONE);

/* Event loop */
BOOL running = TRUE;
while (running) {
    struct IntuiMessage *msg;
    while ((msg = (struct IntuiMessage *)IExec->GetMsg(window->UserPort))) {
        if (msg->Class == IDCMP_CLOSEWINDOW) running = FALSE;
        if (msg->Class == IDCMP_NEWSIZE) {
            /* Window resized -- recreate swapchain */
        }
        IExec->ReplyMsg((struct Message *)msg);
    }
    /* Render frame */
    RenderFrame();
}

IIntuition->CloseWindow(window);
IIntuition->UnlockPubScreen(NULL, screen);
```

If the application uses SDL2 and an AmigaOS SDL2 port with Vulkan backend
is available (Section 15.7), only the SDL2 backend changes -- application
code remains identical.

**3. Library opening -- may need change**

If the application manually loads the Vulkan loader (e.g., `dlopen`
on Linux), replace with the AmigaOS pattern:

```c
/* BEFORE (Linux): */
void *lib = dlopen("libvulkan.so.1", RTLD_NOW);
PFN_vkGetInstanceProcAddr getProc = dlsym(lib, "vkGetInstanceProcAddr");

/* AFTER (AmigaOS) -- Option A: auto-open via link library (recommended) */
#include <proto/vulkan.h>
/* IVulkan is available automatically; use inline macros or vkGetInstanceProcAddr */

/* AFTER (AmigaOS) -- Option B: manual open */
struct Library *VulkanBase = IExec->OpenLibrary("vulkan.library", 1);
struct VulkanIFace *IVulkan = (struct VulkanIFace *)
    IExec->GetInterface(VulkanBase, "main", 1, NULL);
```

**4. Endianness -- usually no change needed**

Vulkan applications pass data in host byte order. The ICD handles all
byte-swapping between the CPU (big-endian PPC) and GPU (little-endian).

However, watch out for these cases where applications embed raw byte data:

| Case | Issue | Fix |
|------|-------|-----|
| Hard-coded pixel data in RGBA byte order | Usually fine -- Vulkan formats specify component order, not byte order | Verify `VK_FORMAT_R8G8B8A8_UNORM` is used correctly |
| Custom file formats with little-endian fields | Application must byte-swap when loading on PPC | Use `__builtin_bswap32` / `__builtin_bswap16` |
| Push constants / uniform data | No issue -- CPU writes in native order, ICD presents to GPU correctly | No change needed |
| Vertex data in buffers | No issue -- same as push constants | No change needed |
| SPIR-V shader files | SPIR-V is LE by spec; `vkCreateShaderModule` handles the swap | No change needed |
| Pre-computed lookup tables in shaders | No issue if stored as float/int uniforms | No change needed |

**5. Compiler flags -- mandatory change**

```bash
# AmigaOS-specific flags:
-mcrt=newlib                    # Use newlib C runtime (only required flag)

# The following are predefined by ppc-amigaos-gcc -- do NOT add manually:
#   __AMIGAOS4__, __amigaos4__ (and 15+ other AMIGA variants)
# SDK/include/include_h is in the default search path -- no -I needed.

# Optional: useful for #ifdef guards in cross-platform code:
-DVK_USE_PLATFORM_AMIGA
```

**6. Summary: what changes, what doesn't**

| Component | Changes Needed |
|-----------|---------------|
| Core Vulkan API calls | **None.** `vkCreateInstance`, `vkCmdDraw`, etc. are identical. |
| Shaders (SPIR-V) | **None.** Same SPIR-V bytecode runs on all platforms. |
| Pipeline creation | **None.** |
| Memory management | **None.** (But see Section 18.4 for PPC-specific tips.) |
| Descriptor sets | **None.** |
| Synchronisation | **None.** |
| WSI (surface/swapchain) | **Change required.** Use `VK_AMIGA_surface`. |
| Window management | **Change required.** Use Intuition (or SDL2 if ported). |
| Library loading | **Change required.** Use `proto/vulkan.h` + link library. |
| Build system | **Change required.** PPC cross-compiler + AmigaOS flags. |
| Endianness | **Usually none.** App data is host-order; ICD handles GPU order. |

### 18.3 Shader Development Workflow

Vulkan shaders are written in GLSL (or HLSL), compiled to SPIR-V bytecode
offline, and shipped with the application as `.spv` files.

**Workflow:**

```
1. Write shader        2. Compile to SPIR-V     3. Ship .spv files
   (GLSL source)          (on host machine)         (with application)

   shader.vert    -->   glslangValidator    -->   vert.spv
   shader.frag    -->   -V shader.vert      -->   frag.spv
                        -o vert.spv
```

**Step 1: Write GLSL shaders**

Use any text editor. Target GLSL 4.50 or 4.60 (Vulkan profile):

```glsl
/* shader.vert -- vertex shader */
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
}
```

```glsl
/* shader.frag -- fragment shader */
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    outColor = texture(texSampler, fragTexCoord);
}
```

**Step 2: Compile to SPIR-V**

On the developer's host machine (x86 Linux/Windows/macOS):

```bash
# Compile vertex shader
glslangValidator -V shader.vert -o vert.spv

# Compile fragment shader
glslangValidator -V shader.frag -o frag.spv

# Optional: optimise (reduces shader size, can improve GPU perf)
spirv-opt -O vert.spv -o vert.spv
spirv-opt -O frag.spv -o frag.spv

# Optional: validate (catches errors before runtime)
spirv-val vert.spv
spirv-val frag.spv

# Optional: disassemble (human-readable, for debugging)
spirv-dis vert.spv
```

If `glslangValidator` has been ported to AmigaOS PPC (it already ships with
Warp3D Nova), shaders can also be compiled directly on the target machine.
However, compiling on the x86 host is much faster.

**Step 3: Load in application**

```c
/* Read .spv file into memory */
FILE *f = fopen("PROGDIR:shaders/vert.spv", "rb");
fseek(f, 0, SEEK_END);
size_t size = ftell(f);
rewind(f);
uint32_t *code = malloc(size);
fread(code, 1, size, f);
fclose(f);

/* Create shader module -- ICD handles LE byte-swap internally */
VkShaderModuleCreateInfo ci = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = size,
    .pCode = code
};
VkShaderModule module;
vkCreateShaderModule(device, &ci, NULL, &module);
free(code);
```

**Automating shader compilation in the Makefile:**

```makefile
SHADERS_SRC = $(wildcard shaders/*.vert shaders/*.frag shaders/*.comp)
SHADERS_SPV = $(SHADERS_SRC:=.spv)

shaders: $(SHADERS_SPV)

%.vert.spv: %.vert
	glslangValidator -V $< -o $@

%.frag.spv: %.frag
	glslangValidator -V $< -o $@

%.comp.spv: %.comp
	glslangValidator -V $< -o $@
```

### 18.4 Memory Management Best Practices

Vulkan gives applications explicit control over GPU memory. On AmigaOS PPC,
the memory hierarchy has platform-specific characteristics that affect
performance.

**Memory types on a typical AmigaOS Vulkan system (RX 580):**

| Memory Type | Properties | Size | Use For |
|-------------|-----------|------|---------|
| Device-local (VRAM) | `DEVICE_LOCAL` | 8 GB | Textures, framebuffers, static vertex/index buffers -- anything the GPU reads frequently |
| Host-visible (BAR aperture) | `HOST_VISIBLE` &#124; `HOST_COHERENT` | 256 MB | Staging buffers, uniform buffers updated per-frame, small dynamic vertex data |
| Host-visible + device-local (Resizable BAR, if available) | `DEVICE_LOCAL` &#124; `HOST_VISIBLE` | Varies | CPU-writable GPU memory -- ideal for streaming, avoids staging copy |

**Recommended patterns:**

**Static resources (textures, meshes loaded once):**
```
1. Create staging buffer in HOST_VISIBLE memory
2. Map staging buffer, copy data from CPU
3. Create final buffer/image in DEVICE_LOCAL memory
4. vkCmdCopyBufferToImage / vkCmdCopyBuffer (GPU DMA copy)
5. Free staging buffer
```
The GPU DMA copy is fast and does not use the CPU. The staging buffer is
temporary.

**Dynamic resources (uniform buffers updated every frame):**
```
1. Create buffer in HOST_VISIBLE | HOST_COHERENT memory
2. Map buffer ONCE (persistent mapping)
3. Each frame: write new data, call vkCmdBindDescriptorSets
   (no unmap/remap needed)
```
Persistent mapping avoids the overhead of `vkMapMemory` / `vkUnmapMemory`
per frame.

**Vulkan Memory Allocator (VMA):**

For most applications, using the VMA helper library (AMD, MIT license)
is strongly recommended instead of managing `vkAllocateMemory` directly.
VMA handles sub-allocation, memory type selection, and defragmentation:

```c
#include "vk_mem_alloc.h"

VmaAllocatorCreateInfo allocCI = {
    .physicalDevice = physicalDevice,
    .device = device,
    .instance = instance
};
VmaAllocator allocator;
vmaCreateAllocator(&allocCI, &allocator);

/* Allocate a buffer with VMA -- it picks the right memory type */
VkBufferCreateInfo bufCI = { /* ... */ };
VmaAllocationCreateInfo allocInfo = {
    .usage = VMA_MEMORY_USAGE_GPU_ONLY  /* VMA picks DEVICE_LOCAL */
};
VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufCI, &allocInfo, &buffer, &allocation, NULL);
```

VMA is included in the SDK (Stream D, Section 10) and requires no
AmigaOS-specific modifications beyond the Vulkan headers.

### 18.5 Debugging Vulkan Applications

**Validation layers (first line of defence):**

Enable the Khronos validation layer during development. It catches most
API usage errors (invalid parameters, missing synchronisation, resource
leaks) and reports them via a debug callback:

```c
/* Enable validation at instance creation */
const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
VkInstanceCreateInfo ci = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .enabledLayerCount = 1,
    .ppEnabledLayerNames = layers,
    /* ... */
};

/* Set up debug messenger to receive validation messages */
VkDebugUtilsMessengerCreateInfoEXT debugCI = {
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = DebugCallback
};

/* Callback function */
static VkBool32 DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *userData)
{
    IExec->DebugPrintF("[Vulkan Validation] %s\n", data->pMessage);
    return VK_FALSE;
}
```

Validation layers have zero overhead when not loaded. Never ship with
validation enabled in production builds.

**Serial debug output:**

AmigaOS serial debug (`IExec->DebugPrintF`) is the primary output channel
for low-level debugging. Both the loader and ICD output diagnostic messages
to serial. Use a serial terminal or Sashimi to capture output.

```c
/* Application-level debug logging */
#ifdef DEBUG
  #define DPRINTF(fmt, ...) IExec->DebugPrintF("[MyApp] " fmt, ##__VA_ARGS__)
#else
  #define DPRINTF(fmt, ...)
#endif

DPRINTF("Creating swapchain: %ux%u\n", width, height);
```

**Common errors and their causes:**

| Error | Likely Cause | Fix |
|-------|-------------|-----|
| `VK_ERROR_INCOMPATIBLE_DRIVER` | No ICD installed, or ICD JSON manifest missing from `DEVS:Vulkan/icd.d/` | Install ICD and manifest |
| `VK_ERROR_INITIALIZATION_FAILED` | ICD found but GPU init failed (wrong card, driver mismatch) | Check `vulkaninfo` output, verify GPU is supported |
| `VK_ERROR_OUT_OF_DEVICE_MEMORY` | VRAM exhausted | Reduce texture sizes, use compressed formats (BC), free unused resources |
| `VK_ERROR_OUT_OF_HOST_MEMORY` | System RAM exhausted | Reduce staging buffer sizes, free memory earlier |
| `VK_ERROR_DEVICE_LOST` | GPU hang (shader infinite loop, invalid command buffer, hardware fault) | Check serial output for ICD error messages; simplify shaders; check synchronisation |
| `VK_ERROR_SURFACE_LOST_KHR` | Intuition screen mode changed or window closed while presenting | Recreate surface and swapchain |
| `VK_ERROR_OUT_OF_DATE_KHR` | Window resized | Recreate swapchain with new dimensions |
| Black screen, no errors | Presentation works but rendering is wrong | Use validation layers; check vertex winding order; check viewport/scissor; verify shader outputs |
| Corrupted pixels | Synchronisation missing between render and present | Add proper semaphore waits; check pipeline barriers |

**Using the software ICD for debugging:**

If a rendering bug might be in the GPU ICD, test the same application against
`software_vk.library` to determine if the bug is in the app or the driver:

```bash
# Force the software ICD (bypass hardware ICD)
setenv VK_ICD_FILENAMES DEVS:Vulkan/icd.d/software_vk.json
myapp

# If the bug disappears: it's a hardware ICD bug -- report it
# If the bug remains: it's an application bug -- fix your code
```

### 18.6 Performance Tips for AmigaOS

**1. Use `vkGetDeviceProcAddr` for hot-path functions (Section 13.3)**

```c
/* At init time: */
PFN_vkCmdDraw pfnDraw = (PFN_vkCmdDraw)
    vkGetDeviceProcAddr(device, "vkCmdDraw");

/* In render loop -- bypasses loader vtable entirely: */
pfnDraw(cmdBuf, vertexCount, 1, 0, 0);
```

**2. Minimise pipeline creation during rendering**

Pipeline creation triggers SPIR-V compilation, which is slow on PPC
(Section 13.7). Create all pipelines at load time, not during gameplay:

```c
/* GOOD: create all pipelines during loading screen */
for (int i = 0; i < materialCount; i++) {
    CreatePipeline(materials[i].shader, &materials[i].pipeline);
}

/* BAD: create pipeline on first use during rendering */
if (!material->pipeline) {
    CreatePipeline(material->shader, &material->pipeline);  /* STALL */
}
```

**3. Use pipeline cache and serialise to disk**

```c
/* At startup: load cached pipelines from disk */
VkPipelineCacheCreateInfo cacheCI = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    .initialDataSize = LoadFile("PROGDIR:pipeline.cache", &cacheData),
    .pInitialData = cacheData
};
vkCreatePipelineCache(device, &cacheCI, NULL, &cache);

/* At shutdown: save cache to disk */
size_t cacheSize;
vkGetPipelineCacheData(device, cache, &cacheSize, NULL);
void *data = malloc(cacheSize);
vkGetPipelineCacheData(device, cache, &cacheSize, data);
SaveFile("PROGDIR:pipeline.cache", data, cacheSize);
```

First run: shaders compile (slow). Subsequent runs: cache hit (instant).

**4. Batch draw calls**

Each `vkCmdDraw` has overhead. Combine meshes into larger vertex buffers
and use instancing or multi-draw-indirect where possible:

```c
/* Instead of 100 separate draw calls: */
for (int i = 0; i < 100; i++)
    vkCmdDraw(cmd, meshes[i].vertexCount, 1, meshes[i].offset, 0);

/* Use one indirect draw call: */
vkCmdDrawIndirect(cmd, indirectBuffer, 0, 100, sizeof(VkDrawIndirectCommand));
```

**5. Keep transfers off the graphics queue**

Use the DMA/copy queue (if available) for texture uploads and buffer copies.
This lets the GPU render and transfer simultaneously:

```c
/* Submit upload work to the DMA queue */
vkQueueSubmit(transferQueue, 1, &transferSubmit, transferFence);
/* Submit render work to the graphics queue -- can overlap with transfer */
vkQueueSubmit(graphicsQueue, 1, &renderSubmit, renderFence);
```

### 18.7 Known Limitations by Implementation Phase

Developers should be aware of which Vulkan features are available at each
phase of the implementation. Applications should query features via
`vkGetPhysicalDeviceFeatures` rather than assuming all features are present.

**Software ICD (`software_vk.library`):**

| Phase | Available | Not Yet Available |
|-------|-----------|-------------------|
| A0 | Instance, device, command buffers, basic rendering (triangles), fences | Textures, compute, most features |
| A1 | + textures, samplers, indexed drawing | Compute, geometry/tessellation, most extensions |
| A2 (JIT) | + improved performance | Same feature set as A1, just faster |

**Hardware ICD (`radeon_vk.library`):**

| Phase | Available | Not Yet Available |
|-------|-----------|-------------------|
| B1 | Instance, device, memory, basic command submission | Rendering (no shader compiler yet) |
| B2 | + full graphics pipeline, vertex/index buffers, textures, descriptors, push constants, swapchain presentation | Compute, geometry/tessellation, sparse resources, timeline semaphores |
| B3 | Full Vulkan 1.3 core | Extensions beyond core may be added incrementally |

**Feature detection pattern (applications should always do this):**

```c
VkPhysicalDeviceFeatures features;
vkGetPhysicalDeviceFeatures(physicalDevice, &features);

if (!features.geometryShader) {
    printf("Geometry shaders not supported -- using fallback\n");
    useGeometryShader = FALSE;
}

if (!features.multiDrawIndirect) {
    printf("Multi-draw-indirect not supported -- using loop\n");
    useMultiDrawIndirect = FALSE;
}

/* Enable only the features you actually use */
VkPhysicalDeviceFeatures enabledFeatures = {0};
enabledFeatures.samplerAnisotropy = features.samplerAnisotropy;
enabledFeatures.multiDrawIndirect = features.multiDrawIndirect;

VkDeviceCreateInfo deviceCI = {
    /* ... */
    .pEnabledFeatures = &enabledFeatures
};
```

### 18.8 Cross-Platform Code Structure

For applications targeting both AmigaOS and Linux/Windows, use preprocessor
guards to isolate platform-specific code:

```c
/* platform.h -- platform abstraction */

#if defined(__amigaos4__)
    #define VK_USE_PLATFORM_AMIGA
    #include <proto/exec.h>
    #include <proto/intuition.h>
    #include <proto/vulkan.h>
    #include <vulkan/vulkan_amiga.h>
#elif defined(__linux__)
    #define VK_USE_PLATFORM_XCB_KHR
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_xcb.h>
#elif defined(_WIN32)
    #define VK_USE_PLATFORM_WIN32_KHR
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_win32.h>
#endif

/* platform_surface.c -- WSI abstraction */
VkResult CreateSurface(VkInstance instance, PlatformWindow *win,
                       VkSurfaceKHR *surface)
{
#if defined(VK_USE_PLATFORM_AMIGA)
    VkAmigaSurfaceCreateInfoAMIGA ci = {
        .sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO,
        .pScreen = win->screen,
        .pWindow = win->window
    };
    return vkCreateAmigaSurfaceAMIGA(instance, &ci, NULL, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    VkXcbSurfaceCreateInfoKHR ci = { /* ... */ };
    return vkCreateXcbSurfaceKHR(instance, &ci, NULL, surface);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR ci = { /* ... */ };
    return vkCreateWin32SurfaceKHR(instance, &ci, NULL, surface);
#endif
}
```

All non-WSI Vulkan code (pipeline creation, rendering, memory management,
shaders) is identical across all platforms. Only the surface creation and
window management code needs platform guards.
