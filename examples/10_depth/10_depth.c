/*
** Vulkan Depth Test -- AmigaOS 4
**
** Demonstrates depth testing: two overlapping coloured triangles
** are rendered at different Z depths. With depth test (VK_COMPARE_OP_LESS),
** the red triangle (Z=0.3, closer) occludes the blue triangle (Z=0.7)
** where they overlap.
**
** Also demonstrates:
** - Vertex buffers (VkBuffer + VkDeviceMemory + vertex input bindings)
** - Push constants for per-draw Z depth and X offset
** - Depth image creation (VK_FORMAT_D32_SFLOAT)
** - VkRenderingInfo with both color and depth attachments
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o depth depth.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

struct Vertex {
    float pos[2];    /* x, y */
    float color[3];  /* r, g, b */
};

struct PushData {
    float offsetZ;
    float offsetX;
};

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance       instance  = VK_NULL_HANDLE;
    VkDevice         device    = VK_NULL_HANDLE;
    VkSurfaceKHR     surface   = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain = VK_NULL_HANDLE;
    VkShaderModule   vertMod   = VK_NULL_HANDLE;
    VkShaderModule   fragMod   = VK_NULL_HANDLE;
    VkPipelineLayout pipLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline  = VK_NULL_HANDLE;
    VkCommandPool    cmdPool   = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf    = VK_NULL_HANDLE;
    VkFence          fence     = VK_NULL_HANDLE;
    VkBuffer         vtxBuf    = VK_NULL_HANDLE;
    VkDeviceMemory   vtxMem    = VK_NULL_HANDLE;
    VkImage          depthImg  = VK_NULL_HANDLE;
    VkDeviceMemory   depthMem  = VK_NULL_HANDLE;
    VkImageView      depthView = VK_NULL_HANDLE;
    struct Screen   *screen    = NULL;
    struct Window   *window    = NULL;
    VkImage         *swapImgs  = NULL;
    VkImageView     *swapViews = NULL;
    uint32_t         imgCount  = 0;
    int exitCode = 0;

    printf("=== Vulkan Depth Test ===\n");
    printf("Two overlapping triangles at different Z depths:\n");
    printf("  Red triangle  at Z=0.3 (closer), shifted left\n");
    printf("  Blue triangle at Z=0.7 (farther), shifted right\n");
    printf("Where they overlap, the red triangle should occlude the blue.\n\n");

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* --- Vertex data: 6 vertices (2 triangles) --- */
    struct Vertex verts[] = {
        /* Red triangle */
        {{ 0.0f, -0.4f}, {1.0f, 0.2f, 0.2f}},
        {{ 0.3f,  0.4f}, {1.0f, 0.2f, 0.2f}},
        {{-0.3f,  0.4f}, {1.0f, 0.2f, 0.2f}},
        /* Blue triangle */
        {{ 0.0f, -0.4f}, {0.2f, 0.2f, 1.0f}},
        {{ 0.3f,  0.4f}, {0.2f, 0.2f, 1.0f}},
        {{-0.3f,  0.4f}, {0.2f, 0.2f, 1.0f}},
    };

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Depth Test";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;

    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); return 1; }

    /* Device */
    VkPhysicalDevice physDev;
    uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(instance, &cnt, &physDev);

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

    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Window + Surface */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }

    window = OpenWindowTags(NULL,
        WA_Title, (Tag)"Vulkan Depth Test",
        WA_Width, WIN_WIDTH, WA_Height, WIN_HEIGHT,
        WA_DragBar, TRUE, WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE, WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_PubScreen, (Tag)screen, TAG_DONE);
    if (!window) { exitCode = 1; goto cleanup; }

    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen;
    sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    /* Swapchain */
    VkSwapchainCreateInfoKHR swci;
    memset(&swci, 0, sizeof(swci));
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface;
    swci.minImageCount = 2;
    swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent.width = innerW;
    swci.imageExtent.height = innerH;
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
    for (uint32_t i = 0; i < imgCount; i++)
    {
        VkImageViewCreateInfo vci;
        memset(&vci, 0, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapImgs[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, NULL, &swapViews[i]) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
    }

    /* --- Depth image (D32_SFLOAT, same dimensions as swapchain) --- */
    printf("Creating depth buffer (%ux%u, D32_SFLOAT)...\n", innerW, innerH);
    {
        VkImageCreateInfo dici;
        memset(&dici, 0, sizeof(dici));
        dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dici.imageType = VK_IMAGE_TYPE_2D;
        dici.format = VK_FORMAT_D32_SFLOAT;
        dici.extent.width = innerW;
        dici.extent.height = innerH;
        dici.extent.depth = 1;
        dici.mipLevels = 1;
        dici.arrayLayers = 1;
        dici.samples = VK_SAMPLE_COUNT_1_BIT;
        dici.tiling = VK_IMAGE_TILING_OPTIMAL;
        dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &dici, NULL, &depthImg) != VK_SUCCESS)
        { printf("ERROR: vkCreateImage (depth)\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, depthImg, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = 0; /* software ICD supports type 0 */

        if (vkAllocateMemory(device, &mai, NULL, &depthMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (depth)\n"); exitCode = 1; goto cleanup; }

        if (vkBindImageMemory(device, depthImg, depthMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindImageMemory (depth)\n"); exitCode = 1; goto cleanup; }

        VkImageViewCreateInfo dvci;
        memset(&dvci, 0, sizeof(dvci));
        dvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvci.image = depthImg;
        dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvci.format = VK_FORMAT_D32_SFLOAT;
        dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dvci.subresourceRange.levelCount = 1;
        dvci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &dvci, NULL, &depthView) != VK_SUCCESS)
        { printf("ERROR: vkCreateImageView (depth)\n"); exitCode = 1; goto cleanup; }
    }
    printf("Depth buffer created.\n");

    /* --- Vertex buffer --- */
    printf("Creating vertex buffer (%u bytes, %u vertices)...\n",
           (unsigned)sizeof(verts), (unsigned)(sizeof(verts) / sizeof(verts[0])));
    {
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizeof(verts);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        if (vkCreateBuffer(device, &bci, NULL, &vtxBuf) != VK_SUCCESS)
        { printf("ERROR: vkCreateBuffer\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, vtxBuf, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = 0; /* software ICD: host-visible type 0 */

        if (vkAllocateMemory(device, &mai, NULL, &vtxMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (vertex)\n"); exitCode = 1; goto cleanup; }

        if (vkBindBufferMemory(device, vtxBuf, vtxMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindBufferMemory\n"); exitCode = 1; goto cleanup; }

        /* Map and upload vertex data */
        void *mapped = NULL;
        if (vkMapMemory(device, vtxMem, 0, sizeof(verts), 0, &mapped) != VK_SUCCESS)
        { printf("ERROR: vkMapMemory\n"); exitCode = 1; goto cleanup; }
        memcpy(mapped, verts, sizeof(verts));
        vkUnmapMemory(device, vtxMem);
    }
    printf("Vertex buffer created and uploaded.\n");

    /* Shaders */
    VkShaderModuleCreateInfo smci;
    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = vert_spv_len;
    smci.pCode = (const uint32_t *)vert_spv;
    if (vkCreateShaderModule(device, &smci, NULL, &vertMod) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }
    smci.codeSize = frag_spv_len;
    smci.pCode = (const uint32_t *)frag_spv;
    if (vkCreateShaderModule(device, &smci, NULL, &fragMod) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    /* Pipeline layout with push constants (offsetZ + offsetX) */
    VkPushConstantRange pcRange;
    memset(&pcRange, 0, sizeof(pcRange));
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(struct PushData);

    VkPipelineLayoutCreateInfo plci;
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &plci, NULL, &pipLayout) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    /* Pipeline */
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    /* Vertex input: binding 0, stride = sizeof(Vertex), two attributes */
    VkVertexInputBindingDescription vibd;
    memset(&vibd, 0, sizeof(vibd));
    vibd.binding = 0;
    vibd.stride = sizeof(struct Vertex);
    vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription viad[2];
    memset(viad, 0, sizeof(viad));
    /* location 0: pos (vec2) at offset 0 */
    viad[0].location = 0;
    viad[0].binding = 0;
    viad[0].format = VK_FORMAT_R32G32_SFLOAT;
    viad[0].offset = 0;
    /* location 1: color (vec3) at offset 8 */
    viad[1].location = 1;
    viad[1].binding = 0;
    viad[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    viad[1].offset = 2 * sizeof(float); /* after pos[2] */

    VkPipelineVertexInputStateCreateInfo vi;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vibd;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = viad;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs;
    memset(&vs, 0, sizeof(vs));
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    /* Depth/stencil state: enable depth test with LESS compare */
    VkPipelineDepthStencilStateCreateInfo dss;
    memset(&dss, 0, sizeof(dss));
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS;
    dss.minDepthBounds = 0.0f;
    dss.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState cba;
    memset(&cba, 0, sizeof(cba));
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    /* Pipeline rendering: color + depth attachment formats */
    VkFormat colFmt = VK_FORMAT_B8G8R8A8_UNORM;
    VkPipelineRenderingCreateInfo prci;
    memset(&prci, 0, sizeof(prci));
    prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &colFmt;
    prci.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo gpci;
    memset(&gpci, 0, sizeof(gpci));
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.pNext = &prci;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vs;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &dss;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &ds;
    gpci.layout = pipLayout;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline) != VK_SUCCESS)
    { printf("ERROR: pipeline\n"); exitCode = 1; goto cleanup; }

    /* Command pool + buffer + fence */
    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &cpci, NULL, &cmdPool);

    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cbai, &cmdBuf);

    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fci, NULL, &fence);

    printf("Rendering depth test scene... close window to exit.\n");

    /* Render loop */
    uint32_t frame = 0;
    BOOL running = TRUE;
    while (running)
    {
        /* Check for close gadget (non-blocking) */
        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL)
        {
            if (msg->Class == IDCMP_CLOSEWINDOW) running = FALSE;
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
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuf, &bi);

        /* Color attachment */
        VkRenderingAttachmentInfo ca;
        memset(&ca, 0, sizeof(ca));
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = swapViews[imgIdx];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color.float32[0] = 0.1f;
        ca.clearValue.color.float32[1] = 0.1f;
        ca.clearValue.color.float32[2] = 0.15f;
        ca.clearValue.color.float32[3] = 1.0f;

        /* Depth attachment */
        VkRenderingAttachmentInfo depthAtt;
        memset(&depthAtt, 0, sizeof(depthAtt));
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil.depth = 1.0f;

        VkRenderingInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent.width = innerW;
        ri.renderArea.extent.height = innerH;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        ri.pDepthAttachment = &depthAtt;

        vkCmdBeginRendering(cmdBuf, &ri);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, {innerW, innerH} };
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        /* Bind vertex buffer */
        VkDeviceSize vtxOffset = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOffset);

        /* Draw red triangle (Z=0.3, shifted left) */
        struct PushData pd;
        pd.offsetZ = 0.3f;
        pd.offsetX = -0.15f;
        vkCmdPushConstants(cmdBuf, pipLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(struct PushData), &pd);
        vkCmdDraw(cmdBuf, 3, 1, 0, 0);

        /* Draw blue triangle (Z=0.7, shifted right) */
        pd.offsetZ = 0.7f;
        pd.offsetX = 0.15f;
        vkCmdPushConstants(cmdBuf, pipLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(struct PushData), &pd);
        vkCmdDraw(cmdBuf, 3, 1, 3, 0);

        vkCmdEndRendering(cmdBuf);
        vkEndCommandBuffer(cmdBuf);

        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmdBuf;
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imgIdx;
        vkQueuePresentKHR(queue, &pi);

        frame++;
    }

    printf("Rendered %u frames\n", frame);

cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (cmdBuf) vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (pipLayout) vkDestroyPipelineLayout(device, pipLayout, NULL);
    if (fragMod) vkDestroyShaderModule(device, fragMod, NULL);
    if (vertMod) vkDestroyShaderModule(device, vertMod, NULL);
    if (depthView) vkDestroyImageView(device, depthView, NULL);
    if (depthMem) vkFreeMemory(device, depthMem, NULL);
    if (depthImg) vkDestroyImage(device, depthImg, NULL);
    if (vtxMem) vkFreeMemory(device, vtxMem, NULL);
    if (vtxBuf) vkDestroyBuffer(device, vtxBuf, NULL);
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
