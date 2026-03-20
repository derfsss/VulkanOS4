/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_wsi.c -- Window System Integration via OGLES2
**
** Uses ogles2.library for context and presentation.
** aglCreateContextTags2 creates the GL context from a window.
** aglSwapBuffers handles double-buffered presentation.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <graphics/gfx.h>
#include <interfaces/graphics.h>
#include <intuition/intuition.h>
#include <string.h>
#include <stdint.h>

#include "ogles2vk_internal.h"

/* Minimal OGLES2IFace -- just the methods we need for WSI */
struct OGLES2IFace_WSI
{
    struct InterfaceData Data;
    uint32 APICALL (*Obtain)(void *Self);
    uint32 APICALL (*Release)(void *Self);
    void   APICALL (*Expunge)(void *Self);
    void * APICALL (*Clone)(void *Self);
    void * APICALL (*aglCreateContext_AVOID)(void *Self, uint32 *errcode, struct TagItem *tags);
    void * APICALL (*aglCreateContextTags_AVOID)(void *Self, uint32 *errcode, ...);
    void   APICALL (*aglDestroyContext)(void *Self, void *context);
    void   APICALL (*aglMakeCurrent)(void *Self, void *context);
    void   APICALL (*aglSwapBuffers)(void *Self);
};
extern void *IOGLES2;

/* Graphics library for fallback presentation */
static struct Library       *GfxBase    = NULL;
static struct GraphicsIFace *IGraphics  = NULL;

static int ogles2vk_OpenGraphics(void)
{
    if (IGraphics)
        return 1;

    GfxBase = IExec->OpenLibrary("graphics.library", 54);
    if (!GfxBase) return 0;

    IGraphics = (struct GraphicsIFace *)IExec->GetInterface(GfxBase, "main", 1, NULL);
    if (!IGraphics)
    {
        IExec->CloseLibrary(GfxBase);
        GfxBase = NULL;
        return 0;
    }
    return 1;
}

void ogles2vk_CloseGraphics(void)
{
    if (IGraphics) { IExec->DropInterface((struct Interface *)IGraphics); IGraphics = NULL; }
    if (GfxBase)   { IExec->CloseLibrary(GfxBase); GfxBase = NULL; }
}

/****************************************************************************/
/* Surface queries                                                          */
/****************************************************************************/

VkResult ogles2vk_GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physDev, uint32_t queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32 *pSupported)
{
    (void)physDev; (void)queueFamilyIndex; (void)surface;
    if (!pSupported) return VK_ERROR_INITIALIZATION_FAILED;
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult ogles2vk_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physDev, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pCapabilities)
{
    (void)physDev;
    if (!pCapabilities || surface == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    struct LoaderSurface *ls = (struct LoaderSurface *)(uintptr_t)surface;

    pCapabilities->minImageCount = 2;
    pCapabilities->maxImageCount = OGLES2VK_MAX_SWAPCHAIN_IMAGES;
    pCapabilities->currentExtent.width  = ls->width;
    pCapabilities->currentExtent.height = ls->height;
    pCapabilities->minImageExtent.width  = 1;
    pCapabilities->minImageExtent.height = 1;
    pCapabilities->maxImageExtent.width  = g_physDevice.properties.limits.maxFramebufferWidth;
    pCapabilities->maxImageExtent.height = g_physDevice.properties.limits.maxFramebufferHeight;
    pCapabilities->maxImageArrayLayers  = 1;
    pCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCapabilities->currentTransform    = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    return VK_SUCCESS;
}

VkResult ogles2vk_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physDev, VkSurfaceKHR surface,
    uint32_t *pCount, VkSurfaceFormatKHR *pFormats)
{
    (void)physDev; (void)surface;
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pFormats) { *pCount = 1; return VK_SUCCESS; }
    if (*pCount < 1) { *pCount = 1; return VK_INCOMPLETE; }
    pFormats[0].format     = VK_FORMAT_B8G8R8A8_UNORM;
    pFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    *pCount = 1;
    return VK_SUCCESS;
}

VkResult ogles2vk_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physDev, VkSurfaceKHR surface,
    uint32_t *pCount, VkPresentModeKHR *pModes)
{
    (void)physDev; (void)surface;
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pModes) { *pCount = 2; return VK_SUCCESS; }
    uint32_t toCopy = *pCount < 2 ? *pCount : 2;
    if (toCopy >= 1) pModes[0] = VK_PRESENT_MODE_FIFO_KHR;
    if (toCopy >= 2) pModes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR;
    *pCount = toCopy;
    return (toCopy < 2) ? VK_INCOMPLETE : VK_SUCCESS;
}

/****************************************************************************/
/* Swapchain creation                                                       */
/****************************************************************************/

VkResult ogles2vk_CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    (void)pAllocator;

    if (!device || !pCreateInfo || !pSwapchain)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKDevice *dev = (OGLES2VKDevice *)device;

    OGLES2VKSwapchain *sc = (OGLES2VKSwapchain *)IExec->AllocVecTags(
        sizeof(OGLES2VKSwapchain),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sc)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    sc->device  = dev;
    sc->surface = (struct LoaderSurface *)(uintptr_t)pCreateInfo->surface;
    sc->width   = pCreateInfo->imageExtent.width;
    sc->height  = pCreateInfo->imageExtent.height;
    sc->format  = pCreateInfo->imageFormat;

    uint32_t imageCount = pCreateInfo->minImageCount;
    if (imageCount < 2) imageCount = 2;
    if (imageCount > OGLES2VK_MAX_SWAPCHAIN_IMAGES)
        imageCount = OGLES2VK_MAX_SWAPCHAIN_IMAGES;
    sc->imageCount = imageCount;

    /* Initialise OGLES2 context if not already done */
    if (!dev->glContext && sc->surface && sc->surface->window)
    {
        if (!ogles2vk_InitOGLES2Context(dev, sc->surface->window))
        {
            IExec->DebugPrintF("[ogles2_vk] WARNING: OGLES2 context init failed\n");
        }
    }

    /* Allocate swapchain image metadata (for Vulkan API tracking) */
    VkDeviceSize imageSize = (VkDeviceSize)sc->width * sc->height * 4;
    if (imageSize > 0xFFFFFFFFULL)
    {
        IExec->FreeVec(sc);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    for (uint32_t i = 0; i < sc->imageCount; i++)
    {
        OGLES2VKImage *img = (OGLES2VKImage *)IExec->AllocVecTags(
            sizeof(OGLES2VKImage), AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0, TAG_DONE);
        if (!img) goto fail;

        img->width = sc->width;
        img->height = sc->height;
        img->depth = 1;
        img->mipLevels = 1;
        img->arrayLayers = 1;
        img->format = sc->format;
        img->imageType = VK_IMAGE_TYPE_2D;
        img->usage = pCreateInfo->imageUsage;
        img->memorySize = imageSize;

        OGLES2VKMemory *mem = (OGLES2VKMemory *)IExec->AllocVecTags(
            sizeof(OGLES2VKMemory), AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0, TAG_DONE);
        if (!mem) { IExec->FreeVec(img); goto fail; }

        mem->size = imageSize;
        mem->data = IExec->AllocVecTags((uint32_t)imageSize,
            AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
        if (!mem->data) { IExec->FreeVec(mem); IExec->FreeVec(img); goto fail; }

        img->boundMemory = mem;
        img->boundOffset = 0;
        sc->images[i] = img;
        sc->memory[i] = mem;
    }

    sc->currentImage = 0;
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)sc;

    IExec->DebugPrintF("[ogles2_vk] Swapchain created: %lux%lu, %lu images (OGLES2)\n",
        (unsigned long)sc->width, (unsigned long)sc->height,
        (unsigned long)sc->imageCount);

    return VK_SUCCESS;

fail:
    for (uint32_t j = 0; j < OGLES2VK_MAX_SWAPCHAIN_IMAGES; j++)
    {
        if (sc->memory[j]) {
            if (sc->memory[j]->data) IExec->FreeVec(sc->memory[j]->data);
            IExec->FreeVec(sc->memory[j]);
        }
        if (sc->images[j]) IExec->FreeVec(sc->images[j]);
    }
    IExec->FreeVec(sc);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void ogles2vk_DestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (swapchain == VK_NULL_HANDLE) return;

    OGLES2VKSwapchain *sc = (OGLES2VKSwapchain *)(uintptr_t)swapchain;

    for (uint32_t i = 0; i < OGLES2VK_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (sc->memory[i]) {
            if (sc->memory[i]->data) IExec->FreeVec(sc->memory[i]->data);
            IExec->FreeVec(sc->memory[i]);
        }
        if (sc->images[i]) IExec->FreeVec(sc->images[i]);
    }
    IExec->FreeVec(sc);
}

/****************************************************************************/
/* Image management                                                         */
/****************************************************************************/

VkResult ogles2vk_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
    uint32_t *pCount, VkImage *pImages)
{
    (void)device;
    if (!pCount || swapchain == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKSwapchain *sc = (OGLES2VKSwapchain *)(uintptr_t)swapchain;
    if (!pImages) { *pCount = sc->imageCount; return VK_SUCCESS; }

    uint32_t toCopy = *pCount < sc->imageCount ? *pCount : sc->imageCount;
    for (uint32_t i = 0; i < toCopy; i++)
        pImages[i] = (VkImage)(uintptr_t)sc->images[i];
    *pCount = toCopy;
    return (toCopy < sc->imageCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult ogles2vk_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
    (void)device; (void)timeout; (void)semaphore;
    if (!pImageIndex || swapchain == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKSwapchain *sc = (OGLES2VKSwapchain *)(uintptr_t)swapchain;
    *pImageIndex = sc->currentImage;
    sc->currentImage = (sc->currentImage + 1) % sc->imageCount;

    if (fence != VK_NULL_HANDLE)
    {
        OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }
    return VK_SUCCESS;
}

/****************************************************************************/
/* Presentation via OGLES2 aglSwapBuffers                                   */
/****************************************************************************/

VkResult ogles2vk_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    (void)queue;

    if (!pPresentInfo || !pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices)
        return VK_ERROR_UNKNOWN;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
    {
        OGLES2VKSwapchain *sc = (OGLES2VKSwapchain *)(uintptr_t)
            pPresentInfo->pSwapchains[i];

        if (!sc || !sc->device)
            continue;

        /* Present via OGLES2 -- handles double-buffering internally */
        if (sc->device->glContext && IOGLES2)
        {
            struct OGLES2IFace_WSI *gl = (struct OGLES2IFace_WSI *)IOGLES2;
            gl->aglSwapBuffers();
        }
        else if (ogles2vk_OpenGraphics())
        {
            /* Fallback: blit from CPU memory (software path) */
            uint32_t idx = pPresentInfo->pImageIndices[i];
            if (idx < sc->imageCount && sc->images[idx] &&
                sc->images[idx]->boundMemory && sc->images[idx]->boundMemory->data)
            {
                struct Window *win = sc->surface ?
                    (struct Window *)sc->surface->window : NULL;
                if (win)
                {
                    uint8_t *pixels = (uint8_t *)sc->images[idx]->boundMemory->data
                                    + sc->images[idx]->boundOffset;
                    IGraphics->WritePixelArray(pixels, 0, 0,
                        sc->width * 4, PIXF_A8R8G8B8,
                        win->RPort, win->BorderLeft, win->BorderTop,
                        sc->width, sc->height);
                }
            }
        }

        if (pPresentInfo->pResults)
            pPresentInfo->pResults[i] = VK_SUCCESS;
    }

    return VK_SUCCESS;
}
