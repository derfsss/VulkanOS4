/*
** vulkan.library -- Vulkan Loader for AmigaOS 4
**
** loader_main.c -- Library skeleton: resident tag, init, open, close, expunge
**
** Follows standard AmigaOS 4 shared library conventions using
** RTF_AUTOINIT and CLT_* initialization tags.
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/interfaces.h>
#include <dos/dos.h>
#include <interfaces/exec.h>
#include <interfaces/dos.h>
#include <utility/tagitem.h>

#include "loader_internal.h"

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

struct ExecIFace    *IExec   = NULL;
struct DOSIFace     *IDOS    = NULL;
struct Library      *DOSBase = NULL;

/* newlib support (needed for string.h, stdio.h functions) */
static struct Library   *NewlibBase = NULL;
struct Interface        *INewlib    = NULL;

/* Loader global state */
struct LoaderState g_loaderState = {0};

/****************************************************************************/
/* Forward declarations                                                     */
/****************************************************************************/

static uint32 _manager_Obtain(struct LibraryManagerInterface *Self);
static uint32 _manager_Release(struct LibraryManagerInterface *Self);
static struct VulkanLibBase *_manager_Open(struct LibraryManagerInterface *Self, uint32 version);
static BPTR _manager_Close(struct LibraryManagerInterface *Self);
static BPTR _manager_Expunge(struct LibraryManagerInterface *Self);

static uint32 APICALL _main_Obtain(struct VulkanIFace *Self);
static uint32 APICALL _main_Release(struct VulkanIFace *Self);

/****************************************************************************/
/* Main interface (VulkanIFace) vector table                                */
/* Order MUST match the struct VulkanIFace field order in interfaces/vulkan.h*/
/****************************************************************************/

static const APTR _main_Vectors[] =
{
    /* Standard AmigaOS interface methods */
    (APTR)_main_Obtain,
    (APTR)_main_Release,
    NULL,                               /* Expunge */
    NULL,                               /* Clone */

    /* AmigaOS-specific loader functions */
    (APTR)Loader_VkAmigaGetLoaderVersion,
    (APTR)Loader_VkAmigaSetICDSearchPath,
    (APTR)Loader_VkAmigaSetLayerSearchPath,

    /* Vulkan 1.0 - Instance (loader-implemented) */
    (APTR)Loader_vkCreateInstance,
    (APTR)Loader_vkDestroyInstance,
    (APTR)Loader_vkEnumerateInstanceVersion,
    (APTR)Loader_vkEnumerateInstanceExtensionProperties,
    (APTR)Loader_vkEnumerateInstanceLayerProperties,
    (APTR)Loader_vkEnumeratePhysicalDevices,
    NULL,   /* vkGetPhysicalDeviceProperties -- populated from ICD */
    NULL,   /* vkGetPhysicalDeviceFeatures */
    NULL,   /* vkGetPhysicalDeviceQueueFamilyProperties */
    NULL,   /* vkGetPhysicalDeviceMemoryProperties */
    NULL,   /* vkGetPhysicalDeviceFormatProperties */
    (APTR)Loader_vkGetInstanceProcAddr,
    (APTR)Loader_vkGetDeviceProcAddr,

    /* Device extension enumeration (populated from ICD) */
    NULL,                               /* vkEnumerateDeviceExtensionProperties */

    /* Vulkan 1.0 - Device (populated from ICD at vkCreateInstance time) */
    NULL, NULL, NULL, NULL, NULL,       /* CreateDevice..QueueWaitIdle */

    /* Memory (populated from ICD) */
    NULL, NULL, NULL, NULL,             /* AllocateMemory..UnmapMemory */

    /* Buffer / Image (populated from ICD) */
    NULL, NULL, NULL, NULL,             /* CreateBuffer..BindBufferMemory */
    NULL, NULL, NULL, NULL,             /* CreateImage..BindImageMemory */
    NULL, NULL,                         /* CreateImageView, DestroyImageView */

    /* Sampler (populated from ICD) */
    NULL, NULL,                         /* CreateSampler, DestroySampler */

    /* Shader / Pipeline (populated from ICD) */
    NULL, NULL,                         /* CreateShaderModule, DestroyShaderModule */
    NULL, NULL,                         /* CreatePipelineLayout, DestroyPipelineLayout */
    NULL, NULL,                         /* CreateGraphicsPipelines, DestroyPipeline */
    NULL, NULL,                         /* CreatePipelineCache, DestroyPipelineCache */

    /* Descriptor sets (populated from ICD) */
    NULL, NULL,                         /* CreateDescriptorSetLayout, DestroyDescriptorSetLayout */
    NULL, NULL,                         /* CreateDescriptorPool, DestroyDescriptorPool */
    NULL, NULL, NULL,                   /* AllocateDescriptorSets, FreeDescriptorSets, UpdateDescriptorSets */

    /* Command buffer (populated from ICD) */
    NULL, NULL,                         /* CreateCommandPool, DestroyCommandPool */
    NULL, NULL,                         /* AllocateCommandBuffers, FreeCommandBuffers */
    NULL, NULL,                         /* BeginCommandBuffer, EndCommandBuffer */

    /* Command recording (populated from ICD) */
    NULL, NULL, NULL, NULL,             /* CmdBindPipeline..CmdDraw */
    NULL, NULL,                         /* CmdBeginRendering, CmdEndRendering */
    NULL, NULL, NULL, NULL,             /* CmdPushConstants, CmdBindVertexBuffers, CmdBindIndexBuffer, CmdDrawIndexed */
    NULL,                               /* CmdBindDescriptorSets */

    /* Synchronisation (populated from ICD) */
    NULL, NULL, NULL, NULL,             /* CreateFence..ResetFences */
    NULL, NULL,                         /* CreateSemaphore, DestroySemaphore */
    NULL,                               /* QueueSubmit */

    /* WSI - Surface (loader-owned) */
    (APTR)Loader_vkDestroySurfaceKHR,
    NULL, NULL, NULL, NULL,             /* GetPhysicalDeviceSurface*KHR -- forwarded to ICD */

    /* WSI - Swapchain (populated from ICD) */
    NULL, NULL, NULL, NULL, NULL,       /* Create/Destroy/GetImages/Acquire/Present */

    /* WSI - AmigaOS extension (loader-owned) */
    (APTR)Loader_vkCreateAmigaSurfaceAMIGA,
    (APTR)Loader_vkGetPhysicalDeviceAmigaPresentationSupportAMIGA,

    /* Memory, command buffer, and pipeline cache management (populated from ICD) */
    NULL, NULL,                         /* FlushMappedMemoryRanges, InvalidateMappedMemoryRanges */
    NULL,                               /* ResetCommandBuffer */
    NULL,                               /* CmdPipelineBarrier */
    NULL, NULL,                         /* ResetCommandPool, TrimCommandPool */
    NULL,                               /* GetFenceStatus */
    NULL, NULL,                         /* GetPipelineCacheData, MergePipelineCaches */
    NULL,                               /* ResetDescriptorPool */

    /* Legacy render pass (populated from ICD) */
    NULL, NULL,                         /* CreateRenderPass, DestroyRenderPass */
    NULL, NULL,                         /* CreateFramebuffer, DestroyFramebuffer */
    NULL, NULL, NULL,                   /* CmdBeginRenderPass, CmdEndRenderPass, CmdNextSubpass */

    /* Vulkan 1.3 dynamic state (populated from ICD) */
    NULL, NULL, NULL,                   /* CmdSetCullMode, CmdSetFrontFace, CmdSetPrimitiveTopology */
    NULL, NULL,                         /* CmdSetViewportWithCount, CmdSetScissorWithCount */
    NULL,                               /* CmdBindVertexBuffers2 */
    NULL, NULL, NULL,                   /* CmdSetDepthTestEnable, CmdSetDepthWriteEnable, CmdSetDepthCompareOp */
    NULL, NULL, NULL,                   /* CmdSetDepthBoundsTestEnable, CmdSetStencilTestEnable, CmdSetStencilOp */
    NULL, NULL, NULL,                   /* CmdSetRasterizerDiscardEnable, CmdSetDepthBiasEnable, CmdSetPrimitiveRestartEnable */

    /* Transfer commands (populated from ICD) */
    NULL, NULL,                         /* CmdCopyBuffer, CmdCopyBufferToImage */
    NULL, NULL,                         /* CmdCopyImageToBuffer, CmdCopyImage */
    NULL, NULL,                         /* CmdFillBuffer, CmdUpdateBuffer */

    /* Vulkan 1.1/1.2/1.3 wrappers (populated from ICD) */
    NULL, NULL,                         /* GetPhysicalDeviceProperties2, GetPhysicalDeviceFeatures2 */
    NULL, NULL,                         /* GetPhysicalDeviceQueueFamilyProperties2, GetPhysicalDeviceMemoryProperties2 */
    NULL,                               /* GetPhysicalDeviceFormatProperties2 */
    NULL, NULL,                         /* GetBufferMemoryRequirements2, GetImageMemoryRequirements2 */
    NULL, NULL,                         /* BindBufferMemory2, BindImageMemory2 */
    NULL, NULL,                         /* CmdPipelineBarrier2, QueueSubmit2 */
    NULL,                               /* GetPhysicalDeviceImageFormatProperties */

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
    {CLT_DataSize,      sizeof(struct VulkanLibBase)},
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

    DOSBase = IExec->OpenLibrary("dos.library", 50);
    if (!DOSBase) return FALSE;
    IDOS = (struct DOSIFace *)IExec->GetInterface(DOSBase, "main", 1, NULL);
    if (!IDOS) return FALSE;

    return TRUE;
}

static void _close_dependencies(void)
{
    if (IDOS)       { IExec->DropInterface((struct Interface *)IDOS); IDOS = NULL; }
    if (DOSBase)    { IExec->CloseLibrary(DOSBase); DOSBase = NULL; }
    if (INewlib)    { IExec->DropInterface(INewlib); INewlib = NULL; }
    if (NewlibBase) { IExec->CloseLibrary(NewlibBase); NewlibBase = NULL; }
}

/****************************************************************************/
/* Library initialization                                                   */
/****************************************************************************/

static struct Library *_lib_Init(struct Library *libBase, BPTR seglist,
                                 struct Interface *exec)
{
    struct VulkanLibBase *base = (struct VulkanLibBase *)libBase;

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

    IExec->InitSemaphore(&base->lib_Lock);

    if (!_open_dependencies())
    {
        _close_dependencies();
        return NULL;
    }

    IExec->DebugPrintF("[vulkan.library] Initialized v%ld.%ld\n",
                       (long)LIBVER, (long)LIBREV);

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

static struct VulkanLibBase *_manager_Open(struct LibraryManagerInterface *Self,
                                          uint32 version)
{
    struct VulkanLibBase *base = (struct VulkanLibBase *)Self->Data.LibBase;

    if (version > LIBVER)
        return NULL;

    base->lib_Lib.lib_OpenCnt++;
    base->lib_Lib.lib_Flags &= ~LIBF_DELEXP;

    return base;
}

static BPTR _manager_Close(struct LibraryManagerInterface *Self)
{
    struct VulkanLibBase *base = (struct VulkanLibBase *)Self->Data.LibBase;

    base->lib_Lib.lib_OpenCnt--;

    if (base->lib_Lib.lib_OpenCnt == 0)
    {
        /* Last close -- unload ICDs */
        LoaderUnloadICDs();

        if (base->lib_Lib.lib_Flags & LIBF_DELEXP)
            return _manager_Expunge(Self);
    }

    return (BPTR)0;
}

static BPTR _manager_Expunge(struct LibraryManagerInterface *Self)
{
    struct VulkanLibBase *base = (struct VulkanLibBase *)Self->Data.LibBase;

    if (base->lib_Lib.lib_OpenCnt == 0)
    {
        BPTR seglist = base->lib_SegList;

        LoaderUnloadICDs();
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

static uint32 APICALL _main_Obtain(struct VulkanIFace *Self)
{
    return ++Self->Data.RefCount;
}

static uint32 APICALL _main_Release(struct VulkanIFace *Self)
{
    return Self->Data.RefCount--;
}
