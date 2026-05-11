/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_device.c -- Logical device and queue management
**
** Functions here use standard Vulkan calling convention (no APICALL,
** no Self parameter). They are returned by vk_icdGetInstanceProcAddr
** and stored in VulkanIFace by the loader.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Device creation                                                          */
/****************************************************************************/

VkResult swvk_CreateDevice(VkPhysicalDevice physicalDevice,
                           const VkDeviceCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkDevice *pDevice)
{
    (void)pAllocator;
    (void)pCreateInfo;

    SWVKPhysicalDevice *pd = (SWVKPhysicalDevice *)physicalDevice;
    (void)pd;

    SWVKDevice *dev = (SWVKDevice *)IExec->AllocVecTags(
        sizeof(SWVKDevice),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!dev)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* The instance is not tracked in the device.
    ** The device has a single graphics queue. */
    dev->instance = NULL;

    /* Initialise the graphics queue */
    dev->graphicsQueue.device      = dev;
    dev->graphicsQueue.familyIndex = 0;
    dev->graphicsQueue.queueIndex  = 0;

    *pDevice = (VkDevice)dev;

    D(("[software_vk] Device created\n"));

    return VK_SUCCESS;
}

void swvk_DestroyDevice(VkDevice device,
                        const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (device != VK_NULL_HANDLE)
    {
        IExec->FreeVec((APTR)device);
        D(("[software_vk] Device destroyed\n"));
    }
}

/****************************************************************************/
/* Queue retrieval                                                          */
/****************************************************************************/

void swvk_GetDeviceQueue(VkDevice device,
                         uint32_t queueFamilyIndex,
                         uint32_t queueIndex,
                         VkQueue *pQueue)
{
    SWVKDevice *dev = (SWVKDevice *)device;
    (void)queueFamilyIndex;
    (void)queueIndex;

    /* We have a single queue. Return a pointer to the embedded struct. */
    *pQueue = (VkQueue)&dev->graphicsQueue;
}

/****************************************************************************/
/* Idle waits (no-op for software renderer -- everything is synchronous)    */
/****************************************************************************/

VkResult swvk_DeviceWaitIdle(VkDevice device)
{
    (void)device;
    /* Software renderer executes everything synchronously.
    ** Nothing to wait for. */
    return VK_SUCCESS;
}

VkResult swvk_QueueWaitIdle(VkQueue queue)
{
    (void)queue;
    /* Software renderer executes everything synchronously.
    ** Nothing to wait for. */
    return VK_SUCCESS;
}

/****************************************************************************/
/* Command pool management                                                  */
/****************************************************************************/

VkResult swvk_ResetCommandPool(VkDevice device,
                                VkCommandPool commandPool,
                                VkCommandPoolResetFlags flags)
{
    /* Known limitation: pool doesn't track allocated cmdbufs, so we
    ** cannot reset them individually. Just accept the call. */
    (void)device; (void)commandPool; (void)flags;
    return VK_SUCCESS;
}

void swvk_TrimCommandPool(VkDevice device,
                            VkCommandPool commandPool,
                            VkFlags flags)
{
    (void)device; (void)commandPool; (void)flags;
    /* No-op -- nothing to trim in the software renderer */
}

/****************************************************************************/
/* Device extension enumeration                                             */
/****************************************************************************/

VkResult swvk_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    (void)physicalDevice;
    (void)pLayerName;

    /* Report VK_KHR_swapchain as the only device extension */
    static const VkExtensionProperties deviceExts[] = {
        { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION },
    };
    uint32_t count = sizeof(deviceExts) / sizeof(deviceExts[0]);

    if (pProperties == NULL)
    {
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pPropertyCount < count ? *pPropertyCount : count;
    memcpy(pProperties, deviceExts, toCopy * sizeof(VkExtensionProperties));
    *pPropertyCount = toCopy;

    return (toCopy < count) ? VK_INCOMPLETE : VK_SUCCESS;
}

/****************************************************************************/
/* GetDeviceQueue2                                                          */
/****************************************************************************/

void swvk_GetDeviceQueue2(VkDevice device,
                            const VkDeviceQueueInfo2 *pQueueInfo,
                            VkQueue *pQueue)
{
    /* Forward to existing GetDeviceQueue, ignoring flags in pQueueInfo */
    swvk_GetDeviceQueue(device, pQueueInfo->queueFamilyIndex,
                         pQueueInfo->queueIndex, pQueue);
}
