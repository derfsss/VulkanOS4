/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_wsi.c -- WSI surface queries, swapchain, and presentation
**
** The ICD receives VkSurfaceKHR handles from the loader. These are
** pointers to LoaderSurface structs (allocated in loader_wsi.c) that
** contain the Intuition Screen*, Window*, and dimensions.
**
** Presentation uses IGraphics->WritePixelArray to blit the ARGB32
** framebuffer to the Intuition window's RastPort.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <interfaces/graphics.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <string.h>
#include <stdint.h>

#include "swvk_internal.h"

/* Forward declaration of LoaderSurface layout (owned by loader) */
struct LoaderSurface
{
    struct Screen *screen;
    struct Window *window;
    uint32_t       width;
    uint32_t       height;
};

/****************************************************************************/
/* Graphics library interface (opened lazily at first present)              */
/****************************************************************************/

static struct Library     *GraphicsBase = NULL;
static struct GraphicsIFace *IGraphics  = NULL;

static int swvk_OpenGraphics(void)
{
    if (IGraphics)
        return 1;

    GraphicsBase = IExec->OpenLibrary("graphics.library", 54);
    if (!GraphicsBase)
    {
        D(("[software_vk] Cannot open graphics.library\n"));
        return 0;
    }

    IGraphics = (struct GraphicsIFace *)IExec->GetInterface(
        GraphicsBase, "main", 1, NULL);
    if (!IGraphics)
    {
        IExec->CloseLibrary(GraphicsBase);
        GraphicsBase = NULL;
        D(("[software_vk] Cannot get graphics interface\n"));
        return 0;
    }

    return 1;
}

void swvk_CloseGraphics(void)
{
    if (IGraphics)
    {
        IExec->DropInterface((struct Interface *)IGraphics);
        IGraphics = NULL;
    }
    if (GraphicsBase)
    {
        IExec->CloseLibrary(GraphicsBase);
        GraphicsBase = NULL;
    }
}

/****************************************************************************/
/* Surface queries (forwarded from loader to ICD)                           */
/****************************************************************************/

VkResult swvk_GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    VkSurfaceKHR surface,
    VkBool32 *pSupported)
{
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)surface;

    /* Software renderer: all queue families support presentation */
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult swvk_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    (void)physicalDevice;

    struct LoaderSurface *ls = (struct LoaderSurface *)(uintptr_t)surface;

    memset(pSurfaceCapabilities, 0, sizeof(VkSurfaceCapabilitiesKHR));

    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = SWVK_MAX_SWAPCHAIN_IMAGES;

    pSurfaceCapabilities->currentExtent.width  = ls->width;
    pSurfaceCapabilities->currentExtent.height = ls->height;

    pSurfaceCapabilities->minImageExtent.width  = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width  = 4096;
    pSurfaceCapabilities->maxImageExtent.height = 4096;

    pSurfaceCapabilities->maxImageArrayLayers = 1;

    pSurfaceCapabilities->supportedTransforms =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    pSurfaceCapabilities->supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    return VK_SUCCESS;
}

VkResult swvk_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats)
{
    (void)physicalDevice;
    (void)surface;

    /* Support a single format: B8G8R8A8_UNORM in SRGB_NONLINEAR color space.
    ** On big-endian PowerPC, this maps to ARGB32 byte order which matches
    ** AmigaOS RECTFMT_ARGB. */
    const uint32_t formatCount = 1;

    if (pSurfaceFormats == NULL)
    {
        *pSurfaceFormatCount = formatCount;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pSurfaceFormatCount < formatCount
                      ? *pSurfaceFormatCount : formatCount;

    if (toCopy >= 1)
    {
        pSurfaceFormats[0].format     = VK_FORMAT_B8G8R8A8_UNORM;
        pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }

    *pSurfaceFormatCount = toCopy;

    return (toCopy < formatCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult swvk_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes)
{
    (void)physicalDevice;
    (void)surface;

    /* Software renderer supports FIFO (vsync-like, always available per spec)
    ** and IMMEDIATE (no waiting). Both are effectively the same since we
    ** blit synchronously. */
    const uint32_t modeCount = 2;

    if (pPresentModes == NULL)
    {
        *pPresentModeCount = modeCount;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pPresentModeCount < modeCount
                      ? *pPresentModeCount : modeCount;

    if (toCopy >= 1) pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    if (toCopy >= 2) pPresentModes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR;

    *pPresentModeCount = toCopy;

    return (toCopy < modeCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

/****************************************************************************/
/* Swapchain                                                                */
/****************************************************************************/

VkResult swvk_CreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain)
{
    (void)pAllocator;

    SWVKDevice *dev = (SWVKDevice *)device;

    SWVKSwapchain *sc = (SWVKSwapchain *)IExec->AllocVecTags(
        sizeof(SWVKSwapchain),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sc)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    sc->device = dev;

    /* Extract surface info from the loader's LoaderSurface */
    struct LoaderSurface *ls =
        (struct LoaderSurface *)(uintptr_t)pCreateInfo->surface;
    sc->surface = ls;

    sc->width  = pCreateInfo->imageExtent.width;
    sc->height = pCreateInfo->imageExtent.height;
    sc->format = pCreateInfo->imageFormat;

    /* Clamp image count */
    uint32_t imageCount = pCreateInfo->minImageCount;
    if (imageCount < 2) imageCount = 2;
    if (imageCount > SWVK_MAX_SWAPCHAIN_IMAGES)
        imageCount = SWVK_MAX_SWAPCHAIN_IMAGES;
    sc->imageCount = imageCount;

    /* Allocate images and backing memory for each swapchain image */
    uint32_t bytesPerPixel = 4;  /* ARGB32 */
    VkDeviceSize imageSize = (VkDeviceSize)sc->width * sc->height * bytesPerPixel;

    /* Guard against overflow when casting to uint32_t for AllocVecTags */
    if (imageSize > 0xFFFFFFFFULL)
    {
        IExec->FreeVec(sc);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    for (uint32_t i = 0; i < sc->imageCount; i++)
    {
        /* Allocate image struct */
        SWVKImage *img = (SWVKImage *)IExec->AllocVecTags(
            sizeof(SWVKImage),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!img)
            goto fail;

        img->width       = sc->width;
        img->height      = sc->height;
        img->depth       = 1;
        img->mipLevels   = 1;
        img->arrayLayers = 1;
        img->format      = sc->format;
        img->imageType   = VK_IMAGE_TYPE_2D;
        img->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img->usage       = pCreateInfo->imageUsage;
        img->memorySize  = imageSize;

        /* Allocate backing memory */
        SWVKMemory *mem = (SWVKMemory *)IExec->AllocVecTags(
            sizeof(SWVKMemory),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!mem)
        {
            IExec->FreeVec(img);
            goto fail;
        }

        mem->size = imageSize;
        mem->data = IExec->AllocVecTags(
            (uint32_t)imageSize,
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!mem->data)
        {
            IExec->FreeVec(mem);
            IExec->FreeVec(img);
            goto fail;
        }

        img->boundMemory = mem;
        img->boundOffset = 0;

        sc->images[i]  = img;
        sc->memory[i]  = mem;
    }

    sc->currentImage = 0;

    *pSwapchain = (VkSwapchainKHR)(uintptr_t)sc;

    D(("[software_vk] Swapchain created: %lux%lu, %lu images\n",
        (unsigned long)sc->width, (unsigned long)sc->height,
        (unsigned long)sc->imageCount));

    return VK_SUCCESS;

fail:
    /* Clean up any partially allocated images */
    for (uint32_t j = 0; j < SWVK_MAX_SWAPCHAIN_IMAGES; j++)
    {
        if (sc->memory[j])
        {
            if (sc->memory[j]->data)
                IExec->FreeVec(sc->memory[j]->data);
            IExec->FreeVec(sc->memory[j]);
        }
        if (sc->images[j])
            IExec->FreeVec(sc->images[j]);
    }
    IExec->FreeVec(sc);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void swvk_DestroySwapchainKHR(VkDevice device,
                                VkSwapchainKHR swapchain,
                                const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (swapchain == VK_NULL_HANDLE)
        return;

    SWVKSwapchain *sc = (SWVKSwapchain *)(uintptr_t)swapchain;

    for (uint32_t i = 0; i < sc->imageCount; i++)
    {
        if (sc->memory[i])
        {
            if (sc->memory[i]->data)
                IExec->FreeVec(sc->memory[i]->data);
            IExec->FreeVec(sc->memory[i]);
        }
        if (sc->images[i])
            IExec->FreeVec(sc->images[i]);
    }

    IExec->FreeVec(sc);

    D(("[software_vk] Swapchain destroyed\n"));
}

VkResult swvk_GetSwapchainImagesKHR(VkDevice device,
                                     VkSwapchainKHR swapchain,
                                     uint32_t *pSwapchainImageCount,
                                     VkImage *pSwapchainImages)
{
    (void)device;

    SWVKSwapchain *sc = (SWVKSwapchain *)(uintptr_t)swapchain;

    if (pSwapchainImages == NULL)
    {
        *pSwapchainImageCount = sc->imageCount;
        return VK_SUCCESS;
    }

    uint32_t toCopy = *pSwapchainImageCount < sc->imageCount
                      ? *pSwapchainImageCount : sc->imageCount;

    for (uint32_t i = 0; i < toCopy; i++)
        pSwapchainImages[i] = (VkImage)(uintptr_t)sc->images[i];

    *pSwapchainImageCount = toCopy;

    return (toCopy < sc->imageCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult swvk_AcquireNextImageKHR(VkDevice device,
                                   VkSwapchainKHR swapchain,
                                   uint64_t timeout,
                                   VkSemaphore semaphore,
                                   VkFence fence,
                                   uint32_t *pImageIndex)
{
    (void)device;
    (void)timeout;

    SWVKSwapchain *sc = (SWVKSwapchain *)(uintptr_t)swapchain;

    /* Round-robin image selection */
    *pImageIndex = sc->currentImage;
    sc->currentImage = (sc->currentImage + 1) % sc->imageCount;

    /* Signal the semaphore (no-op for software renderer) */
    (void)semaphore;

    /* Signal the fence if provided */
    if (fence != VK_NULL_HANDLE)
    {
        SWVKFence *f = (SWVKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }

    return VK_SUCCESS;
}

/****************************************************************************/
/* Presentation (blit to Intuition window)                                  */
/****************************************************************************/

VkResult swvk_QueuePresentKHR(VkQueue queue,
                               const VkPresentInfoKHR *pPresentInfo)
{
    (void)queue;

    if (!pPresentInfo || !pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices)
        return VK_ERROR_UNKNOWN;

    /* Open graphics library lazily */
    if (!swvk_OpenGraphics())
        return VK_ERROR_DEVICE_LOST;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
    {
        SWVKSwapchain *sc = (SWVKSwapchain *)(uintptr_t)
            pPresentInfo->pSwapchains[i];
        uint32_t idx = pPresentInfo->pImageIndices[i];

        if (idx >= sc->imageCount)
            continue;

        SWVKImage *img = sc->images[idx];
        if (!img || !img->boundMemory || !img->boundMemory->data)
            continue;

        uint8_t *pixels = (uint8_t *)img->boundMemory->data + img->boundOffset;

        /* Get the window's RastPort for blitting */
        struct Window *win = sc->surface->window;
        if (!win)
            continue;

        /* Blit ARGB32 framebuffer to Intuition window.
        ** AmigaOS 4 graphics.library v54 WritePixelArray signature:
        ** (src, srcX, srcY, srcMod, srcFormat, rp, destX, destY, sizeX, sizeY) */
        IGraphics->WritePixelArray(
            pixels,                     /* source data */
            0, 0,                       /* source x, y */
            sc->width * 4,              /* source bytes per row */
            PIXF_A8R8G8B8,             /* pixel format (graphics.library PIX_FMT) */
            win->RPort,                 /* destination RastPort */
            win->BorderLeft,            /* dest x (inside window border) */
            win->BorderTop,             /* dest y (inside window border) */
            sc->width, sc->height       /* blit size */
        );

        /* Report per-swapchain result if requested */
        if (pPresentInfo->pResults)
            pPresentInfo->pResults[i] = VK_SUCCESS;
    }

    return VK_SUCCESS;
}
