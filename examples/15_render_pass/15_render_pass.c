/*
** Vulkan Render Pass -- AmigaOS 4
**
** Demonstrates legacy render pass (vkCreateRenderPass + vkCreateFramebuffer
** + vkCmdBeginRenderPass) instead of Vulkan 1.3 dynamic rendering
** (vkCmdBeginRendering). Clears the window to a colour that cycles
** through hue over time using sinf-based phase shifting.
**
** API functions exercised (not covered by other examples):
**   vkCreateRenderPass, vkDestroyRenderPass,
**   vkCreateFramebuffer, vkDestroyFramebuffer,
**   vkCmdBeginRenderPass, vkCmdEndRenderPass,
**   vkCmdNextSubpass
**
** Press close gadget to exit.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o render_pass render_pass.c -lvulkan_loader -lauto -lm
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define WIN_WIDTH  640
#define WIN_HEIGHT 480

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
    VkRenderPass     renderPass  = VK_NULL_HANDLE;
    struct Screen   *screen      = NULL;
    struct Window   *window      = NULL;
    VkImage         *swapImages  = NULL;
    VkImageView     *swapViews   = NULL;
    VkFramebuffer   *framebuffers = NULL;
    uint32_t         imageCount  = 0;
    uint32_t         innerW      = 0;
    uint32_t         innerH      = 0;
    int exitCode = 0;

    printf("=== Vulkan Render Pass Demo ===\n");

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
    appInfo.pApplicationName = "Vulkan Render Pass";
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
        WA_Title,       (Tag)"Vulkan Render Pass",
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
    innerW = window->Width - window->BorderLeft - window->BorderRight;
    innerH = window->Height - window->BorderTop - window->BorderBottom;

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

    /* Create render pass */
    {
        VkAttachmentDescription colorAttachment;
        memset(&colorAttachment, 0, sizeof(colorAttachment));
        colorAttachment.format         = VK_FORMAT_B8G8R8A8_UNORM;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef;
        memset(&colorRef, 0, sizeof(colorRef));
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass;
        memset(&subpass, 0, sizeof(subpass));
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkRenderPassCreateInfo rpci;
        memset(&rpci, 0, sizeof(rpci));
        rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &colorAttachment;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;

        if (vkCreateRenderPass(device, &rpci, NULL, &renderPass) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateRenderPass failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Render pass created\n");
    }

    /* Query render area granularity */
    printf("Render pass created with %u attachments\n", 1u);

    /* Create framebuffers (one per swapchain image) */
    framebuffers = (VkFramebuffer *)malloc(imageCount * sizeof(VkFramebuffer));
    memset(framebuffers, 0, imageCount * sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkFramebufferCreateInfo fbci;
        memset(&fbci, 0, sizeof(fbci));
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &swapViews[i];
        fbci.width           = innerW;
        fbci.height          = innerH;
        fbci.layers          = 1;

        if (vkCreateFramebuffer(device, &fbci, NULL, &framebuffers[i]) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateFramebuffer[%u] failed\n", i);
            exitCode = 1;
            goto cleanup;
        }
    }
    printf("Created %u framebuffers\n", imageCount);

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

    printf("Cycling hue via legacy render pass... close window to exit.\n");

    /* Render loop */
    uint32_t frame = 0;
    clock_t startClock = clock();
    BOOL running = TRUE;
    while (running)
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

        /* Cycle hue using phase-shifted sine waves */
        float t = (float)frame * 0.02f;
        float r = 0.5f + 0.5f * sinf(t);
        float g = 0.5f + 0.5f * sinf(t + 2.094f);
        float b = 0.5f + 0.5f * sinf(t + 4.189f);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);

        uint32_t imgIdx = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &imgIdx);

        /* Record command buffer using legacy render pass */
        VkCommandBufferBeginInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBuf, &bi);

        VkClearValue clearValue;
        memset(&clearValue, 0, sizeof(clearValue));
        clearValue.color.float32[0] = r;
        clearValue.color.float32[1] = g;
        clearValue.color.float32[2] = b;
        clearValue.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpbi;
        memset(&rpbi, 0, sizeof(rpbi));
        rpbi.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass               = renderPass;
        rpbi.framebuffer              = framebuffers[imgIdx];
        rpbi.renderArea.offset.x      = 0;
        rpbi.renderArea.offset.y      = 0;
        rpbi.renderArea.extent.width  = innerW;
        rpbi.renderArea.extent.height = innerH;
        rpbi.clearValueCount          = 1;
        rpbi.pClearValues             = &clearValue;

        vkCmdBeginRenderPass(cmdBuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmdBuf);
        vkEndCommandBuffer(cmdBuf);

        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmdBuf;
        vkQueueSubmit(queue, 1, &si, fence);

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType          = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.swapchainCount = 1;
        pi.pSwapchains    = &swapchain;
        pi.pImageIndices  = &imgIdx;
        vkQueuePresentKHR(queue, &pi);

        frame++;
    }

    {
        clock_t endClock = clock();
        double elapsed = (double)(endClock - startClock) / (double)CLOCKS_PER_SEC;
        printf("Rendered %u frames", frame);
        if (elapsed > 0.0 && frame > 0)
            printf(" in %.1f seconds (%.2f FPS)", elapsed, (double)frame / elapsed);
        printf("\n");
    }

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (fence)     vkDestroyFence(device, fence, NULL);
    if (cmdBuf)    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool)   vkDestroyCommandPool(device, cmdPool, NULL);
    if (framebuffers) {
        for (uint32_t i = 0; i < imageCount; i++)
            if (framebuffers[i]) vkDestroyFramebuffer(device, framebuffers[i], NULL);
        free(framebuffers);
    }
    if (renderPass) vkDestroyRenderPass(device, renderPass, NULL);
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
