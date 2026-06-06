/*
** vulkan.library -- Vulkan Loader for AmigaOS 4
**
** loader_dispatch.c -- Loader-level Vulkan function implementations.
** These handle instance-level operations. Device-level functions are
** populated directly from the ICD at vkCreateInstance time.
*/

#include <exec/types.h>
#include <interfaces/exec.h>
#include <string.h>

#include "loader_internal.h"

/****************************************************************************/
/* Loader-implemented Vulkan functions                                      */
/****************************************************************************/

VkResult APICALL Loader_vkCreateInstance(struct VulkanIFace *Self,
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance)
{
    (void)pAllocator;

    IExec->DebugPrintF("[vulkan.library] vkCreateInstance\n");

    /* Discover and load ICDs if not already done */
    if (!g_loaderState.initialized)
    {
        int count = LoaderDiscoverICDs();
        if (count == 0)
        {
            IExec->DebugPrintF("[vulkan.library] No ICD found in DEVS:Vulkan/icd.d/\n");
            return VK_ERROR_INCOMPATIBLE_DRIVER;
        }
        g_loaderState.initialized = VK_TRUE;
    }

    /* Try each ICD in priority order (GPU first, software last).
    ** Skip ICDs that lack minimum required functions (vkCreateDevice).
    ** This prevents crashes when a skeleton ICD (like the W3D Nova
    ** ICD during early development) is loaded but incomplete. */
    for (uint32_t icdIdx = 0; icdIdx < g_loaderState.icdCount; icdIdx++)
    {
        struct ICDState *icd = &g_loaderState.icds[icdIdx];

        if (!icd->iface)
            continue;

        /* Access the ICD's vk_icdGetInstanceProcAddr (slot 4).
        ** APICALL passes Self implicitly -- only pass visible args. */
        struct {
            struct InterfaceData Data;
            uint32 APICALL (*Obtain)(void *Self);
            uint32 APICALL (*Release)(void *Self);
            void   APICALL (*Expunge)(void *Self);
            void * APICALL (*Clone)(void *Self);
            PFN_vkVoidFunction APICALL (*GetInstanceProcAddr)(void *Self, VkInstance inst, const char *name);
        } *icdIFace = (void *)icd->iface;

        /* Get vkCreateInstance from ICD */
        PFN_vkVoidFunction pfnRaw = icdIFace->GetInstanceProcAddr(
            NULL, "vkCreateInstance");
        if (!pfnRaw)
            continue;

        /* Try to create instance on this ICD */
        PFN_vkCreateInstance pfn = (PFN_vkCreateInstance)pfnRaw;
        VkResult result = pfn(pCreateInfo, pAllocator, pInstance);
        if (result != VK_SUCCESS)
        {
            /* RawDoFmt-style: %s for strings, %ld for longs (%d is unsupported). */
            IExec->DebugPrintF("[vulkan.library] ICD %s: vkCreateInstance failed (%ld)\n",
                               icd->libraryPath, (long)result);
            continue;
        }

        icd->instance = *pInstance;
        g_loaderState.activeICD = icdIdx;

        /* Populate VulkanIFace and check viability */
        int viable = LoaderPopulateIFace(Self);
        if (viable)
        {
            IExec->DebugPrintF("[vulkan.library] Using ICD: %s\n",
                               icd->libraryPath);
            return VK_SUCCESS;
        }

        /* ICD lacks minimum required functions -- destroy and try next */
        IExec->DebugPrintF("[vulkan.library] ICD %s lacks required functions, trying next\n",
                           icd->libraryPath);

        PFN_vkVoidFunction pfnDestroy = icdIFace->GetInstanceProcAddr(
            icd->instance, "vkDestroyInstance");
        if (pfnDestroy)
        {
            PFN_vkDestroyInstance pfnDI = (PFN_vkDestroyInstance)pfnDestroy;
            pfnDI(icd->instance, NULL);
        }
        icd->instance = NULL;
    }

    IExec->DebugPrintF("[vulkan.library] No suitable ICD found\n");
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

void APICALL Loader_vkDestroyInstance(struct VulkanIFace *Self,
    VkInstance instance,
    const VkAllocationCallbacks *pAllocator)
{
    (void)Self;

    if (instance == VK_NULL_HANDLE)
        return;

    /* Forward to the active ICD's vkDestroyInstance */
    uint32_t idx = g_loaderState.activeICD;
    if (idx < g_loaderState.icdCount && g_loaderState.icds[idx].iface)
    {
        struct {
            struct InterfaceData Data;
            uint32 APICALL (*Obtain)(void *Self);
            uint32 APICALL (*Release)(void *Self);
            void   APICALL (*Expunge)(void *Self);
            void * APICALL (*Clone)(void *Self);
            PFN_vkVoidFunction APICALL (*GetInstanceProcAddr)(void *Self, VkInstance inst, const char *name);
        } *icdIFace = (void *)g_loaderState.icds[idx].iface;

        PFN_vkVoidFunction pfnRaw = icdIFace->GetInstanceProcAddr(
            instance, "vkDestroyInstance");
        if (pfnRaw)
        {
            PFN_vkDestroyInstance pfn = (PFN_vkDestroyInstance)pfnRaw;
            pfn(instance, pAllocator);
        }
    }

    if (idx < g_loaderState.icdCount)
        g_loaderState.icds[idx].instance = NULL;

    IExec->DebugPrintF("[vulkan.library] Instance destroyed\n");
}

VkResult APICALL Loader_vkEnumerateInstanceVersion(struct VulkanIFace *Self,
    uint32_t *pApiVersion)
{
    *pApiVersion = VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

VkResult APICALL Loader_vkEnumerateInstanceExtensionProperties(
    struct VulkanIFace *Self,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    /* Report the extensions the loader provides */
    static const VkExtensionProperties loaderExts[] = {
        { VK_KHR_SURFACE_EXTENSION_NAME,   VK_KHR_SURFACE_SPEC_VERSION },
        { VK_AMIGA_SURFACE_EXTENSION_NAME,  VK_AMIGA_SURFACE_SPEC_VERSION },
    };
    uint32_t count = sizeof(loaderExts) / sizeof(loaderExts[0]);

    if (pProperties == NULL)
    {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pPropertyCount < count ? *pPropertyCount : count;
    memcpy(pProperties, loaderExts, toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;

    return (toCopy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult APICALL Loader_vkEnumerateInstanceLayerProperties(
    struct VulkanIFace *Self,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties)
{
    /* No layers available */
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult APICALL Loader_vkEnumeratePhysicalDevices(struct VulkanIFace *Self,
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices)
{
    (void)Self;

    /* This is the loader's initial implementation of vkEnumeratePhysicalDevices.
    ** Once PopulateIFace runs, the function pointer in VulkanIFace is
    ** overwritten with the ICD's implementation. If the ICD is not yet
    ** ready, return 0 devices. */
    *pPhysicalDeviceCount = 0;
    return VK_SUCCESS;
}

PFN_vkVoidFunction APICALL Loader_vkGetInstanceProcAddr(
    struct VulkanIFace *Self,
    VkInstance instance,
    const char *pName)
{
    (void)Self;

    /* Return NULL -- function lookup is handled by the ICD's interface. */
    (void)instance;
    (void)pName;
    return NULL;
}

PFN_vkVoidFunction APICALL Loader_vkGetDeviceProcAddr(
    struct VulkanIFace *Self,
    VkDevice device,
    const char *pName)
{
    (void)Self;
    (void)device;

    if (!pName)
        return NULL;

    /* Resolve the ICD's vkGetDeviceProcAddr function and call it.
    ** The ICD's GetDeviceProcAddr returns raw (non-APICALL) function
    ** pointers suitable for direct C calls by the application. */
    uint32_t icdIdx = g_loaderState.activeICD;
    if (icdIdx >= g_loaderState.icdCount)
        return NULL;

    struct ICDState *icd = &g_loaderState.icds[icdIdx];
    if (!icd->iface)
        return NULL;

    /* Get the ICD's vkGetDeviceProcAddr trampoline via GetInstanceProcAddr */
    struct {
        struct InterfaceData Data;
        uint32 APICALL (*Obtain)(void *Self);
        uint32 APICALL (*Release)(void *Self);
        void   APICALL (*Expunge)(void *Self);
        void * APICALL (*Clone)(void *Self);
        PFN_vkVoidFunction APICALL (*GetInstanceProcAddr)(void *Self, VkInstance inst, const char *name);
    } *icdIFace = (void *)icd->iface;

    /* Get the APICALL trampoline for vkGetDeviceProcAddr.
    ** Cast as a regular 3-arg function (Self, device, pName) since
    ** we're calling it directly, not through an AmigaOS interface. */
    typedef PFN_vkVoidFunction (*PFN_GetDevProcAddr)(void *, VkDevice, const char *);
    PFN_GetDevProcAddr icdGetDevProcAddr = (PFN_GetDevProcAddr)
        icdIFace->GetInstanceProcAddr(icd->instance, "vkGetDeviceProcAddr");

    if (!icdGetDevProcAddr)
        return NULL;

    /* Call the trampoline with NULL as Self (trampoline discards it).
    ** Returns raw C function pointers via the ICD's LookupRawProcAddr. */
    return icdGetDevProcAddr(NULL, device, pName);
}

/****************************************************************************/
/* Loader utility functions                                                 */
/****************************************************************************/

uint32 APICALL Loader_VkAmigaGetLoaderVersion(struct VulkanIFace *Self)
{
    return VK_MAKE_API_VERSION(0, LIBVER, LIBREV, 0);
}

VkResult APICALL Loader_VkAmigaSetICDSearchPath(struct VulkanIFace *Self,
    const char *path)
{
    /* TODO: allow overriding ICD search path */
    (void)path;
    return VK_SUCCESS;
}

VkResult APICALL Loader_VkAmigaSetLayerSearchPath(struct VulkanIFace *Self,
    const char *path)
{
    /* TODO: allow overriding layer search path */
    (void)path;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Error stub for unimplemented ICD functions.                              */
/* Prevents NULL pointer crash when an ICD doesn't implement a function.    */
/* Returns VK_ERROR_FEATURE_NOT_PRESENT for VkResult functions;             */
/* for void functions the return value is harmlessly ignored.               */
/****************************************************************************/

static VkResult APICALL _stub_not_implemented(struct VulkanIFace *Self)
{
    (void)Self;
    IExec->DebugPrintF("[vulkan.library] ERROR: unimplemented ICD function called\n");
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/****************************************************************************/
/* Populate VulkanIFace with ICD function pointers                          */
/* Uses g_loaderState.activeICD to select which ICD to resolve from.        */
/* Unresolved functions get an error stub instead of NULL (crash safety).   */
/* Returns 1 if ICD provides minimum required functions, 0 otherwise.       */
/****************************************************************************/

int LoaderPopulateIFace(struct VulkanIFace *iface)
{
    uint32_t icdIdx = g_loaderState.activeICD;
    if (icdIdx >= g_loaderState.icdCount)
        return 0;

    struct ICDState *icd = &g_loaderState.icds[icdIdx];

    /* Access the ICD's GetInstanceProcAddr through its interface.
    ** Slot 4 in the ICD interface = vk_icdGetInstanceProcAddr. */
    struct {
        struct InterfaceData Data;
        uint32 APICALL (*Obtain)(void *Self);
        uint32 APICALL (*Release)(void *Self);
        void   APICALL (*Expunge)(void *Self);
        void * APICALL (*Clone)(void *Self);
        PFN_vkVoidFunction APICALL (*GetInstanceProcAddr)(void *Self, VkInstance inst, const char *name);
    } *icdIFace = (void *)icd->iface;

    /* Resolve ICD function via the interface and store in VulkanIFace.
    ** If the ICD doesn't provide the function, install an error stub
    ** instead of leaving NULL (prevents crash on unimplemented calls).
    ** APICALL handles Self implicitly -- only pass instance + name. */
    #define RESOLVE(name) do { \
        PFN_vkVoidFunction fn = icdIFace->GetInstanceProcAddr( \
            icd->instance, #name); \
        iface->name = fn ? (void *)fn : (void *)_stub_not_implemented; \
    } while(0)

    /* Physical device enumeration (instance-level, forwarded to ICD) */
    RESOLVE(vkEnumeratePhysicalDevices);

    /* Physical device queries (instance-level, forwarded to ICD) */
    RESOLVE(vkGetPhysicalDeviceProperties);
    RESOLVE(vkGetPhysicalDeviceFeatures);
    RESOLVE(vkGetPhysicalDeviceQueueFamilyProperties);
    RESOLVE(vkGetPhysicalDeviceMemoryProperties);
    RESOLVE(vkGetPhysicalDeviceFormatProperties);

    /* Device extension enumeration */
    RESOLVE(vkEnumerateDeviceExtensionProperties);

    /* Device */
    RESOLVE(vkCreateDevice);
    RESOLVE(vkDestroyDevice);
    RESOLVE(vkGetDeviceQueue);
    RESOLVE(vkDeviceWaitIdle);
    RESOLVE(vkQueueWaitIdle);

    /* Memory */
    RESOLVE(vkAllocateMemory);
    RESOLVE(vkFreeMemory);
    RESOLVE(vkMapMemory);
    RESOLVE(vkUnmapMemory);

    /* Buffer / Image */
    RESOLVE(vkCreateBuffer);
    RESOLVE(vkDestroyBuffer);
    RESOLVE(vkGetBufferMemoryRequirements);
    RESOLVE(vkBindBufferMemory);
    RESOLVE(vkCreateImage);
    RESOLVE(vkDestroyImage);
    RESOLVE(vkGetImageMemoryRequirements);
    RESOLVE(vkBindImageMemory);
    RESOLVE(vkCreateImageView);
    RESOLVE(vkDestroyImageView);

    /* Sampler */
    RESOLVE(vkCreateSampler);
    RESOLVE(vkDestroySampler);

    /* Shader / Pipeline */
    RESOLVE(vkCreateShaderModule);
    RESOLVE(vkDestroyShaderModule);
    RESOLVE(vkCreatePipelineLayout);
    RESOLVE(vkDestroyPipelineLayout);
    RESOLVE(vkCreateGraphicsPipelines);
    RESOLVE(vkDestroyPipeline);
    RESOLVE(vkCreatePipelineCache);
    RESOLVE(vkDestroyPipelineCache);

    /* Descriptor sets */
    RESOLVE(vkCreateDescriptorSetLayout);
    RESOLVE(vkDestroyDescriptorSetLayout);
    RESOLVE(vkCreateDescriptorPool);
    RESOLVE(vkDestroyDescriptorPool);
    RESOLVE(vkAllocateDescriptorSets);
    RESOLVE(vkFreeDescriptorSets);
    RESOLVE(vkUpdateDescriptorSets);

    /* Command buffer */
    RESOLVE(vkCreateCommandPool);
    RESOLVE(vkDestroyCommandPool);
    RESOLVE(vkAllocateCommandBuffers);
    RESOLVE(vkFreeCommandBuffers);
    RESOLVE(vkBeginCommandBuffer);
    RESOLVE(vkEndCommandBuffer);

    /* Command recording */
    RESOLVE(vkCmdBindPipeline);
    RESOLVE(vkCmdSetViewport);
    RESOLVE(vkCmdSetScissor);
    RESOLVE(vkCmdDraw);
    RESOLVE(vkCmdBeginRendering);
    RESOLVE(vkCmdEndRendering);
    RESOLVE(vkCmdPushConstants);
    RESOLVE(vkCmdBindVertexBuffers);
    RESOLVE(vkCmdBindIndexBuffer);
    RESOLVE(vkCmdDrawIndexed);
    RESOLVE(vkCmdBindDescriptorSets);

    /* Synchronisation */
    RESOLVE(vkCreateFence);
    RESOLVE(vkDestroyFence);
    RESOLVE(vkWaitForFences);
    RESOLVE(vkResetFences);
    RESOLVE(vkCreateSemaphore);
    RESOLVE(vkDestroySemaphore);
    RESOLVE(vkQueueSubmit);

    /* WSI surface queries (forwarded to ICD) */
    RESOLVE(vkGetPhysicalDeviceSurfaceSupportKHR);
    RESOLVE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    RESOLVE(vkGetPhysicalDeviceSurfaceFormatsKHR);
    RESOLVE(vkGetPhysicalDeviceSurfacePresentModesKHR);

    /* WSI swapchain (ICD-owned) */
    RESOLVE(vkCreateSwapchainKHR);
    RESOLVE(vkDestroySwapchainKHR);
    RESOLVE(vkGetSwapchainImagesKHR);
    RESOLVE(vkAcquireNextImageKHR);
    RESOLVE(vkQueuePresentKHR);

    /* Memory, command buffer, and pipeline cache management */
    RESOLVE(vkFlushMappedMemoryRanges);
    RESOLVE(vkInvalidateMappedMemoryRanges);
    RESOLVE(vkResetCommandBuffer);
    RESOLVE(vkCmdPipelineBarrier);
    RESOLVE(vkResetCommandPool);
    RESOLVE(vkTrimCommandPool);
    RESOLVE(vkGetFenceStatus);
    RESOLVE(vkGetPipelineCacheData);
    RESOLVE(vkMergePipelineCaches);
    RESOLVE(vkResetDescriptorPool);

    /* Legacy render pass */
    RESOLVE(vkCreateRenderPass);
    RESOLVE(vkDestroyRenderPass);
    RESOLVE(vkCreateFramebuffer);
    RESOLVE(vkDestroyFramebuffer);
    RESOLVE(vkCmdBeginRenderPass);
    RESOLVE(vkCmdEndRenderPass);
    RESOLVE(vkCmdNextSubpass);

    /* Vulkan 1.3 dynamic state */
    RESOLVE(vkCmdSetCullMode);
    RESOLVE(vkCmdSetFrontFace);
    RESOLVE(vkCmdSetPrimitiveTopology);
    RESOLVE(vkCmdSetViewportWithCount);
    RESOLVE(vkCmdSetScissorWithCount);
    RESOLVE(vkCmdBindVertexBuffers2);
    RESOLVE(vkCmdSetDepthTestEnable);
    RESOLVE(vkCmdSetDepthWriteEnable);
    RESOLVE(vkCmdSetDepthCompareOp);
    RESOLVE(vkCmdSetDepthBoundsTestEnable);
    RESOLVE(vkCmdSetStencilTestEnable);
    RESOLVE(vkCmdSetStencilOp);
    RESOLVE(vkCmdSetRasterizerDiscardEnable);
    RESOLVE(vkCmdSetDepthBiasEnable);
    RESOLVE(vkCmdSetPrimitiveRestartEnable);

    /* Transfer commands */
    RESOLVE(vkCmdCopyBuffer);
    RESOLVE(vkCmdCopyBufferToImage);
    RESOLVE(vkCmdCopyImageToBuffer);
    RESOLVE(vkCmdCopyImage);
    RESOLVE(vkCmdFillBuffer);
    RESOLVE(vkCmdUpdateBuffer);

    /* Vulkan 1.1/1.2/1.3 wrappers */
    RESOLVE(vkGetPhysicalDeviceProperties2);
    RESOLVE(vkGetPhysicalDeviceFeatures2);
    RESOLVE(vkGetPhysicalDeviceQueueFamilyProperties2);
    RESOLVE(vkGetPhysicalDeviceMemoryProperties2);
    RESOLVE(vkGetPhysicalDeviceFormatProperties2);
    RESOLVE(vkGetBufferMemoryRequirements2);
    RESOLVE(vkGetImageMemoryRequirements2);
    RESOLVE(vkBindBufferMemory2);
    RESOLVE(vkBindImageMemory2);
    RESOLVE(vkCmdPipelineBarrier2);
    RESOLVE(vkQueueSubmit2);
    RESOLVE(vkGetPhysicalDeviceImageFormatProperties);

    #undef RESOLVE

    /* Check minimum viability: ICD must provide vkCreateDevice */
    int viable = (iface->vkCreateDevice != (void *)_stub_not_implemented);

    IExec->DebugPrintF("[vulkan.library] ICD functions resolved (%s)\n",
                       viable ? "viable" : "incomplete");

    return viable;
}
