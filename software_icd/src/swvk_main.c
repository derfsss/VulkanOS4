/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_main.c -- Library skeleton: resident tag, init, open, close, expunge
**                ICD interface: GetInstanceProcAddr, NegotiateVersion
**
** Follows the same AmigaOS 4 shared library conventions as vulkan.library
** (loader_main.c). The key difference is the "main" interface exposes
** the ICD entry points instead of VulkanIFace.
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/interfaces.h>
#include <interfaces/exec.h>
#include <string.h>

#include "swvk_internal.h"

/****************************************************************************/

#ifndef USED
#define USED __attribute__((used))
#endif

static const char USED lib_verstag[] = "$VER: " LIBVSTRING;

/****************************************************************************/
/* Stub entry point -- prevents linker warning for -nostartfiles             */
/****************************************************************************/

int _start(void)
{
    return -1;
}

/****************************************************************************/
/* Global interface pointers                                                */
/****************************************************************************/

struct ExecIFace  *IExec   = NULL;

/* newlib support (needed for string.h functions) */
static struct Library   *NewlibBase = NULL;
struct Interface        *INewlib    = NULL;

/****************************************************************************/
/* ICD interface struct (matches loader's ICDMainIFace layout)              */
/****************************************************************************/

struct SWVKICDIFace
{
    struct InterfaceData Data;
    uint32 APICALL (*Obtain)(struct SWVKICDIFace *Self);
    uint32 APICALL (*Release)(struct SWVKICDIFace *Self);
    void   APICALL (*Expunge)(struct SWVKICDIFace *Self);
    struct Interface * APICALL (*Clone)(struct SWVKICDIFace *Self);
    PFN_vkVoidFunction APICALL (*vk_icdGetInstanceProcAddr)(struct SWVKICDIFace *Self, VkInstance instance, const char *pName);
    VkResult APICALL (*vk_icdNegotiateLoaderICDInterfaceVersion)(struct SWVKICDIFace *Self, uint32_t *pSupportedVersion);
    PFN_vkVoidFunction APICALL (*vk_icdGetPhysicalDeviceProcAddr)(struct SWVKICDIFace *Self, VkInstance instance, const char *pName);
};

/****************************************************************************/
/* Forward declarations                                                     */
/****************************************************************************/

static uint32 _manager_Obtain(struct LibraryManagerInterface *Self);
static uint32 _manager_Release(struct LibraryManagerInterface *Self);
static struct SWVKLibBase *_manager_Open(struct LibraryManagerInterface *Self, uint32 version);
static BPTR _manager_Close(struct LibraryManagerInterface *Self);
static BPTR _manager_Expunge(struct LibraryManagerInterface *Self);

static uint32 APICALL _main_Obtain(struct SWVKICDIFace *Self);
static uint32 APICALL _main_Release(struct SWVKICDIFace *Self);

/* ICD interface entry points */
static PFN_vkVoidFunction APICALL _icd_GetInstanceProcAddr(struct SWVKICDIFace *Self, VkInstance instance, const char *pName);
static VkResult APICALL _icd_NegotiateLoaderICDInterfaceVersion(struct SWVKICDIFace *Self, uint32_t *pSupportedVersion);
static PFN_vkVoidFunction APICALL _icd_GetPhysicalDeviceProcAddr(struct SWVKICDIFace *Self, VkInstance instance, const char *pName);

/****************************************************************************/
/* Main interface (ICD entry points) vector table                           */
/* Order MUST match the struct SWVKICDIFace field order AND the loader's    */
/* ICDMainIFace struct in loader_icd.c                                      */
/****************************************************************************/

static const APTR _main_Vectors[] =
{
    /* Standard AmigaOS interface methods */
    (APTR)_main_Obtain,
    (APTR)_main_Release,
    NULL,                               /* Expunge */
    NULL,                               /* Clone */

    /* ICD entry points (Design 6.1) */
    (APTR)_icd_GetInstanceProcAddr,
    (APTR)_icd_NegotiateLoaderICDInterfaceVersion,
    (APTR)_icd_GetPhysicalDeviceProcAddr,

    (APTR)-1                            /* Sentinel */
};

static const struct TagItem _main_Tags[] =
{
    {MIT_Name,        (Tag)"main"},
    {MIT_VectorTable, (Tag)_main_Vectors},
    {MIT_Version,     1},
    {TAG_DONE,        0}
};

/****************************************************************************/
/* Manager interface vector table                                           */
/****************************************************************************/

static const APTR _manager_Vectors[] =
{
    (APTR)_manager_Obtain,
    (APTR)_manager_Release,
    NULL,                       /* Expunge -- handled by Close */
    NULL,                       /* Clone */
    (APTR)_manager_Open,
    (APTR)_manager_Close,
    (APTR)_manager_Expunge,
    NULL,                       /* Reserved */
    (APTR)-1                    /* Sentinel */
};

static const struct TagItem _manager_Tags[] =
{
    {MIT_Name,        (Tag)"__library"},
    {MIT_VectorTable, (Tag)_manager_Vectors},
    {MIT_Version,     1},
    {TAG_DONE,        0}
};

/****************************************************************************/
/* Interface list and init tags                                             */
/****************************************************************************/

static const CONST_APTR _lib_Interfaces[] =
{
    (CONST_APTR)_manager_Tags,
    (CONST_APTR)_main_Tags,
    NULL
};

static struct Library *_lib_Init(struct Library *libBase, BPTR seglist,
                                 struct Interface *exec);

static const struct TagItem _lib_InitTags[] =
{
    {CLT_DataSize,      sizeof(struct SWVKLibBase)},
    {CLT_Interfaces,    (Tag)_lib_Interfaces},
    {CLT_InitFunc,      (Tag)_lib_Init},
    {CLT_NoLegacyIFace, TRUE},
    {TAG_DONE,          0}
};

/****************************************************************************/
/* Resident structure (ROMTAG)                                              */
/****************************************************************************/

static const struct Resident lib_res USED =
{
    RTC_MATCHWORD,
    (struct Resident *)&lib_res,
    (struct Resident *)(&lib_res + 1),
    RTF_NATIVE | RTF_AUTOINIT,
    LIBVER,
    NT_LIBRARY,
    0,                          /* Priority */
    LIBNAME,
    LIBVSTRING,
    (APTR)_lib_InitTags
};

/****************************************************************************/
/* Dependency management                                                    */
/****************************************************************************/

static BOOL _open_dependencies(void)
{
    NewlibBase = IExec->OpenLibrary("newlib.library", 4);
    if (!NewlibBase) return FALSE;
    INewlib = IExec->GetInterface(NewlibBase, "main", 1, NULL);
    if (!INewlib) return FALSE;

    return TRUE;
}

static void _close_dependencies(void)
{
    swvk_CloseGraphics();
    if (INewlib)    { IExec->DropInterface(INewlib); INewlib = NULL; }
    if (NewlibBase) { IExec->CloseLibrary(NewlibBase); NewlibBase = NULL; }
}

/****************************************************************************/
/* Library initialization                                                   */
/****************************************************************************/

static struct Library *_lib_Init(struct Library *libBase, BPTR seglist,
                                 struct Interface *exec)
{
    struct SWVKLibBase *base = (struct SWVKLibBase *)libBase;

    base->lib_IExec = (struct ExecIFace *)exec;
    IExec = (struct ExecIFace *)exec;
    base->lib_SegList = seglist;

    base->lib_Lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib_Lib.lib_Node.ln_Pri  = 0;
    base->lib_Lib.lib_Node.ln_Name = LIBNAME;
    base->lib_Lib.lib_Flags        = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib_Lib.lib_Version      = LIBVER;
    base->lib_Lib.lib_Revision     = LIBREV;
    base->lib_Lib.lib_IdString     = LIBVSTRING;

    if (!_open_dependencies())
    {
        _close_dependencies();
        return NULL;
    }

    D(("[software_vk] Initialized v%ld.%ld\n",
                       (long)LIBVER, (long)LIBREV));

    return (struct Library *)base;
}

/****************************************************************************/
/* Manager interface implementation                                         */
/****************************************************************************/

static uint32 _manager_Obtain(struct LibraryManagerInterface *Self)
{
    return ++Self->Data.RefCount;
}

static uint32 _manager_Release(struct LibraryManagerInterface *Self)
{
    return Self->Data.RefCount--;
}

static struct SWVKLibBase *_manager_Open(struct LibraryManagerInterface *Self,
                                         uint32 version)
{
    struct SWVKLibBase *base = (struct SWVKLibBase *)Self->Data.LibBase;

    if (version > LIBVER)
        return NULL;

    base->lib_Lib.lib_OpenCnt++;
    base->lib_Lib.lib_Flags &= ~LIBF_DELEXP;

    return base;
}

static BPTR _manager_Close(struct LibraryManagerInterface *Self)
{
    struct SWVKLibBase *base = (struct SWVKLibBase *)Self->Data.LibBase;

    base->lib_Lib.lib_OpenCnt--;

    if (base->lib_Lib.lib_OpenCnt == 0)
    {
        if (base->lib_Lib.lib_Flags & LIBF_DELEXP)
            return _manager_Expunge(Self);
    }

    return (BPTR)0;
}

static BPTR _manager_Expunge(struct LibraryManagerInterface *Self)
{
    struct SWVKLibBase *base = (struct SWVKLibBase *)Self->Data.LibBase;

    if (base->lib_Lib.lib_OpenCnt == 0)
    {
        BPTR seglist = base->lib_SegList;

        _close_dependencies();

        IExec->Remove((struct Node *)base);
        IExec->DeleteLibrary((struct Library *)base);

        return seglist;
    }
    else
    {
        base->lib_Lib.lib_Flags |= LIBF_DELEXP;
        return (BPTR)0;
    }
}

/****************************************************************************/
/* Main interface Obtain/Release                                            */
/****************************************************************************/

static uint32 APICALL _main_Obtain(struct SWVKICDIFace *Self)
{
    return ++Self->Data.RefCount;
}

static uint32 APICALL _main_Release(struct SWVKICDIFace *Self)
{
    return Self->Data.RefCount--;
}

/****************************************************************************/
/* APICALL trampolines                                                      */
/*                                                                          */
/* On AmigaOS 4, APICALL (__attribute__((libcall))) passes Self in r3,      */
/* shifting all visible arguments by one register position. When the loader */
/* stores ICD function pointers in VulkanIFace (which uses APICALL), we     */
/* need APICALL wrapper functions that receive and discard Self, then call   */
/* the standard-C Vulkan functions with the remaining arguments.            */
/*                                                                          */
/* Without these trampolines, every call through VulkanIFace would pass     */
/* Self as the first Vulkan argument, corrupting all subsequent arguments.  */
/****************************************************************************/

/* Trampoline macros: _T(name) generates an APICALL wrapper that discards
** Self and forwards arguments to the real swvk_* function. */

/* VkResult-returning trampolines */
#define T_R1(name, t1) \
    static VkResult APICALL _t_##name(void *S, t1 a) { (void)S; return name(a); }
#define T_R2(name, t1, t2) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b) { (void)S; return name(a,b); }
#define T_R3(name, t1, t2, t3) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c) { (void)S; return name(a,b,c); }
#define T_R4(name, t1, t2, t3, t4) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d) { (void)S; return name(a,b,c,d); }
#define T_R5(name, t1, t2, t3, t4, t5) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e) { (void)S; return name(a,b,c,d,e); }
#define T_R6(name, t1, t2, t3, t4, t5, t6) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) { (void)S; return name(a,b,c,d,e,f); }

/* void-returning trampolines */
#define T_V1(name, t1) \
    static void APICALL _t_##name(void *S, t1 a) { (void)S; name(a); }
#define T_V2(name, t1, t2) \
    static void APICALL _t_##name(void *S, t1 a, t2 b) { (void)S; name(a,b); }
#define T_V3(name, t1, t2, t3) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c) { (void)S; name(a,b,c); }
#define T_V4(name, t1, t2, t3, t4) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d) { (void)S; name(a,b,c,d); }
#define T_V5(name, t1, t2, t3, t4, t5) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e) { (void)S; name(a,b,c,d,e); }

/* --- Instance --- */
/* NOTE: swvk_CreateInstance, swvk_DestroyInstance, swvk_EnumerateInstanceVersion,
** and swvk_EnumerateInstanceExtensionProperties do NOT get trampolines.
** The loader calls these directly as standard PFN_vk* function pointers,
** not through VulkanIFace. */
T_R3(swvk_EnumeratePhysicalDevices, VkInstance, uint32_t*, VkPhysicalDevice*)

/* --- Physical device queries --- */
T_V2(swvk_GetPhysicalDeviceProperties, VkPhysicalDevice, VkPhysicalDeviceProperties*)
T_V2(swvk_GetPhysicalDeviceFeatures, VkPhysicalDevice, VkPhysicalDeviceFeatures*)
T_V3(swvk_GetPhysicalDeviceQueueFamilyProperties, VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*)
T_V2(swvk_GetPhysicalDeviceMemoryProperties, VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*)
T_V3(swvk_GetPhysicalDeviceFormatProperties, VkPhysicalDevice, VkFormat, VkFormatProperties*)

/* --- Device --- */
T_R4(swvk_CreateDevice, VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*)
T_V2(swvk_DestroyDevice, VkDevice, const VkAllocationCallbacks*)
T_V4(swvk_GetDeviceQueue, VkDevice, uint32_t, uint32_t, VkQueue*)
T_R1(swvk_DeviceWaitIdle, VkDevice)
T_R1(swvk_QueueWaitIdle, VkQueue)

/* --- Memory --- */
T_R4(swvk_AllocateMemory, VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*)
T_V3(swvk_FreeMemory, VkDevice, VkDeviceMemory, const VkAllocationCallbacks*)
T_R6(swvk_MapMemory, VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**)
T_V2(swvk_UnmapMemory, VkDevice, VkDeviceMemory)

/* --- Buffer --- */
T_R4(swvk_CreateBuffer, VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*)
T_V3(swvk_DestroyBuffer, VkDevice, VkBuffer, const VkAllocationCallbacks*)
T_V3(swvk_GetBufferMemoryRequirements, VkDevice, VkBuffer, VkMemoryRequirements*)
T_R4(swvk_BindBufferMemory, VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize)

/* --- Image --- */
T_R4(swvk_CreateImage, VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*)
T_V3(swvk_DestroyImage, VkDevice, VkImage, const VkAllocationCallbacks*)
T_V3(swvk_GetImageMemoryRequirements, VkDevice, VkImage, VkMemoryRequirements*)
T_R4(swvk_BindImageMemory, VkDevice, VkImage, VkDeviceMemory, VkDeviceSize)
T_R4(swvk_CreateImageView, VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*)
T_V3(swvk_DestroyImageView, VkDevice, VkImageView, const VkAllocationCallbacks*)

/* --- Sampler --- */
T_R4(swvk_CreateSampler, VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*)
T_V3(swvk_DestroySampler, VkDevice, VkSampler, const VkAllocationCallbacks*)

/* --- Shader --- */
T_R4(swvk_CreateShaderModule, VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*)
T_V3(swvk_DestroyShaderModule, VkDevice, VkShaderModule, const VkAllocationCallbacks*)

/* --- Pipeline --- */
T_R4(swvk_CreatePipelineLayout, VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*)
T_V3(swvk_DestroyPipelineLayout, VkDevice, VkPipelineLayout, const VkAllocationCallbacks*)
T_R4(swvk_CreatePipelineCache, VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*)
T_V3(swvk_DestroyPipelineCache, VkDevice, VkPipelineCache, const VkAllocationCallbacks*)
T_R6(swvk_CreateGraphicsPipelines, VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*)
T_V3(swvk_DestroyPipeline, VkDevice, VkPipeline, const VkAllocationCallbacks*)

/* --- Descriptor sets --- */
T_R4(swvk_CreateDescriptorSetLayout, VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
T_V3(swvk_DestroyDescriptorSetLayout, VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*)
T_R4(swvk_CreateDescriptorPool, VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*)
T_V3(swvk_DestroyDescriptorPool, VkDevice, VkDescriptorPool, const VkAllocationCallbacks*)
T_R3(swvk_AllocateDescriptorSets, VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*)
T_R4(swvk_FreeDescriptorSets, VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*)
T_V5(swvk_UpdateDescriptorSets, VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*)

/* --- Command buffer --- */
T_R4(swvk_CreateCommandPool, VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*)
T_V3(swvk_DestroyCommandPool, VkDevice, VkCommandPool, const VkAllocationCallbacks*)
T_R3(swvk_AllocateCommandBuffers, VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*)
T_V4(swvk_FreeCommandBuffers, VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*)
T_R2(swvk_BeginCommandBuffer, VkCommandBuffer, const VkCommandBufferBeginInfo*)
T_R1(swvk_EndCommandBuffer, VkCommandBuffer)

/* --- Command recording --- */
T_V3(swvk_CmdBindPipeline, VkCommandBuffer, VkPipelineBindPoint, VkPipeline)
T_V4(swvk_CmdSetViewport, VkCommandBuffer, uint32_t, uint32_t, const VkViewport*)
T_V4(swvk_CmdSetScissor, VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*)
T_V5(swvk_CmdDraw, VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t)
T_V2(swvk_CmdBeginRendering, VkCommandBuffer, const VkRenderingInfo*)
T_V1(swvk_CmdEndRendering, VkCommandBuffer)

/* --- Push constants, vertex/index buffers, draw indexed --- */
/* vkCmdPushConstants has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdPushConstants(void *S, VkCommandBuffer a, VkPipelineLayout b,
    VkShaderStageFlags c, uint32_t d, uint32_t e, const void *f)
{ (void)S; swvk_CmdPushConstants(a,b,c,d,e,f); }

T_V5(swvk_CmdBindVertexBuffers, VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*)
T_V4(swvk_CmdBindIndexBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType)

/* vkCmdDrawIndexed has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdDrawIndexed(void *S, VkCommandBuffer a, uint32_t b,
    uint32_t c, uint32_t d, int32_t e, uint32_t f)
{ (void)S; swvk_CmdDrawIndexed(a,b,c,d,e,f); }

/* vkCmdBindDescriptorSets has 8 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdBindDescriptorSets(void *S, VkCommandBuffer a,
    VkPipelineBindPoint b, VkPipelineLayout c, uint32_t d, uint32_t e,
    const VkDescriptorSet *f, uint32_t g, const uint32_t *h)
{ (void)S; swvk_CmdBindDescriptorSets(a,b,c,d,e,f,g,h); }

/* --- Queue submit --- */
T_R4(swvk_QueueSubmit, VkQueue, uint32_t, const VkSubmitInfo*, VkFence)

/* --- Sync --- */
T_R4(swvk_CreateFence, VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*)
T_V3(swvk_DestroyFence, VkDevice, VkFence, const VkAllocationCallbacks*)
T_R5(swvk_WaitForFences, VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t)
T_R3(swvk_ResetFences, VkDevice, uint32_t, const VkFence*)
T_R4(swvk_CreateSemaphore, VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*)
T_V3(swvk_DestroySemaphore, VkDevice, VkSemaphore, const VkAllocationCallbacks*)

/* --- Device extension enumeration --- */
T_R4(swvk_EnumerateDeviceExtensionProperties, VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*)

/* --- WSI surface queries --- */
T_R4(swvk_GetPhysicalDeviceSurfaceSupportKHR, VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*)
T_R3(swvk_GetPhysicalDeviceSurfaceCapabilitiesKHR, VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*)
T_R4(swvk_GetPhysicalDeviceSurfaceFormatsKHR, VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*)
T_R4(swvk_GetPhysicalDeviceSurfacePresentModesKHR, VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*)

/* --- WSI swapchain --- */
T_R4(swvk_CreateSwapchainKHR, VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*)
T_V3(swvk_DestroySwapchainKHR, VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*)
T_R4(swvk_GetSwapchainImagesKHR, VkDevice, VkSwapchainKHR, uint32_t*, VkImage*)
T_R6(swvk_AcquireNextImageKHR, VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*)
T_R2(swvk_QueuePresentKHR, VkQueue, const VkPresentInfoKHR*)

/* --- Stubs --- */
T_R3(swvk_FlushMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*)
T_R3(swvk_InvalidateMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*)
T_R2(swvk_ResetCommandBuffer, VkCommandBuffer, VkCommandBufferResetFlags)
T_R3(swvk_ResetCommandPool, VkDevice, VkCommandPool, VkCommandPoolResetFlags)
T_V3(swvk_TrimCommandPool, VkDevice, VkCommandPool, VkFlags)
T_R2(swvk_GetFenceStatus, VkDevice, VkFence)
T_R4(swvk_GetPipelineCacheData, VkDevice, VkPipelineCache, size_t*, void*)
T_R4(swvk_MergePipelineCaches, VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*)
T_R3(swvk_ResetDescriptorPool, VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags)

/* vkCmdPipelineBarrier has 10 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdPipelineBarrier(void *S, VkCommandBuffer a,
    VkPipelineStageFlags b, VkPipelineStageFlags c, VkDependencyFlags d,
    uint32_t e, const void *f, uint32_t g, const void *h,
    uint32_t ii, const void *j)
{ (void)S; swvk_CmdPipelineBarrier(a,b,c,d,e,f,g,h,ii,j); }

/* --- Legacy render pass --- */
T_R4(swvk_CreateRenderPass, VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
T_V3(swvk_DestroyRenderPass, VkDevice, VkRenderPass, const VkAllocationCallbacks*)
T_R4(swvk_CreateFramebuffer, VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*)
T_V3(swvk_DestroyFramebuffer, VkDevice, VkFramebuffer, const VkAllocationCallbacks*)
T_V3(swvk_CmdBeginRenderPass, VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents)
T_V1(swvk_CmdEndRenderPass, VkCommandBuffer)
T_V2(swvk_CmdNextSubpass, VkCommandBuffer, VkSubpassContents)

/* --- Dynamic state --- */
T_V2(swvk_CmdSetCullMode, VkCommandBuffer, VkCullModeFlags)
T_V2(swvk_CmdSetFrontFace, VkCommandBuffer, VkFrontFace)
T_V2(swvk_CmdSetPrimitiveTopology, VkCommandBuffer, VkPrimitiveTopology)
T_V3(swvk_CmdSetViewportWithCount, VkCommandBuffer, uint32_t, const VkViewport*)
T_V3(swvk_CmdSetScissorWithCount, VkCommandBuffer, uint32_t, const VkRect2D*)

/* vkCmdBindVertexBuffers2 has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdBindVertexBuffers2(void *S, VkCommandBuffer a,
    uint32_t b, uint32_t c, const VkBuffer *d, const VkDeviceSize *e,
    const VkDeviceSize *f, const VkDeviceSize *g)
{ (void)S; swvk_CmdBindVertexBuffers2(a,b,c,d,e,f,g); }

T_V2(swvk_CmdSetDepthTestEnable, VkCommandBuffer, VkBool32)
T_V2(swvk_CmdSetDepthWriteEnable, VkCommandBuffer, VkBool32)
T_V2(swvk_CmdSetDepthCompareOp, VkCommandBuffer, VkCompareOp)
T_V2(swvk_CmdSetDepthBoundsTestEnable, VkCommandBuffer, VkBool32)
T_V2(swvk_CmdSetStencilTestEnable, VkCommandBuffer, VkBool32)

/* vkCmdSetStencilOp has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdSetStencilOp(void *S, VkCommandBuffer a,
    VkStencilFaceFlags b, VkStencilOp c, VkStencilOp d, VkStencilOp e,
    VkCompareOp f)
{ (void)S; swvk_CmdSetStencilOp(a,b,c,d,e,f); }

T_V2(swvk_CmdSetRasterizerDiscardEnable, VkCommandBuffer, VkBool32)
T_V2(swvk_CmdSetDepthBiasEnable, VkCommandBuffer, VkBool32)
T_V2(swvk_CmdSetPrimitiveRestartEnable, VkCommandBuffer, VkBool32)

/* --- Transfer commands --- */
T_V5(swvk_CmdCopyBuffer, VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*)

/* vkCmdCopyBufferToImage has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdCopyBufferToImage(void *S, VkCommandBuffer a,
    VkBuffer b, VkImage c, VkImageLayout d, uint32_t e, const VkBufferImageCopy *f)
{ (void)S; swvk_CmdCopyBufferToImage(a,b,c,d,e,f); }

/* vkCmdCopyImageToBuffer has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdCopyImageToBuffer(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkBuffer d, uint32_t e, const VkBufferImageCopy *f)
{ (void)S; swvk_CmdCopyImageToBuffer(a,b,c,d,e,f); }

/* vkCmdCopyImage has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdCopyImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f,
    const VkImageCopy *g)
{ (void)S; swvk_CmdCopyImage(a,b,c,d,e,f,g); }

T_V5(swvk_CmdFillBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t)
T_V5(swvk_CmdUpdateBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void*)

/* --- Vulkan 1.1/1.2/1.3 wrappers --- */
T_V2(swvk_GetPhysicalDeviceProperties2, VkPhysicalDevice, VkPhysicalDeviceProperties2*)
T_V2(swvk_GetPhysicalDeviceFeatures2, VkPhysicalDevice, VkPhysicalDeviceFeatures2*)
T_V3(swvk_GetPhysicalDeviceQueueFamilyProperties2, VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties2*)
T_V2(swvk_GetPhysicalDeviceMemoryProperties2, VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*)
T_V3(swvk_GetPhysicalDeviceFormatProperties2, VkPhysicalDevice, VkFormat, VkFormatProperties2*)
T_V3(swvk_GetBufferMemoryRequirements2, VkDevice, const VkBufferMemoryRequirementsInfo2*, VkMemoryRequirements2*)
T_V3(swvk_GetImageMemoryRequirements2, VkDevice, const VkImageMemoryRequirementsInfo2*, VkMemoryRequirements2*)
T_R3(swvk_BindBufferMemory2, VkDevice, uint32_t, const VkBindBufferMemoryInfo*)
T_R3(swvk_BindImageMemory2, VkDevice, uint32_t, const VkBindImageMemoryInfo*)
T_V2(swvk_CmdPipelineBarrier2, VkCommandBuffer, const void*)
T_R4(swvk_QueueSubmit2, VkQueue, uint32_t, const VkSubmitInfo2*, VkFence)

/* vkGetPhysicalDeviceImageFormatProperties has 7 visible args -- custom trampoline */
static VkResult APICALL _t_swvk_GetPhysicalDeviceImageFormatProperties(void *S,
    VkPhysicalDevice a, VkFormat b, VkImageType c, VkImageTiling d,
    VkImageUsageFlags e, VkImageCreateFlags f, VkImageFormatProperties *g)
{ (void)S; return swvk_GetPhysicalDeviceImageFormatProperties(a,b,c,d,e,f,g); }

/* --- Stub trampolines --- */
T_V3(swvk_GetDeviceMemoryCommitment, VkDevice, VkDeviceMemory, VkDeviceSize*)
T_V4(swvk_GetImageSubresourceLayout, VkDevice, VkImage, const VkImageSubresource*, VkSubresourceLayout*)
T_V3(swvk_GetRenderAreaGranularity, VkDevice, VkRenderPass, VkExtent2D*)
T_V3(swvk_GetDeviceQueue2, VkDevice, const VkDeviceQueueInfo2*, VkQueue*)
T_V3(swvk_GetDescriptorSetLayoutSupport, VkDevice, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayoutSupport*)
T_R3(swvk_GetPhysicalDeviceToolProperties, VkPhysicalDevice, uint32_t*, void*)
T_R3(swvk_GetPhysicalDeviceImageFormatProperties2, VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2*, VkImageFormatProperties2*)
T_V3(swvk_GetPhysicalDeviceExternalBufferProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo*, VkExternalBufferProperties*)
T_V3(swvk_GetPhysicalDeviceExternalFenceProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo*, VkExternalFenceProperties*)
T_V3(swvk_GetPhysicalDeviceExternalSemaphoreProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo*, VkExternalSemaphoreProperties*)
T_R3(swvk_EnumeratePhysicalDeviceGroups, VkInstance, uint32_t*, VkPhysicalDeviceGroupProperties*)

T_R6(swvk_CreateComputePipelines, VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*)
T_R4(swvk_CreateBufferView, VkDevice, const VkBufferViewCreateInfo*, const VkAllocationCallbacks*, VkBufferView*)
T_V3(swvk_DestroyBufferView, VkDevice, VkBufferView, const VkAllocationCallbacks*)
T_R4(swvk_CreateSamplerYcbcrConversion, VkDevice, const VkSamplerYcbcrConversionCreateInfo*, const VkAllocationCallbacks*, VkSamplerYcbcrConversion*)
T_V3(swvk_DestroySamplerYcbcrConversion, VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks*)
T_R4(swvk_CreateDescriptorUpdateTemplate, VkDevice, const VkDescriptorUpdateTemplateCreateInfo*, const VkAllocationCallbacks*, VkDescriptorUpdateTemplate*)
T_V3(swvk_DestroyDescriptorUpdateTemplate, VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks*)
T_V4(swvk_UpdateDescriptorSetWithTemplate, VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void*)
T_R4(swvk_CreatePrivateDataSlot, VkDevice, const VkPrivateDataSlotCreateInfo*, const VkAllocationCallbacks*, VkPrivateDataSlot*)
T_V3(swvk_DestroyPrivateDataSlot, VkDevice, VkPrivateDataSlot, const VkAllocationCallbacks*)
T_V5(swvk_GetPrivateData, VkDevice, VkObjectType, uint64_t, VkPrivateDataSlot, uint64_t*)
/* vkGetBufferDeviceAddress returns VkDeviceAddress (uint64_t) -- custom trampoline */
static VkDeviceAddress APICALL _t_swvk_GetBufferDeviceAddress(void *S,
    VkDevice a, const VkBufferDeviceAddressInfo *b)
{ (void)S; return swvk_GetBufferDeviceAddress(a,b); }
T_R3(swvk_GetSemaphoreCounterValue, VkDevice, VkSemaphore, uint64_t*)
T_R3(swvk_WaitSemaphores, VkDevice, const VkSemaphoreWaitInfo*, uint64_t)
T_R2(swvk_SignalSemaphore, VkDevice, const VkSemaphoreSignalInfo*)

/* vkSetPrivateData has 5 visible args -- custom trampoline */
static VkResult APICALL _t_swvk_SetPrivateData(void *S, VkDevice a,
    VkObjectType b, uint64_t c, VkPrivateDataSlot d, uint64_t e)
{ (void)S; return swvk_SetPrivateData(a,b,c,d,e); }

/* vkGetBufferOpaqueCaptureAddress returns uint64_t -- custom trampoline */
static uint64_t APICALL _t_swvk_GetBufferOpaqueCaptureAddress(void *S,
    VkDevice a, const VkBufferDeviceAddressInfo *b)
{ (void)S; return swvk_GetBufferOpaqueCaptureAddress(a,b); }

/* vkGetDeviceMemoryOpaqueCaptureAddress returns uint64_t -- custom trampoline */
static uint64_t APICALL _t_swvk_GetDeviceMemoryOpaqueCaptureAddress(void *S,
    VkDevice a, const VkDeviceMemoryOpaqueCaptureAddressInfo *b)
{ (void)S; return swvk_GetDeviceMemoryOpaqueCaptureAddress(a,b); }

T_V4(swvk_GetImageSparseMemoryRequirements, VkDevice, VkImage, uint32_t*, VkSparseImageMemoryRequirements*)
T_V4(swvk_GetImageSparseMemoryRequirements2, VkDevice, const VkImageSparseMemoryRequirementsInfo2*, uint32_t*, VkSparseImageMemoryRequirements2*)
T_V4(swvk_GetPhysicalDeviceSparseImageFormatProperties2, VkPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2*, uint32_t*, VkSparseImageFormatProperties2*)

/* vkGetPhysicalDeviceSparseImageFormatProperties has 8 visible args -- custom trampoline */
static void APICALL _t_swvk_GetPhysicalDeviceSparseImageFormatProperties(void *S,
    VkPhysicalDevice a, VkFormat b, VkImageType c, VkSampleCountFlagBits d,
    VkImageUsageFlags e, VkImageTiling f, uint32_t *g, VkSparseImageFormatProperties *h)
{ (void)S; swvk_GetPhysicalDeviceSparseImageFormatProperties(a,b,c,d,e,f,g,h); }

/* --- Event trampolines --- */
T_R4(swvk_CreateEvent, VkDevice, const VkEventCreateInfo*, const VkAllocationCallbacks*, VkEvent*)
T_V3(swvk_DestroyEvent, VkDevice, VkEvent, const VkAllocationCallbacks*)
T_R2(swvk_GetEventStatus, VkDevice, VkEvent)
T_R2(swvk_SetEvent, VkDevice, VkEvent)
T_R2(swvk_ResetEvent, VkDevice, VkEvent)

/* --- Query pool trampolines --- */
T_R4(swvk_CreateQueryPool, VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool*)
T_V3(swvk_DestroyQueryPool, VkDevice, VkQueryPool, const VkAllocationCallbacks*)
T_V4(swvk_ResetQueryPool, VkDevice, VkQueryPool, uint32_t, uint32_t)
T_R4(swvk_QueueBindSparse, VkQueue, uint32_t, const VkBindSparseInfo*, VkFence)

/* vkGetQueryPoolResults has 8 visible args -- custom trampoline */
static VkResult APICALL _t_swvk_GetQueryPoolResults(void *S, VkDevice a,
    VkQueryPool b, uint32_t c, uint32_t d, size_t e, void *f,
    VkDeviceSize g, VkQueryResultFlags h)
{ (void)S; return swvk_GetQueryPoolResults(a,b,c,d,e,f,g,h); }

/* --- Render pass 2 trampolines --- */
T_R4(swvk_CreateRenderPass2, VkDevice, const VkRenderPassCreateInfo2*, const VkAllocationCallbacks*, VkRenderPass*)
T_V3(swvk_CmdBeginRenderPass2, VkCommandBuffer, const VkRenderPassBeginInfo*, const VkSubpassBeginInfo*)
T_V3(swvk_CmdNextSubpass2, VkCommandBuffer, const VkSubpassBeginInfo*, const VkSubpassEndInfo*)
T_V2(swvk_CmdEndRenderPass2, VkCommandBuffer, const VkSubpassEndInfo*)

/* --- Dynamic state + clear command trampolines --- */
T_V2(swvk_CmdSetLineWidth, VkCommandBuffer, float)
T_V4(swvk_CmdSetDepthBias, VkCommandBuffer, float, float, float)
T_V2(swvk_CmdSetBlendConstants, VkCommandBuffer, const float*)
T_V3(swvk_CmdSetDepthBounds, VkCommandBuffer, float, float)
T_V3(swvk_CmdSetStencilCompareMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
T_V3(swvk_CmdSetStencilWriteMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
T_V3(swvk_CmdSetStencilReference, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
/* vkCmdClearAttachments has 5 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdClearAttachments(void *S, VkCommandBuffer a,
    uint32_t b, const VkClearAttachment *c, uint32_t d, const VkClearRect *e)
{ (void)S; swvk_CmdClearAttachments(a,b,c,d,e); }

/* vkCmdClearColorImage has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdClearColorImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, const VkClearColorValue *d,
    uint32_t e, const VkImageSubresourceRange *f)
{ (void)S; swvk_CmdClearColorImage(a,b,c,d,e,f); }

/* vkCmdClearDepthStencilImage has 6 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdClearDepthStencilImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, const VkClearDepthStencilValue *d,
    uint32_t e, const VkImageSubresourceRange *f)
{ (void)S; swvk_CmdClearDepthStencilImage(a,b,c,d,e,f); }

/* vkCmdBlitImage has 8 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdBlitImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkImage d, VkImageLayout e,
    uint32_t f, const VkImageBlit *g, VkFilter h)
{ (void)S; swvk_CmdBlitImage(a,b,c,d,e,f,g,h); }

/* vkCmdResolveImage has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdResolveImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkImage d, VkImageLayout e,
    uint32_t f, const VkImageResolve *g)
{ (void)S; swvk_CmdResolveImage(a,b,c,d,e,f,g); }

/* --- Vulkan 1.3 "2" command variant trampolines --- */
T_V2(swvk_CmdCopyBuffer2, VkCommandBuffer, const VkCopyBufferInfo2*)
T_V2(swvk_CmdCopyImage2, VkCommandBuffer, const VkCopyImageInfo2*)
T_V2(swvk_CmdCopyBufferToImage2, VkCommandBuffer, const VkCopyBufferToImageInfo2*)
T_V2(swvk_CmdCopyImageToBuffer2, VkCommandBuffer, const VkCopyImageToBufferInfo2*)
T_V2(swvk_CmdBlitImage2, VkCommandBuffer, const VkBlitImageInfo2*)
T_V2(swvk_CmdResolveImage2, VkCommandBuffer, const VkResolveImageInfo2*)

/* --- Command no-op trampolines --- */
T_V2(swvk_CmdSetDeviceMask, VkCommandBuffer, uint32_t)
T_V4(swvk_CmdDispatch, VkCommandBuffer, uint32_t, uint32_t, uint32_t)
T_V3(swvk_CmdDispatchIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize)
T_V3(swvk_CmdExecuteCommands, VkCommandBuffer, uint32_t, const VkCommandBuffer*)

/* vkCmdDispatchBase has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdDispatchBase(void *S, VkCommandBuffer a,
    uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{ (void)S; swvk_CmdDispatchBase(a,b,c,d,e,f,g); }

/* --- Event command trampolines --- */
T_V3(swvk_CmdSetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags)
T_V3(swvk_CmdResetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags)
T_V3(swvk_CmdSetEvent2, VkCommandBuffer, VkEvent, const VkDependencyInfo*)
T_V3(swvk_CmdResetEvent2, VkCommandBuffer, VkEvent, VkPipelineStageFlags2)
T_V4(swvk_CmdWaitEvents2, VkCommandBuffer, uint32_t, const VkEvent*, const VkDependencyInfo*)

/* vkCmdWaitEvents has 11 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdWaitEvents(void *S, VkCommandBuffer a,
    uint32_t b, const VkEvent *c, VkPipelineStageFlags d, VkPipelineStageFlags e,
    uint32_t f, const void *g, uint32_t h, const void *ii,
    uint32_t j, const void *k)
{ (void)S; swvk_CmdWaitEvents(a,b,c,d,e,f,g,h,ii,j,k); }

/* --- Query command trampolines --- */
T_V4(swvk_CmdBeginQuery, VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags)
T_V3(swvk_CmdEndQuery, VkCommandBuffer, VkQueryPool, uint32_t)
T_V4(swvk_CmdResetQueryPool, VkCommandBuffer, VkQueryPool, uint32_t, uint32_t)
T_V4(swvk_CmdWriteTimestamp, VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t)
T_V4(swvk_CmdWriteTimestamp2, VkCommandBuffer, VkPipelineStageFlags2, VkQueryPool, uint32_t)

/* vkCmdCopyQueryPoolResults has 8 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdCopyQueryPoolResults(void *S, VkCommandBuffer a,
    VkQueryPool b, uint32_t c, uint32_t d, VkBuffer e, VkDeviceSize f,
    VkDeviceSize g, VkQueryResultFlags h)
{ (void)S; swvk_CmdCopyQueryPoolResults(a,b,c,d,e,f,g,h); }

/* --- Indirect draw trampolines --- */
T_V5(swvk_CmdDrawIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t)
T_V5(swvk_CmdDrawIndexedIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t)

/* vkCmdDrawIndirectCount has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdDrawIndirectCount(void *S, VkCommandBuffer a,
    VkBuffer b, VkDeviceSize c, VkBuffer d, VkDeviceSize e,
    uint32_t f, uint32_t g)
{ (void)S; swvk_CmdDrawIndirectCount(a,b,c,d,e,f,g); }

/* vkCmdDrawIndexedIndirectCount has 7 visible args -- custom trampoline */
static void APICALL _t_swvk_CmdDrawIndexedIndirectCount(void *S, VkCommandBuffer a,
    VkBuffer b, VkDeviceSize c, VkBuffer d, VkDeviceSize e,
    uint32_t f, uint32_t g)
{ (void)S; swvk_CmdDrawIndexedIndirectCount(a,b,c,d,e,f,g); }

/****************************************************************************/
/* vkGetDeviceProcAddr -- forward to GetInstanceProcAddr lookup              */
/* This is declared here (not in a separate .c) because it needs access to  */
/* the DISPATCH table below.                                                */
/****************************************************************************/

/* Forward declaration of the DISPATCH lookup */
static PFN_vkVoidFunction swvk_LookupProcAddr(const char *pName);

/****************************************************************************/
/* Raw (non-APICALL) function lookup for vkGetDeviceProcAddr.               */
/* Returns standard C calling convention pointers that apps call directly.  */
/* Unlike LookupProcAddr (which returns APICALL trampolines for             */
/* VulkanIFace), this returns the actual swvk_* implementation functions.   */
/****************************************************************************/

static PFN_vkVoidFunction swvk_LookupRawProcAddr(const char *pName);

PFN_vkVoidFunction swvk_GetDeviceProcAddr(VkDevice device, const char *pName)
{
    (void)device;
    if (!pName) return NULL;
    return swvk_LookupRawProcAddr(pName);
}

/* vkGetDeviceProcAddr returns PFN_vkVoidFunction -- custom trampoline */
static PFN_vkVoidFunction APICALL _t_swvk_GetDeviceProcAddr(void *S,
    VkDevice a, const char *b)
{ (void)S; return swvk_GetDeviceProcAddr(a,b); }

/****************************************************************************/
/* ICD interface: vk_icdGetInstanceProcAddr                                 */
/* Returns APICALL trampoline function pointers that bridge between the     */
/* AmigaOS APICALL convention (Self in r3) and standard Vulkan functions.   */
/****************************************************************************/

/****************************************************************************/
/* Shared DISPATCH lookup used by both GetInstanceProcAddr and              */
/* GetDeviceProcAddr.                                                       */
/****************************************************************************/

static PFN_vkVoidFunction swvk_LookupProcAddr(const char *pName)
{
    #define DISPATCH(vkName, tramp) \
        if (strcmp(pName, #vkName) == 0) return (PFN_vkVoidFunction)(tramp)

    /* Global functions -- called directly by loader, NO trampoline */
    if (strcmp(pName, "vkCreateInstance") == 0)
        return (PFN_vkVoidFunction)swvk_CreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0)
        return (PFN_vkVoidFunction)swvk_DestroyInstance;
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0)
        return (PFN_vkVoidFunction)swvk_EnumerateInstanceVersion;
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)swvk_EnumerateInstanceExtensionProperties;
    DISPATCH(vkEnumeratePhysicalDevices, _t_swvk_EnumeratePhysicalDevices);
    DISPATCH(vkGetPhysicalDeviceProperties, _t_swvk_GetPhysicalDeviceProperties);
    DISPATCH(vkGetPhysicalDeviceFeatures, _t_swvk_GetPhysicalDeviceFeatures);
    DISPATCH(vkGetPhysicalDeviceQueueFamilyProperties, _t_swvk_GetPhysicalDeviceQueueFamilyProperties);
    DISPATCH(vkGetPhysicalDeviceMemoryProperties, _t_swvk_GetPhysicalDeviceMemoryProperties);
    DISPATCH(vkGetPhysicalDeviceFormatProperties, _t_swvk_GetPhysicalDeviceFormatProperties);

    /* Device */
    DISPATCH(vkCreateDevice, _t_swvk_CreateDevice);
    DISPATCH(vkDestroyDevice, _t_swvk_DestroyDevice);
    DISPATCH(vkGetDeviceQueue, _t_swvk_GetDeviceQueue);
    DISPATCH(vkDeviceWaitIdle, _t_swvk_DeviceWaitIdle);
    DISPATCH(vkQueueWaitIdle, _t_swvk_QueueWaitIdle);

    /* Memory */
    DISPATCH(vkAllocateMemory, _t_swvk_AllocateMemory);
    DISPATCH(vkFreeMemory, _t_swvk_FreeMemory);
    DISPATCH(vkMapMemory, _t_swvk_MapMemory);
    DISPATCH(vkUnmapMemory, _t_swvk_UnmapMemory);

    /* Buffer */
    DISPATCH(vkCreateBuffer, _t_swvk_CreateBuffer);
    DISPATCH(vkDestroyBuffer, _t_swvk_DestroyBuffer);
    DISPATCH(vkGetBufferMemoryRequirements, _t_swvk_GetBufferMemoryRequirements);
    DISPATCH(vkBindBufferMemory, _t_swvk_BindBufferMemory);

    /* Image */
    DISPATCH(vkCreateImage, _t_swvk_CreateImage);
    DISPATCH(vkDestroyImage, _t_swvk_DestroyImage);
    DISPATCH(vkGetImageMemoryRequirements, _t_swvk_GetImageMemoryRequirements);
    DISPATCH(vkBindImageMemory, _t_swvk_BindImageMemory);
    DISPATCH(vkCreateImageView, _t_swvk_CreateImageView);
    DISPATCH(vkDestroyImageView, _t_swvk_DestroyImageView);

    /* Sampler */
    DISPATCH(vkCreateSampler, _t_swvk_CreateSampler);
    DISPATCH(vkDestroySampler, _t_swvk_DestroySampler);

    /* Shader */
    DISPATCH(vkCreateShaderModule, _t_swvk_CreateShaderModule);
    DISPATCH(vkDestroyShaderModule, _t_swvk_DestroyShaderModule);

    /* Pipeline */
    DISPATCH(vkCreatePipelineLayout, _t_swvk_CreatePipelineLayout);
    DISPATCH(vkDestroyPipelineLayout, _t_swvk_DestroyPipelineLayout);
    DISPATCH(vkCreatePipelineCache, _t_swvk_CreatePipelineCache);
    DISPATCH(vkDestroyPipelineCache, _t_swvk_DestroyPipelineCache);
    DISPATCH(vkCreateGraphicsPipelines, _t_swvk_CreateGraphicsPipelines);
    DISPATCH(vkDestroyPipeline, _t_swvk_DestroyPipeline);

    /* Descriptor sets */
    DISPATCH(vkCreateDescriptorSetLayout, _t_swvk_CreateDescriptorSetLayout);
    DISPATCH(vkDestroyDescriptorSetLayout, _t_swvk_DestroyDescriptorSetLayout);
    DISPATCH(vkCreateDescriptorPool, _t_swvk_CreateDescriptorPool);
    DISPATCH(vkDestroyDescriptorPool, _t_swvk_DestroyDescriptorPool);
    DISPATCH(vkAllocateDescriptorSets, _t_swvk_AllocateDescriptorSets);
    DISPATCH(vkFreeDescriptorSets, _t_swvk_FreeDescriptorSets);
    DISPATCH(vkUpdateDescriptorSets, _t_swvk_UpdateDescriptorSets);

    /* Command buffer */
    DISPATCH(vkCreateCommandPool, _t_swvk_CreateCommandPool);
    DISPATCH(vkDestroyCommandPool, _t_swvk_DestroyCommandPool);
    DISPATCH(vkAllocateCommandBuffers, _t_swvk_AllocateCommandBuffers);
    DISPATCH(vkFreeCommandBuffers, _t_swvk_FreeCommandBuffers);
    DISPATCH(vkBeginCommandBuffer, _t_swvk_BeginCommandBuffer);
    DISPATCH(vkEndCommandBuffer, _t_swvk_EndCommandBuffer);

    /* Command recording */
    DISPATCH(vkCmdBindPipeline, _t_swvk_CmdBindPipeline);
    DISPATCH(vkCmdSetViewport, _t_swvk_CmdSetViewport);
    DISPATCH(vkCmdSetScissor, _t_swvk_CmdSetScissor);
    DISPATCH(vkCmdDraw, _t_swvk_CmdDraw);
    DISPATCH(vkCmdBeginRendering, _t_swvk_CmdBeginRendering);
    DISPATCH(vkCmdEndRendering, _t_swvk_CmdEndRendering);
    DISPATCH(vkCmdPushConstants, _t_swvk_CmdPushConstants);
    DISPATCH(vkCmdBindVertexBuffers, _t_swvk_CmdBindVertexBuffers);
    DISPATCH(vkCmdBindIndexBuffer, _t_swvk_CmdBindIndexBuffer);
    DISPATCH(vkCmdDrawIndexed, _t_swvk_CmdDrawIndexed);
    DISPATCH(vkCmdBindDescriptorSets, _t_swvk_CmdBindDescriptorSets);

    /* Queue submit */
    DISPATCH(vkQueueSubmit, _t_swvk_QueueSubmit);

    /* Sync */
    DISPATCH(vkCreateFence, _t_swvk_CreateFence);
    DISPATCH(vkDestroyFence, _t_swvk_DestroyFence);
    DISPATCH(vkWaitForFences, _t_swvk_WaitForFences);
    DISPATCH(vkResetFences, _t_swvk_ResetFences);
    DISPATCH(vkCreateSemaphore, _t_swvk_CreateSemaphore);
    DISPATCH(vkDestroySemaphore, _t_swvk_DestroySemaphore);

    /* Device extension enumeration */
    DISPATCH(vkEnumerateDeviceExtensionProperties, _t_swvk_EnumerateDeviceExtensionProperties);

    /* WSI surface queries */
    DISPATCH(vkGetPhysicalDeviceSurfaceSupportKHR, _t_swvk_GetPhysicalDeviceSurfaceSupportKHR);
    DISPATCH(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, _t_swvk_GetPhysicalDeviceSurfaceCapabilitiesKHR);
    DISPATCH(vkGetPhysicalDeviceSurfaceFormatsKHR, _t_swvk_GetPhysicalDeviceSurfaceFormatsKHR);
    DISPATCH(vkGetPhysicalDeviceSurfacePresentModesKHR, _t_swvk_GetPhysicalDeviceSurfacePresentModesKHR);

    /* WSI swapchain */
    DISPATCH(vkCreateSwapchainKHR, _t_swvk_CreateSwapchainKHR);
    DISPATCH(vkDestroySwapchainKHR, _t_swvk_DestroySwapchainKHR);
    DISPATCH(vkGetSwapchainImagesKHR, _t_swvk_GetSwapchainImagesKHR);
    DISPATCH(vkAcquireNextImageKHR, _t_swvk_AcquireNextImageKHR);
    DISPATCH(vkQueuePresentKHR, _t_swvk_QueuePresentKHR);

    /* Stubs */
    DISPATCH(vkFlushMappedMemoryRanges, _t_swvk_FlushMappedMemoryRanges);
    DISPATCH(vkInvalidateMappedMemoryRanges, _t_swvk_InvalidateMappedMemoryRanges);
    DISPATCH(vkResetCommandBuffer, _t_swvk_ResetCommandBuffer);
    DISPATCH(vkCmdPipelineBarrier, _t_swvk_CmdPipelineBarrier);
    DISPATCH(vkResetCommandPool, _t_swvk_ResetCommandPool);
    DISPATCH(vkTrimCommandPool, _t_swvk_TrimCommandPool);
    DISPATCH(vkGetFenceStatus, _t_swvk_GetFenceStatus);
    DISPATCH(vkGetPipelineCacheData, _t_swvk_GetPipelineCacheData);
    DISPATCH(vkMergePipelineCaches, _t_swvk_MergePipelineCaches);
    DISPATCH(vkResetDescriptorPool, _t_swvk_ResetDescriptorPool);

    /* Legacy render pass */
    DISPATCH(vkCreateRenderPass, _t_swvk_CreateRenderPass);
    DISPATCH(vkDestroyRenderPass, _t_swvk_DestroyRenderPass);
    DISPATCH(vkCreateFramebuffer, _t_swvk_CreateFramebuffer);
    DISPATCH(vkDestroyFramebuffer, _t_swvk_DestroyFramebuffer);
    DISPATCH(vkCmdBeginRenderPass, _t_swvk_CmdBeginRenderPass);
    DISPATCH(vkCmdEndRenderPass, _t_swvk_CmdEndRenderPass);
    DISPATCH(vkCmdNextSubpass, _t_swvk_CmdNextSubpass);

    /* Dynamic state */
    DISPATCH(vkCmdSetCullMode, _t_swvk_CmdSetCullMode);
    DISPATCH(vkCmdSetFrontFace, _t_swvk_CmdSetFrontFace);
    DISPATCH(vkCmdSetPrimitiveTopology, _t_swvk_CmdSetPrimitiveTopology);
    DISPATCH(vkCmdSetViewportWithCount, _t_swvk_CmdSetViewportWithCount);
    DISPATCH(vkCmdSetScissorWithCount, _t_swvk_CmdSetScissorWithCount);
    DISPATCH(vkCmdBindVertexBuffers2, _t_swvk_CmdBindVertexBuffers2);
    DISPATCH(vkCmdSetDepthTestEnable, _t_swvk_CmdSetDepthTestEnable);
    DISPATCH(vkCmdSetDepthWriteEnable, _t_swvk_CmdSetDepthWriteEnable);
    DISPATCH(vkCmdSetDepthCompareOp, _t_swvk_CmdSetDepthCompareOp);
    DISPATCH(vkCmdSetDepthBoundsTestEnable, _t_swvk_CmdSetDepthBoundsTestEnable);
    DISPATCH(vkCmdSetStencilTestEnable, _t_swvk_CmdSetStencilTestEnable);
    DISPATCH(vkCmdSetStencilOp, _t_swvk_CmdSetStencilOp);
    DISPATCH(vkCmdSetRasterizerDiscardEnable, _t_swvk_CmdSetRasterizerDiscardEnable);
    DISPATCH(vkCmdSetDepthBiasEnable, _t_swvk_CmdSetDepthBiasEnable);
    DISPATCH(vkCmdSetPrimitiveRestartEnable, _t_swvk_CmdSetPrimitiveRestartEnable);

    /* Transfer commands */
    DISPATCH(vkCmdCopyBuffer, _t_swvk_CmdCopyBuffer);
    DISPATCH(vkCmdCopyBufferToImage, _t_swvk_CmdCopyBufferToImage);
    DISPATCH(vkCmdCopyImageToBuffer, _t_swvk_CmdCopyImageToBuffer);
    DISPATCH(vkCmdCopyImage, _t_swvk_CmdCopyImage);
    DISPATCH(vkCmdFillBuffer, _t_swvk_CmdFillBuffer);
    DISPATCH(vkCmdUpdateBuffer, _t_swvk_CmdUpdateBuffer);

    /* Vulkan 1.1/1.2/1.3 wrappers */
    DISPATCH(vkGetPhysicalDeviceProperties2, _t_swvk_GetPhysicalDeviceProperties2);
    DISPATCH(vkGetPhysicalDeviceFeatures2, _t_swvk_GetPhysicalDeviceFeatures2);
    DISPATCH(vkGetPhysicalDeviceQueueFamilyProperties2, _t_swvk_GetPhysicalDeviceQueueFamilyProperties2);
    DISPATCH(vkGetPhysicalDeviceMemoryProperties2, _t_swvk_GetPhysicalDeviceMemoryProperties2);
    DISPATCH(vkGetPhysicalDeviceFormatProperties2, _t_swvk_GetPhysicalDeviceFormatProperties2);
    DISPATCH(vkGetBufferMemoryRequirements2, _t_swvk_GetBufferMemoryRequirements2);
    DISPATCH(vkGetImageMemoryRequirements2, _t_swvk_GetImageMemoryRequirements2);
    DISPATCH(vkBindBufferMemory2, _t_swvk_BindBufferMemory2);
    DISPATCH(vkBindImageMemory2, _t_swvk_BindImageMemory2);
    DISPATCH(vkCmdPipelineBarrier2, _t_swvk_CmdPipelineBarrier2);
    DISPATCH(vkQueueSubmit2, _t_swvk_QueueSubmit2);
    DISPATCH(vkGetPhysicalDeviceImageFormatProperties, _t_swvk_GetPhysicalDeviceImageFormatProperties);

    /* Stub functions */
    DISPATCH(vkGetDeviceProcAddr, _t_swvk_GetDeviceProcAddr);
    DISPATCH(vkGetDeviceMemoryCommitment, _t_swvk_GetDeviceMemoryCommitment);
    DISPATCH(vkGetImageSubresourceLayout, _t_swvk_GetImageSubresourceLayout);
    DISPATCH(vkGetRenderAreaGranularity, _t_swvk_GetRenderAreaGranularity);
    DISPATCH(vkGetDeviceQueue2, _t_swvk_GetDeviceQueue2);
    DISPATCH(vkGetDescriptorSetLayoutSupport, _t_swvk_GetDescriptorSetLayoutSupport);
    DISPATCH(vkGetPhysicalDeviceToolProperties, _t_swvk_GetPhysicalDeviceToolProperties);
    DISPATCH(vkGetPhysicalDeviceImageFormatProperties2, _t_swvk_GetPhysicalDeviceImageFormatProperties2);
    DISPATCH(vkGetPhysicalDeviceExternalBufferProperties, _t_swvk_GetPhysicalDeviceExternalBufferProperties);
    DISPATCH(vkGetPhysicalDeviceExternalFenceProperties, _t_swvk_GetPhysicalDeviceExternalFenceProperties);
    DISPATCH(vkGetPhysicalDeviceExternalSemaphoreProperties, _t_swvk_GetPhysicalDeviceExternalSemaphoreProperties);
    DISPATCH(vkEnumeratePhysicalDeviceGroups, _t_swvk_EnumeratePhysicalDeviceGroups);
    DISPATCH(vkCreateComputePipelines, _t_swvk_CreateComputePipelines);
    DISPATCH(vkCreateBufferView, _t_swvk_CreateBufferView);
    DISPATCH(vkDestroyBufferView, _t_swvk_DestroyBufferView);
    DISPATCH(vkCreateSamplerYcbcrConversion, _t_swvk_CreateSamplerYcbcrConversion);
    DISPATCH(vkDestroySamplerYcbcrConversion, _t_swvk_DestroySamplerYcbcrConversion);
    DISPATCH(vkCreateDescriptorUpdateTemplate, _t_swvk_CreateDescriptorUpdateTemplate);
    DISPATCH(vkDestroyDescriptorUpdateTemplate, _t_swvk_DestroyDescriptorUpdateTemplate);
    DISPATCH(vkUpdateDescriptorSetWithTemplate, _t_swvk_UpdateDescriptorSetWithTemplate);
    DISPATCH(vkCreatePrivateDataSlot, _t_swvk_CreatePrivateDataSlot);
    DISPATCH(vkDestroyPrivateDataSlot, _t_swvk_DestroyPrivateDataSlot);
    DISPATCH(vkSetPrivateData, _t_swvk_SetPrivateData);
    DISPATCH(vkGetPrivateData, _t_swvk_GetPrivateData);
    DISPATCH(vkGetBufferDeviceAddress, _t_swvk_GetBufferDeviceAddress);
    DISPATCH(vkGetBufferOpaqueCaptureAddress, _t_swvk_GetBufferOpaqueCaptureAddress);
    DISPATCH(vkGetDeviceMemoryOpaqueCaptureAddress, _t_swvk_GetDeviceMemoryOpaqueCaptureAddress);
    DISPATCH(vkGetSemaphoreCounterValue, _t_swvk_GetSemaphoreCounterValue);
    DISPATCH(vkWaitSemaphores, _t_swvk_WaitSemaphores);
    DISPATCH(vkSignalSemaphore, _t_swvk_SignalSemaphore);
    DISPATCH(vkGetImageSparseMemoryRequirements, _t_swvk_GetImageSparseMemoryRequirements);
    DISPATCH(vkGetImageSparseMemoryRequirements2, _t_swvk_GetImageSparseMemoryRequirements2);
    DISPATCH(vkGetPhysicalDeviceSparseImageFormatProperties, _t_swvk_GetPhysicalDeviceSparseImageFormatProperties);
    DISPATCH(vkGetPhysicalDeviceSparseImageFormatProperties2, _t_swvk_GetPhysicalDeviceSparseImageFormatProperties2);

    /* Events */
    DISPATCH(vkCreateEvent, _t_swvk_CreateEvent);
    DISPATCH(vkDestroyEvent, _t_swvk_DestroyEvent);
    DISPATCH(vkGetEventStatus, _t_swvk_GetEventStatus);
    DISPATCH(vkSetEvent, _t_swvk_SetEvent);
    DISPATCH(vkResetEvent, _t_swvk_ResetEvent);
    DISPATCH(vkCmdSetEvent, _t_swvk_CmdSetEvent);
    DISPATCH(vkCmdResetEvent, _t_swvk_CmdResetEvent);
    DISPATCH(vkCmdWaitEvents, _t_swvk_CmdWaitEvents);
    DISPATCH(vkCmdSetEvent2, _t_swvk_CmdSetEvent2);
    DISPATCH(vkCmdResetEvent2, _t_swvk_CmdResetEvent2);
    DISPATCH(vkCmdWaitEvents2, _t_swvk_CmdWaitEvents2);

    /* Query pools */
    DISPATCH(vkCreateQueryPool, _t_swvk_CreateQueryPool);
    DISPATCH(vkDestroyQueryPool, _t_swvk_DestroyQueryPool);
    DISPATCH(vkGetQueryPoolResults, _t_swvk_GetQueryPoolResults);
    DISPATCH(vkResetQueryPool, _t_swvk_ResetQueryPool);
    DISPATCH(vkCmdBeginQuery, _t_swvk_CmdBeginQuery);
    DISPATCH(vkCmdEndQuery, _t_swvk_CmdEndQuery);
    DISPATCH(vkCmdResetQueryPool, _t_swvk_CmdResetQueryPool);
    DISPATCH(vkCmdWriteTimestamp, _t_swvk_CmdWriteTimestamp);
    DISPATCH(vkCmdWriteTimestamp2, _t_swvk_CmdWriteTimestamp2);
    DISPATCH(vkCmdCopyQueryPoolResults, _t_swvk_CmdCopyQueryPoolResults);

    /* Render pass 2 */
    DISPATCH(vkCreateRenderPass2, _t_swvk_CreateRenderPass2);
    DISPATCH(vkCmdBeginRenderPass2, _t_swvk_CmdBeginRenderPass2);
    DISPATCH(vkCmdNextSubpass2, _t_swvk_CmdNextSubpass2);
    DISPATCH(vkCmdEndRenderPass2, _t_swvk_CmdEndRenderPass2);

    /* Dynamic state + clear commands */
    DISPATCH(vkCmdSetLineWidth, _t_swvk_CmdSetLineWidth);
    DISPATCH(vkCmdSetDepthBias, _t_swvk_CmdSetDepthBias);
    DISPATCH(vkCmdSetBlendConstants, _t_swvk_CmdSetBlendConstants);
    DISPATCH(vkCmdSetDepthBounds, _t_swvk_CmdSetDepthBounds);
    DISPATCH(vkCmdSetStencilCompareMask, _t_swvk_CmdSetStencilCompareMask);
    DISPATCH(vkCmdSetStencilWriteMask, _t_swvk_CmdSetStencilWriteMask);
    DISPATCH(vkCmdSetStencilReference, _t_swvk_CmdSetStencilReference);
    DISPATCH(vkCmdClearColorImage, _t_swvk_CmdClearColorImage);
    DISPATCH(vkCmdClearDepthStencilImage, _t_swvk_CmdClearDepthStencilImage);
    DISPATCH(vkCmdClearAttachments, _t_swvk_CmdClearAttachments);
    DISPATCH(vkCmdBlitImage, _t_swvk_CmdBlitImage);
    DISPATCH(vkCmdResolveImage, _t_swvk_CmdResolveImage);

    /* Vulkan 1.3 "2" command variants */
    DISPATCH(vkCmdCopyBuffer2, _t_swvk_CmdCopyBuffer2);
    DISPATCH(vkCmdCopyImage2, _t_swvk_CmdCopyImage2);
    DISPATCH(vkCmdCopyBufferToImage2, _t_swvk_CmdCopyBufferToImage2);
    DISPATCH(vkCmdCopyImageToBuffer2, _t_swvk_CmdCopyImageToBuffer2);
    DISPATCH(vkCmdBlitImage2, _t_swvk_CmdBlitImage2);
    DISPATCH(vkCmdResolveImage2, _t_swvk_CmdResolveImage2);

    /* Command no-ops */
    DISPATCH(vkCmdSetDeviceMask, _t_swvk_CmdSetDeviceMask);
    DISPATCH(vkCmdDispatch, _t_swvk_CmdDispatch);
    DISPATCH(vkCmdDispatchBase, _t_swvk_CmdDispatchBase);
    DISPATCH(vkCmdDispatchIndirect, _t_swvk_CmdDispatchIndirect);
    DISPATCH(vkCmdExecuteCommands, _t_swvk_CmdExecuteCommands);

    /* Indirect draw + sparse */
    DISPATCH(vkCmdDrawIndirect, _t_swvk_CmdDrawIndirect);
    DISPATCH(vkCmdDrawIndexedIndirect, _t_swvk_CmdDrawIndexedIndirect);
    DISPATCH(vkCmdDrawIndirectCount, _t_swvk_CmdDrawIndirectCount);
    DISPATCH(vkCmdDrawIndexedIndirectCount, _t_swvk_CmdDrawIndexedIndirectCount);
    DISPATCH(vkQueueBindSparse, _t_swvk_QueueBindSparse);

    #undef DISPATCH

    return NULL;
}

/****************************************************************************/
/* Raw proc addr lookup -- returns standard C function pointers             */
/* (no APICALL, no Self parameter) for direct app use via                   */
/* vkGetDeviceProcAddr.                                                     */
/****************************************************************************/

static PFN_vkVoidFunction swvk_LookupRawProcAddr(const char *pName)
{
    #define RAW(vkName, fn) \
        if (strcmp(pName, #vkName) == 0) return (PFN_vkVoidFunction)(fn)

    RAW(vkCreateInstance, swvk_CreateInstance);
    RAW(vkDestroyInstance, swvk_DestroyInstance);
    RAW(vkEnumerateInstanceVersion, swvk_EnumerateInstanceVersion);
    RAW(vkEnumerateInstanceExtensionProperties, swvk_EnumerateInstanceExtensionProperties);
    RAW(vkEnumeratePhysicalDevices, swvk_EnumeratePhysicalDevices);
    RAW(vkGetPhysicalDeviceProperties, swvk_GetPhysicalDeviceProperties);
    RAW(vkGetPhysicalDeviceFeatures, swvk_GetPhysicalDeviceFeatures);
    RAW(vkGetPhysicalDeviceQueueFamilyProperties, swvk_GetPhysicalDeviceQueueFamilyProperties);
    RAW(vkGetPhysicalDeviceMemoryProperties, swvk_GetPhysicalDeviceMemoryProperties);
    RAW(vkGetPhysicalDeviceFormatProperties, swvk_GetPhysicalDeviceFormatProperties);
    RAW(vkCreateDevice, swvk_CreateDevice);
    RAW(vkDestroyDevice, swvk_DestroyDevice);
    RAW(vkGetDeviceQueue, swvk_GetDeviceQueue);
    RAW(vkDeviceWaitIdle, swvk_DeviceWaitIdle);
    RAW(vkQueueWaitIdle, swvk_QueueWaitIdle);
    RAW(vkAllocateMemory, swvk_AllocateMemory);
    RAW(vkFreeMemory, swvk_FreeMemory);
    RAW(vkMapMemory, swvk_MapMemory);
    RAW(vkUnmapMemory, swvk_UnmapMemory);
    RAW(vkCreateBuffer, swvk_CreateBuffer);
    RAW(vkDestroyBuffer, swvk_DestroyBuffer);
    RAW(vkGetBufferMemoryRequirements, swvk_GetBufferMemoryRequirements);
    RAW(vkBindBufferMemory, swvk_BindBufferMemory);
    RAW(vkCreateImage, swvk_CreateImage);
    RAW(vkDestroyImage, swvk_DestroyImage);
    RAW(vkGetImageMemoryRequirements, swvk_GetImageMemoryRequirements);
    RAW(vkBindImageMemory, swvk_BindImageMemory);
    RAW(vkCreateImageView, swvk_CreateImageView);
    RAW(vkDestroyImageView, swvk_DestroyImageView);
    RAW(vkCreateSampler, swvk_CreateSampler);
    RAW(vkDestroySampler, swvk_DestroySampler);
    RAW(vkCreateShaderModule, swvk_CreateShaderModule);
    RAW(vkDestroyShaderModule, swvk_DestroyShaderModule);
    RAW(vkCreatePipelineLayout, swvk_CreatePipelineLayout);
    RAW(vkDestroyPipelineLayout, swvk_DestroyPipelineLayout);
    RAW(vkCreatePipelineCache, swvk_CreatePipelineCache);
    RAW(vkDestroyPipelineCache, swvk_DestroyPipelineCache);
    RAW(vkCreateGraphicsPipelines, swvk_CreateGraphicsPipelines);
    RAW(vkDestroyPipeline, swvk_DestroyPipeline);
    RAW(vkCreateDescriptorSetLayout, swvk_CreateDescriptorSetLayout);
    RAW(vkDestroyDescriptorSetLayout, swvk_DestroyDescriptorSetLayout);
    RAW(vkCreateDescriptorPool, swvk_CreateDescriptorPool);
    RAW(vkDestroyDescriptorPool, swvk_DestroyDescriptorPool);
    RAW(vkAllocateDescriptorSets, swvk_AllocateDescriptorSets);
    RAW(vkFreeDescriptorSets, swvk_FreeDescriptorSets);
    RAW(vkUpdateDescriptorSets, swvk_UpdateDescriptorSets);
    RAW(vkCreateCommandPool, swvk_CreateCommandPool);
    RAW(vkDestroyCommandPool, swvk_DestroyCommandPool);
    RAW(vkAllocateCommandBuffers, swvk_AllocateCommandBuffers);
    RAW(vkFreeCommandBuffers, swvk_FreeCommandBuffers);
    RAW(vkBeginCommandBuffer, swvk_BeginCommandBuffer);
    RAW(vkEndCommandBuffer, swvk_EndCommandBuffer);
    RAW(vkCmdBindPipeline, swvk_CmdBindPipeline);
    RAW(vkCmdSetViewport, swvk_CmdSetViewport);
    RAW(vkCmdSetScissor, swvk_CmdSetScissor);
    RAW(vkCmdDraw, swvk_CmdDraw);
    RAW(vkCmdBeginRendering, swvk_CmdBeginRendering);
    RAW(vkCmdEndRendering, swvk_CmdEndRendering);
    RAW(vkCmdPushConstants, swvk_CmdPushConstants);
    RAW(vkCmdBindVertexBuffers, swvk_CmdBindVertexBuffers);
    RAW(vkCmdBindIndexBuffer, swvk_CmdBindIndexBuffer);
    RAW(vkCmdDrawIndexed, swvk_CmdDrawIndexed);
    RAW(vkCmdBindDescriptorSets, swvk_CmdBindDescriptorSets);
    RAW(vkQueueSubmit, swvk_QueueSubmit);
    RAW(vkCreateFence, swvk_CreateFence);
    RAW(vkDestroyFence, swvk_DestroyFence);
    RAW(vkWaitForFences, swvk_WaitForFences);
    RAW(vkResetFences, swvk_ResetFences);
    RAW(vkCreateSemaphore, swvk_CreateSemaphore);
    RAW(vkDestroySemaphore, swvk_DestroySemaphore);
    RAW(vkCreateEvent, swvk_CreateEvent);
    RAW(vkDestroyEvent, swvk_DestroyEvent);
    RAW(vkGetEventStatus, swvk_GetEventStatus);
    RAW(vkSetEvent, swvk_SetEvent);
    RAW(vkResetEvent, swvk_ResetEvent);
    RAW(vkCreateQueryPool, swvk_CreateQueryPool);
    RAW(vkDestroyQueryPool, swvk_DestroyQueryPool);
    RAW(vkGetQueryPoolResults, swvk_GetQueryPoolResults);
    RAW(vkResetQueryPool, swvk_ResetQueryPool);
    RAW(vkCmdWriteTimestamp, swvk_CmdWriteTimestamp);
    RAW(vkCmdWriteTimestamp2, swvk_CmdWriteTimestamp2);
    RAW(vkCmdBeginQuery, swvk_CmdBeginQuery);
    RAW(vkCmdEndQuery, swvk_CmdEndQuery);
    RAW(vkCmdResetQueryPool, swvk_CmdResetQueryPool);
    RAW(vkCmdCopyQueryPoolResults, swvk_CmdCopyQueryPoolResults);
    RAW(vkGetDeviceProcAddr, swvk_GetDeviceProcAddr);
    RAW(vkGetFenceStatus, swvk_GetFenceStatus);
    RAW(vkGetPipelineCacheData, swvk_GetPipelineCacheData);
    RAW(vkMergePipelineCaches, swvk_MergePipelineCaches);
    RAW(vkQueueSubmit2, swvk_QueueSubmit2);
    RAW(vkQueueBindSparse, swvk_QueueBindSparse);
    RAW(vkResetCommandBuffer, swvk_ResetCommandBuffer);
    RAW(vkResetCommandPool, swvk_ResetCommandPool);
    RAW(vkTrimCommandPool, swvk_TrimCommandPool);
    RAW(vkResetDescriptorPool, swvk_ResetDescriptorPool);
    RAW(vkFlushMappedMemoryRanges, swvk_FlushMappedMemoryRanges);
    RAW(vkInvalidateMappedMemoryRanges, swvk_InvalidateMappedMemoryRanges);
    RAW(vkCmdPipelineBarrier, swvk_CmdPipelineBarrier);
    RAW(vkCmdPipelineBarrier2, swvk_CmdPipelineBarrier2);
    RAW(vkCreateRenderPass, swvk_CreateRenderPass);
    RAW(vkDestroyRenderPass, swvk_DestroyRenderPass);
    RAW(vkCreateFramebuffer, swvk_CreateFramebuffer);
    RAW(vkDestroyFramebuffer, swvk_DestroyFramebuffer);
    RAW(vkCmdBeginRenderPass, swvk_CmdBeginRenderPass);
    RAW(vkCmdEndRenderPass, swvk_CmdEndRenderPass);
    RAW(vkCmdNextSubpass, swvk_CmdNextSubpass);

    /* WSI entries -- added alongside the matching ogles2_icd fix for the
    ** vkGetDeviceProcAddr ABI bug diagnosed by afxgroup
    ** (derfsss/VulkanOS4#1). Without these, apps resolving WSI by raw
    ** PFN_vk* pointer would get NULL. */

    /* WSI -- surface queries */
    RAW(vkGetPhysicalDeviceSurfaceSupportKHR, swvk_GetPhysicalDeviceSurfaceSupportKHR);
    RAW(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, swvk_GetPhysicalDeviceSurfaceCapabilitiesKHR);
    RAW(vkGetPhysicalDeviceSurfaceFormatsKHR, swvk_GetPhysicalDeviceSurfaceFormatsKHR);
    RAW(vkGetPhysicalDeviceSurfacePresentModesKHR, swvk_GetPhysicalDeviceSurfacePresentModesKHR);

    /* WSI -- swapchain */
    RAW(vkCreateSwapchainKHR, swvk_CreateSwapchainKHR);
    RAW(vkDestroySwapchainKHR, swvk_DestroySwapchainKHR);
    RAW(vkGetSwapchainImagesKHR, swvk_GetSwapchainImagesKHR);
    RAW(vkAcquireNextImageKHR, swvk_AcquireNextImageKHR);
    RAW(vkQueuePresentKHR, swvk_QueuePresentKHR);

    #undef RAW
    return NULL;
}

static PFN_vkVoidFunction APICALL _icd_GetInstanceProcAddr(
    struct SWVKICDIFace *Self,
    VkInstance instance,
    const char *pName)
{
    (void)Self;
    (void)instance;

    if (!pName) return NULL;

    return swvk_LookupProcAddr(pName);
}

/****************************************************************************/
/* ICD interface: vk_icdNegotiateLoaderICDInterfaceVersion                  */
/****************************************************************************/

static VkResult APICALL _icd_NegotiateLoaderICDInterfaceVersion(
    struct SWVKICDIFace *Self,
    uint32_t *pSupportedVersion)
{
    (void)Self;

    if (!pSupportedVersion)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* We support ICD interface version 3 (the latest).
    ** The loader passes its maximum supported version; we return
    ** the minimum of the loader's and our supported version. */
    if (*pSupportedVersion > 3)
        *pSupportedVersion = 3;

    D(("[software_vk] Negotiated ICD interface version %lu\n",
                       (unsigned long)*pSupportedVersion));

    return VK_SUCCESS;
}

/****************************************************************************/
/* ICD interface: vk_icdGetPhysicalDeviceProcAddr                           */
/* Optional in ICD interface version 3. Returns physical-device-specific    */
/* function pointers, or NULL to use vk_icdGetInstanceProcAddr instead.     */
/****************************************************************************/

static PFN_vkVoidFunction APICALL _icd_GetPhysicalDeviceProcAddr(
    struct SWVKICDIFace *Self,
    VkInstance instance,
    const char *pName)
{
    (void)Self;
    (void)instance;
    (void)pName;

    /* All functions are resolved via GetInstanceProcAddr */
    return NULL;
}
