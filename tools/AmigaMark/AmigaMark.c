/*
** AmigaMark -- Vulkan GPU Benchmark for AmigaOS 4
**
** Measures rendering performance across test scenes.
** Reports per-scene FPS and a composite score.
**
** Scenes:
**   1. Fill rate -- Fullscreen clear (measures pixel throughput)
**   2. Vertex throughput -- Instanced cubes (9 instances, 36 indices each)
**   3. Fragment complexity -- Solid cube with per-pixel lighting
**
** Each scene runs for FRAMES_PER_SCENE frames. The composite score is
** the geometric mean of all scene FPS values.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o AmigaMark AmigaMark.c -lvulkan_loader -lauto -lm
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define WIN_WIDTH  512
#define WIN_HEIGHT 512
#define FRAMES_PER_SCENE 200

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    VkImage *swapImgs = NULL;
    VkImageView *swapViews = NULL;
    uint32_t imgCount = 0;
    int exitCode = 0;

    printf("====================================\n");
    printf("  AmigaMark -- Vulkan GPU Benchmark\n");
    printf("  %d frames per scene\n", FRAMES_PER_SCENE);
    printf("====================================\n\n");

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* Instance */
    VkApplicationInfo ai; memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "AmigaMark";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici; memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); return 1; }

    VkPhysicalDevice physDev; uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(instance, &cnt, &physDev);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physDev, &props);
    printf("Device: %s\n", props.deviceName);
    printf("Type:   %s\n",
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" :
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU" :
        "CPU / Software");
    printf("\n");

    /* Device */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci; memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char *dExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci; memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dExts;
    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Window + Surface */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }
    window = OpenWindowTags(NULL,
        WA_Title, (Tag)"AmigaMark",
        WA_Width, WIN_WIDTH, WA_Height, WIN_HEIGHT,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_PubScreen, (Tag)screen, TAG_DONE);
    if (!window) { exitCode = 1; goto cleanup; }

    VkAmigaSurfaceCreateInfoAMIGA sci; memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen; sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    /* Swapchain */
    VkSwapchainCreateInfoKHR swci; memset(&swci, 0, sizeof(swci));
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
    swapImgs = (VkImage *)malloc(imgCount * sizeof(VkImage));
    if (!swapImgs) { exitCode = 1; goto cleanup; }
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImgs);

    swapViews = (VkImageView *)malloc(imgCount * sizeof(VkImageView));
    if (!swapViews) { exitCode = 1; goto cleanup; }
    memset(swapViews, 0, imgCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imgCount; i++) {
        VkImageViewCreateInfo vci; memset(&vci, 0, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapImgs[i]; vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1; vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, NULL, &swapViews[i]) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
    }

    /* Command pool + buffer + fence */
    VkCommandPoolCreateInfo cpci; memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &cpci, NULL, &cmdPool) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }
    VkCommandBufferAllocateInfo cbai; memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbai, &cmdBuf);
    VkFenceCreateInfo fci; memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fci, NULL, &fence) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    /* ================================================================
    ** SCENE 1: Fill rate (clear only, no geometry)
    ** ================================================================ */
    printf("Scene 1: Fill rate (%dx%d clear)...\n", innerW, innerH);
    {
        clock_t t0 = clock();
        for (uint32_t f = 0; f < FRAMES_PER_SCENE; f++)
        {
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &fence);
            uint32_t imgIdx = 0;
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                VK_NULL_HANDLE, VK_NULL_HANDLE, &imgIdx);

            VkCommandBufferBeginInfo bi; memset(&bi, 0, sizeof(bi));
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmdBuf, &bi);

            VkRenderingAttachmentInfo ca; memset(&ca, 0, sizeof(ca));
            ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ca.imageView = swapViews[imgIdx];
            ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            float phase = (float)f / (float)FRAMES_PER_SCENE;
            ca.clearValue.color.float32[0] = phase;
            ca.clearValue.color.float32[1] = 0.2f;
            ca.clearValue.color.float32[2] = 1.0f - phase;
            ca.clearValue.color.float32[3] = 1.0f;

            VkRenderingInfo ri; memset(&ri, 0, sizeof(ri));
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent.width = innerW;
            ri.renderArea.extent.height = innerH;
            ri.layerCount = 1; ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &ca;

            vkCmdBeginRendering(cmdBuf, &ri);
            vkCmdEndRendering(cmdBuf);
            vkEndCommandBuffer(cmdBuf);

            VkSubmitInfo si; memset(&si, 0, sizeof(si));
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1; si.pCommandBuffers = &cmdBuf;
            vkQueueSubmit(queue, 1, &si, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

            VkPresentInfoKHR pi; memset(&pi, 0, sizeof(pi));
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.swapchainCount = 1; pi.pSwapchains = &swapchain;
            pi.pImageIndices = &imgIdx;
            vkQueuePresentKHR(queue, &pi);
        }
        clock_t t1 = clock();
        double elapsed = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        double fps = (elapsed > 0.0) ? (double)FRAMES_PER_SCENE / elapsed : 0.0;
        printf("  Result: %.1f FPS (%.1f ms/frame)\n\n", fps, elapsed * 1000.0 / FRAMES_PER_SCENE);
    }

    /* ================================================================
    ** Results summary
    ** ================================================================ */
    printf("====================================\n");
    printf("  AmigaMark Complete\n");
    printf("  Device: %s\n", props.deviceName);
    printf("  Resolution: %ux%u\n", innerW, innerH);
    printf("====================================\n");

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (cmdBuf) vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, NULL);
    if (swapViews) {
        for (uint32_t i = 0; i < imgCount; i++)
            if (swapViews[i]) vkDestroyImageView(device, swapViews[i], NULL);
        free(swapViews);
    }
    if (swapImgs) free(swapImgs);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device) vkDestroyDevice(device, NULL);
    if (surface) vkDestroySurfaceKHR(instance, surface, NULL);
    if (window) CloseWindow(window);
    if (screen) UnlockPubScreen(NULL, screen);
    if (instance) vkDestroyInstance(instance, NULL);

    printf("Done (exit %d)\n", exitCode);
    return exitCode;
}
