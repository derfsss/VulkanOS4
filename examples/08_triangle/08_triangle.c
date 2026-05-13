/*
** Vulkan Triangle - AmigaOS 4
**
** Proof-of-concept: renders a coloured triangle using Vulkan 1.3
** with dynamic rendering (no render pass objects).
**
** The triangle has red, green, and blue vertices with colour
** interpolated across the surface, on a dark blue background.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -o triangle triangle.c
**          -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Embedded SPIR-V bytecode (pre-compiled from GLSL) */
#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"
#include "../common/vkex_loop.h"

/* Window dimensions */
#define WIN_WIDTH  800
#define WIN_HEIGHT 600

/****************************************************************************/
/* Globals                                                                  */
/* IntuitionBase and IIntuition are auto-opened by -lauto.                  */
/* VulkanBase and IVulkan are auto-opened by libvulkan_loader.a.            */
/****************************************************************************/

/****************************************************************************/
/* Helper: create shader module from embedded SPIR-V                        */
/****************************************************************************/

static VkShaderModule createShaderModule(VkDevice device,
                                          const unsigned char *code,
                                          unsigned int codeSize)
{
    VkShaderModuleCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = codeSize;
    ci.pCode    = (const uint32_t *)code;

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device, &ci, NULL, &module);
    if (result != VK_SUCCESS)
    {
        printf("Failed to create shader module (error %d)\n", (int)result);
        return VK_NULL_HANDLE;
    }
    return module;
}

/****************************************************************************/
/* Main                                                                     */
/****************************************************************************/

int main(int argc, char **argv)
{
    int vkex_dur = vkex_duration_secs(argc, argv);
    (void)argc;
    (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance       instance       = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain      = VK_NULL_HANDLE;
    VkShaderModule   vertModule     = VK_NULL_HANDLE;
    VkShaderModule   fragModule     = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache  pipelineCache  = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;
    VkCommandPool    commandPool    = VK_NULL_HANDLE;
    VkCommandBuffer  commandBuffer  = VK_NULL_HANDLE;
    VkFence          renderFence    = VK_NULL_HANDLE;
    VkSemaphore      renderSemaphore = VK_NULL_HANDLE;
    struct Screen   *screen         = NULL;
    struct Window   *window         = NULL;
    VkImage         *swapImages     = NULL;
    VkImageView     *swapViews      = NULL;
    uint32_t         swapImageCount = 0;

    int exitCode = 0;

    printf("=== Vulkan Triangle for AmigaOS 4 ===\n");

    /*----------------------------------------------------------------------
    ** 0. Check that vulkan.library auto-opened
    **--------------------------------------------------------------------*/
    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available (IVulkan is NULL)\n");
        printf("Make sure vulkan.library is in LIBS:\n");
        return 1;
    }

    /*----------------------------------------------------------------------
    ** 1. Create Vulkan instance
    **--------------------------------------------------------------------*/
    {
        VkApplicationInfo appInfo;
        memset(&appInfo, 0, sizeof(appInfo));
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Vulkan Triangle";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName        = "VulkanOS4";
        appInfo.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        const char *instExts[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_AMIGA_SURFACE_EXTENSION_NAME
        };

        VkInstanceCreateInfo instanceCI;
        memset(&instanceCI, 0, sizeof(instanceCI));
        instanceCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCI.pApplicationInfo        = &appInfo;
        instanceCI.enabledExtensionCount   = 2;
        instanceCI.ppEnabledExtensionNames = instExts;

        VkResult result = vkCreateInstance(&instanceCI, NULL, &instance);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateInstance failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }
        printf("Vulkan instance created\n");
    }

    /*----------------------------------------------------------------------
    ** 3. Select physical device
    **--------------------------------------------------------------------*/
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
        if (deviceCount == 0)
        {
            printf("ERROR: No Vulkan physical devices found\n");
            exitCode = 1;
            goto cleanup;
        }

        deviceCount = 1;
        vkEnumeratePhysicalDevices(instance, &deviceCount, &physicalDevice);

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        printf("Device: %s (Vulkan %u.%u.%u)\n",
            props.deviceName,
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion));
    }

    /*----------------------------------------------------------------------
    ** 4. Find graphics queue family
    **--------------------------------------------------------------------*/
    uint32_t graphicsFamily = 0;
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &queueFamilyCount, NULL);

        VkQueueFamilyProperties qfProps;
        uint32_t count = 1;
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &count, &qfProps);

        if (!(qfProps.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            printf("ERROR: No graphics queue family\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Graphics queue family: %u\n", graphicsFamily);
    }

    /*----------------------------------------------------------------------
    ** 5. Create logical device
    **--------------------------------------------------------------------*/
    {
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCI;
        memset(&queueCI, 0, sizeof(queueCI));
        queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCI.queueFamilyIndex = graphicsFamily;
        queueCI.queueCount       = 1;
        queueCI.pQueuePriorities = &queuePriority;

        const char *devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo deviceCI;
        memset(&deviceCI, 0, sizeof(deviceCI));
        deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCI.queueCreateInfoCount    = 1;
        deviceCI.pQueueCreateInfos       = &queueCI;
        deviceCI.enabledExtensionCount   = 1;
        deviceCI.ppEnabledExtensionNames = devExts;

        VkResult result = vkCreateDevice(physicalDevice, &deviceCI, NULL, &device);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateDevice failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }
        printf("Logical device created\n");
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);

    /*----------------------------------------------------------------------
    ** 6. Open Intuition window and create Vulkan surface
    **--------------------------------------------------------------------*/
    screen = LockPubScreen(NULL);
    if (!screen)
    {
        printf("ERROR: Cannot lock public screen\n");
        exitCode = 1;
        goto cleanup;
    }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Triangle",
        WA_Width,       WIN_WIDTH,
        WA_Height,      WIN_HEIGHT,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget,  FALSE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   (Tag)screen,
        TAG_DONE);

    if (!window)
    {
        printf("ERROR: Cannot open window\n");
        exitCode = 1;
        goto cleanup;
    }
    printf("Window opened: %dx%d\n", WIN_WIDTH, WIN_HEIGHT);

    {
        VkAmigaSurfaceCreateInfoAMIGA surfaceCI;
        memset(&surfaceCI, 0, sizeof(surfaceCI));
        surfaceCI.sType   = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
        surfaceCI.pScreen = screen;
        surfaceCI.pWindow = window;

        VkResult result = vkCreateAmigaSurfaceAMIGA(
            instance, &surfaceCI, NULL, &surface);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateAmigaSurfaceAMIGA failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }
        printf("Vulkan surface created\n");
    }

    /*----------------------------------------------------------------------
    ** 7. Create swapchain
    **--------------------------------------------------------------------*/
    {
        VkSurfaceCapabilitiesKHR surfCaps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physicalDevice, surface, &surfCaps);

        uint32_t imageCount = surfCaps.minImageCount + 1;
        if (surfCaps.maxImageCount > 0 && imageCount > surfCaps.maxImageCount)
            imageCount = surfCaps.maxImageCount;

        /* Query inner window dimensions (minus borders) */
        uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
        uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

        VkSwapchainCreateInfoKHR swapCI;
        memset(&swapCI, 0, sizeof(swapCI));
        swapCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapCI.surface          = surface;
        swapCI.minImageCount    = imageCount;
        swapCI.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
        swapCI.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapCI.imageExtent.width  = innerW;
        swapCI.imageExtent.height = innerH;
        swapCI.imageArrayLayers = 1;
        swapCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swapCI.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapCI.clipped          = VK_TRUE;

        VkResult result = vkCreateSwapchainKHR(device, &swapCI, NULL, &swapchain);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateSwapchainKHR failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        /* Get swapchain images */
        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, NULL);
        swapImages = (VkImage *)malloc(swapImageCount * sizeof(VkImage));
        if (!swapImages)
        {
            printf("ERROR: Out of memory for swapchain images\n");
            exitCode = 1;
            goto cleanup;
        }
        vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

        printf("Swapchain created: %ux%u, %u images\n",
            innerW, innerH, swapImageCount);

        /* Create image views for each swapchain image */
        swapViews = (VkImageView *)malloc(swapImageCount * sizeof(VkImageView));
        if (!swapViews)
        {
            printf("ERROR: Out of memory for image views\n");
            exitCode = 1;
            goto cleanup;
        }
        memset(swapViews, 0, swapImageCount * sizeof(VkImageView));

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageViewCreateInfo viewCI;
            memset(&viewCI, 0, sizeof(viewCI));
            viewCI.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.image    = swapImages[i];
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format   = VK_FORMAT_B8G8R8A8_UNORM;
            viewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.baseMipLevel   = 0;
            viewCI.subresourceRange.levelCount     = 1;
            viewCI.subresourceRange.baseArrayLayer = 0;
            viewCI.subresourceRange.layerCount     = 1;

            result = vkCreateImageView(device, &viewCI, NULL, &swapViews[i]);
            if (result != VK_SUCCESS)
            {
                printf("ERROR: vkCreateImageView failed for image %u (%d)\n",
                    i, (int)result);
                exitCode = 1;
                goto cleanup;
            }
        }
    }

    /*----------------------------------------------------------------------
    ** 8. Create shader modules
    **--------------------------------------------------------------------*/
    vertModule = createShaderModule(device, vert_spv, vert_spv_len);
    fragModule = createShaderModule(device, frag_spv, frag_spv_len);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
    {
        exitCode = 1;
        goto cleanup;
    }
    printf("Shader modules created\n");

    /*----------------------------------------------------------------------
    ** 9. Create pipeline layout and pipeline
    **--------------------------------------------------------------------*/
    {
        VkPipelineLayoutCreateInfo layoutCI;
        memset(&layoutCI, 0, sizeof(layoutCI));
        layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkResult result = vkCreatePipelineLayout(device, &layoutCI, NULL, &pipelineLayout);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreatePipelineLayout failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        VkPipelineCacheCreateInfo cacheCI;
        memset(&cacheCI, 0, sizeof(cacheCI));
        cacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        vkCreatePipelineCache(device, &cacheCI, NULL, &pipelineCache);

        /* Shader stages */
        VkPipelineShaderStageCreateInfo stages[2];
        memset(stages, 0, sizeof(stages));

        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName  = "main";

        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName  = "main";

        /* Vertex input (no vertex buffers -- positions in shader) */
        VkPipelineVertexInputStateCreateInfo vertexInput;
        memset(&vertexInput, 0, sizeof(vertexInput));
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        /* Input assembly */
        VkPipelineInputAssemblyStateCreateInfo inputAssembly;
        memset(&inputAssembly, 0, sizeof(inputAssembly));
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        /* Viewport state (dynamic -- set in command buffer) */
        VkPipelineViewportStateCreateInfo viewportState;
        memset(&viewportState, 0, sizeof(viewportState));
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        /* Rasterisation */
        VkPipelineRasterizationStateCreateInfo raster;
        memset(&raster, 0, sizeof(raster));
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        /* Multisample (disabled) */
        VkPipelineMultisampleStateCreateInfo multisample;
        memset(&multisample, 0, sizeof(multisample));
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        /* Colour blend (no blending) */
        VkPipelineColorBlendAttachmentState blendAttachment;
        memset(&blendAttachment, 0, sizeof(blendAttachment));
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend;
        memset(&colorBlend, 0, sizeof(colorBlend));
        colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments    = &blendAttachment;

        /* Dynamic state (viewport + scissor) */
        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState;
        memset(&dynamicState, 0, sizeof(dynamicState));
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates    = dynStates;

        /* Vulkan 1.3 dynamic rendering format */
        VkPipelineRenderingCreateInfo renderingCI;
        memset(&renderingCI, 0, sizeof(renderingCI));
        renderingCI.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingCI.colorAttachmentCount    = 1;
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
        renderingCI.pColorAttachmentFormats = &colorFormat;

        /* Create the graphics pipeline */
        VkGraphicsPipelineCreateInfo pipelineCI;
        memset(&pipelineCI, 0, sizeof(pipelineCI));
        pipelineCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.pNext               = &renderingCI;
        pipelineCI.stageCount          = 2;
        pipelineCI.pStages             = stages;
        pipelineCI.pVertexInputState   = &vertexInput;
        pipelineCI.pInputAssemblyState = &inputAssembly;
        pipelineCI.pViewportState      = &viewportState;
        pipelineCI.pRasterizationState = &raster;
        pipelineCI.pMultisampleState   = &multisample;
        pipelineCI.pColorBlendState    = &colorBlend;
        pipelineCI.pDynamicState       = &dynamicState;
        pipelineCI.layout              = pipelineLayout;

        result = vkCreateGraphicsPipelines(
            device, pipelineCache, 1, &pipelineCI, NULL, &pipeline);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateGraphicsPipelines failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }
        printf("Graphics pipeline created\n");
    }

    /*----------------------------------------------------------------------
    ** 10. Create command pool, command buffer, sync objects
    **--------------------------------------------------------------------*/
    {
        VkCommandPoolCreateInfo poolCI;
        memset(&poolCI, 0, sizeof(poolCI));
        poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.queueFamilyIndex = graphicsFamily;
        poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult result = vkCreateCommandPool(device, &poolCI, NULL, &commandPool);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateCommandPool failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        VkCommandBufferAllocateInfo allocInfo;
        memset(&allocInfo, 0, sizeof(allocInfo));
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkAllocateCommandBuffers failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        VkFenceCreateInfo fenceCI;
        memset(&fenceCI, 0, sizeof(fenceCI));
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        result = vkCreateFence(device, &fenceCI, NULL, &renderFence);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateFence failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        VkSemaphoreCreateInfo semCI;
        memset(&semCI, 0, sizeof(semCI));
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(device, &semCI, NULL, &renderSemaphore);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkCreateSemaphore failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        printf("Command buffer and sync objects created\n");
    }

    /*----------------------------------------------------------------------
    ** 11. Render one frame: acquire, record, submit, present
    **--------------------------------------------------------------------*/
    {
        /* Wait for previous frame's fence */
        vkWaitForFences(device, 1, &renderFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &renderFence);

        /* Acquire next swapchain image */
        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
            device, swapchain, UINT64_MAX, renderSemaphore,
            VK_NULL_HANDLE, &imageIndex);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkAcquireNextImageKHR failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        /* Get inner window dimensions for viewport */
        uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
        uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

        /* Record command buffer */
        VkCommandBufferBeginInfo beginInfo;
        memset(&beginInfo, 0, sizeof(beginInfo));
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        /* Begin rendering (Vulkan 1.3 dynamic rendering) */
        VkRenderingAttachmentInfo colorAttachment;
        memset(&colorAttachment, 0, sizeof(colorAttachment));
        colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView   = swapViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        /* Dark blue background */
        colorAttachment.clearValue.color.float32[0] = 0.0f;   /* R */
        colorAttachment.clearValue.color.float32[1] = 0.0f;   /* G */
        colorAttachment.clearValue.color.float32[2] = 0.2f;   /* B */
        colorAttachment.clearValue.color.float32[3] = 1.0f;   /* A */

        VkRenderingInfo renderInfo;
        memset(&renderInfo, 0, sizeof(renderInfo));
        renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.offset.x  = 0;
        renderInfo.renderArea.offset.y  = 0;
        renderInfo.renderArea.extent.width  = innerW;
        renderInfo.renderArea.extent.height = innerH;
        renderInfo.layerCount           = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments    = &colorAttachment;

        vkCmdBeginRendering(commandBuffer, &renderInfo);

        /* Bind pipeline */
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        /* Set viewport */
        VkViewport viewport;
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = (float)innerW;
        viewport.height   = (float)innerH;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        /* Set scissor */
        VkRect2D scissor;
        scissor.offset.x      = 0;
        scissor.offset.y      = 0;
        scissor.extent.width  = innerW;
        scissor.extent.height = innerH;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        /* Draw triangle (3 vertices, 1 instance) */
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRendering(commandBuffer);

        vkEndCommandBuffer(commandBuffer);

        /* Submit */
        VkSubmitInfo submitInfo;
        memset(&submitInfo, 0, sizeof(submitInfo));
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &commandBuffer;

        result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, renderFence);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkQueueSubmit failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        /* Wait for rendering to complete */
        vkWaitForFences(device, 1, &renderFence, VK_TRUE, UINT64_MAX);

        /* Present */
        VkPresentInfoKHR presentInfo;
        memset(&presentInfo, 0, sizeof(presentInfo));
        presentInfo.sType          = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains    = &swapchain;
        presentInfo.pImageIndices  = &imageIndex;

        result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
        if (result != VK_SUCCESS)
        {
            printf("ERROR: vkQueuePresentKHR failed (%d)\n", (int)result);
            exitCode = 1;
            goto cleanup;
        }

        printf("Triangle rendered and presented!\n");
    }

    /*----------------------------------------------------------------------
    ** 12. Wait for close gadget
    **--------------------------------------------------------------------*/
    printf("Waiting for close gadget...\n");
    vkex_wait_close_or_timer(window, vkex_dur);
    printf("Close gadget pressed (or -d N expired), cleaning up...\n");

    /*----------------------------------------------------------------------
    ** 13. Cleanup (reverse order)
    **--------------------------------------------------------------------*/
cleanup:
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);

    if (renderSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device, renderSemaphore, NULL);
    if (renderFence != VK_NULL_HANDLE)
        vkDestroyFence(device, renderFence, NULL);

    if (commandBuffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    if (commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, commandPool, NULL);

    if (pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, pipeline, NULL);
    if (pipelineCache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(device, pipelineCache, NULL);
    if (pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipelineLayout, NULL);

    if (fragModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, fragModule, NULL);
    if (vertModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, vertModule, NULL);

    if (swapViews)
    {
        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            if (swapViews[i] != VK_NULL_HANDLE)
                vkDestroyImageView(device, swapViews[i], NULL);
        }
        free(swapViews);
    }
    if (swapImages)
        free(swapImages);

    if (swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, swapchain, NULL);

    if (device != VK_NULL_HANDLE)
        vkDestroyDevice(device, NULL);

    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, NULL);

    if (window)
        CloseWindow(window);
    if (screen)
        UnlockPubScreen(NULL, screen);

    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, NULL);

    printf("Cleanup complete (exit code %d)\n", exitCode);
    return exitCode;
}
