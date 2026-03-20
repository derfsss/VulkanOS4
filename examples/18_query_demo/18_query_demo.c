/*
** Vulkan Query Demo -- AmigaOS 4
**
** Demonstrates query pools and timestamp queries. Renders a clear
** frame (dark blue) and measures the time using timestamp queries,
** printing the results periodically.
**
** API functions exercised (not covered by other examples):
**   vkCreateQueryPool (VK_QUERY_TYPE_TIMESTAMP)
**   vkDestroyQueryPool
**   vkResetQueryPool (host reset)
**   vkCmdWriteTimestamp (before and after rendering)
**   vkGetQueryPoolResults (retrieve timestamps)
**
** Press close gadget or wait 300 frames to exit.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o query_demo query_demo.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define WIN_WIDTH  640
#define WIN_HEIGHT 480

/* Query pool constants -- may not be in SDK headers yet */
#ifndef VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
#define VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO 11
#endif

#ifndef VK_QUERY_RESULT_64_BIT
#define VK_QUERY_RESULT_64_BIT 0x00000001
#endif

/* Function pointer types for query pool operations */
typedef VkResult (VKAPI_PTR *PFN_vkCreateQueryPool)(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool);
typedef void     (VKAPI_PTR *PFN_vkDestroyQueryPool)(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator);
typedef void     (VKAPI_PTR *PFN_vkResetQueryPool)(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
typedef void     (VKAPI_PTR *PFN_vkCmdWriteTimestamp)(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);
typedef VkResult (VKAPI_PTR *PFN_vkGetQueryPoolResults)(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance       instance    = VK_NULL_HANDLE;
    VkDevice         device      = VK_NULL_HANDLE;
    VkSurfaceKHR     surface     = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain   = VK_NULL_HANDLE;
    VkCommandPool    cmdPool     = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf      = VK_NULL_HANDLE;
    VkFence          fence       = VK_NULL_HANDLE;
    VkQueryPool      queryPool   = VK_NULL_HANDLE;
    struct Screen   *screen      = NULL;
    struct Window   *window      = NULL;
    VkImage         *swapImages  = NULL;
    VkImageView     *swapViews   = NULL;
    uint32_t         imageCount  = 0;
    int exitCode = 0;

    /* Query pool function pointers (resolved via vkGetDeviceProcAddr) */
    PFN_vkCreateQueryPool      pfnCreateQueryPool      = NULL;
    PFN_vkDestroyQueryPool     pfnDestroyQueryPool     = NULL;
    PFN_vkResetQueryPool       pfnResetQueryPool       = NULL;
    PFN_vkCmdWriteTimestamp    pfnCmdWriteTimestamp     = NULL;
    PFN_vkGetQueryPoolResults  pfnGetQueryPoolResults   = NULL;

    printf("=== Vulkan Query Demo ===\n");

    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available\n");
        return 1;
    }

    /* Show library info */
    {
        uint32_t ver = 0;
        vkEnumerateInstanceVersion(&ver);
        printf("vulkan.library: Vulkan %u.%u.%u\n",
            VK_API_VERSION_MAJOR(ver), VK_API_VERSION_MINOR(ver), VK_API_VERSION_PATCH(ver));
    }

    /* Create instance */
    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Query Demo";
    appInfo.apiVersion       = VK_API_VERSION_1_3;

    const char *exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo        = &appInfo;
    ici.enabledExtensionCount   = 2;
    ici.ppEnabledExtensionNames = exts;

    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    {
        printf("ERROR: vkCreateInstance failed\n");
        return 1;
    }

    /* Get physical device and create logical device */
    VkPhysicalDevice physDev;
    uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(instance, &cnt, &physDev);

    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDev, &props);
        printf("ICD: %s (driver %u.%u.%u, API %u.%u.%u)\n",
            props.deviceName,
            VK_API_VERSION_MAJOR(props.driverVersion),
            VK_API_VERSION_MINOR(props.driverVersion),
            VK_API_VERSION_PATCH(props.driverVersion),
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion));
        printf("Timestamp period: %.1f ns\n", props.limits.timestampPeriod);
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
    {
        printf("ERROR: vkCreateDevice failed\n");
        exitCode = 1;
        goto cleanup;
    }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Resolve query pool function pointers */
    pfnCreateQueryPool     = (PFN_vkCreateQueryPool)    vkGetDeviceProcAddr(device, "vkCreateQueryPool");
    pfnDestroyQueryPool    = (PFN_vkDestroyQueryPool)   vkGetDeviceProcAddr(device, "vkDestroyQueryPool");
    pfnResetQueryPool      = (PFN_vkResetQueryPool)     vkGetDeviceProcAddr(device, "vkResetQueryPool");
    pfnCmdWriteTimestamp   = (PFN_vkCmdWriteTimestamp)  vkGetDeviceProcAddr(device, "vkCmdWriteTimestamp");
    pfnGetQueryPoolResults = (PFN_vkGetQueryPoolResults) vkGetDeviceProcAddr(device, "vkGetQueryPoolResults");

    if (!pfnCreateQueryPool || !pfnDestroyQueryPool || !pfnResetQueryPool ||
        !pfnCmdWriteTimestamp || !pfnGetQueryPoolResults)
    {
        printf("ERROR: Query pool functions not available in ICD\n");
        printf("  vkCreateQueryPool:     %s\n", pfnCreateQueryPool     ? "OK" : "MISSING");
        printf("  vkDestroyQueryPool:    %s\n", pfnDestroyQueryPool    ? "OK" : "MISSING");
        printf("  vkResetQueryPool:      %s\n", pfnResetQueryPool      ? "OK" : "MISSING");
        printf("  vkCmdWriteTimestamp:   %s\n", pfnCmdWriteTimestamp   ? "OK" : "MISSING");
        printf("  vkGetQueryPoolResults: %s\n", pfnGetQueryPoolResults ? "OK" : "MISSING");
        exitCode = 1;
        goto cleanup;
    }
    printf("Query pool functions resolved via vkGetDeviceProcAddr\n");

    /* Open window */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Query Demo",
        WA_Width,       WIN_WIDTH,
        WA_Height,      WIN_HEIGHT,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   (Tag)screen,
        TAG_DONE);
    if (!window) { exitCode = 1; goto cleanup; }

    /* Create surface */
    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType   = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen;
    sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    {
        exitCode = 1;
        goto cleanup;
    }

    /* Create swapchain */
    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    VkSwapchainCreateInfoKHR swci;
    memset(&swci, 0, sizeof(swci));
    swci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface          = surface;
    swci.minImageCount    = 2;
    swci.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
    swci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent.width  = innerW;
    swci.imageExtent.height = innerH;
    swci.imageArrayLayers = 1;
    swci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    swci.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &swci, NULL, &swapchain) != VK_SUCCESS)
    {
        exitCode = 1;
        goto cleanup;
    }

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
    swapImages = (VkImage *)malloc(imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapImages);

    /* Create image views */
    swapViews = (VkImageView *)malloc(imageCount * sizeof(VkImageView));
    memset(swapViews, 0, imageCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo vci;
        memset(&vci, 0, sizeof(vci));
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = swapImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vci, NULL, &swapViews[i]);
    }

    /* Command pool and buffer */
    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &cpci, NULL, &cmdPool);

    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbai, &cmdBuf);

    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fci, NULL, &fence);

    /* Create query pool with 2 timestamp queries */
    {
        VkQueryPoolCreateInfo qpci;
        memset(&qpci, 0, sizeof(qpci));
        qpci.sType      = (VkStructureType)VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType   = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount  = 2;

        VkResult qpResult = pfnCreateQueryPool(device, &qpci, NULL, &queryPool);
        if (qpResult != VK_SUCCESS)
        {
            printf("ERROR: vkCreateQueryPool failed (%d)\n", qpResult);
            exitCode = 1;
            goto cleanup;
        }
        printf("Query pool created (type=TIMESTAMP, count=2)\n");
    }

    printf("Rendering with timestamp queries... close window or wait 300 frames.\n");

    /* Render loop */
    uint32_t frame = 0;
    uint64_t timestamps[2] = {0, 0};
    uint64_t lastStart = 0, lastEnd = 0;
    clock_t startClock = clock();
    BOOL running = TRUE;

    while (running && frame < 300)
    {
        /* Check for close gadget (non-blocking) */
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL)
        {
            if (msg->Class == IDCMP_CLOSEWINDOW)
                running = FALSE;
            ReplyMsg((struct Message *)msg);
        }
        if (!running) break;

        /* Host-side reset of query pool (Vulkan 1.2 hostQueryReset) */
        pfnResetQueryPool(device, queryPool, 0, 2);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        uint32_t imgIdx = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &imgIdx);

        /* Record command buffer */
        VkCommandBufferBeginInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBuf, &bi);

        /* Timestamp: start of rendering */
        pfnCmdWriteTimestamp(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);

        /* Clear to dark blue */
        VkRenderingAttachmentInfo ca;
        memset(&ca, 0, sizeof(ca));
        ca.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView   = swapViews[imgIdx];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color.float32[0] = 0.0f;
        ca.clearValue.color.float32[1] = 0.0f;
        ca.clearValue.color.float32[2] = 0.3f;
        ca.clearValue.color.float32[3] = 1.0f;

        VkRenderingInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.sType                    = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent.width  = innerW;
        ri.renderArea.extent.height = innerH;
        ri.layerCount               = 1;
        ri.colorAttachmentCount     = 1;
        ri.pColorAttachments        = &ca;

        vkCmdBeginRendering(cmdBuf, &ri);
        vkCmdEndRendering(cmdBuf);

        /* Timestamp: end of rendering */
        pfnCmdWriteTimestamp(cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);

        vkEndCommandBuffer(cmdBuf);

        /* Submit */
        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmdBuf;
        vkQueueSubmit(queue, 1, &si, fence);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        /* Retrieve timestamp results */
        VkResult qr = pfnGetQueryPoolResults(
            device, queryPool, 0, 2,
            sizeof(timestamps), timestamps,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        if (qr == VK_SUCCESS)
        {
            lastStart = timestamps[0];
            lastEnd   = timestamps[1];
        }

        /* Print results every 60 frames */
        if (frame > 0 && (frame % 60) == 0)
        {
            printf("Frame %u: timestamp start=%llu, end=%llu",
                frame, (unsigned long long)lastStart, (unsigned long long)lastEnd);
            if (lastEnd > lastStart)
                printf(", delta=%llu", (unsigned long long)(lastEnd - lastStart));
            printf("\n");
        }

        /* Present */
        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType          = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.swapchainCount = 1;
        pi.pSwapchains    = &swapchain;
        pi.pImageIndices  = &imgIdx;
        vkQueuePresentKHR(queue, &pi);

        frame++;
    }

    /* Final summary */
    {
        clock_t endClock = clock();
        double elapsed = (double)(endClock - startClock) / (double)CLOCKS_PER_SEC;
        printf("\n--- Query Demo Results ---\n");
        printf("Rendered %u frames", frame);
        if (elapsed > 0.0 && frame > 0)
            printf(" in %.1f seconds (%.2f FPS)", elapsed, (double)frame / elapsed);
        printf("\n");
        printf("Final timestamps: start=%llu, end=%llu",
            (unsigned long long)lastStart, (unsigned long long)lastEnd);
        if (lastEnd > lastStart)
            printf(", delta=%llu", (unsigned long long)(lastEnd - lastStart));
        printf("\n");
    }

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (queryPool && pfnDestroyQueryPool) pfnDestroyQueryPool(device, queryPool, NULL);
    if (fence)     vkDestroyFence(device, fence, NULL);
    if (cmdBuf)    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool)   vkDestroyCommandPool(device, cmdPool, NULL);
    if (swapViews) {
        for (uint32_t i = 0; i < imageCount; i++)
            if (swapViews[i]) vkDestroyImageView(device, swapViews[i], NULL);
        free(swapViews);
    }
    if (swapImages) free(swapImages);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device)    vkDestroyDevice(device, NULL);
    if (surface)   vkDestroySurfaceKHR(instance, surface, NULL);
    if (window)    CloseWindow(window);
    if (screen)    UnlockPubScreen(NULL, screen);
    if (instance)  vkDestroyInstance(instance, NULL);

    printf("Cleanup complete\n");
    return exitCode;
}
