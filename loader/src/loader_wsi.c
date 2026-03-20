/*
** vulkan.library -- Vulkan Loader for AmigaOS 4
**
** loader_wsi.c -- VK_AMIGA_surface implementation.
** Surfaces are owned by the loader, not the ICD.
** Swapchains are forwarded to the ICD (populated in VulkanIFace).
*/

#include <exec/types.h>
#include <interfaces/exec.h>
#include <intuition/intuition.h>
#include <string.h>

#include "loader_internal.h"

/****************************************************************************/

VkResult APICALL Loader_vkCreateAmigaSurfaceAMIGA(
    struct VulkanIFace *Self,
    VkInstance instance,
    const VkAmigaSurfaceCreateInfoAMIGA *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSurfaceKHR *pSurface)
{
    (void)Self;
    (void)instance;
    (void)pAllocator;

    if (!pCreateInfo || !pSurface)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!pCreateInfo->pScreen)
        return VK_ERROR_INITIALIZATION_FAILED;

    struct LoaderSurface *surface = (struct LoaderSurface *)
        IExec->AllocVecTags(sizeof(struct LoaderSurface),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

    if (!surface)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    surface->screen = pCreateInfo->pScreen;
    surface->window = pCreateInfo->pWindow;

    if (pCreateInfo->pWindow)
    {
        /* Store inner (renderable) dimensions, excluding window borders.
        ** Use signed arithmetic to prevent underflow on tiny windows. */
        struct Window *w = pCreateInfo->pWindow;
        int32_t innerW = (int32_t)w->Width  - w->BorderLeft - w->BorderRight;
        int32_t innerH = (int32_t)w->Height - w->BorderTop  - w->BorderBottom;
        surface->width  = (innerW > 0) ? (uint32_t)innerW : 1;
        surface->height = (innerH > 0) ? (uint32_t)innerH : 1;
    }
    else
    {
        /* Fullscreen -- use screen dimensions */
        surface->width  = pCreateInfo->pScreen->Width;
        surface->height = pCreateInfo->pScreen->Height;
    }

    *pSurface = (VkSurfaceKHR)(uintptr_t)surface;

    IExec->DebugPrintF("[vulkan.library] Surface created: %lux%lu (%s)\n",
        (unsigned long)surface->width, (unsigned long)surface->height,
        pCreateInfo->pWindow ? "windowed" : "fullscreen");

    return VK_SUCCESS;
}

void APICALL Loader_vkDestroySurfaceKHR(
    struct VulkanIFace *Self,
    VkInstance instance,
    VkSurfaceKHR surface,
    const VkAllocationCallbacks *pAllocator)
{
    (void)Self;
    (void)instance;
    (void)pAllocator;

    if (surface != VK_NULL_HANDLE)
    {
        struct LoaderSurface *s = (struct LoaderSurface *)(uintptr_t)surface;
        IExec->FreeVec(s);

        IExec->DebugPrintF("[vulkan.library] Surface destroyed\n");
    }
}

VkBool32 APICALL Loader_vkGetPhysicalDeviceAmigaPresentationSupportAMIGA(
    struct VulkanIFace *Self,
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    struct Screen *screen)
{
    (void)Self;
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)screen;

    /* All queue families support presentation on AmigaOS */
    return VK_TRUE;
}
