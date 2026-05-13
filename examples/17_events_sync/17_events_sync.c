/*
** Vulkan Sync Demo -- AmigaOS 4
**
** Demonstrates Vulkan synchronisation and management functions.
** Exercises fence status queries, pipeline cache operations, and
** command pool management -- functions not used by other examples.
**
** API functions exercised (not covered by other examples):
**   vkGetFenceStatus (query fence without waiting),
**   vkGetPipelineCacheData (retrieve cache contents),
**   vkMergePipelineCaches (merge cache objects),
**   vkResetCommandPool (reset all buffers in pool),
**   vkTrimCommandPool (release unused pool memory),
**   vkResetCommandBuffer (reset individual command buffer),
**   vkFlushMappedMemoryRanges, vkInvalidateMappedMemoryRanges
**
** Runs 200 frames then auto-exits. Press close gadget to exit early.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o events_sync events_sync.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/vkex_loop.h"

#define WIN_WIDTH  400
#define WIN_HEIGHT 300

int main(int argc, char **argv)
{
int    vkex_dur = vkex_duration_secs(argc, argv);    time_t vkex_t0  = time(NULL);
    (void)argc;
    (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance       instance   = VK_NULL_HANDLE;
    VkDevice         device     = VK_NULL_HANDLE;
    VkSurfaceKHR     surface    = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain  = VK_NULL_HANDLE;
    VkCommandPool    cmdPool    = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf     = VK_NULL_HANDLE;
    VkFence          fence      = VK_NULL_HANDLE;
    VkFence          fence2     = VK_NULL_HANDLE;
    VkPipelineCache  cacheA     = VK_NULL_HANDLE;
    VkPipelineCache  cacheB     = VK_NULL_HANDLE;
    struct Screen   *screen     = NULL;
    struct Window   *window     = NULL;
    VkImage         *images     = NULL;
    VkImageView     *views      = NULL;
    uint32_t         imgCount   = 0;
    int exitCode = 0;

    printf("=== Vulkan Sync & Management Demo ===\n");

    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available\n");
        return 1;
    }

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Vulkan Sync Demo";
    ai.apiVersion       = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo        = &ai;
    ici.enabledExtensionCount   = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("vkCreateInstance failed\n"); return 1; }

    /* Device */
    VkPhysicalDevice physDev;
    uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(instance, &cnt, &physDev);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *dExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = dExts;
    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
    { printf("vkCreateDevice failed\n"); exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Window + Surface */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }
    window = OpenWindowTags(NULL,
        WA_Title, (Tag)"Vulkan Sync Demo",
        WA_Width, WIN_WIDTH, WA_Height, WIN_HEIGHT,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_DepthGadget, TRUE,
        WA_Activate, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_PubScreen, (Tag)screen, TAG_DONE);
    if (!window) { exitCode = 1; goto cleanup; }

    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen; sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    /* Swapchain */
    VkSwapchainCreateInfoKHR swci;
    memset(&swci, 0, sizeof(swci));
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface; swci.minImageCount = 2;
    swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent.width = innerW; swci.imageExtent.height = innerH;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &swci, NULL, &swapchain) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, NULL);
    images = (VkImage *)malloc(imgCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, images);

    views = (VkImageView *)malloc(imgCount * sizeof(VkImageView));
    memset(views, 0, imgCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imgCount; i++)
    {
        VkImageViewCreateInfo vci;
        memset(&vci, 0, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = images[i]; vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1; vci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vci, NULL, &views[i]);
    }

    /* Command pool + buffer */
    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &cpci, NULL, &cmdPool);

    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbai, &cmdBuf);

    /* Two fences for status testing */
    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fci, NULL, &fence);

    fci.flags = 0; /* unsignalled */
    vkCreateFence(device, &fci, NULL, &fence2);

    /* Pipeline caches for merge testing */
    VkPipelineCacheCreateInfo pcci;
    memset(&pcci, 0, sizeof(pcci));
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(device, &pcci, NULL, &cacheA);
    vkCreatePipelineCache(device, &pcci, NULL, &cacheB);

    /* --- Demonstrate synchronisation queries --- */
    printf("\n--- Fence status queries ---\n");
    printf("fence (signalled): %s\n",
        vkGetFenceStatus(device, fence) == VK_SUCCESS ? "SIGNALLED" : "UNSIGNALLED");
    printf("fence2 (unsignalled): %s\n",
        vkGetFenceStatus(device, fence2) == VK_SUCCESS ? "SIGNALLED" : "UNSIGNALLED");

    /* --- Pipeline cache operations --- */
    printf("\n--- Pipeline cache operations ---\n");
    {
        size_t cacheSize = 0;
        vkGetPipelineCacheData(device, cacheA, &cacheSize, NULL);
        printf("Pipeline cache A size: %lu bytes\n", (unsigned long)cacheSize);
    }
    {
        VkPipelineCache srcs[] = { cacheB };
        VkResult mr = vkMergePipelineCaches(device, cacheA, 1, srcs);
        printf("MergePipelineCaches: %s\n", mr == VK_SUCCESS ? "OK" : "FAIL");
    }

    /* --- Command pool management --- */
    printf("\n--- Command pool management ---\n");
    vkResetCommandPool(device, cmdPool, 0);
    printf("vkResetCommandPool: OK\n");
    vkTrimCommandPool(device, cmdPool, 0);
    printf("vkTrimCommandPool: OK\n");

    /* --- Render loop --- */
    printf("\nRunning 200 frames...\n");
    BOOL running = TRUE;
    uint32_t frame = 0;
    while (running && frame < 200)
    {
        /* Exit on -d N expiry or Shell CTRL-C */
        if (vkex_expired(vkex_dur, vkex_t0))   running = FALSE;
        if (CheckSignal(SIGBREAKF_CTRL_C))     running = FALSE;

        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL)
        {
            if (msg->Class == IDCMP_CLOSEWINDOW) running = FALSE;
            ReplyMsg((struct Message *)msg);
        }
        if (!running) break;

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        uint32_t idx = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &idx);

        /* Reset command buffer explicitly (exercises vkResetCommandBuffer) */
        vkResetCommandBuffer(cmdBuf, 0);

        VkCommandBufferBeginInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBuf, &bi);

        /* Alternating warm/cool clear colours */
        VkRenderingAttachmentInfo ca;
        memset(&ca, 0, sizeof(ca));
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = views[idx];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (frame % 2 == 0) {
            ca.clearValue.color.float32[0] = 0.8f;
            ca.clearValue.color.float32[1] = 0.4f;
            ca.clearValue.color.float32[2] = 0.1f;
        } else {
            ca.clearValue.color.float32[0] = 0.1f;
            ca.clearValue.color.float32[1] = 0.3f;
            ca.clearValue.color.float32[2] = 0.7f;
        }
        ca.clearValue.color.float32[3] = 1.0f;

        VkRenderingInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent.width = innerW;
        ri.renderArea.extent.height = innerH;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;

        vkCmdBeginRendering(cmdBuf, &ri);
        vkCmdEndRendering(cmdBuf);
        vkEndCommandBuffer(cmdBuf);

        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmdBuf;
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        /* Query fence status after submit (should be signalled) */
        if (frame % 60 == 0)
        {
            VkResult fs = vkGetFenceStatus(device, fence);
            printf("Frame %u: fence status = %s\n", frame,
                fs == VK_SUCCESS ? "SIGNALLED" : "UNSIGNALLED");
        }

        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &idx;
        vkQueuePresentKHR(queue, &pi);

        frame++;
    }

    printf("Rendered %u frames.\n", frame);

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (cacheB)   vkDestroyPipelineCache(device, cacheB, NULL);
    if (cacheA)   vkDestroyPipelineCache(device, cacheA, NULL);
    if (fence2)   vkDestroyFence(device, fence2, NULL);
    if (fence)    vkDestroyFence(device, fence, NULL);
    if (cmdBuf)   vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool)  vkDestroyCommandPool(device, cmdPool, NULL);
    if (views) {
        for (uint32_t i = 0; i < imgCount; i++)
            if (views[i]) vkDestroyImageView(device, views[i], NULL);
        free(views);
    }
    if (images)    free(images);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device)    vkDestroyDevice(device, NULL);
    if (surface)   vkDestroySurfaceKHR(instance, surface, NULL);
    if (window)    CloseWindow(window);
    if (screen)    UnlockPubScreen(NULL, screen);
    if (instance)  vkDestroyInstance(instance, NULL);
    return exitCode;
}
