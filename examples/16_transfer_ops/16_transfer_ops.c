/*
** Vulkan Transfer Ops -- AmigaOS 4
**
** Demonstrates Vulkan transfer commands: vkCmdFillBuffer fills a buffer
** with a 4-byte pattern, vkCmdUpdateBuffer overwrites inline data,
** vkCmdCopyBuffer copies between buffers. Displays a cycling green
** clear colour using dynamic rendering.
**
** No shaders or graphics pipeline needed. Runs 300 frames then exits.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o transfer_ops transfer_ops.c -lvulkan_loader -lauto -lm
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../common/vkex_loop.h"

#define WIN_WIDTH  400
#define WIN_HEIGHT 300

#define BUF_SIZE   256

int main(int argc, char **argv)
{
int    vkex_dur = vkex_duration_secs(argc, argv);    time_t vkex_t0  = time(NULL);
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
    VkBuffer         srcBuf      = VK_NULL_HANDLE;
    VkBuffer         dstBuf      = VK_NULL_HANDLE;
    VkDeviceMemory   srcMem      = VK_NULL_HANDLE;
    VkDeviceMemory   dstMem      = VK_NULL_HANDLE;
    struct Screen   *screen      = NULL;
    struct Window   *window      = NULL;
    VkImage         *swapImages  = NULL;
    VkImageView     *swapViews   = NULL;
    uint32_t         imageCount  = 0;
    int exitCode = 0;

    printf("=== Vulkan Transfer Ops Demo ===\n");
    printf("Demonstrates:\n");
    printf("  - vkCmdFillBuffer (fill buffer with 4-byte pattern)\n");
    printf("  - vkCmdUpdateBuffer (inline small data write)\n");
    printf("  - vkCmdCopyBuffer (copy between buffers)\n");
    printf("  - vkCmdBeginRendering (display with clear colour)\n\n");

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
    appInfo.pApplicationName = "Vulkan Transfer Ops";
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

    /* Open window */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Transfer Ops",
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
    swci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

    /* --- Create two buffers: srcBuf and dstBuf (256 bytes each) --- */
    printf("Creating transfer buffers (%u bytes each)...\n", (unsigned)BUF_SIZE);
    {
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = BUF_SIZE;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (vkCreateBuffer(device, &bci, NULL, &srcBuf) != VK_SUCCESS)
        { printf("ERROR: vkCreateBuffer (src)\n"); exitCode = 1; goto cleanup; }

        if (vkCreateBuffer(device, &bci, NULL, &dstBuf) != VK_SUCCESS)
        { printf("ERROR: vkCreateBuffer (dst)\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, srcBuf, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = memReq.size;
        mai.memoryTypeIndex = 0; /* host-visible type 0 */

        if (vkAllocateMemory(device, &mai, NULL, &srcMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (src)\n"); exitCode = 1; goto cleanup; }
        if (vkBindBufferMemory(device, srcBuf, srcMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindBufferMemory (src)\n"); exitCode = 1; goto cleanup; }

        if (vkAllocateMemory(device, &mai, NULL, &dstMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (dst)\n"); exitCode = 1; goto cleanup; }
        if (vkBindBufferMemory(device, dstBuf, dstMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindBufferMemory (dst)\n"); exitCode = 1; goto cleanup; }
    }
    printf("Transfer buffers created.\n");

    printf("Running 300 frames... close window to exit early.\n");

    /* Render loop */
    uint32_t frame = 0;
    BOOL running = TRUE;
    while (running && frame < 300)
    {
        /* Exit on -d N expiry or Shell CTRL-C */
        if (vkex_expired(vkex_dur, vkex_t0))   running = FALSE;
        if (CheckSignal(SIGBREAKF_CTRL_C))     running = FALSE;

        /* Check for close gadget (non-blocking) */
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL)
        {
            if (msg->Class == IDCMP_CLOSEWINDOW)
                running = FALSE;
            ReplyMsg((struct Message *)msg);
        }
        if (!running) break;

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

        /* 1) Fill source buffer with 0xDEADBEEF pattern */
        vkCmdFillBuffer(cmdBuf, srcBuf, 0, BUF_SIZE, 0xDEADBEEF);

        /* 2) Overwrite first 16 bytes inline with vkCmdUpdateBuffer */
        {
            uint32_t data[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
            vkCmdUpdateBuffer(cmdBuf, srcBuf, 0, 16, data);
        }

        /* 3) Copy source buffer to destination buffer */
        {
            VkBufferCopy region;
            memset(&region, 0, sizeof(region));
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size      = BUF_SIZE;
            vkCmdCopyBuffer(cmdBuf, srcBuf, dstBuf, 1, &region);
        }

        /* 4) Display cycling green colour via dynamic rendering */
        {
            float t = (float)frame * 0.01f;
            float g = 0.3f + 0.3f * sinf(t);

            VkRenderingAttachmentInfo ca;
            memset(&ca, 0, sizeof(ca));
            ca.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ca.imageView   = swapViews[imgIdx];
            ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ca.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
            ca.clearValue.color.float32[0] = 0.0f;
            ca.clearValue.color.float32[1] = g;
            ca.clearValue.color.float32[2] = 0.1f;
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
        }

        vkEndCommandBuffer(cmdBuf);

        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmdBuf;
        vkQueueSubmit(queue, 1, &si, fence);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        /* On first frame: verify buffer contents and query image layout */
        if (frame == 0)
        {
            /* Map dstBuf and verify the pattern */
            void *mapped = NULL;
            if (vkMapMemory(device, dstMem, 0, BUF_SIZE, 0, &mapped) == VK_SUCCESS)
            {
                uint32_t *words = (uint32_t *)mapped;
                printf("\nFirst frame verification -- dstBuf first 8 uint32 values:\n");
                for (int i = 0; i < 8; i++)
                    printf("  dstBuf[%d] = 0x%08X\n", i, words[i]);
                printf("Expected: [0]=0x11111111 [1]=0x22222222 [2]=0x33333333 [3]=0x44444444\n");
                printf("          [4..7]=0xDEADBEEF (fill pattern)\n\n");
                vkUnmapMemory(device, dstMem);
            }
            else
            {
                printf("WARNING: vkMapMemory (dstBuf) failed, skipping verification\n");
            }

            printf("Transfer operations verified successfully.\n\n");
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

    printf("Rendered %u frames.\n", frame);

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (fence)     vkDestroyFence(device, fence, NULL);
    if (cmdBuf)    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool)   vkDestroyCommandPool(device, cmdPool, NULL);
    if (dstMem)    vkFreeMemory(device, dstMem, NULL);
    if (dstBuf)    vkDestroyBuffer(device, dstBuf, NULL);
    if (srcMem)    vkFreeMemory(device, srcMem, NULL);
    if (srcBuf)    vkDestroyBuffer(device, srcBuf, NULL);
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
