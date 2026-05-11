/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_main.c -- Library skeleton: resident tag, init, open, close, expunge
**                 ICD interface: GetInstanceProcAddr, NegotiateVersion
**                 Vulkan instance and physical device implementations
**
** Follows the same AmigaOS 4 shared library conventions as software_vk.library.
** Opens Warp3DNova.library to discover GPUs and report device properties.
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/interfaces.h>
#include <exec/emulation.h>
#include <interfaces/exec.h>
#include <string.h>

#include "ogles2vk_internal.h"

/* W3D Nova types -- we only need the struct definitions for GPU enumeration.
** We use void* in the public header to avoid forcing this dependency
** on every file, but here in the main source we need the real types. */
#include <Warp3DNova/Warp3DNova.h>

/*--------------------------------------------------------------------------
** Minimal IW3DNova interface definition.
** The full <interfaces/Warp3DNova.h> may not be available in all SDK
** installations. We define only the interface methods we actually call.
** The layout matches the real interface: standard Obtain/Release/Expunge/Clone
** followed by library methods in the order they appear in the IDL.
**------------------------------------------------------------------------*/
struct IW3DNova
{
    struct InterfaceData Data;

    uint32 APICALL (*Obtain)(struct IW3DNova *Self);
    uint32 APICALL (*Release)(struct IW3DNova *Self);
    void   APICALL (*Expunge)(struct IW3DNova *Self);
    struct Interface * APICALL (*Clone)(struct IW3DNova *Self);

    /* Library methods (order matches IDL / inline4 header) */
    W3DN_Gpu * APICALL (*W3DN_GetGPUsList)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_Gpu * APICALL (*W3DN_GetGPUsListTags)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, ...);
    void APICALL (*W3DN_FreeGPUsList)(struct IW3DNova *Self, W3DN_Gpu *gpusList);
    W3DN_ScreenMode * APICALL (*W3DN_GetScreenModeList)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_ScreenMode * APICALL (*W3DN_GetScreenModeListTags)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, ...);
    void APICALL (*W3DN_FreeScreenModeList)(struct IW3DNova *Self, W3DN_ScreenMode *screenModeList);
    uint32 APICALL (*W3DN_BestModeID)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, struct TagItem *tags);
    uint32 APICALL (*W3DN_BestModeIDTags)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, ...);
    uint32 APICALL (*W3DN_Query)(struct IW3DNova *Self, W3DN_Gpu *gpu, W3DN_CapQuery query);

    /* Methods 13-19 (needed for context creation) */
    void *_W3DN_GetTexFmtInfo;          /* slot 13 - placeholder */
    void *_W3DN_GetTexFmtInfoTags;      /* slot 14 - placeholder */
    void *_W3DN_GetBMFmtInfo;           /* slot 15 - placeholder */
    void *_W3DN_GetBMFmtInfoTags;       /* slot 16 - placeholder */
    W3DN_Context * APICALL (*W3DN_CreateContext)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_Context * APICALL (*W3DN_CreateContextTags)(struct IW3DNova *Self, W3DN_ErrorCode *errCode, ...);
    const char * APICALL (*W3DN_GetErrorString)(struct IW3DNova *Self, W3DN_ErrorCode errCode);
};

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

/* Warp3DNova library and interface (for GPU enumeration) */
static struct Library        *W3DNovaBase = NULL;
struct IW3DNova              *IW3DNova    = NULL;

/* OGLES2 library and interface (for rendering) */
static struct Library        *OGLES2Base = NULL;
struct OGLES2IFace           *IOGLES2    = NULL;

/****************************************************************************/
/* Global physical device state (singleton -- first W3D Nova GPU)           */
/****************************************************************************/

OGLES2VKPhysicalDevice g_physDevice;

/* Instance state (singleton) */
static OGLES2VKInstance g_instance;
static VkBool32      g_instanceCreated = VK_FALSE;

/* Track whether GPU was discovered */
static VkBool32 g_gpuDiscovered = VK_FALSE;

/****************************************************************************/
/* ICD interface struct (matches loader's ICDMainIFace layout)              */
/****************************************************************************/

struct OGLES2VKICDIFace
{
    struct InterfaceData Data;
    uint32 APICALL (*Obtain)(struct OGLES2VKICDIFace *Self);
    uint32 APICALL (*Release)(struct OGLES2VKICDIFace *Self);
    void   APICALL (*Expunge)(struct OGLES2VKICDIFace *Self);
    struct Interface * APICALL (*Clone)(struct OGLES2VKICDIFace *Self);
    PFN_vkVoidFunction APICALL (*vk_icdGetInstanceProcAddr)(struct OGLES2VKICDIFace *Self, VkInstance instance, const char *pName);
    VkResult APICALL (*vk_icdNegotiateLoaderICDInterfaceVersion)(struct OGLES2VKICDIFace *Self, uint32_t *pSupportedVersion);
    PFN_vkVoidFunction APICALL (*vk_icdGetPhysicalDeviceProcAddr)(struct OGLES2VKICDIFace *Self, VkInstance instance, const char *pName);
};

/****************************************************************************/
/* Forward declarations                                                     */
/****************************************************************************/

static uint32 _manager_Obtain(struct LibraryManagerInterface *Self);
static uint32 _manager_Release(struct LibraryManagerInterface *Self);
static struct OGLES2VKLibBase *_manager_Open(struct LibraryManagerInterface *Self, uint32 version);
static BPTR _manager_Close(struct LibraryManagerInterface *Self);
static BPTR _manager_Expunge(struct LibraryManagerInterface *Self);

static uint32 APICALL _main_Obtain(struct OGLES2VKICDIFace *Self);
static uint32 APICALL _main_Release(struct OGLES2VKICDIFace *Self);

/* ICD interface entry points */
static PFN_vkVoidFunction APICALL _icd_GetInstanceProcAddr(struct OGLES2VKICDIFace *Self, VkInstance instance, const char *pName);
static VkResult APICALL _icd_NegotiateLoaderICDInterfaceVersion(struct OGLES2VKICDIFace *Self, uint32_t *pSupportedVersion);
static PFN_vkVoidFunction APICALL _icd_GetPhysicalDeviceProcAddr(struct OGLES2VKICDIFace *Self, VkInstance instance, const char *pName);

/****************************************************************************/
/* Main interface (ICD entry points) vector table                           */
/* Order MUST match the struct OGLES2VKICDIFace field order AND the loader's   */
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
    {CLT_DataSize,      sizeof(struct OGLES2VKLibBase)},
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
/* GPU discovery via Warp3DNova                                             */
/****************************************************************************/

static void _discover_gpu(void)
{
    W3DN_ErrorCode err;
    W3DN_Gpu *gpuList;

    memset(&g_physDevice, 0, sizeof(g_physDevice));
    g_gpuDiscovered = VK_FALSE;

    gpuList = IW3DNova->W3DN_GetGPUsList(&err, NULL);
    if (!gpuList || err != W3DNEC_SUCCESS)
    {
        IExec->DebugPrintF("[ogles2_vk] W3DN_GetGPUsList failed (err=%ld)\n",
                           (long)err);
        if (gpuList)
            IW3DNova->W3DN_FreeGPUsList(gpuList);
        return;
    }

    /* Use the first GPU in the list */
    IExec->DebugPrintF("[ogles2_vk] Found GPU: %s (board %lu)\n",
                       gpuList->name, (unsigned long)gpuList->boardNum);

    /* Populate VkPhysicalDeviceProperties */
    g_physDevice.properties.apiVersion    = VK_MAKE_API_VERSION(0, 1, 3, 0);
    g_physDevice.properties.driverVersion = VK_MAKE_API_VERSION(0, LIBVER, LIBREV, 0);
    g_physDevice.properties.vendorID      = 0x10001;  /* AmigaOS vendor placeholder */
    g_physDevice.properties.deviceID      = gpuList->boardNum;
    g_physDevice.properties.deviceType    = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

    strncpy(g_physDevice.properties.deviceName, gpuList->name,
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    g_physDevice.properties.deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';

    /* Pipeline cache UUID -- unique per driver version */
    memset(g_physDevice.properties.pipelineCacheUUID, 0, VK_UUID_SIZE);
    g_physDevice.properties.pipelineCacheUUID[0] = 'W';
    g_physDevice.properties.pipelineCacheUUID[1] = '3';
    g_physDevice.properties.pipelineCacheUUID[2] = 'D';
    g_physDevice.properties.pipelineCacheUUID[3] = (uint8_t)LIBVER;
    g_physDevice.properties.pipelineCacheUUID[4] = (uint8_t)LIBREV;

    /* Basic limits -- query W3D Nova for what we can */
    g_physDevice.properties.limits.maxImageDimension1D          = 4096;
    g_physDevice.properties.limits.maxImageDimension2D          = 4096;
    g_physDevice.properties.limits.maxImageDimension3D          = 256;
    g_physDevice.properties.limits.maxImageDimensionCube         = 4096;
    g_physDevice.properties.limits.maxFramebufferWidth          = 4096;
    g_physDevice.properties.limits.maxFramebufferHeight         = 4096;
    g_physDevice.properties.limits.maxViewportDimensions[0]     = 4096;
    g_physDevice.properties.limits.maxViewportDimensions[1]     = 4096;
    g_physDevice.properties.limits.maxColorAttachments          = 1;
    g_physDevice.properties.limits.maxBoundDescriptorSets       = 4;
    g_physDevice.properties.limits.maxPushConstantsSize         = 128;
    g_physDevice.properties.limits.maxVertexInputAttributes     = 16;
    g_physDevice.properties.limits.maxVertexInputBindings       = 8;

    /*
     * Allocation / alignment limits — VMA queries these during init AND on
     * every allocation to round sizes / pick block layout. With the prior
     * memset() they stayed 0, which made VMA's internal
     * `min(blockSize, heapSize) % bufferImageGranularity` math divide by
     * zero (or compute alignment 0), causing vmaCreateImage to bail out
     * with VK_ERROR_OUT_OF_DEVICE_MEMORY before ever calling
     * vkAllocateMemory. These conservative defaults match typical desktop
     * GPUs and let VMA proceed.
     */
    g_physDevice.properties.limits.bufferImageGranularity            = 1024;
    g_physDevice.properties.limits.nonCoherentAtomSize               = 64;
    g_physDevice.properties.limits.minMemoryMapAlignment             = 64;
    g_physDevice.properties.limits.minUniformBufferOffsetAlignment   = 256;
    g_physDevice.properties.limits.minStorageBufferOffsetAlignment   = 256;
    g_physDevice.properties.limits.minTexelBufferOffsetAlignment     = 16;
    g_physDevice.properties.limits.optimalBufferCopyOffsetAlignment  = 4;
    g_physDevice.properties.limits.optimalBufferCopyRowPitchAlignment = 4;
    g_physDevice.properties.limits.maxMemoryAllocationCount          = 4096;
    g_physDevice.properties.limits.maxSamplerAllocationCount         = 4096;
    g_physDevice.properties.limits.maxStorageBufferRange             = 0x80000000u;  /* 2 GB */
    g_physDevice.properties.limits.maxUniformBufferRange             = 65536;
    g_physDevice.properties.limits.sparseAddressSpaceSize            = 0;

    /* Query W3D Nova capabilities where available */
    {
        uint32 maxTexW = (uint32)IW3DNova->W3DN_Query(gpuList, W3DN_Q_MAXTEXWIDTH);
        uint32 maxTexH = (uint32)IW3DNova->W3DN_Query(gpuList, W3DN_Q_MAXTEXHEIGHT);
        uint32 maxRW   = (uint32)IW3DNova->W3DN_Query(gpuList, W3DN_Q_MAXRENDERWIDTH);
        uint32 maxRH   = (uint32)IW3DNova->W3DN_Query(gpuList, W3DN_Q_MAXRENDERHEIGHT);
        uint32 maxTU   = (uint32)IW3DNova->W3DN_Query(gpuList, W3DN_Q_MAXTEXUNITS);

        if (maxTexW > 0)
        {
            g_physDevice.properties.limits.maxImageDimension2D = maxTexW;
            g_physDevice.properties.limits.maxImageDimensionCube = maxTexW;
        }
        if (maxTexH > 0 && maxTexH < g_physDevice.properties.limits.maxImageDimension2D)
            g_physDevice.properties.limits.maxImageDimension2D = maxTexH;
        if (maxRW > 0)
            g_physDevice.properties.limits.maxFramebufferWidth = maxRW;
        if (maxRH > 0)
            g_physDevice.properties.limits.maxFramebufferHeight = maxRH;
        if (maxTU > 0)
        {
            g_physDevice.properties.limits.maxPerStageDescriptorSamplers = maxTU;
            g_physDevice.properties.limits.maxPerStageDescriptorSampledImages = maxTU;
        }

        IExec->DebugPrintF("[ogles2_vk] GPU caps: maxTex=%lux%lu maxRender=%lux%lu texUnits=%lu\n",
                           (unsigned long)maxTexW, (unsigned long)maxTexH,
                           (unsigned long)maxRW, (unsigned long)maxRH,
                           (unsigned long)maxTU);
    }

    /* VkPhysicalDeviceFeatures -- conservative initial set */
    memset(&g_physDevice.features, 0, sizeof(g_physDevice.features));
    g_physDevice.features.robustBufferAccess = VK_FALSE;

    /* VkPhysicalDeviceMemoryProperties -- report one VRAM heap + one host heap */
    memset(&g_physDevice.memoryProperties, 0, sizeof(g_physDevice.memoryProperties));
    g_physDevice.memoryProperties.memoryTypeCount = 2;
    /* Type 0: Device-local (VRAM) */
    g_physDevice.memoryProperties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    g_physDevice.memoryProperties.memoryTypes[0].heapIndex = 0;
    /* Type 1: Host-visible, host-coherent (system RAM for staging) */
    g_physDevice.memoryProperties.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    g_physDevice.memoryProperties.memoryTypes[1].heapIndex = 1;

    g_physDevice.memoryProperties.memoryHeapCount = 2;
    /* Heap 0: VRAM (placeholder size -- real detection not yet implemented) */
    g_physDevice.memoryProperties.memoryHeaps[0].size  = 256ULL * 1024 * 1024;
    g_physDevice.memoryProperties.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    /* Heap 1: System RAM */
    g_physDevice.memoryProperties.memoryHeaps[1].size  = 512ULL * 1024 * 1024;
    g_physDevice.memoryProperties.memoryHeaps[1].flags = 0;

    /* Queue family -- one universal queue supporting graphics + compute + transfer */
    g_physDevice.queueFamilyProperties.queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    g_physDevice.queueFamilyProperties.queueCount              = 1;
    g_physDevice.queueFamilyProperties.timestampValidBits      = 0;
    g_physDevice.queueFamilyProperties.minImageTransferGranularity.width  = 1;
    g_physDevice.queueFamilyProperties.minImageTransferGranularity.height = 1;
    g_physDevice.queueFamilyProperties.minImageTransferGranularity.depth  = 1;

    /* Store GPU pointer (as void*) -- note: we free the list below,
    ** so this pointer becomes invalid. We only need the properties. */
    g_physDevice.w3dGpu = NULL;

    IW3DNova->W3DN_FreeGPUsList(gpuList);
    g_gpuDiscovered = VK_TRUE;

    IExec->DebugPrintF("[ogles2_vk] GPU discovery complete: %s\n",
                       g_physDevice.properties.deviceName);
}

/****************************************************************************/
/* Dependency management                                                    */
/****************************************************************************/

static BOOL _open_dependencies(void)
{
    NewlibBase = IExec->OpenLibrary("newlib.library", 4);
    if (!NewlibBase) return FALSE;
    INewlib = IExec->GetInterface(NewlibBase, "main", 1, NULL);
    if (!INewlib) return FALSE;

    W3DNovaBase = IExec->OpenLibrary("Warp3DNova.library", 0);
    if (!W3DNovaBase)
    {
        IExec->DebugPrintF("[ogles2_vk] Failed to open Warp3DNova.library\n");
        return FALSE;
    }
    IW3DNova = (struct IW3DNova *)IExec->GetInterface(W3DNovaBase, "main", 1, NULL);
    if (!IW3DNova)
    {
        IExec->DebugPrintF("[ogles2_vk] Failed to get IW3DNova interface\n");
        return FALSE;
    }

    /* Open ogles2.library for rendering */
    OGLES2Base = IExec->OpenLibrary("ogles2.library", 0);
    if (OGLES2Base)
    {
        IOGLES2 = (struct OGLES2IFace *)IExec->GetInterface(OGLES2Base, "main", 1, NULL);
        if (!IOGLES2)
            IExec->DebugPrintF("[ogles2_vk] Warning: no IOGLES2 interface\n");
    }
    else
    {
        IExec->DebugPrintF("[ogles2_vk] Warning: ogles2.library not found\n");
    }

    return TRUE;
}

static void _close_dependencies(void)
{
    ogles2vk_CloseGraphics();
    if (IOGLES2)     { IExec->DropInterface((struct Interface *)IOGLES2); IOGLES2 = NULL; }
    if (OGLES2Base)  { IExec->CloseLibrary(OGLES2Base); OGLES2Base = NULL; }
    if (IW3DNova)    { IExec->DropInterface((struct Interface *)IW3DNova); IW3DNova = NULL; }
    if (W3DNovaBase) { IExec->CloseLibrary(W3DNovaBase); W3DNovaBase = NULL; }
    if (INewlib)     { IExec->DropInterface(INewlib); INewlib = NULL; }
    if (NewlibBase)  { IExec->CloseLibrary(NewlibBase); NewlibBase = NULL; }
}

/****************************************************************************/
/* Library initialization                                                   */
/****************************************************************************/

static struct Library *_lib_Init(struct Library *libBase, BPTR seglist,
                                 struct Interface *exec)
{
    struct OGLES2VKLibBase *base = (struct OGLES2VKLibBase *)libBase;

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

    /* Discover GPU at library init time */
    _discover_gpu();

    IExec->DebugPrintF("[ogles2_vk] Initialized v%ld.%ld (built " __DATE__ " " __TIME__ ")\n",
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

static struct OGLES2VKLibBase *_manager_Open(struct LibraryManagerInterface *Self,
                                          uint32 version)
{
    struct OGLES2VKLibBase *base = (struct OGLES2VKLibBase *)Self->Data.LibBase;

    if (version > LIBVER)
        return NULL;

    base->lib_Lib.lib_OpenCnt++;
    base->lib_Lib.lib_Flags &= ~LIBF_DELEXP;

    return base;
}

static BPTR _manager_Close(struct LibraryManagerInterface *Self)
{
    struct OGLES2VKLibBase *base = (struct OGLES2VKLibBase *)Self->Data.LibBase;

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
    struct OGLES2VKLibBase *base = (struct OGLES2VKLibBase *)Self->Data.LibBase;

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

static uint32 APICALL _main_Obtain(struct OGLES2VKICDIFace *Self)
{
    return ++Self->Data.RefCount;
}

static uint32 APICALL _main_Release(struct OGLES2VKICDIFace *Self)
{
    return Self->Data.RefCount--;
}

/****************************************************************************/
/* Vulkan instance and physical device implementations                      */
/****************************************************************************/

/*--------------------------------------------------------------------------
** vkCreateInstance
**------------------------------------------------------------------------*/
VkResult ogles2vk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkInstance *pInstance)
{
    (void)pAllocator;

    if (!pCreateInfo || !pInstance)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (g_instanceCreated)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pCreateInfo->pApplicationInfo)
        g_instance.apiVersion = pCreateInfo->pApplicationInfo->apiVersion;
    else
        g_instance.apiVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);

    g_instanceCreated = VK_TRUE;
    *pInstance = (VkInstance)&g_instance;

    IExec->DebugPrintF("[ogles2_vk] CreateInstance (API %u.%u.%u)\n",
                       VK_API_VERSION_MAJOR(g_instance.apiVersion),
                       VK_API_VERSION_MINOR(g_instance.apiVersion),
                       VK_API_VERSION_PATCH(g_instance.apiVersion));

    return VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkDestroyInstance
**------------------------------------------------------------------------*/
void ogles2vk_DestroyInstance(VkInstance instance,
                           const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (!instance) return;

    g_instanceCreated = VK_FALSE;

    IExec->DebugPrintF("[ogles2_vk] DestroyInstance\n");
}

/*--------------------------------------------------------------------------
** vkEnumerateInstanceVersion
**------------------------------------------------------------------------*/
VkResult ogles2vk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (!pApiVersion)
        return VK_ERROR_INITIALIZATION_FAILED;

    *pApiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    return VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkEnumerateInstanceExtensionProperties
**------------------------------------------------------------------------*/
VkResult ogles2vk_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                                     uint32_t *pPropertyCount,
                                                     VkExtensionProperties *pProperties)
{
    (void)pLayerName;

    if (!pPropertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* No instance extensions supported */
    if (!pProperties)
    {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    *pPropertyCount = 0;
    return VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkEnumeratePhysicalDevices
**------------------------------------------------------------------------*/
VkResult ogles2vk_EnumeratePhysicalDevices(VkInstance instance,
                                         uint32_t *pPhysicalDeviceCount,
                                         VkPhysicalDevice *pPhysicalDevices)
{
    (void)instance;

    if (!pPhysicalDeviceCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!g_gpuDiscovered)
    {
        *pPhysicalDeviceCount = 0;
        return VK_SUCCESS;
    }

    if (!pPhysicalDevices)
    {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceCount < 1)
    {
        *pPhysicalDeviceCount = 1;
        return VK_INCOMPLETE;
    }

    pPhysicalDevices[0] = (VkPhysicalDevice)&g_physDevice;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkGetPhysicalDeviceProperties
**------------------------------------------------------------------------*/
void ogles2vk_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceProperties *pProperties)
{
    (void)physicalDevice;
    if (pProperties)
        memcpy(pProperties, &g_physDevice.properties, sizeof(VkPhysicalDeviceProperties));
}

/*--------------------------------------------------------------------------
** vkGetPhysicalDeviceFeatures
**------------------------------------------------------------------------*/
void ogles2vk_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                      VkPhysicalDeviceFeatures *pFeatures)
{
    (void)physicalDevice;
    if (pFeatures)
        memcpy(pFeatures, &g_physDevice.features, sizeof(VkPhysicalDeviceFeatures));
}

/*--------------------------------------------------------------------------
** vkGetPhysicalDeviceQueueFamilyProperties
**------------------------------------------------------------------------*/
void ogles2vk_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties)
{
    (void)physicalDevice;

    if (!pQueueFamilyPropertyCount)
        return;

    if (!pQueueFamilyProperties)
    {
        *pQueueFamilyPropertyCount = 1;
        return;
    }

    if (*pQueueFamilyPropertyCount < 1)
    {
        *pQueueFamilyPropertyCount = 1;
        return;
    }

    pQueueFamilyProperties[0] = g_physDevice.queueFamilyProperties;
    *pQueueFamilyPropertyCount = 1;
}

/*--------------------------------------------------------------------------
** vkGetPhysicalDeviceMemoryProperties
**------------------------------------------------------------------------*/
void ogles2vk_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    (void)physicalDevice;
    if (pMemoryProperties)
        memcpy(pMemoryProperties, &g_physDevice.memoryProperties,
               sizeof(VkPhysicalDeviceMemoryProperties));
}

/*--------------------------------------------------------------------------
** vkGetPhysicalDeviceFormatProperties
**------------------------------------------------------------------------*/
void ogles2vk_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties *pFormatProperties)
{
    (void)physicalDevice;

    if (!pFormatProperties)
        return;

    memset(pFormatProperties, 0, sizeof(VkFormatProperties));

    /* Report basic format support for common formats.
    ** Conservative defaults -- actual format capabilities may be
    ** queried from W3D Nova in the future. */
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
        pFormatProperties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        pFormatProperties->linearTilingFeatures =
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        break;

    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
        pFormatProperties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        break;

    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        pFormatProperties->optimalTilingFeatures =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        pFormatProperties->bufferFeatures =
            VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
        break;

    default:
        break;
    }
}

/*--------------------------------------------------------------------------
** vkEnumerateDeviceExtensionProperties
**------------------------------------------------------------------------*/
VkResult ogles2vk_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    (void)physicalDevice;
    (void)pLayerName;

    if (!pPropertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* Report VK_KHR_swapchain */
    static const VkExtensionProperties devExts[] = {
        { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION },
    };
    uint32_t count = sizeof(devExts) / sizeof(devExts[0]);

    if (!pProperties)
    {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pPropertyCount < count ? *pPropertyCount : count;
    memcpy(pProperties, devExts, toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;
    return (toCopy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkDeviceWaitIdle -- stub, always succeeds
**------------------------------------------------------------------------*/
VkResult ogles2vk_DeviceWaitIdle(VkDevice device)
{
    (void)device;
    return VK_SUCCESS;
}

/*--------------------------------------------------------------------------
** vkQueueWaitIdle -- stub, always succeeds
**------------------------------------------------------------------------*/
VkResult ogles2vk_QueueWaitIdle(VkQueue queue)
{
    (void)queue;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Vulkan 1.1/1.2/1.3 wrappers                                              */
/* Thin wrappers that delegate to v1 functions.                             */
/****************************************************************************/

void ogles2vk_GetPhysicalDeviceProperties2(VkPhysicalDevice physDev,
                                         VkPhysicalDeviceProperties2 *pProperties)
{
    ogles2vk_GetPhysicalDeviceProperties(physDev, &pProperties->properties);
}

void ogles2vk_GetPhysicalDeviceFeatures2(VkPhysicalDevice physDev,
                                       VkPhysicalDeviceFeatures2 *pFeatures)
{
    ogles2vk_GetPhysicalDeviceFeatures(physDev, &pFeatures->features);
}

void ogles2vk_GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physDev,
    uint32_t *pCount,
    VkQueueFamilyProperties2 *pProps)
{
    if (!pProps)
    {
        ogles2vk_GetPhysicalDeviceQueueFamilyProperties(physDev, pCount, NULL);
        return;
    }

    uint32_t count = *pCount;
    VkQueueFamilyProperties tempProps;
    uint32_t tempCount = 1;
    ogles2vk_GetPhysicalDeviceQueueFamilyProperties(physDev, &tempCount, &tempProps);

    if (count >= 1 && tempCount >= 1)
    {
        pProps[0].queueFamilyProperties = tempProps;
        *pCount = 1;
    }
    else
    {
        *pCount = 0;
    }
}

void ogles2vk_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physDev,
    VkPhysicalDeviceMemoryProperties2 *pMemProps)
{
    ogles2vk_GetPhysicalDeviceMemoryProperties(physDev, &pMemProps->memoryProperties);
}

void ogles2vk_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physDev,
    VkFormat format,
    VkFormatProperties2 *pFormatProps)
{
    ogles2vk_GetPhysicalDeviceFormatProperties(physDev, format,
        &pFormatProps->formatProperties);
}

VkResult ogles2vk_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physDev,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties *pProps)
{
    (void)physDev;
    (void)type;
    (void)tiling;
    (void)flags;

    if (!pProps)
        return VK_ERROR_INITIALIZATION_FAILED;

    memset(pProps, 0, sizeof(VkImageFormatProperties));

    /* Report basic capabilities for common formats */
    if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB)
    {
        pProps->maxExtent.width  = 4096;
        pProps->maxExtent.height = 4096;
        pProps->maxExtent.depth  = 1;
        pProps->maxMipLevels     = 1;
        pProps->maxArrayLayers   = 1;
        pProps->sampleCounts     = VK_SAMPLE_COUNT_1_BIT;
        pProps->maxResourceSize  = 4096 * 4096 * 4;
        return VK_SUCCESS;
    }

    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        pProps->maxExtent.width  = 4096;
        pProps->maxExtent.height = 4096;
        pProps->maxExtent.depth  = 1;
        pProps->maxMipLevels     = 1;
        pProps->maxArrayLayers   = 1;
        pProps->sampleCounts     = VK_SAMPLE_COUNT_1_BIT;
        pProps->maxResourceSize  = 4096 * 4096 * 4;
        return VK_SUCCESS;
    }

    return VK_ERROR_FORMAT_NOT_SUPPORTED;
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
** Self and forwards arguments to the real ogles2vk_* function. */

/* VkResult-returning trampolines */
#define T_R1(name, t1) \
    static VkResult APICALL _t_##name(void *S, t1 a) { (void)S; return name(a); }
#define T_R2(name, t1, t2) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b) { (void)S; return name(a,b); }
#define T_R3(name, t1, t2, t3) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c) { (void)S; return name(a,b,c); }
#define T_R4(name, t1, t2, t3, t4) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d) { (void)S; return name(a,b,c,d); }

/* void-returning trampolines */
#define T_V1(name, t1) \
    static void APICALL _t_##name(void *S, t1 a) { (void)S; name(a); }
#define T_V2(name, t1, t2) \
    static void APICALL _t_##name(void *S, t1 a, t2 b) { (void)S; name(a,b); }
#define T_V3(name, t1, t2, t3) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c) { (void)S; name(a,b,c); }

/* Additional trampoline macros for multi-argument signatures */
#define T_V4(name, t1, t2, t3, t4) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d) { (void)S; name(a,b,c,d); }
#define T_R5(name, t1, t2, t3, t4, t5) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e) { (void)S; return name(a,b,c,d,e); }
#define T_R6(name, t1, t2, t3, t4, t5, t6) \
    static VkResult APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) { (void)S; return name(a,b,c,d,e,f); }

/* --- Instance --- */
/* NOTE: ogles2vk_CreateInstance, ogles2vk_DestroyInstance, ogles2vk_EnumerateInstanceVersion,
** and ogles2vk_EnumerateInstanceExtensionProperties do NOT get trampolines.
** The loader calls these directly as standard PFN_vk* function pointers,
** not through VulkanIFace. */
T_R3(ogles2vk_EnumeratePhysicalDevices, VkInstance, uint32_t*, VkPhysicalDevice*)

/* --- Physical device queries --- */
T_V2(ogles2vk_GetPhysicalDeviceProperties, VkPhysicalDevice, VkPhysicalDeviceProperties*)
T_V2(ogles2vk_GetPhysicalDeviceFeatures, VkPhysicalDevice, VkPhysicalDeviceFeatures*)
T_V3(ogles2vk_GetPhysicalDeviceQueueFamilyProperties, VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*)
T_V2(ogles2vk_GetPhysicalDeviceMemoryProperties, VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*)
T_V3(ogles2vk_GetPhysicalDeviceFormatProperties, VkPhysicalDevice, VkFormat, VkFormatProperties*)

/* --- Device extension enumeration --- */
T_R4(ogles2vk_EnumerateDeviceExtensionProperties, VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*)

/* --- Device --- */
T_R1(ogles2vk_DeviceWaitIdle, VkDevice)
T_R1(ogles2vk_QueueWaitIdle, VkQueue)
T_R4(ogles2vk_CreateDevice, VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*)
T_V2(ogles2vk_DestroyDevice, VkDevice, const VkAllocationCallbacks*)
T_V4(ogles2vk_GetDeviceQueue, VkDevice, uint32_t, uint32_t, VkQueue*)

/* --- Memory --- */
T_R4(ogles2vk_AllocateMemory, VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*)
T_V3(ogles2vk_FreeMemory, VkDevice, VkDeviceMemory, const VkAllocationCallbacks*)
T_R6(ogles2vk_MapMemory, VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**)
T_V2(ogles2vk_UnmapMemory, VkDevice, VkDeviceMemory)

/* --- Buffer --- */
T_R4(ogles2vk_CreateBuffer, VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*)
T_V3(ogles2vk_DestroyBuffer, VkDevice, VkBuffer, const VkAllocationCallbacks*)
T_V3(ogles2vk_GetBufferMemoryRequirements, VkDevice, VkBuffer, VkMemoryRequirements*)
T_R4(ogles2vk_BindBufferMemory, VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize)

/* --- Image --- */
T_R4(ogles2vk_CreateImage, VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*)
T_V3(ogles2vk_DestroyImage, VkDevice, VkImage, const VkAllocationCallbacks*)
T_V3(ogles2vk_GetImageMemoryRequirements, VkDevice, VkImage, VkMemoryRequirements*)
T_R4(ogles2vk_BindImageMemory, VkDevice, VkImage, VkDeviceMemory, VkDeviceSize)
T_R4(ogles2vk_CreateImageView, VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*)
T_V3(ogles2vk_DestroyImageView, VkDevice, VkImageView, const VkAllocationCallbacks*)

/* --- Sampler --- */
T_R4(ogles2vk_CreateSampler, VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*)
T_V3(ogles2vk_DestroySampler, VkDevice, VkSampler, const VkAllocationCallbacks*)

/* --- Descriptor sets --- */
T_R4(ogles2vk_CreateDescriptorSetLayout, VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
T_V3(ogles2vk_DestroyDescriptorSetLayout, VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*)
T_R4(ogles2vk_CreateDescriptorPool, VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*)
T_V3(ogles2vk_DestroyDescriptorPool, VkDevice, VkDescriptorPool, const VkAllocationCallbacks*)
T_R3(ogles2vk_AllocateDescriptorSets, VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*)
T_R4(ogles2vk_FreeDescriptorSets, VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*)
T_R3(ogles2vk_ResetDescriptorPool, VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags)
/* T_V5 for UpdateDescriptorSets is below (needs T_V5 macro) */

/* --- Shader module --- */
T_R4(ogles2vk_CreateShaderModule, VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*)
T_V3(ogles2vk_DestroyShaderModule, VkDevice, VkShaderModule, const VkAllocationCallbacks*)

/* --- Pipeline layout --- */
T_R4(ogles2vk_CreatePipelineLayout, VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*)
T_V3(ogles2vk_DestroyPipelineLayout, VkDevice, VkPipelineLayout, const VkAllocationCallbacks*)

/* --- Pipeline cache --- */
T_R4(ogles2vk_CreatePipelineCache, VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*)
T_V3(ogles2vk_DestroyPipelineCache, VkDevice, VkPipelineCache, const VkAllocationCallbacks*)

/* --- Graphics pipeline --- */
T_R6(ogles2vk_CreateGraphicsPipelines, VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*)
T_V3(ogles2vk_DestroyPipeline, VkDevice, VkPipeline, const VkAllocationCallbacks*)

/* --- Command pool/buffer --- */
T_R4(ogles2vk_CreateCommandPool, VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*)
T_V3(ogles2vk_DestroyCommandPool, VkDevice, VkCommandPool, const VkAllocationCallbacks*)
T_R3(ogles2vk_AllocateCommandBuffers, VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*)
T_V4(ogles2vk_FreeCommandBuffers, VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*)
T_R2(ogles2vk_BeginCommandBuffer, VkCommandBuffer, const VkCommandBufferBeginInfo*)
T_R1(ogles2vk_EndCommandBuffer, VkCommandBuffer)
T_R2(ogles2vk_ResetCommandBuffer, VkCommandBuffer, VkCommandBufferResetFlags)

/* Additional trampoline macros for complex signatures */
#define T_V5(name, t1, t2, t3, t4, t5) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e) { (void)S; name(a,b,c,d,e); }
#define T_V6(name, t1, t2, t3, t4, t5, t6) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f) { (void)S; name(a,b,c,d,e,f); }
#define T_V7(name, t1, t2, t3, t4, t5, t6, t7) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f, t7 g) { (void)S; name(a,b,c,d,e,f,g); }
#define T_V8(name, t1, t2, t3, t4, t5, t6, t7, t8) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f, t7 g, t8 h) { (void)S; name(a,b,c,d,e,f,g,h); }
#define T_V10(name, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10) \
    static void APICALL _t_##name(void *S, t1 a, t2 b, t3 c, t4 d, t5 e, t6 f, t7 g, t8 h, t9 i, t10 j) { (void)S; name(a,b,c,d,e,f,g,h,i,j); }

/* --- Descriptor sets (continued -- needs T_V5) --- */
T_V5(ogles2vk_UpdateDescriptorSets, VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*)

/* --- Command recording --- */
T_V3(ogles2vk_CmdBindPipeline, VkCommandBuffer, VkPipelineBindPoint, VkPipeline)
T_V4(ogles2vk_CmdSetViewport, VkCommandBuffer, uint32_t, uint32_t, const VkViewport*)
T_V4(ogles2vk_CmdSetScissor, VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*)
T_V5(ogles2vk_CmdDraw, VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t)
T_V6(ogles2vk_CmdDrawIndexed, VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
T_V2(ogles2vk_CmdBeginRendering, VkCommandBuffer, const VkRenderingInfo*)
T_V1(ogles2vk_CmdEndRendering, VkCommandBuffer)
T_V6(ogles2vk_CmdPushConstants, VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*)
T_V5(ogles2vk_CmdBindVertexBuffers, VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*)
T_V4(ogles2vk_CmdBindIndexBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType)
T_V8(ogles2vk_CmdBindDescriptorSets, VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*)
T_V3(ogles2vk_CmdBeginRenderPass, VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents)
T_V1(ogles2vk_CmdEndRenderPass, VkCommandBuffer)
T_V2(ogles2vk_CmdNextSubpass, VkCommandBuffer, VkSubpassContents)
T_V10(ogles2vk_CmdPipelineBarrier, VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*)
T_V2(ogles2vk_CmdPipelineBarrier2, VkCommandBuffer, const void*)

/* --- Dynamic state --- */
T_V2(ogles2vk_CmdSetCullMode, VkCommandBuffer, VkCullModeFlags)
T_V2(ogles2vk_CmdSetFrontFace, VkCommandBuffer, VkFrontFace)
T_V2(ogles2vk_CmdSetPrimitiveTopology, VkCommandBuffer, VkPrimitiveTopology)
T_V3(ogles2vk_CmdSetViewportWithCount, VkCommandBuffer, uint32_t, const VkViewport*)
T_V3(ogles2vk_CmdSetScissorWithCount, VkCommandBuffer, uint32_t, const VkRect2D*)
T_V7(ogles2vk_CmdBindVertexBuffers2, VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*, const VkDeviceSize*, const VkDeviceSize*)
T_V2(ogles2vk_CmdSetDepthTestEnable, VkCommandBuffer, VkBool32)
T_V2(ogles2vk_CmdSetDepthWriteEnable, VkCommandBuffer, VkBool32)
T_V2(ogles2vk_CmdSetDepthCompareOp, VkCommandBuffer, VkCompareOp)
T_V2(ogles2vk_CmdSetDepthBoundsTestEnable, VkCommandBuffer, VkBool32)
T_V2(ogles2vk_CmdSetStencilTestEnable, VkCommandBuffer, VkBool32)
T_V6(ogles2vk_CmdSetStencilOp, VkCommandBuffer, VkStencilFaceFlags, VkStencilOp, VkStencilOp, VkStencilOp, VkCompareOp)
T_V2(ogles2vk_CmdSetRasterizerDiscardEnable, VkCommandBuffer, VkBool32)
T_V2(ogles2vk_CmdSetDepthBiasEnable, VkCommandBuffer, VkBool32)
T_V2(ogles2vk_CmdSetPrimitiveRestartEnable, VkCommandBuffer, VkBool32)

/* --- Transfer commands --- */
T_V5(ogles2vk_CmdCopyBuffer, VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*)
T_V6(ogles2vk_CmdCopyBufferToImage, VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*)
T_V6(ogles2vk_CmdCopyImageToBuffer, VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy*)
T_V7(ogles2vk_CmdCopyImage, VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy*)
T_V5(ogles2vk_CmdFillBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t)
T_V5(ogles2vk_CmdUpdateBuffer, VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void*)

/* --- Sync --- */
T_R4(ogles2vk_CreateFence, VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*)
T_V3(ogles2vk_DestroyFence, VkDevice, VkFence, const VkAllocationCallbacks*)
T_R5(ogles2vk_WaitForFences, VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t)
T_R3(ogles2vk_ResetFences, VkDevice, uint32_t, const VkFence*)
T_R2(ogles2vk_GetFenceStatus, VkDevice, VkFence)
T_R4(ogles2vk_CreateSemaphore, VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*)
T_V3(ogles2vk_DestroySemaphore, VkDevice, VkSemaphore, const VkAllocationCallbacks*)

/* --- Queue submit --- */
T_R4(ogles2vk_QueueSubmit, VkQueue, uint32_t, const VkSubmitInfo*, VkFence)
T_R4(ogles2vk_QueueSubmit2, VkQueue, uint32_t, const VkSubmitInfo2*, VkFence)

/* --- Stubs --- */
T_R3(ogles2vk_FlushMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*)
T_R3(ogles2vk_InvalidateMappedMemoryRanges, VkDevice, uint32_t, const VkMappedMemoryRange*)
T_V3(ogles2vk_ResetCommandPool, VkDevice, VkCommandPool, VkCommandPoolResetFlags)
T_V3(ogles2vk_TrimCommandPool, VkDevice, VkCommandPool, uint32_t)

/* --- Vulkan 1.1/1.2/1.3 wrappers --- */
T_V2(ogles2vk_GetPhysicalDeviceProperties2, VkPhysicalDevice, VkPhysicalDeviceProperties2*)
T_V2(ogles2vk_GetPhysicalDeviceFeatures2, VkPhysicalDevice, VkPhysicalDeviceFeatures2*)
T_V3(ogles2vk_GetPhysicalDeviceQueueFamilyProperties2, VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties2*)
T_V2(ogles2vk_GetPhysicalDeviceMemoryProperties2, VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*)
T_V3(ogles2vk_GetPhysicalDeviceFormatProperties2, VkPhysicalDevice, VkFormat, VkFormatProperties2*)
T_V3(ogles2vk_GetBufferMemoryRequirements2, VkDevice, const VkBufferMemoryRequirementsInfo2*, VkMemoryRequirements2*)
T_V3(ogles2vk_GetImageMemoryRequirements2, VkDevice, const VkImageMemoryRequirementsInfo2*, VkMemoryRequirements2*)
T_R3(ogles2vk_BindBufferMemory2, VkDevice, uint32_t, const VkBindBufferMemoryInfo*)
T_R3(ogles2vk_BindImageMemory2, VkDevice, uint32_t, const VkBindImageMemoryInfo*)

/* vkGetPhysicalDeviceImageFormatProperties has 7 visible args -- custom trampoline */
static VkResult APICALL _t_ogles2vk_GetPhysicalDeviceImageFormatProperties(void *S,
    VkPhysicalDevice a, VkFormat b, VkImageType c, VkImageTiling d,
    VkImageUsageFlags e, VkImageCreateFlags f, VkImageFormatProperties *g)
{ (void)S; return ogles2vk_GetPhysicalDeviceImageFormatProperties(a,b,c,d,e,f,g); }

/* --- WSI --- */
T_R4(ogles2vk_GetPhysicalDeviceSurfaceSupportKHR, VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*)
T_R3(ogles2vk_GetPhysicalDeviceSurfaceCapabilitiesKHR, VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*)
T_R4(ogles2vk_GetPhysicalDeviceSurfaceFormatsKHR, VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*)
T_R4(ogles2vk_GetPhysicalDeviceSurfacePresentModesKHR, VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*)
T_R4(ogles2vk_CreateSwapchainKHR, VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*)
T_V3(ogles2vk_DestroySwapchainKHR, VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*)
T_R4(ogles2vk_GetSwapchainImagesKHR, VkDevice, VkSwapchainKHR, uint32_t*, VkImage*)
T_R6(ogles2vk_AcquireNextImageKHR, VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*)
T_R2(ogles2vk_QueuePresentKHR, VkQueue, const VkPresentInfoKHR*)

/****************************************************************************/
/* 100% API coverage: Instance-level stub implementations                   */
/****************************************************************************/

void ogles2vk_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice pd,
    const VkPhysicalDeviceExternalBufferInfo *pInfo, VkExternalBufferProperties *pProps)
{ (void)pd; (void)pInfo; if (pProps) memset(&pProps->externalMemoryProperties, 0, sizeof(VkExternalMemoryProperties)); }

void ogles2vk_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice pd,
    const VkPhysicalDeviceExternalFenceInfo *pInfo, VkExternalFenceProperties *pProps)
{ (void)pd; (void)pInfo; if (pProps) { pProps->exportFromImportedHandleTypes=0; pProps->compatibleHandleTypes=0; pProps->externalFenceFeatures=0; } }

void ogles2vk_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice pd,
    const VkPhysicalDeviceExternalSemaphoreInfo *pInfo, VkExternalSemaphoreProperties *pProps)
{ (void)pd; (void)pInfo; if (pProps) { pProps->exportFromImportedHandleTypes=0; pProps->compatibleHandleTypes=0; pProps->externalSemaphoreFeatures=0; } }

VkResult ogles2vk_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pCount, VkPhysicalDeviceGroupProperties *pProps)
{
    (void)instance;
    if (!pProps) { *pCount = 1; return VK_SUCCESS; }
    if (*pCount >= 1) {
        memset(&pProps[0], 0, sizeof(VkPhysicalDeviceGroupProperties));
        pProps[0].sType = (VkStructureType)1000070000;
        pProps[0].physicalDeviceCount = 1;
        pProps[0].physicalDevices[0] = (VkPhysicalDevice)&g_physDevice;
        pProps[0].subsetAllocation = VK_FALSE;
        *pCount = 1; return VK_SUCCESS;
    }
    *pCount = 0; return VK_INCOMPLETE;
}

VkResult ogles2vk_GetPhysicalDeviceToolProperties(VkPhysicalDevice pd, uint32_t *pCount, void *pProps)
{ (void)pd; (void)pProps; if (pCount) *pCount = 0; return VK_SUCCESS; }

VkResult ogles2vk_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice pd,
    const VkPhysicalDeviceImageFormatInfo2 *pInfo, VkImageFormatProperties2 *pProps)
{ return ogles2vk_GetPhysicalDeviceImageFormatProperties(pd, pInfo->format, pInfo->type, pInfo->tiling, pInfo->usage, pInfo->flags, &pProps->imageFormatProperties); }

void ogles2vk_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity)
{ (void)device; (void)renderPass; if (pGranularity) { pGranularity->width=1; pGranularity->height=1; } }

void ogles2vk_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pd, VkFormat fmt, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t *pCount, VkSparseImageFormatProperties *pProps)
{ (void)pd; (void)fmt; (void)type; (void)samples; (void)usage; (void)tiling; (void)pProps; if (pCount) *pCount = 0; }

void ogles2vk_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice pd,
    const VkPhysicalDeviceSparseImageFormatInfo2 *pInfo, uint32_t *pCount, VkSparseImageFormatProperties2 *pProps)
{ (void)pd; (void)pInfo; (void)pProps; if (pCount) *pCount = 0; }

/****************************************************************************/
/* 100% API coverage: GetDeviceProcAddr                                      */
/****************************************************************************/

static PFN_vkVoidFunction ogles2vk_LookupProcAddr(const char *pName);

static PFN_vkVoidFunction ogles2vk_LookupRawProcAddr(const char *pName);

PFN_vkVoidFunction ogles2vk_GetDeviceProcAddr(VkDevice device, const char *pName)
{ (void)device; if (!pName) return NULL; return ogles2vk_LookupRawProcAddr(pName); }

static PFN_vkVoidFunction APICALL _t_ogles2vk_GetDeviceProcAddr(void *S, VkDevice a, const char *b)
{ (void)S; return ogles2vk_GetDeviceProcAddr(a,b); }

/****************************************************************************/
/* 100% API coverage: New trampolines                                        */
/****************************************************************************/

T_V3(ogles2vk_GetDeviceMemoryCommitment, VkDevice, VkDeviceMemory, VkDeviceSize*)
T_V4(ogles2vk_GetImageSubresourceLayout, VkDevice, VkImage, const VkImageSubresource*, VkSubresourceLayout*)
T_V3(ogles2vk_GetRenderAreaGranularity, VkDevice, VkRenderPass, VkExtent2D*)
T_V3(ogles2vk_GetDeviceQueue2, VkDevice, const VkDeviceQueueInfo2*, VkQueue*)
T_V3(ogles2vk_GetDescriptorSetLayoutSupport, VkDevice, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayoutSupport*)
T_R3(ogles2vk_GetPhysicalDeviceToolProperties, VkPhysicalDevice, uint32_t*, void*)
T_R3(ogles2vk_GetPhysicalDeviceImageFormatProperties2, VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2*, VkImageFormatProperties2*)
T_V3(ogles2vk_GetPhysicalDeviceExternalBufferProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo*, VkExternalBufferProperties*)
T_V3(ogles2vk_GetPhysicalDeviceExternalFenceProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo*, VkExternalFenceProperties*)
T_V3(ogles2vk_GetPhysicalDeviceExternalSemaphoreProperties, VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo*, VkExternalSemaphoreProperties*)
T_R3(ogles2vk_EnumeratePhysicalDeviceGroups, VkInstance, uint32_t*, VkPhysicalDeviceGroupProperties*)
T_R6(ogles2vk_CreateComputePipelines, VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*)
T_R4(ogles2vk_CreateBufferView, VkDevice, const VkBufferViewCreateInfo*, const VkAllocationCallbacks*, VkBufferView*)
T_V3(ogles2vk_DestroyBufferView, VkDevice, VkBufferView, const VkAllocationCallbacks*)
T_R4(ogles2vk_CreateSamplerYcbcrConversion, VkDevice, const VkSamplerYcbcrConversionCreateInfo*, const VkAllocationCallbacks*, VkSamplerYcbcrConversion*)
T_V3(ogles2vk_DestroySamplerYcbcrConversion, VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks*)
T_R4(ogles2vk_CreateDescriptorUpdateTemplate, VkDevice, const VkDescriptorUpdateTemplateCreateInfo*, const VkAllocationCallbacks*, VkDescriptorUpdateTemplate*)
T_V3(ogles2vk_DestroyDescriptorUpdateTemplate, VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks*)
T_V4(ogles2vk_UpdateDescriptorSetWithTemplate, VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void*)
T_R4(ogles2vk_CreatePrivateDataSlot, VkDevice, const VkPrivateDataSlotCreateInfo*, const VkAllocationCallbacks*, VkPrivateDataSlot*)
T_V3(ogles2vk_DestroyPrivateDataSlot, VkDevice, VkPrivateDataSlot, const VkAllocationCallbacks*)
T_V5(ogles2vk_GetPrivateData, VkDevice, VkObjectType, uint64_t, VkPrivateDataSlot, uint64_t*)
T_R3(ogles2vk_GetSemaphoreCounterValue, VkDevice, VkSemaphore, uint64_t*)
T_R3(ogles2vk_WaitSemaphores, VkDevice, const VkSemaphoreWaitInfo*, uint64_t)
T_R2(ogles2vk_SignalSemaphore, VkDevice, const VkSemaphoreSignalInfo*)
T_R4(ogles2vk_GetPipelineCacheData, VkDevice, VkPipelineCache, size_t*, void*)
T_R4(ogles2vk_MergePipelineCaches, VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*)
T_V4(ogles2vk_GetImageSparseMemoryRequirements, VkDevice, VkImage, uint32_t*, VkSparseImageMemoryRequirements*)
T_V4(ogles2vk_GetImageSparseMemoryRequirements2, VkDevice, const VkImageSparseMemoryRequirementsInfo2*, uint32_t*, VkSparseImageMemoryRequirements2*)
T_V4(ogles2vk_GetPhysicalDeviceSparseImageFormatProperties2, VkPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2*, uint32_t*, VkSparseImageFormatProperties2*)
T_R4(ogles2vk_CreateEvent, VkDevice, const VkEventCreateInfo*, const VkAllocationCallbacks*, VkEvent*)
T_V3(ogles2vk_DestroyEvent, VkDevice, VkEvent, const VkAllocationCallbacks*)
T_R2(ogles2vk_GetEventStatus, VkDevice, VkEvent)
T_R2(ogles2vk_SetEvent, VkDevice, VkEvent)
T_R2(ogles2vk_ResetEvent, VkDevice, VkEvent)
T_R4(ogles2vk_CreateQueryPool, VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool*)
T_V3(ogles2vk_DestroyQueryPool, VkDevice, VkQueryPool, const VkAllocationCallbacks*)
T_V4(ogles2vk_ResetQueryPool, VkDevice, VkQueryPool, uint32_t, uint32_t)
T_R4(ogles2vk_QueueBindSparse, VkQueue, uint32_t, const VkBindSparseInfo*, VkFence)
T_R4(ogles2vk_CreateRenderPass, VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
T_V3(ogles2vk_DestroyRenderPass, VkDevice, VkRenderPass, const VkAllocationCallbacks*)
T_R4(ogles2vk_CreateFramebuffer, VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*)
T_V3(ogles2vk_DestroyFramebuffer, VkDevice, VkFramebuffer, const VkAllocationCallbacks*)
T_R4(ogles2vk_CreateRenderPass2, VkDevice, const VkRenderPassCreateInfo2*, const VkAllocationCallbacks*, VkRenderPass*)
T_V3(ogles2vk_CmdBeginRenderPass2, VkCommandBuffer, const VkRenderPassBeginInfo*, const VkSubpassBeginInfo*)
T_V3(ogles2vk_CmdNextSubpass2, VkCommandBuffer, const VkSubpassBeginInfo*, const VkSubpassEndInfo*)
T_V2(ogles2vk_CmdEndRenderPass2, VkCommandBuffer, const VkSubpassEndInfo*)
T_V2(ogles2vk_CmdSetLineWidth, VkCommandBuffer, float)
T_V4(ogles2vk_CmdSetDepthBias, VkCommandBuffer, float, float, float)
T_V2(ogles2vk_CmdSetBlendConstants, VkCommandBuffer, const float*)
T_V3(ogles2vk_CmdSetDepthBounds, VkCommandBuffer, float, float)
T_V3(ogles2vk_CmdSetStencilCompareMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
T_V3(ogles2vk_CmdSetStencilWriteMask, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
T_V3(ogles2vk_CmdSetStencilReference, VkCommandBuffer, VkStencilFaceFlags, uint32_t)
T_V2(ogles2vk_CmdCopyBuffer2, VkCommandBuffer, const VkCopyBufferInfo2*)
T_V2(ogles2vk_CmdCopyImage2, VkCommandBuffer, const VkCopyImageInfo2*)
T_V2(ogles2vk_CmdCopyBufferToImage2, VkCommandBuffer, const VkCopyBufferToImageInfo2*)
T_V2(ogles2vk_CmdCopyImageToBuffer2, VkCommandBuffer, const VkCopyImageToBufferInfo2*)
T_V2(ogles2vk_CmdBlitImage2, VkCommandBuffer, const VkBlitImageInfo2*)
T_V2(ogles2vk_CmdResolveImage2, VkCommandBuffer, const VkResolveImageInfo2*)
T_V2(ogles2vk_CmdSetDeviceMask, VkCommandBuffer, uint32_t)
T_V4(ogles2vk_CmdDispatch, VkCommandBuffer, uint32_t, uint32_t, uint32_t)
T_V3(ogles2vk_CmdDispatchIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize)
T_V3(ogles2vk_CmdExecuteCommands, VkCommandBuffer, uint32_t, const VkCommandBuffer*)
T_V3(ogles2vk_CmdSetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags)
T_V3(ogles2vk_CmdResetEvent, VkCommandBuffer, VkEvent, VkPipelineStageFlags)
T_V3(ogles2vk_CmdSetEvent2, VkCommandBuffer, VkEvent, const VkDependencyInfo*)
T_V3(ogles2vk_CmdResetEvent2, VkCommandBuffer, VkEvent, VkPipelineStageFlags2)
T_V4(ogles2vk_CmdWaitEvents2, VkCommandBuffer, uint32_t, const VkEvent*, const VkDependencyInfo*)
T_V4(ogles2vk_CmdBeginQuery, VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags)
T_V3(ogles2vk_CmdEndQuery, VkCommandBuffer, VkQueryPool, uint32_t)
T_V4(ogles2vk_CmdResetQueryPool, VkCommandBuffer, VkQueryPool, uint32_t, uint32_t)
T_V4(ogles2vk_CmdWriteTimestamp, VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t)
T_V4(ogles2vk_CmdWriteTimestamp2, VkCommandBuffer, VkPipelineStageFlags2, VkQueryPool, uint32_t)
T_V5(ogles2vk_CmdDrawIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t)
T_V5(ogles2vk_CmdDrawIndexedIndirect, VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t)

/* Custom trampolines for functions with 5+ visible args */
static VkResult APICALL _t_ogles2vk_SetPrivateData(void *S, VkDevice a, VkObjectType b, uint64_t c, VkPrivateDataSlot d, uint64_t e)
{ (void)S; return ogles2vk_SetPrivateData(a,b,c,d,e); }
static VkDeviceAddress APICALL _t_ogles2vk_GetBufferDeviceAddress(void *S, VkDevice a, const VkBufferDeviceAddressInfo *b)
{ (void)S; return ogles2vk_GetBufferDeviceAddress(a,b); }
static uint64_t APICALL _t_ogles2vk_GetBufferOpaqueCaptureAddress(void *S, VkDevice a, const VkBufferDeviceAddressInfo *b)
{ (void)S; return ogles2vk_GetBufferOpaqueCaptureAddress(a,b); }
static uint64_t APICALL _t_ogles2vk_GetDeviceMemoryOpaqueCaptureAddress(void *S, VkDevice a, const VkDeviceMemoryOpaqueCaptureAddressInfo *b)
{ (void)S; return ogles2vk_GetDeviceMemoryOpaqueCaptureAddress(a,b); }
static void APICALL _t_ogles2vk_GetPhysicalDeviceSparseImageFormatProperties(void *S,
    VkPhysicalDevice a, VkFormat b, VkImageType c, VkSampleCountFlagBits d,
    VkImageUsageFlags e, VkImageTiling f, uint32_t *g, VkSparseImageFormatProperties *h)
{ (void)S; ogles2vk_GetPhysicalDeviceSparseImageFormatProperties(a,b,c,d,e,f,g,h); }
static VkResult APICALL _t_ogles2vk_GetQueryPoolResults(void *S, VkDevice a,
    VkQueryPool b, uint32_t c, uint32_t d, size_t e, void *f, VkDeviceSize g, VkQueryResultFlags h)
{ (void)S; return ogles2vk_GetQueryPoolResults(a,b,c,d,e,f,g,h); }
static void APICALL _t_ogles2vk_CmdClearColorImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, const VkClearColorValue *d, uint32_t e, const VkImageSubresourceRange *f)
{ (void)S; ogles2vk_CmdClearColorImage(a,b,c,d,e,f); }
static void APICALL _t_ogles2vk_CmdClearDepthStencilImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, const VkClearDepthStencilValue *d, uint32_t e, const VkImageSubresourceRange *f)
{ (void)S; ogles2vk_CmdClearDepthStencilImage(a,b,c,d,e,f); }
static void APICALL _t_ogles2vk_CmdClearAttachments(void *S, VkCommandBuffer a,
    uint32_t b, const VkClearAttachment *c, uint32_t d, const VkClearRect *e)
{ (void)S; ogles2vk_CmdClearAttachments(a,b,c,d,e); }
static void APICALL _t_ogles2vk_CmdBlitImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageBlit *g, VkFilter h)
{ (void)S; ogles2vk_CmdBlitImage(a,b,c,d,e,f,g,h); }
static void APICALL _t_ogles2vk_CmdResolveImage(void *S, VkCommandBuffer a,
    VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageResolve *g)
{ (void)S; ogles2vk_CmdResolveImage(a,b,c,d,e,f,g); }
static void APICALL _t_ogles2vk_CmdDispatchBase(void *S, VkCommandBuffer a,
    uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{ (void)S; ogles2vk_CmdDispatchBase(a,b,c,d,e,f,g); }
static void APICALL _t_ogles2vk_CmdWaitEvents(void *S, VkCommandBuffer a,
    uint32_t b, const VkEvent *c, VkPipelineStageFlags d, VkPipelineStageFlags e,
    uint32_t f, const void *g, uint32_t h, const void *ii, uint32_t j, const void *k)
{ (void)S; ogles2vk_CmdWaitEvents(a,b,c,d,e,f,g,h,ii,j,k); }
static void APICALL _t_ogles2vk_CmdCopyQueryPoolResults(void *S, VkCommandBuffer a,
    VkQueryPool b, uint32_t c, uint32_t d, VkBuffer e, VkDeviceSize f, VkDeviceSize g, VkQueryResultFlags h)
{ (void)S; ogles2vk_CmdCopyQueryPoolResults(a,b,c,d,e,f,g,h); }
static void APICALL _t_ogles2vk_CmdDrawIndirectCount(void *S, VkCommandBuffer a,
    VkBuffer b, VkDeviceSize c, VkBuffer d, VkDeviceSize e, uint32_t f, uint32_t g)
{ (void)S; ogles2vk_CmdDrawIndirectCount(a,b,c,d,e,f,g); }
static void APICALL _t_ogles2vk_CmdDrawIndexedIndirectCount(void *S, VkCommandBuffer a,
    VkBuffer b, VkDeviceSize c, VkBuffer d, VkDeviceSize e, uint32_t f, uint32_t g)
{ (void)S; ogles2vk_CmdDrawIndexedIndirectCount(a,b,c,d,e,f,g); }

/****************************************************************************/
/* Shared DISPATCH lookup                                                    */
/****************************************************************************/

static PFN_vkVoidFunction ogles2vk_LookupProcAddr(const char *pName)
{
    #define DISPATCH(vkName, tramp) \
        if (strcmp(pName, #vkName) == 0) return (PFN_vkVoidFunction)(tramp)

    /* Global functions -- called directly by loader, NO trampoline */
    if (strcmp(pName, "vkCreateInstance") == 0)
        return (PFN_vkVoidFunction)ogles2vk_CreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0)
        return (PFN_vkVoidFunction)ogles2vk_DestroyInstance;
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0)
        return (PFN_vkVoidFunction)ogles2vk_EnumerateInstanceVersion;
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return (PFN_vkVoidFunction)ogles2vk_EnumerateInstanceExtensionProperties;

    /* Physical device enumeration */
    DISPATCH(vkEnumeratePhysicalDevices, _t_ogles2vk_EnumeratePhysicalDevices);

    /* Physical device queries */
    DISPATCH(vkGetPhysicalDeviceProperties, _t_ogles2vk_GetPhysicalDeviceProperties);
    DISPATCH(vkGetPhysicalDeviceFeatures, _t_ogles2vk_GetPhysicalDeviceFeatures);
    DISPATCH(vkGetPhysicalDeviceQueueFamilyProperties, _t_ogles2vk_GetPhysicalDeviceQueueFamilyProperties);
    DISPATCH(vkGetPhysicalDeviceMemoryProperties, _t_ogles2vk_GetPhysicalDeviceMemoryProperties);
    DISPATCH(vkGetPhysicalDeviceFormatProperties, _t_ogles2vk_GetPhysicalDeviceFormatProperties);

    /* Device extension enumeration */
    DISPATCH(vkEnumerateDeviceExtensionProperties, _t_ogles2vk_EnumerateDeviceExtensionProperties);

    /* Device */
    DISPATCH(vkCreateDevice, _t_ogles2vk_CreateDevice);
    DISPATCH(vkDestroyDevice, _t_ogles2vk_DestroyDevice);
    DISPATCH(vkGetDeviceQueue, _t_ogles2vk_GetDeviceQueue);
    DISPATCH(vkDeviceWaitIdle, _t_ogles2vk_DeviceWaitIdle);
    DISPATCH(vkQueueWaitIdle, _t_ogles2vk_QueueWaitIdle);

    /* Memory */
    DISPATCH(vkAllocateMemory, _t_ogles2vk_AllocateMemory);
    DISPATCH(vkFreeMemory, _t_ogles2vk_FreeMemory);
    DISPATCH(vkMapMemory, _t_ogles2vk_MapMemory);
    DISPATCH(vkUnmapMemory, _t_ogles2vk_UnmapMemory);

    /* Buffer */
    DISPATCH(vkCreateBuffer, _t_ogles2vk_CreateBuffer);
    DISPATCH(vkDestroyBuffer, _t_ogles2vk_DestroyBuffer);
    DISPATCH(vkGetBufferMemoryRequirements, _t_ogles2vk_GetBufferMemoryRequirements);
    DISPATCH(vkBindBufferMemory, _t_ogles2vk_BindBufferMemory);

    /* Image */
    DISPATCH(vkCreateImage, _t_ogles2vk_CreateImage);
    DISPATCH(vkDestroyImage, _t_ogles2vk_DestroyImage);
    DISPATCH(vkGetImageMemoryRequirements, _t_ogles2vk_GetImageMemoryRequirements);
    DISPATCH(vkBindImageMemory, _t_ogles2vk_BindImageMemory);
    DISPATCH(vkCreateImageView, _t_ogles2vk_CreateImageView);
    DISPATCH(vkDestroyImageView, _t_ogles2vk_DestroyImageView);

    /* Sampler */
    DISPATCH(vkCreateSampler, _t_ogles2vk_CreateSampler);
    DISPATCH(vkDestroySampler, _t_ogles2vk_DestroySampler);

    /* Descriptor sets */
    DISPATCH(vkCreateDescriptorSetLayout, _t_ogles2vk_CreateDescriptorSetLayout);
    DISPATCH(vkDestroyDescriptorSetLayout, _t_ogles2vk_DestroyDescriptorSetLayout);
    DISPATCH(vkCreateDescriptorPool, _t_ogles2vk_CreateDescriptorPool);
    DISPATCH(vkDestroyDescriptorPool, _t_ogles2vk_DestroyDescriptorPool);
    DISPATCH(vkAllocateDescriptorSets, _t_ogles2vk_AllocateDescriptorSets);
    DISPATCH(vkFreeDescriptorSets, _t_ogles2vk_FreeDescriptorSets);
    DISPATCH(vkResetDescriptorPool, _t_ogles2vk_ResetDescriptorPool);
    DISPATCH(vkUpdateDescriptorSets, _t_ogles2vk_UpdateDescriptorSets);

    /* Shader module */
    DISPATCH(vkCreateShaderModule, _t_ogles2vk_CreateShaderModule);
    DISPATCH(vkDestroyShaderModule, _t_ogles2vk_DestroyShaderModule);

    /* Pipeline layout */
    DISPATCH(vkCreatePipelineLayout, _t_ogles2vk_CreatePipelineLayout);
    DISPATCH(vkDestroyPipelineLayout, _t_ogles2vk_DestroyPipelineLayout);

    /* Pipeline cache */
    DISPATCH(vkCreatePipelineCache, _t_ogles2vk_CreatePipelineCache);
    DISPATCH(vkDestroyPipelineCache, _t_ogles2vk_DestroyPipelineCache);

    /* Graphics pipeline */
    DISPATCH(vkCreateGraphicsPipelines, _t_ogles2vk_CreateGraphicsPipelines);
    DISPATCH(vkDestroyPipeline, _t_ogles2vk_DestroyPipeline);

    /* Command pool/buffer */
    DISPATCH(vkCreateCommandPool, _t_ogles2vk_CreateCommandPool);
    DISPATCH(vkDestroyCommandPool, _t_ogles2vk_DestroyCommandPool);
    DISPATCH(vkAllocateCommandBuffers, _t_ogles2vk_AllocateCommandBuffers);
    DISPATCH(vkFreeCommandBuffers, _t_ogles2vk_FreeCommandBuffers);
    DISPATCH(vkBeginCommandBuffer, _t_ogles2vk_BeginCommandBuffer);
    DISPATCH(vkEndCommandBuffer, _t_ogles2vk_EndCommandBuffer);
    DISPATCH(vkResetCommandBuffer, _t_ogles2vk_ResetCommandBuffer);
    DISPATCH(vkResetCommandPool, _t_ogles2vk_ResetCommandPool);
    DISPATCH(vkTrimCommandPool, _t_ogles2vk_TrimCommandPool);

    /* Command recording */
    DISPATCH(vkCmdBindPipeline, _t_ogles2vk_CmdBindPipeline);
    DISPATCH(vkCmdSetViewport, _t_ogles2vk_CmdSetViewport);
    DISPATCH(vkCmdSetScissor, _t_ogles2vk_CmdSetScissor);
    DISPATCH(vkCmdDraw, _t_ogles2vk_CmdDraw);
    DISPATCH(vkCmdDrawIndexed, _t_ogles2vk_CmdDrawIndexed);
    DISPATCH(vkCmdBeginRendering, _t_ogles2vk_CmdBeginRendering);
    DISPATCH(vkCmdEndRendering, _t_ogles2vk_CmdEndRendering);
    DISPATCH(vkCmdPushConstants, _t_ogles2vk_CmdPushConstants);
    DISPATCH(vkCmdBindVertexBuffers, _t_ogles2vk_CmdBindVertexBuffers);
    DISPATCH(vkCmdBindIndexBuffer, _t_ogles2vk_CmdBindIndexBuffer);
    DISPATCH(vkCmdBindDescriptorSets, _t_ogles2vk_CmdBindDescriptorSets);
    DISPATCH(vkCmdBeginRenderPass, _t_ogles2vk_CmdBeginRenderPass);
    DISPATCH(vkCmdEndRenderPass, _t_ogles2vk_CmdEndRenderPass);
    DISPATCH(vkCmdNextSubpass, _t_ogles2vk_CmdNextSubpass);
    DISPATCH(vkCmdPipelineBarrier, _t_ogles2vk_CmdPipelineBarrier);
    DISPATCH(vkCmdPipelineBarrier2, _t_ogles2vk_CmdPipelineBarrier2);

    /* Dynamic state */
    DISPATCH(vkCmdSetCullMode, _t_ogles2vk_CmdSetCullMode);
    DISPATCH(vkCmdSetFrontFace, _t_ogles2vk_CmdSetFrontFace);
    DISPATCH(vkCmdSetPrimitiveTopology, _t_ogles2vk_CmdSetPrimitiveTopology);
    DISPATCH(vkCmdSetViewportWithCount, _t_ogles2vk_CmdSetViewportWithCount);
    DISPATCH(vkCmdSetScissorWithCount, _t_ogles2vk_CmdSetScissorWithCount);
    DISPATCH(vkCmdBindVertexBuffers2, _t_ogles2vk_CmdBindVertexBuffers2);
    DISPATCH(vkCmdSetDepthTestEnable, _t_ogles2vk_CmdSetDepthTestEnable);
    DISPATCH(vkCmdSetDepthWriteEnable, _t_ogles2vk_CmdSetDepthWriteEnable);
    DISPATCH(vkCmdSetDepthCompareOp, _t_ogles2vk_CmdSetDepthCompareOp);
    DISPATCH(vkCmdSetDepthBoundsTestEnable, _t_ogles2vk_CmdSetDepthBoundsTestEnable);
    DISPATCH(vkCmdSetStencilTestEnable, _t_ogles2vk_CmdSetStencilTestEnable);
    DISPATCH(vkCmdSetStencilOp, _t_ogles2vk_CmdSetStencilOp);
    DISPATCH(vkCmdSetRasterizerDiscardEnable, _t_ogles2vk_CmdSetRasterizerDiscardEnable);
    DISPATCH(vkCmdSetDepthBiasEnable, _t_ogles2vk_CmdSetDepthBiasEnable);
    DISPATCH(vkCmdSetPrimitiveRestartEnable, _t_ogles2vk_CmdSetPrimitiveRestartEnable);

    /* Transfer commands */
    DISPATCH(vkCmdCopyBuffer, _t_ogles2vk_CmdCopyBuffer);
    DISPATCH(vkCmdCopyBufferToImage, _t_ogles2vk_CmdCopyBufferToImage);
    DISPATCH(vkCmdCopyImageToBuffer, _t_ogles2vk_CmdCopyImageToBuffer);
    DISPATCH(vkCmdCopyImage, _t_ogles2vk_CmdCopyImage);
    DISPATCH(vkCmdFillBuffer, _t_ogles2vk_CmdFillBuffer);
    DISPATCH(vkCmdUpdateBuffer, _t_ogles2vk_CmdUpdateBuffer);

    /* Synchronisation */
    DISPATCH(vkCreateFence, _t_ogles2vk_CreateFence);
    DISPATCH(vkDestroyFence, _t_ogles2vk_DestroyFence);
    DISPATCH(vkWaitForFences, _t_ogles2vk_WaitForFences);
    DISPATCH(vkResetFences, _t_ogles2vk_ResetFences);
    DISPATCH(vkGetFenceStatus, _t_ogles2vk_GetFenceStatus);
    DISPATCH(vkCreateSemaphore, _t_ogles2vk_CreateSemaphore);
    DISPATCH(vkDestroySemaphore, _t_ogles2vk_DestroySemaphore);
    DISPATCH(vkQueueSubmit, _t_ogles2vk_QueueSubmit);
    DISPATCH(vkQueueSubmit2, _t_ogles2vk_QueueSubmit2);

    /* Stubs */
    DISPATCH(vkFlushMappedMemoryRanges, _t_ogles2vk_FlushMappedMemoryRanges);
    DISPATCH(vkInvalidateMappedMemoryRanges, _t_ogles2vk_InvalidateMappedMemoryRanges);

    /* Vulkan 1.1/1.2/1.3 wrappers */
    DISPATCH(vkGetPhysicalDeviceProperties2, _t_ogles2vk_GetPhysicalDeviceProperties2);
    DISPATCH(vkGetPhysicalDeviceFeatures2, _t_ogles2vk_GetPhysicalDeviceFeatures2);
    DISPATCH(vkGetPhysicalDeviceQueueFamilyProperties2, _t_ogles2vk_GetPhysicalDeviceQueueFamilyProperties2);
    DISPATCH(vkGetPhysicalDeviceMemoryProperties2, _t_ogles2vk_GetPhysicalDeviceMemoryProperties2);
    DISPATCH(vkGetPhysicalDeviceFormatProperties2, _t_ogles2vk_GetPhysicalDeviceFormatProperties2);
    DISPATCH(vkGetBufferMemoryRequirements2, _t_ogles2vk_GetBufferMemoryRequirements2);
    DISPATCH(vkGetImageMemoryRequirements2, _t_ogles2vk_GetImageMemoryRequirements2);
    DISPATCH(vkBindBufferMemory2, _t_ogles2vk_BindBufferMemory2);
    DISPATCH(vkBindImageMemory2, _t_ogles2vk_BindImageMemory2);
    DISPATCH(vkGetPhysicalDeviceImageFormatProperties, _t_ogles2vk_GetPhysicalDeviceImageFormatProperties);

    /* WSI */
    DISPATCH(vkGetPhysicalDeviceSurfaceSupportKHR, _t_ogles2vk_GetPhysicalDeviceSurfaceSupportKHR);
    DISPATCH(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, _t_ogles2vk_GetPhysicalDeviceSurfaceCapabilitiesKHR);
    DISPATCH(vkGetPhysicalDeviceSurfaceFormatsKHR, _t_ogles2vk_GetPhysicalDeviceSurfaceFormatsKHR);
    DISPATCH(vkGetPhysicalDeviceSurfacePresentModesKHR, _t_ogles2vk_GetPhysicalDeviceSurfacePresentModesKHR);
    DISPATCH(vkCreateSwapchainKHR, _t_ogles2vk_CreateSwapchainKHR);
    DISPATCH(vkDestroySwapchainKHR, _t_ogles2vk_DestroySwapchainKHR);
    DISPATCH(vkGetSwapchainImagesKHR, _t_ogles2vk_GetSwapchainImagesKHR);
    DISPATCH(vkAcquireNextImageKHR, _t_ogles2vk_AcquireNextImageKHR);
    DISPATCH(vkQueuePresentKHR, _t_ogles2vk_QueuePresentKHR);

    /* 100% API coverage */
    DISPATCH(vkGetDeviceProcAddr, _t_ogles2vk_GetDeviceProcAddr);
    DISPATCH(vkGetDeviceMemoryCommitment, _t_ogles2vk_GetDeviceMemoryCommitment);
    DISPATCH(vkGetImageSubresourceLayout, _t_ogles2vk_GetImageSubresourceLayout);
    DISPATCH(vkGetRenderAreaGranularity, _t_ogles2vk_GetRenderAreaGranularity);
    DISPATCH(vkGetDeviceQueue2, _t_ogles2vk_GetDeviceQueue2);
    DISPATCH(vkGetDescriptorSetLayoutSupport, _t_ogles2vk_GetDescriptorSetLayoutSupport);
    DISPATCH(vkGetPhysicalDeviceToolProperties, _t_ogles2vk_GetPhysicalDeviceToolProperties);
    DISPATCH(vkGetPhysicalDeviceImageFormatProperties2, _t_ogles2vk_GetPhysicalDeviceImageFormatProperties2);
    DISPATCH(vkGetPhysicalDeviceExternalBufferProperties, _t_ogles2vk_GetPhysicalDeviceExternalBufferProperties);
    DISPATCH(vkGetPhysicalDeviceExternalFenceProperties, _t_ogles2vk_GetPhysicalDeviceExternalFenceProperties);
    DISPATCH(vkGetPhysicalDeviceExternalSemaphoreProperties, _t_ogles2vk_GetPhysicalDeviceExternalSemaphoreProperties);
    DISPATCH(vkEnumeratePhysicalDeviceGroups, _t_ogles2vk_EnumeratePhysicalDeviceGroups);
    DISPATCH(vkCreateComputePipelines, _t_ogles2vk_CreateComputePipelines);
    DISPATCH(vkCreateBufferView, _t_ogles2vk_CreateBufferView);
    DISPATCH(vkDestroyBufferView, _t_ogles2vk_DestroyBufferView);
    DISPATCH(vkCreateSamplerYcbcrConversion, _t_ogles2vk_CreateSamplerYcbcrConversion);
    DISPATCH(vkDestroySamplerYcbcrConversion, _t_ogles2vk_DestroySamplerYcbcrConversion);
    DISPATCH(vkCreateDescriptorUpdateTemplate, _t_ogles2vk_CreateDescriptorUpdateTemplate);
    DISPATCH(vkDestroyDescriptorUpdateTemplate, _t_ogles2vk_DestroyDescriptorUpdateTemplate);
    DISPATCH(vkUpdateDescriptorSetWithTemplate, _t_ogles2vk_UpdateDescriptorSetWithTemplate);
    DISPATCH(vkCreatePrivateDataSlot, _t_ogles2vk_CreatePrivateDataSlot);
    DISPATCH(vkDestroyPrivateDataSlot, _t_ogles2vk_DestroyPrivateDataSlot);
    DISPATCH(vkSetPrivateData, _t_ogles2vk_SetPrivateData);
    DISPATCH(vkGetPrivateData, _t_ogles2vk_GetPrivateData);
    DISPATCH(vkGetBufferDeviceAddress, _t_ogles2vk_GetBufferDeviceAddress);
    DISPATCH(vkGetBufferOpaqueCaptureAddress, _t_ogles2vk_GetBufferOpaqueCaptureAddress);
    DISPATCH(vkGetDeviceMemoryOpaqueCaptureAddress, _t_ogles2vk_GetDeviceMemoryOpaqueCaptureAddress);
    DISPATCH(vkGetSemaphoreCounterValue, _t_ogles2vk_GetSemaphoreCounterValue);
    DISPATCH(vkWaitSemaphores, _t_ogles2vk_WaitSemaphores);
    DISPATCH(vkSignalSemaphore, _t_ogles2vk_SignalSemaphore);
    DISPATCH(vkGetPipelineCacheData, _t_ogles2vk_GetPipelineCacheData);
    DISPATCH(vkMergePipelineCaches, _t_ogles2vk_MergePipelineCaches);
    DISPATCH(vkGetImageSparseMemoryRequirements, _t_ogles2vk_GetImageSparseMemoryRequirements);
    DISPATCH(vkGetImageSparseMemoryRequirements2, _t_ogles2vk_GetImageSparseMemoryRequirements2);
    DISPATCH(vkGetPhysicalDeviceSparseImageFormatProperties, _t_ogles2vk_GetPhysicalDeviceSparseImageFormatProperties);
    DISPATCH(vkGetPhysicalDeviceSparseImageFormatProperties2, _t_ogles2vk_GetPhysicalDeviceSparseImageFormatProperties2);
    DISPATCH(vkCreateEvent, _t_ogles2vk_CreateEvent);
    DISPATCH(vkDestroyEvent, _t_ogles2vk_DestroyEvent);
    DISPATCH(vkGetEventStatus, _t_ogles2vk_GetEventStatus);
    DISPATCH(vkSetEvent, _t_ogles2vk_SetEvent);
    DISPATCH(vkResetEvent, _t_ogles2vk_ResetEvent);
    DISPATCH(vkCmdSetEvent, _t_ogles2vk_CmdSetEvent);
    DISPATCH(vkCmdResetEvent, _t_ogles2vk_CmdResetEvent);
    DISPATCH(vkCmdWaitEvents, _t_ogles2vk_CmdWaitEvents);
    DISPATCH(vkCmdSetEvent2, _t_ogles2vk_CmdSetEvent2);
    DISPATCH(vkCmdResetEvent2, _t_ogles2vk_CmdResetEvent2);
    DISPATCH(vkCmdWaitEvents2, _t_ogles2vk_CmdWaitEvents2);
    DISPATCH(vkCreateQueryPool, _t_ogles2vk_CreateQueryPool);
    DISPATCH(vkDestroyQueryPool, _t_ogles2vk_DestroyQueryPool);
    DISPATCH(vkGetQueryPoolResults, _t_ogles2vk_GetQueryPoolResults);
    DISPATCH(vkResetQueryPool, _t_ogles2vk_ResetQueryPool);
    DISPATCH(vkCmdBeginQuery, _t_ogles2vk_CmdBeginQuery);
    DISPATCH(vkCmdEndQuery, _t_ogles2vk_CmdEndQuery);
    DISPATCH(vkCmdResetQueryPool, _t_ogles2vk_CmdResetQueryPool);
    DISPATCH(vkCmdWriteTimestamp, _t_ogles2vk_CmdWriteTimestamp);
    DISPATCH(vkCmdWriteTimestamp2, _t_ogles2vk_CmdWriteTimestamp2);
    DISPATCH(vkCmdCopyQueryPoolResults, _t_ogles2vk_CmdCopyQueryPoolResults);
    DISPATCH(vkQueueBindSparse, _t_ogles2vk_QueueBindSparse);
    DISPATCH(vkCreateRenderPass, _t_ogles2vk_CreateRenderPass);
    DISPATCH(vkDestroyRenderPass, _t_ogles2vk_DestroyRenderPass);
    DISPATCH(vkCreateFramebuffer, _t_ogles2vk_CreateFramebuffer);
    DISPATCH(vkDestroyFramebuffer, _t_ogles2vk_DestroyFramebuffer);
    DISPATCH(vkCreateRenderPass2, _t_ogles2vk_CreateRenderPass2);
    DISPATCH(vkCmdBeginRenderPass2, _t_ogles2vk_CmdBeginRenderPass2);
    DISPATCH(vkCmdNextSubpass2, _t_ogles2vk_CmdNextSubpass2);
    DISPATCH(vkCmdEndRenderPass2, _t_ogles2vk_CmdEndRenderPass2);
    DISPATCH(vkCmdSetLineWidth, _t_ogles2vk_CmdSetLineWidth);
    DISPATCH(vkCmdSetDepthBias, _t_ogles2vk_CmdSetDepthBias);
    DISPATCH(vkCmdSetBlendConstants, _t_ogles2vk_CmdSetBlendConstants);
    DISPATCH(vkCmdSetDepthBounds, _t_ogles2vk_CmdSetDepthBounds);
    DISPATCH(vkCmdSetStencilCompareMask, _t_ogles2vk_CmdSetStencilCompareMask);
    DISPATCH(vkCmdSetStencilWriteMask, _t_ogles2vk_CmdSetStencilWriteMask);
    DISPATCH(vkCmdSetStencilReference, _t_ogles2vk_CmdSetStencilReference);
    DISPATCH(vkCmdClearColorImage, _t_ogles2vk_CmdClearColorImage);
    DISPATCH(vkCmdClearDepthStencilImage, _t_ogles2vk_CmdClearDepthStencilImage);
    DISPATCH(vkCmdClearAttachments, _t_ogles2vk_CmdClearAttachments);
    DISPATCH(vkCmdBlitImage, _t_ogles2vk_CmdBlitImage);
    DISPATCH(vkCmdResolveImage, _t_ogles2vk_CmdResolveImage);
    DISPATCH(vkCmdCopyBuffer2, _t_ogles2vk_CmdCopyBuffer2);
    DISPATCH(vkCmdCopyImage2, _t_ogles2vk_CmdCopyImage2);
    DISPATCH(vkCmdCopyBufferToImage2, _t_ogles2vk_CmdCopyBufferToImage2);
    DISPATCH(vkCmdCopyImageToBuffer2, _t_ogles2vk_CmdCopyImageToBuffer2);
    DISPATCH(vkCmdBlitImage2, _t_ogles2vk_CmdBlitImage2);
    DISPATCH(vkCmdResolveImage2, _t_ogles2vk_CmdResolveImage2);
    DISPATCH(vkCmdSetDeviceMask, _t_ogles2vk_CmdSetDeviceMask);
    DISPATCH(vkCmdDispatch, _t_ogles2vk_CmdDispatch);
    DISPATCH(vkCmdDispatchBase, _t_ogles2vk_CmdDispatchBase);
    DISPATCH(vkCmdDispatchIndirect, _t_ogles2vk_CmdDispatchIndirect);
    DISPATCH(vkCmdExecuteCommands, _t_ogles2vk_CmdExecuteCommands);
    DISPATCH(vkCmdDrawIndirect, _t_ogles2vk_CmdDrawIndirect);
    DISPATCH(vkCmdDrawIndexedIndirect, _t_ogles2vk_CmdDrawIndexedIndirect);
    DISPATCH(vkCmdDrawIndirectCount, _t_ogles2vk_CmdDrawIndirectCount);
    DISPATCH(vkCmdDrawIndexedIndirectCount, _t_ogles2vk_CmdDrawIndexedIndirectCount);

    #undef DISPATCH

    return NULL;
}

/****************************************************************************/
/* Raw proc addr lookup -- standard C convention for vkGetDeviceProcAddr    */
/****************************************************************************/

static PFN_vkVoidFunction ogles2vk_LookupRawProcAddr(const char *pName)
{
    /* Mirror of the DISPATCH table in ogles2vk_LookupProcAddr, but pointing
    ** at the raw (non-APICALL) underlying functions. vkGetDeviceProcAddr
    ** must return pointers the application can call as standard PFN_vk*
    ** with no Self in r3 -- handing back APICALL trampolines (as the old
    ** fallback did) slides every argument by one register at call time.
    **
    ** Bug diagnosed by afxgroup (Andrea Palmate', derfsss/VulkanOS4#1).
    ** His original fix replaced the trampoline fallback with `return NULL`,
    ** which was ABI-correct but regressed WSI and ~165 other entry points
    ** that lived only in DISPATCH. This expanded table covers every
    ** function in DISPATCH so the fallback can safely return NULL. */
    #define RAW(vkName, fn) \
        if (strcmp(pName, #vkName) == 0) return (PFN_vkVoidFunction)(fn)

    /* Globals */
    RAW(vkCreateInstance, ogles2vk_CreateInstance);
    RAW(vkDestroyInstance, ogles2vk_DestroyInstance);
    RAW(vkEnumerateInstanceVersion, ogles2vk_EnumerateInstanceVersion);
    RAW(vkEnumerateInstanceExtensionProperties, ogles2vk_EnumerateInstanceExtensionProperties);

    /* Physical device enumeration */
    RAW(vkEnumeratePhysicalDevices, ogles2vk_EnumeratePhysicalDevices);

    /* Physical device queries */
    RAW(vkGetPhysicalDeviceProperties, ogles2vk_GetPhysicalDeviceProperties);
    RAW(vkGetPhysicalDeviceFeatures, ogles2vk_GetPhysicalDeviceFeatures);
    RAW(vkGetPhysicalDeviceQueueFamilyProperties, ogles2vk_GetPhysicalDeviceQueueFamilyProperties);
    RAW(vkGetPhysicalDeviceMemoryProperties, ogles2vk_GetPhysicalDeviceMemoryProperties);
    RAW(vkGetPhysicalDeviceFormatProperties, ogles2vk_GetPhysicalDeviceFormatProperties);

    /* Device extension enumeration */
    RAW(vkEnumerateDeviceExtensionProperties, ogles2vk_EnumerateDeviceExtensionProperties);

    /* Device */
    RAW(vkCreateDevice, ogles2vk_CreateDevice);
    RAW(vkDestroyDevice, ogles2vk_DestroyDevice);
    RAW(vkGetDeviceQueue, ogles2vk_GetDeviceQueue);
    RAW(vkDeviceWaitIdle, ogles2vk_DeviceWaitIdle);
    RAW(vkQueueWaitIdle, ogles2vk_QueueWaitIdle);

    /* Memory */
    RAW(vkAllocateMemory, ogles2vk_AllocateMemory);
    RAW(vkFreeMemory, ogles2vk_FreeMemory);
    RAW(vkMapMemory, ogles2vk_MapMemory);
    RAW(vkUnmapMemory, ogles2vk_UnmapMemory);

    /* Buffer */
    RAW(vkCreateBuffer, ogles2vk_CreateBuffer);
    RAW(vkDestroyBuffer, ogles2vk_DestroyBuffer);
    RAW(vkGetBufferMemoryRequirements, ogles2vk_GetBufferMemoryRequirements);
    RAW(vkBindBufferMemory, ogles2vk_BindBufferMemory);

    /* Image */
    RAW(vkCreateImage, ogles2vk_CreateImage);
    RAW(vkDestroyImage, ogles2vk_DestroyImage);
    RAW(vkGetImageMemoryRequirements, ogles2vk_GetImageMemoryRequirements);
    RAW(vkBindImageMemory, ogles2vk_BindImageMemory);
    RAW(vkCreateImageView, ogles2vk_CreateImageView);
    RAW(vkDestroyImageView, ogles2vk_DestroyImageView);

    /* Sampler */
    RAW(vkCreateSampler, ogles2vk_CreateSampler);
    RAW(vkDestroySampler, ogles2vk_DestroySampler);

    /* Descriptor sets */
    RAW(vkCreateDescriptorSetLayout, ogles2vk_CreateDescriptorSetLayout);
    RAW(vkDestroyDescriptorSetLayout, ogles2vk_DestroyDescriptorSetLayout);
    RAW(vkCreateDescriptorPool, ogles2vk_CreateDescriptorPool);
    RAW(vkDestroyDescriptorPool, ogles2vk_DestroyDescriptorPool);
    RAW(vkAllocateDescriptorSets, ogles2vk_AllocateDescriptorSets);
    RAW(vkFreeDescriptorSets, ogles2vk_FreeDescriptorSets);
    RAW(vkResetDescriptorPool, ogles2vk_ResetDescriptorPool);
    RAW(vkUpdateDescriptorSets, ogles2vk_UpdateDescriptorSets);

    /* Shader module */
    RAW(vkCreateShaderModule, ogles2vk_CreateShaderModule);
    RAW(vkDestroyShaderModule, ogles2vk_DestroyShaderModule);

    /* Pipeline layout */
    RAW(vkCreatePipelineLayout, ogles2vk_CreatePipelineLayout);
    RAW(vkDestroyPipelineLayout, ogles2vk_DestroyPipelineLayout);

    /* Pipeline cache */
    RAW(vkCreatePipelineCache, ogles2vk_CreatePipelineCache);
    RAW(vkDestroyPipelineCache, ogles2vk_DestroyPipelineCache);

    /* Pipeline */
    RAW(vkCreateGraphicsPipelines, ogles2vk_CreateGraphicsPipelines);
    RAW(vkDestroyPipeline, ogles2vk_DestroyPipeline);

    /* Command pool/buffer */
    RAW(vkCreateCommandPool, ogles2vk_CreateCommandPool);
    RAW(vkDestroyCommandPool, ogles2vk_DestroyCommandPool);
    RAW(vkAllocateCommandBuffers, ogles2vk_AllocateCommandBuffers);
    RAW(vkFreeCommandBuffers, ogles2vk_FreeCommandBuffers);
    RAW(vkBeginCommandBuffer, ogles2vk_BeginCommandBuffer);
    RAW(vkEndCommandBuffer, ogles2vk_EndCommandBuffer);
    RAW(vkResetCommandBuffer, ogles2vk_ResetCommandBuffer);
    RAW(vkResetCommandPool, ogles2vk_ResetCommandPool);
    RAW(vkTrimCommandPool, ogles2vk_TrimCommandPool);

    /* Command recording */
    RAW(vkCmdBindPipeline, ogles2vk_CmdBindPipeline);
    RAW(vkCmdSetViewport, ogles2vk_CmdSetViewport);
    RAW(vkCmdSetScissor, ogles2vk_CmdSetScissor);
    RAW(vkCmdDraw, ogles2vk_CmdDraw);
    RAW(vkCmdDrawIndexed, ogles2vk_CmdDrawIndexed);
    RAW(vkCmdBeginRendering, ogles2vk_CmdBeginRendering);
    RAW(vkCmdEndRendering, ogles2vk_CmdEndRendering);
    RAW(vkCmdPushConstants, ogles2vk_CmdPushConstants);
    RAW(vkCmdBindVertexBuffers, ogles2vk_CmdBindVertexBuffers);
    RAW(vkCmdBindIndexBuffer, ogles2vk_CmdBindIndexBuffer);
    RAW(vkCmdBindDescriptorSets, ogles2vk_CmdBindDescriptorSets);
    RAW(vkCmdBeginRenderPass, ogles2vk_CmdBeginRenderPass);
    RAW(vkCmdEndRenderPass, ogles2vk_CmdEndRenderPass);
    RAW(vkCmdNextSubpass, ogles2vk_CmdNextSubpass);
    RAW(vkCmdPipelineBarrier, ogles2vk_CmdPipelineBarrier);
    RAW(vkCmdPipelineBarrier2, ogles2vk_CmdPipelineBarrier2);

    /* Dynamic state */
    RAW(vkCmdSetCullMode, ogles2vk_CmdSetCullMode);
    RAW(vkCmdSetFrontFace, ogles2vk_CmdSetFrontFace);
    RAW(vkCmdSetPrimitiveTopology, ogles2vk_CmdSetPrimitiveTopology);
    RAW(vkCmdSetViewportWithCount, ogles2vk_CmdSetViewportWithCount);
    RAW(vkCmdSetScissorWithCount, ogles2vk_CmdSetScissorWithCount);
    RAW(vkCmdBindVertexBuffers2, ogles2vk_CmdBindVertexBuffers2);
    RAW(vkCmdSetDepthTestEnable, ogles2vk_CmdSetDepthTestEnable);
    RAW(vkCmdSetDepthWriteEnable, ogles2vk_CmdSetDepthWriteEnable);
    RAW(vkCmdSetDepthCompareOp, ogles2vk_CmdSetDepthCompareOp);
    RAW(vkCmdSetDepthBoundsTestEnable, ogles2vk_CmdSetDepthBoundsTestEnable);
    RAW(vkCmdSetStencilTestEnable, ogles2vk_CmdSetStencilTestEnable);
    RAW(vkCmdSetStencilOp, ogles2vk_CmdSetStencilOp);
    RAW(vkCmdSetRasterizerDiscardEnable, ogles2vk_CmdSetRasterizerDiscardEnable);
    RAW(vkCmdSetDepthBiasEnable, ogles2vk_CmdSetDepthBiasEnable);
    RAW(vkCmdSetPrimitiveRestartEnable, ogles2vk_CmdSetPrimitiveRestartEnable);

    /* Transfer commands */
    RAW(vkCmdCopyBuffer, ogles2vk_CmdCopyBuffer);
    RAW(vkCmdCopyBufferToImage, ogles2vk_CmdCopyBufferToImage);
    RAW(vkCmdCopyImageToBuffer, ogles2vk_CmdCopyImageToBuffer);
    RAW(vkCmdCopyImage, ogles2vk_CmdCopyImage);
    RAW(vkCmdFillBuffer, ogles2vk_CmdFillBuffer);
    RAW(vkCmdUpdateBuffer, ogles2vk_CmdUpdateBuffer);

    /* Synchronisation */
    RAW(vkCreateFence, ogles2vk_CreateFence);
    RAW(vkDestroyFence, ogles2vk_DestroyFence);
    RAW(vkWaitForFences, ogles2vk_WaitForFences);
    RAW(vkResetFences, ogles2vk_ResetFences);
    RAW(vkGetFenceStatus, ogles2vk_GetFenceStatus);
    RAW(vkCreateSemaphore, ogles2vk_CreateSemaphore);
    RAW(vkDestroySemaphore, ogles2vk_DestroySemaphore);
    RAW(vkQueueSubmit, ogles2vk_QueueSubmit);
    RAW(vkQueueSubmit2, ogles2vk_QueueSubmit2);

    /* Stubs */
    RAW(vkFlushMappedMemoryRanges, ogles2vk_FlushMappedMemoryRanges);
    RAW(vkInvalidateMappedMemoryRanges, ogles2vk_InvalidateMappedMemoryRanges);

    /* Vulkan 1.1/1.2/1.3 wrappers */
    RAW(vkGetPhysicalDeviceProperties2, ogles2vk_GetPhysicalDeviceProperties2);
    RAW(vkGetPhysicalDeviceFeatures2, ogles2vk_GetPhysicalDeviceFeatures2);
    RAW(vkGetPhysicalDeviceQueueFamilyProperties2, ogles2vk_GetPhysicalDeviceQueueFamilyProperties2);
    RAW(vkGetPhysicalDeviceMemoryProperties2, ogles2vk_GetPhysicalDeviceMemoryProperties2);
    RAW(vkGetPhysicalDeviceFormatProperties2, ogles2vk_GetPhysicalDeviceFormatProperties2);
    RAW(vkGetBufferMemoryRequirements2, ogles2vk_GetBufferMemoryRequirements2);
    RAW(vkGetImageMemoryRequirements2, ogles2vk_GetImageMemoryRequirements2);
    RAW(vkBindBufferMemory2, ogles2vk_BindBufferMemory2);
    RAW(vkBindImageMemory2, ogles2vk_BindImageMemory2);
    RAW(vkGetPhysicalDeviceImageFormatProperties, ogles2vk_GetPhysicalDeviceImageFormatProperties);

    /* WSI */
    RAW(vkGetPhysicalDeviceSurfaceSupportKHR, ogles2vk_GetPhysicalDeviceSurfaceSupportKHR);
    RAW(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, ogles2vk_GetPhysicalDeviceSurfaceCapabilitiesKHR);
    RAW(vkGetPhysicalDeviceSurfaceFormatsKHR, ogles2vk_GetPhysicalDeviceSurfaceFormatsKHR);
    RAW(vkGetPhysicalDeviceSurfacePresentModesKHR, ogles2vk_GetPhysicalDeviceSurfacePresentModesKHR);
    RAW(vkCreateSwapchainKHR, ogles2vk_CreateSwapchainKHR);
    RAW(vkDestroySwapchainKHR, ogles2vk_DestroySwapchainKHR);
    RAW(vkGetSwapchainImagesKHR, ogles2vk_GetSwapchainImagesKHR);
    RAW(vkAcquireNextImageKHR, ogles2vk_AcquireNextImageKHR);
    RAW(vkQueuePresentKHR, ogles2vk_QueuePresentKHR);

    /* 100% API coverage */
    RAW(vkGetDeviceProcAddr, ogles2vk_GetDeviceProcAddr);
    RAW(vkGetDeviceMemoryCommitment, ogles2vk_GetDeviceMemoryCommitment);
    RAW(vkGetImageSubresourceLayout, ogles2vk_GetImageSubresourceLayout);
    RAW(vkGetRenderAreaGranularity, ogles2vk_GetRenderAreaGranularity);
    RAW(vkGetDeviceQueue2, ogles2vk_GetDeviceQueue2);
    RAW(vkGetDescriptorSetLayoutSupport, ogles2vk_GetDescriptorSetLayoutSupport);
    RAW(vkGetPhysicalDeviceToolProperties, ogles2vk_GetPhysicalDeviceToolProperties);
    RAW(vkGetPhysicalDeviceImageFormatProperties2, ogles2vk_GetPhysicalDeviceImageFormatProperties2);
    RAW(vkGetPhysicalDeviceExternalBufferProperties, ogles2vk_GetPhysicalDeviceExternalBufferProperties);
    RAW(vkGetPhysicalDeviceExternalFenceProperties, ogles2vk_GetPhysicalDeviceExternalFenceProperties);
    RAW(vkGetPhysicalDeviceExternalSemaphoreProperties, ogles2vk_GetPhysicalDeviceExternalSemaphoreProperties);
    RAW(vkEnumeratePhysicalDeviceGroups, ogles2vk_EnumeratePhysicalDeviceGroups);
    RAW(vkCreateComputePipelines, ogles2vk_CreateComputePipelines);
    RAW(vkCreateBufferView, ogles2vk_CreateBufferView);
    RAW(vkDestroyBufferView, ogles2vk_DestroyBufferView);
    RAW(vkCreateSamplerYcbcrConversion, ogles2vk_CreateSamplerYcbcrConversion);
    RAW(vkDestroySamplerYcbcrConversion, ogles2vk_DestroySamplerYcbcrConversion);
    RAW(vkCreateDescriptorUpdateTemplate, ogles2vk_CreateDescriptorUpdateTemplate);
    RAW(vkDestroyDescriptorUpdateTemplate, ogles2vk_DestroyDescriptorUpdateTemplate);
    RAW(vkUpdateDescriptorSetWithTemplate, ogles2vk_UpdateDescriptorSetWithTemplate);
    RAW(vkCreatePrivateDataSlot, ogles2vk_CreatePrivateDataSlot);
    RAW(vkDestroyPrivateDataSlot, ogles2vk_DestroyPrivateDataSlot);
    RAW(vkSetPrivateData, ogles2vk_SetPrivateData);
    RAW(vkGetPrivateData, ogles2vk_GetPrivateData);
    RAW(vkGetBufferDeviceAddress, ogles2vk_GetBufferDeviceAddress);
    RAW(vkGetBufferOpaqueCaptureAddress, ogles2vk_GetBufferOpaqueCaptureAddress);
    RAW(vkGetDeviceMemoryOpaqueCaptureAddress, ogles2vk_GetDeviceMemoryOpaqueCaptureAddress);
    RAW(vkGetSemaphoreCounterValue, ogles2vk_GetSemaphoreCounterValue);
    RAW(vkWaitSemaphores, ogles2vk_WaitSemaphores);
    RAW(vkSignalSemaphore, ogles2vk_SignalSemaphore);
    RAW(vkGetPipelineCacheData, ogles2vk_GetPipelineCacheData);
    RAW(vkMergePipelineCaches, ogles2vk_MergePipelineCaches);
    RAW(vkGetImageSparseMemoryRequirements, ogles2vk_GetImageSparseMemoryRequirements);
    RAW(vkGetImageSparseMemoryRequirements2, ogles2vk_GetImageSparseMemoryRequirements2);
    RAW(vkGetPhysicalDeviceSparseImageFormatProperties, ogles2vk_GetPhysicalDeviceSparseImageFormatProperties);
    RAW(vkGetPhysicalDeviceSparseImageFormatProperties2, ogles2vk_GetPhysicalDeviceSparseImageFormatProperties2);
    RAW(vkCreateEvent, ogles2vk_CreateEvent);
    RAW(vkDestroyEvent, ogles2vk_DestroyEvent);
    RAW(vkGetEventStatus, ogles2vk_GetEventStatus);
    RAW(vkSetEvent, ogles2vk_SetEvent);
    RAW(vkResetEvent, ogles2vk_ResetEvent);
    RAW(vkCmdSetEvent, ogles2vk_CmdSetEvent);
    RAW(vkCmdResetEvent, ogles2vk_CmdResetEvent);
    RAW(vkCmdWaitEvents, ogles2vk_CmdWaitEvents);
    RAW(vkCmdSetEvent2, ogles2vk_CmdSetEvent2);
    RAW(vkCmdResetEvent2, ogles2vk_CmdResetEvent2);
    RAW(vkCmdWaitEvents2, ogles2vk_CmdWaitEvents2);
    RAW(vkCreateQueryPool, ogles2vk_CreateQueryPool);
    RAW(vkDestroyQueryPool, ogles2vk_DestroyQueryPool);
    RAW(vkGetQueryPoolResults, ogles2vk_GetQueryPoolResults);
    RAW(vkResetQueryPool, ogles2vk_ResetQueryPool);
    RAW(vkCmdBeginQuery, ogles2vk_CmdBeginQuery);
    RAW(vkCmdEndQuery, ogles2vk_CmdEndQuery);
    RAW(vkCmdResetQueryPool, ogles2vk_CmdResetQueryPool);
    RAW(vkCmdWriteTimestamp, ogles2vk_CmdWriteTimestamp);
    RAW(vkCmdWriteTimestamp2, ogles2vk_CmdWriteTimestamp2);
    RAW(vkCmdCopyQueryPoolResults, ogles2vk_CmdCopyQueryPoolResults);
    RAW(vkQueueBindSparse, ogles2vk_QueueBindSparse);
    RAW(vkCreateRenderPass, ogles2vk_CreateRenderPass);
    RAW(vkDestroyRenderPass, ogles2vk_DestroyRenderPass);
    RAW(vkCreateFramebuffer, ogles2vk_CreateFramebuffer);
    RAW(vkDestroyFramebuffer, ogles2vk_DestroyFramebuffer);
    RAW(vkCreateRenderPass2, ogles2vk_CreateRenderPass2);
    RAW(vkCmdBeginRenderPass2, ogles2vk_CmdBeginRenderPass2);
    RAW(vkCmdNextSubpass2, ogles2vk_CmdNextSubpass2);
    RAW(vkCmdEndRenderPass2, ogles2vk_CmdEndRenderPass2);
    RAW(vkCmdSetLineWidth, ogles2vk_CmdSetLineWidth);
    RAW(vkCmdSetDepthBias, ogles2vk_CmdSetDepthBias);
    RAW(vkCmdSetBlendConstants, ogles2vk_CmdSetBlendConstants);
    RAW(vkCmdSetDepthBounds, ogles2vk_CmdSetDepthBounds);
    RAW(vkCmdSetStencilCompareMask, ogles2vk_CmdSetStencilCompareMask);
    RAW(vkCmdSetStencilWriteMask, ogles2vk_CmdSetStencilWriteMask);
    RAW(vkCmdSetStencilReference, ogles2vk_CmdSetStencilReference);
    RAW(vkCmdClearColorImage, ogles2vk_CmdClearColorImage);
    RAW(vkCmdClearDepthStencilImage, ogles2vk_CmdClearDepthStencilImage);
    RAW(vkCmdClearAttachments, ogles2vk_CmdClearAttachments);
    RAW(vkCmdBlitImage, ogles2vk_CmdBlitImage);
    RAW(vkCmdResolveImage, ogles2vk_CmdResolveImage);
    RAW(vkCmdCopyBuffer2, ogles2vk_CmdCopyBuffer2);
    RAW(vkCmdCopyImage2, ogles2vk_CmdCopyImage2);
    RAW(vkCmdCopyBufferToImage2, ogles2vk_CmdCopyBufferToImage2);
    RAW(vkCmdCopyImageToBuffer2, ogles2vk_CmdCopyImageToBuffer2);
    RAW(vkCmdBlitImage2, ogles2vk_CmdBlitImage2);
    RAW(vkCmdResolveImage2, ogles2vk_CmdResolveImage2);
    RAW(vkCmdSetDeviceMask, ogles2vk_CmdSetDeviceMask);
    RAW(vkCmdDispatch, ogles2vk_CmdDispatch);
    RAW(vkCmdDispatchBase, ogles2vk_CmdDispatchBase);
    RAW(vkCmdDispatchIndirect, ogles2vk_CmdDispatchIndirect);
    RAW(vkCmdExecuteCommands, ogles2vk_CmdExecuteCommands);
    RAW(vkCmdDrawIndirect, ogles2vk_CmdDrawIndirect);
    RAW(vkCmdDrawIndexedIndirect, ogles2vk_CmdDrawIndexedIndirect);
    RAW(vkCmdDrawIndirectCount, ogles2vk_CmdDrawIndirectCount);
    RAW(vkCmdDrawIndexedIndirectCount, ogles2vk_CmdDrawIndexedIndirectCount);

    #undef RAW

    return NULL;
}

static PFN_vkVoidFunction APICALL _icd_GetInstanceProcAddr(
    struct OGLES2VKICDIFace *Self,
    VkInstance instance,
    const char *pName)
{
    (void)Self;
    (void)instance;
    if (!pName) return NULL;
    return ogles2vk_LookupProcAddr(pName);
}

/****************************************************************************/
/* ICD interface: vk_icdNegotiateLoaderICDInterfaceVersion                  */
/****************************************************************************/

static VkResult APICALL _icd_NegotiateLoaderICDInterfaceVersion(
    struct OGLES2VKICDIFace *Self,
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

    IExec->DebugPrintF("[ogles2_vk] Negotiated ICD interface version %lu\n",
                       (unsigned long)*pSupportedVersion);

    return VK_SUCCESS;
}

/****************************************************************************/
/* ICD interface: vk_icdGetPhysicalDeviceProcAddr                           */
/* Optional in ICD interface version 3. Returns physical-device-specific    */
/* function pointers, or NULL to use vk_icdGetInstanceProcAddr instead.     */
/****************************************************************************/

static PFN_vkVoidFunction APICALL _icd_GetPhysicalDeviceProcAddr(
    struct OGLES2VKICDIFace *Self,
    VkInstance instance,
    const char *pName)
{
    (void)Self;
    (void)instance;
    (void)pName;

    /* For the skeleton, all functions are resolved via GetInstanceProcAddr */
    return NULL;
}
