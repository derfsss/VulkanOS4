#ifndef VK_HELPERS_H
#define VK_HELPERS_H

/*
** vk_helpers.h -- Shared Vulkan example boilerplate for AmigaOS 4
**
** Reduces per-example setup code from ~300 lines to ~50 lines.
** Include this header and call the helper functions for common operations:
**
**   VkHelpers h;
**   if (vkh_Init(&h, "My App", WIN_WIDTH, WIN_HEIGHT) != 0) return 1;
**   // ... create pipeline, record commands, render loop ...
**   vkh_Cleanup(&h);
**
** NOTE: Existing examples are NOT converted to use these helpers.
** New examples can use them for faster development.
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VkHelpers
{
    VkInstance       instance;
    VkDevice         device;
    VkPhysicalDevice physDev;
    VkQueue          queue;
    VkSurfaceKHR     surface;
    VkSwapchainKHR   swapchain;
    VkCommandPool    cmdPool;
    VkCommandBuffer  cmdBuf;
    VkFence          fence;
    struct Screen   *screen;
    struct Window   *window;
    VkImage         *swapImgs;
    VkImageView     *swapViews;
    uint32_t         imgCount;
    uint32_t         innerW;
    uint32_t         innerH;
} VkHelpers;

static int vkh_Init(VkHelpers *h, const char *appName, uint32_t winW, uint32_t winH)
{
    memset(h, 0, sizeof(VkHelpers));

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = appName;
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;

    if (vkCreateInstance(&ici, NULL, &h->instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); return 1; }

    /* Physical device + Device */
    uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(h->instance, &cnt, &h->physDev);

    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(h->physDev, &props);
        printf("ICD: %s\n", props.deviceName);
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *dExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dExts;

    if (vkCreateDevice(h->physDev, &dci, NULL, &h->device) != VK_SUCCESS)
        return 1;
    vkGetDeviceQueue(h->device, 0, 0, &h->queue);

    /* Window + Surface */
    h->screen = LockPubScreen(NULL);
    if (!h->screen) return 1;

    h->window = OpenWindowTags(NULL,
        WA_Title, (Tag)appName,
        WA_Width, winW, WA_Height, winH,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_PubScreen, (Tag)h->screen, TAG_DONE);
    if (!h->window) return 1;

    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = h->screen;
    sci.pWindow = h->window;
    if (vkCreateAmigaSurfaceAMIGA(h->instance, &sci, NULL, &h->surface) != VK_SUCCESS)
        return 1;

    h->innerW = h->window->Width - h->window->BorderLeft - h->window->BorderRight;
    h->innerH = h->window->Height - h->window->BorderTop - h->window->BorderBottom;

    /* Swapchain */
    VkSwapchainCreateInfoKHR swci;
    memset(&swci, 0, sizeof(swci));
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = h->surface;
    swci.minImageCount = 2;
    swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent.width = h->innerW;
    swci.imageExtent.height = h->innerH;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(h->device, &swci, NULL, &h->swapchain) != VK_SUCCESS)
        return 1;

    vkGetSwapchainImagesKHR(h->device, h->swapchain, &h->imgCount, NULL);
    h->swapImgs = (VkImage *)malloc(h->imgCount * sizeof(VkImage));
    if (!h->swapImgs) return 1;
    vkGetSwapchainImagesKHR(h->device, h->swapchain, &h->imgCount, h->swapImgs);

    h->swapViews = (VkImageView *)malloc(h->imgCount * sizeof(VkImageView));
    if (!h->swapViews) return 1;
    memset(h->swapViews, 0, h->imgCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < h->imgCount; i++)
    {
        VkImageViewCreateInfo vci;
        memset(&vci, 0, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = h->swapImgs[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(h->device, &vci, NULL, &h->swapViews[i]) != VK_SUCCESS)
            return 1;
    }

    /* Command pool + buffer + fence */
    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(h->device, &cpci, NULL, &h->cmdPool) != VK_SUCCESS)
        return 1;

    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = h->cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(h->device, &cbai, &h->cmdBuf);

    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(h->device, &fci, NULL, &h->fence) != VK_SUCCESS)
        return 1;

    return 0;
}

static void vkh_Cleanup(VkHelpers *h)
{
    if (h->device) vkDeviceWaitIdle(h->device);
    if (h->fence) vkDestroyFence(h->device, h->fence, NULL);
    if (h->cmdBuf) vkFreeCommandBuffers(h->device, h->cmdPool, 1, &h->cmdBuf);
    if (h->cmdPool) vkDestroyCommandPool(h->device, h->cmdPool, NULL);
    if (h->swapViews) {
        for (uint32_t i = 0; i < h->imgCount; i++)
            if (h->swapViews[i]) vkDestroyImageView(h->device, h->swapViews[i], NULL);
        free(h->swapViews);
    }
    if (h->swapImgs) free(h->swapImgs);
    if (h->swapchain) vkDestroySwapchainKHR(h->device, h->swapchain, NULL);
    if (h->device) vkDestroyDevice(h->device, NULL);
    if (h->surface) vkDestroySurfaceKHR(h->instance, h->surface, NULL);
    if (h->window) CloseWindow(h->window);
    if (h->screen) UnlockPubScreen(NULL, h->screen);
    if (h->instance) vkDestroyInstance(h->instance, NULL);
}

#endif /* VK_HELPERS_H */
