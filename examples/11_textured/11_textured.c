/*
** Vulkan Textured Quad -- AmigaOS 4
**
** Demonstrates texture sampling: a fullscreen quad textured
** with a procedurally generated 64x64 checkerboard pattern using a
** combined image sampler descriptor.
**
** Also demonstrates:
** - Vertex buffers (VkBuffer + VkDeviceMemory + vertex input bindings)
** - VkImage creation and memory upload for textures
** - VkImageView and VkSampler creation
** - Descriptor set layout, descriptor pool, descriptor set allocation
** - vkUpdateDescriptorSets with COMBINED_IMAGE_SAMPLER
** - Binding descriptor sets during rendering
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o textured textured.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"
#include "../common/vkex_loop.h"

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

#define TEX_W 64
#define TEX_H 64

struct Vertex {
    float pos[2];    /* x, y */
    float uv[2];     /* u, v */
};

int main(int argc, char **argv)
{
int    vkex_dur = vkex_duration_secs(argc, argv);    time_t vkex_t0  = time(NULL);
    (void)argc; (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance            instance    = VK_NULL_HANDLE;
    VkDevice              device      = VK_NULL_HANDLE;
    VkSurfaceKHR          surface     = VK_NULL_HANDLE;
    VkSwapchainKHR        swapchain   = VK_NULL_HANDLE;
    VkShaderModule        vertMod     = VK_NULL_HANDLE;
    VkShaderModule        fragMod     = VK_NULL_HANDLE;
    VkPipelineLayout      pipLayout   = VK_NULL_HANDLE;
    VkPipeline            pipeline    = VK_NULL_HANDLE;
    VkCommandPool         cmdPool     = VK_NULL_HANDLE;
    VkCommandBuffer       cmdBuf      = VK_NULL_HANDLE;
    VkFence               fence       = VK_NULL_HANDLE;
    VkBuffer              vtxBuf      = VK_NULL_HANDLE;
    VkDeviceMemory        vtxMem      = VK_NULL_HANDLE;
    VkImage               texImg      = VK_NULL_HANDLE;
    VkDeviceMemory        texMem      = VK_NULL_HANDLE;
    VkImageView           texView     = VK_NULL_HANDLE;
    VkSampler             texSampler  = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout    = VK_NULL_HANDLE;
    VkDescriptorPool      descPool    = VK_NULL_HANDLE;
    VkDescriptorSet       descSet     = VK_NULL_HANDLE;
    struct Screen        *screen      = NULL;
    struct Window        *window      = NULL;
    VkImage              *swapImgs    = NULL;
    VkImageView          *swapViews   = NULL;
    uint32_t              imgCount    = 0;
    int exitCode = 0;

    printf("=== Vulkan Textured Quad ===\n");
    printf("Demonstrates:\n");
    printf("  - Procedural 64x64 checkerboard texture (R8G8B8A8_UNORM)\n");
    printf("  - VkImage + VkImageView + VkSampler creation\n");
    printf("  - Combined image sampler descriptor binding\n");
    printf("  - Vertex buffers with position + UV attributes\n");
    printf("  - TRIANGLE_STRIP quad rendering\n\n");

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* --- Vertex data: 4 vertices (triangle strip quad) --- */
    struct Vertex verts[] = {
        {{-0.8f, -0.8f}, {0.0f, 0.0f}},  /* bottom-left */
        {{ 0.8f, -0.8f}, {1.0f, 0.0f}},  /* bottom-right */
        {{-0.8f,  0.8f}, {0.0f, 1.0f}},  /* top-left */
        {{ 0.8f,  0.8f}, {1.0f, 1.0f}},  /* top-right */
    };

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Textured Quad";
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
        WA_Title, (Tag)"Vulkan Textured Quad",
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

    /* --- Texture: procedural 64x64 checkerboard --- */
    printf("Creating %dx%d checkerboard texture...\n", TEX_W, TEX_H);
    {
        /* Generate checkerboard pixel data */
        uint8_t texData[TEX_W * TEX_H * 4];
        int x, y;
        for (y = 0; y < TEX_H; y++) {
            for (x = 0; x < TEX_W; x++) {
                int checker = ((x / 8) + (y / 8)) % 2;
                uint8_t c = checker ? 220 : 40;
                uint8_t *p = &texData[(y * TEX_W + x) * 4];
                p[0] = c; p[1] = c; p[2] = c; p[3] = 255;
            }
        }

        /* 1. Create VkImage */
        VkImageCreateInfo tici;
        memset(&tici, 0, sizeof(tici));
        tici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        tici.imageType = VK_IMAGE_TYPE_2D;
        tici.format = VK_FORMAT_R8G8B8A8_UNORM;
        tici.extent.width = TEX_W;
        tici.extent.height = TEX_H;
        tici.extent.depth = 1;
        tici.mipLevels = 1;
        tici.arrayLayers = 1;
        tici.samples = VK_SAMPLE_COUNT_1_BIT;
        tici.tiling = VK_IMAGE_TILING_LINEAR;
        tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        tici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

        if (vkCreateImage(device, &tici, NULL, &texImg) != VK_SUCCESS)
        { printf("ERROR: vkCreateImage (texture)\n"); exitCode = 1; goto cleanup; }

        /* 2. Allocate memory */
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, texImg, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = 0; /* software ICD: host-visible type 0 */

        if (vkAllocateMemory(device, &mai, NULL, &texMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (texture)\n"); exitCode = 1; goto cleanup; }

        /* 3. Bind memory to image */
        if (vkBindImageMemory(device, texImg, texMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindImageMemory (texture)\n"); exitCode = 1; goto cleanup; }

        /* 4. Map and upload texture data */
        void *mapped = NULL;
        if (vkMapMemory(device, texMem, 0, TEX_W * TEX_H * 4, 0, &mapped) != VK_SUCCESS)
        { printf("ERROR: vkMapMemory (texture)\n"); exitCode = 1; goto cleanup; }
        memcpy(mapped, texData, TEX_W * TEX_H * 4);
        vkUnmapMemory(device, texMem);

        /* 5. Create image view */
        VkImageViewCreateInfo tvci;
        memset(&tvci, 0, sizeof(tvci));
        tvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        tvci.image = texImg;
        tvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        tvci.format = VK_FORMAT_R8G8B8A8_UNORM;
        tvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        tvci.subresourceRange.levelCount = 1;
        tvci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &tvci, NULL, &texView) != VK_SUCCESS)
        { printf("ERROR: vkCreateImageView (texture)\n"); exitCode = 1; goto cleanup; }

        /* 6. Create sampler */
        VkSamplerCreateInfo saci;
        memset(&saci, 0, sizeof(saci));
        saci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        saci.magFilter = VK_FILTER_LINEAR;
        saci.minFilter = VK_FILTER_LINEAR;
        saci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        saci.maxLod = 0.0f;

        if (vkCreateSampler(device, &saci, NULL, &texSampler) != VK_SUCCESS)
        { printf("ERROR: vkCreateSampler\n"); exitCode = 1; goto cleanup; }
    }
    printf("Texture created and uploaded.\n");

    /* --- Descriptor set layout --- */
    printf("Creating descriptor set layout and pool...\n");
    {
        /* 7. Descriptor set layout: 1 combined image sampler at binding 0 */
        VkDescriptorSetLayoutBinding dslb;
        memset(&dslb, 0, sizeof(dslb));
        dslb.binding = 0;
        dslb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dslb.descriptorCount = 1;
        dslb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslci;
        memset(&dslci, 0, sizeof(dslci));
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 1;
        dslci.pBindings = &dslb;

        if (vkCreateDescriptorSetLayout(device, &dslci, NULL, &dsLayout) != VK_SUCCESS)
        { printf("ERROR: vkCreateDescriptorSetLayout\n"); exitCode = 1; goto cleanup; }

        /* 8. Descriptor pool: 1 set, 1 combined image sampler */
        VkDescriptorPoolSize poolSize;
        memset(&poolSize, 0, sizeof(poolSize));
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo dpci;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(device, &dpci, NULL, &descPool) != VK_SUCCESS)
        { printf("ERROR: vkCreateDescriptorPool\n"); exitCode = 1; goto cleanup; }

        /* 9. Allocate descriptor set */
        VkDescriptorSetAllocateInfo dsai;
        memset(&dsai, 0, sizeof(dsai));
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &dsLayout;

        if (vkAllocateDescriptorSets(device, &dsai, &descSet) != VK_SUCCESS)
        { printf("ERROR: vkAllocateDescriptorSets\n"); exitCode = 1; goto cleanup; }

        /* 10. Update descriptor set with texture sampler + image view */
        VkDescriptorImageInfo dii;
        memset(&dii, 0, sizeof(dii));
        dii.sampler = texSampler;
        dii.imageView = texView;
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wds;
        memset(&wds, 0, sizeof(wds));
        wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds.dstSet = descSet;
        wds.dstBinding = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo = &dii;

        vkUpdateDescriptorSets(device, 1, &wds, 0, NULL);
    }
    printf("Descriptor set created and updated.\n");

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

    /* Pipeline layout with descriptor set layout (no push constants) */
    VkPipelineLayoutCreateInfo plci;
    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsLayout;
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
    /* location 1: uv (vec2) at offset 8 */
    viad[1].location = 1;
    viad[1].binding = 0;
    viad[1].format = VK_FORMAT_R32G32_SFLOAT;
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
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

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

    /* Pipeline rendering: color attachment format (no depth) */
    VkFormat colFmt = VK_FORMAT_B8G8R8A8_UNORM;
    VkPipelineRenderingCreateInfo prci;
    memset(&prci, 0, sizeof(prci));
    prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &colFmt;

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

    printf("Rendering textured quad... close window to exit.\n");

    /* Render loop */
    uint32_t frame = 0;
    BOOL running = TRUE;
    while (running)
    {
        /* Exit on -d N expiry or Shell CTRL-C */
        if (vkex_expired(vkex_dur, vkex_t0))   running = FALSE;
        if (CheckSignal(SIGBREAKF_CTRL_C))     running = FALSE;

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

        VkRenderingInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent.width = innerW;
        ri.renderArea.extent.height = innerH;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;

        vkCmdBeginRendering(cmdBuf, &ri);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        /* Bind descriptor set (set 0: combined image sampler) */
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipLayout, 0, 1, &descSet, 0, NULL);

        VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, {innerW, innerH} };
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        /* Bind vertex buffer */
        VkDeviceSize vtxOffset = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOffset);

        /* Draw textured quad (4 vertices, triangle strip) */
        vkCmdDraw(cmdBuf, 4, 1, 0, 0);

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
    if (descPool) vkDestroyDescriptorPool(device, descPool, NULL);
    if (dsLayout) vkDestroyDescriptorSetLayout(device, dsLayout, NULL);
    if (texSampler) vkDestroySampler(device, texSampler, NULL);
    if (texView) vkDestroyImageView(device, texView, NULL);
    if (texMem) vkFreeMemory(device, texMem, NULL);
    if (texImg) vkDestroyImage(device, texImg, NULL);
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
