/*
** Vulkan Image Texture -- AmigaOS 4
**
** Demonstrates loading textures from image files
** using stb_image.h (https://github.com/nothings/stb, Sean Barrett,
** public domain). Falls back to a procedural gradient texture if
** stb_image.h is not available or the image file is not found.
**
** Also demonstrates:
** - Vertex buffers (fullscreen quad with UV coordinates)
** - VkImage creation and memory upload for textures
** - VkImageView and VkSampler creation
** - Descriptor sets with COMBINED_IMAGE_SAMPLER
**
** To enable image loading, download stb_image.h into external/stb/
** and compile with -DUSE_STB_IMAGE. Without it, a procedural gradient
** texture is generated instead.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o image_texture image_texture.c -lvulkan_loader -lauto
**
** With stb_image:
**          ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__ -DUSE_STB_IMAGE
**          -I../../external/stb
**          -o image_texture image_texture.c -lvulkan_loader -lauto
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

#ifdef USE_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

#define TEX_W 128
#define TEX_H 128

struct Vertex {
    float pos[2];    /* x, y */
    float uv[2];     /* u, v */
};

/*------------------------------------------------------------------------
** Generate procedural gradient texture (fallback when no image loaded)
**----------------------------------------------------------------------*/
static void generate_fallback_texture(uint8_t *pixels, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            int idx = (y * w + x) * 4;
            /* Gradient: red increases left->right, green top->bottom */
            pixels[idx + 0] = (uint8_t)(x * 255 / (w - 1));
            pixels[idx + 1] = (uint8_t)(y * 255 / (h - 1));
            pixels[idx + 2] = 128;
            pixels[idx + 3] = 255;
        }
    }
}

/*------------------------------------------------------------------------
** Load texture pixels -- from file (stb_image) or procedural fallback
** Returns allocated pixel buffer (caller must free). Sets *outW, *outH.
**----------------------------------------------------------------------*/
static uint8_t *load_texture(const char *filename, int *outW, int *outH)
{
    uint8_t *pixels = NULL;

#ifdef USE_STB_IMAGE
    if (filename)
    {
        int channels = 0;
        pixels = stbi_load(filename, outW, outH, &channels, 4);
        if (pixels)
        {
            printf("Loaded image: %s (%dx%d, %d channels)\n",
                   filename, *outW, *outH, channels);
            return pixels;
        }
        printf("Could not load '%s': %s\n", filename, stbi_failure_reason());
        printf("Using procedural fallback texture.\n");
    }
#else
    (void)filename;
    printf("stb_image not available (compile with -DUSE_STB_IMAGE).\n");
    printf("Using procedural fallback texture.\n");
#endif

    /* Procedural fallback */
    *outW = TEX_W;
    *outH = TEX_H;
    pixels = (uint8_t *)malloc((size_t)TEX_W * TEX_H * 4);
    if (pixels)
        generate_fallback_texture(pixels, TEX_W, TEX_H);
    return pixels;
}

/*------------------------------------------------------------------------
** Free texture pixels (always use free -- stbi_image_free is free() by
** default, and the fallback path also uses malloc)
**----------------------------------------------------------------------*/
static void free_texture(uint8_t *pixels)
{
    free(pixels);
}

/*========================================================================
** MAIN
**======================================================================*/
int main(int argc, char **argv)
{
int    vkex_dur = vkex_duration_secs(argc, argv);    time_t vkex_t0  = time(NULL);
    (void)argc;
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
    VkImage          texImage  = VK_NULL_HANDLE;
    VkDeviceMemory   texMem    = VK_NULL_HANDLE;
    VkImageView      texView   = VK_NULL_HANDLE;
    VkSampler        texSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool  = VK_NULL_HANDLE;
    VkDescriptorSet  descSet   = VK_NULL_HANDLE;
    struct Screen   *screen    = NULL;
    struct Window   *window    = NULL;
    VkImage         *swapImgs  = NULL;
    VkImageView     *swapViews = NULL;
    uint32_t         imgCount  = 0;
    int exitCode = 0;

    /* Image filename from command line (first positional, ignoring -d N). */
    const char *imageFile = vkex_positional(argc, argv, 0);

    printf("=== Vulkan Image Texture ===\n");
    if (imageFile)
        printf("Image file: %s\n", imageFile);

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* Load texture pixels */
    int texW = 0, texH = 0;
    uint8_t *texPixels = load_texture(imageFile, &texW, &texH);
    if (!texPixels) { printf("ERROR: texture allocation failed\n"); return 1; }
    printf("Texture: %dx%d RGBA\n", texW, texH);

    /* Vertex data: fullscreen quad with UVs */
    struct Vertex quadVerts[] = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f}, {0.0f, 1.0f}},
    };

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Image Texture";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); free_texture(texPixels); return 1; }

    /* Device */
    VkPhysicalDevice physDev;
    uint32_t cnt = 1;
    vkEnumeratePhysicalDevices(instance, &cnt, &physDev);
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDev, &props);
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
    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /* Window + Surface */
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }
    window = OpenWindowTags(NULL,
        WA_Title, (Tag)"Vulkan Image Texture",
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
    {
        uint32_t i;
        for (i = 0; i < imgCount; i++)
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
    }

    /* Texture image */
    {
        VkDeviceSize texSize;
        if ((uint32_t)texW > 0xFFFF || (uint32_t)texH > 0xFFFF)
        { printf("ERROR: texture too large\n"); exitCode = 1; goto cleanup; }
        texSize = (VkDeviceSize)texW * (VkDeviceSize)texH * 4;
        if (texSize > 0xFFFFFFFF)
        { printf("ERROR: texture size overflow\n"); exitCode = 1; goto cleanup; }

        VkImageCreateInfo tici;
        memset(&tici, 0, sizeof(tici));
        tici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        tici.imageType = VK_IMAGE_TYPE_2D;
        tici.format = VK_FORMAT_R8G8B8A8_UNORM;
        tici.extent.width = (uint32_t)texW;
        tici.extent.height = (uint32_t)texH;
        tici.extent.depth = 1;
        tici.mipLevels = 1;
        tici.arrayLayers = 1;
        tici.samples = VK_SAMPLE_COUNT_1_BIT;
        tici.tiling = VK_IMAGE_TILING_LINEAR;
        tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        if (vkCreateImage(device, &tici, NULL, &texImage) != VK_SUCCESS)
        { printf("ERROR: vkCreateImage\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements tmr;
        vkGetImageMemoryRequirements(device, texImage, &tmr);
        VkMemoryAllocateInfo tmai;
        memset(&tmai, 0, sizeof(tmai));
        tmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        tmai.allocationSize = tmr.size;
        if (vkAllocateMemory(device, &tmai, NULL, &texMem) != VK_SUCCESS)
        { printf("ERROR: vkAllocateMemory (texture)\n"); exitCode = 1; goto cleanup; }
        if (vkBindImageMemory(device, texImage, texMem, 0) != VK_SUCCESS)
        { printf("ERROR: vkBindImageMemory\n"); exitCode = 1; goto cleanup; }

        void *mapped = NULL;
        if (vkMapMemory(device, texMem, 0, tmr.size, 0, &mapped) != VK_SUCCESS)
        { printf("ERROR: vkMapMemory (texture)\n"); exitCode = 1; goto cleanup; }
        memcpy(mapped, texPixels, (size_t)texSize);
        vkUnmapMemory(device, texMem);
    }
    free_texture(texPixels);
    texPixels = NULL;

    /* Image view + sampler */
    {
        VkImageViewCreateInfo tvci;
        memset(&tvci, 0, sizeof(tvci));
        tvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        tvci.image = texImage;
        tvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        tvci.format = VK_FORMAT_R8G8B8A8_UNORM;
        tvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        tvci.subresourceRange.levelCount = 1;
        tvci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &tvci, NULL, &texView) != VK_SUCCESS)
        { printf("ERROR: texture image view\n"); exitCode = 1; goto cleanup; }

        VkSamplerCreateInfo saci;
        memset(&saci, 0, sizeof(saci));
        saci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        saci.magFilter = VK_FILTER_LINEAR;
        saci.minFilter = VK_FILTER_LINEAR;
        saci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        saci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        saci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &saci, NULL, &texSampler) != VK_SUCCESS)
        { printf("ERROR: vkCreateSampler\n"); exitCode = 1; goto cleanup; }
    }

    /* Descriptor set */
    {
        VkDescriptorSetLayoutBinding binding;
        memset(&binding, 0, sizeof(binding));
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dlci;
        memset(&dlci, 0, sizeof(dlci));
        dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 1;
        dlci.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device, &dlci, NULL, &descLayout) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkDescriptorPoolSize poolSize;
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo dpci;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device, &dpci, NULL, &descPool) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkDescriptorSetAllocateInfo dsai;
        memset(&dsai, 0, sizeof(dsai));
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &descLayout;
        if (vkAllocateDescriptorSets(device, &dsai, &descSet) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkDescriptorImageInfo imgInfo;
        memset(&imgInfo, 0, sizeof(imgInfo));
        imgInfo.sampler = texSampler;
        imgInfo.imageView = texView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wds;
        memset(&wds, 0, sizeof(wds));
        wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds.dstSet = descSet;
        wds.dstBinding = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(device, 1, &wds, 0, NULL);
    }

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

    /* Pipeline layout */
    {
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &descLayout;
        if (vkCreatePipelineLayout(device, &plci, NULL, &pipLayout) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
    }

    /* Pipeline */
    {
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

        VkVertexInputBindingDescription vibd;
        memset(&vibd, 0, sizeof(vibd));
        vibd.stride = sizeof(struct Vertex);
        vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription viad[2];
        memset(viad, 0, sizeof(viad));
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].location = 1;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = 8;

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
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms;
        memset(&ms, 0, sizeof(ms));
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba;
        memset(&cba, 0, sizeof(cba));
        cba.colorWriteMask = 0xF;

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
    }

    /* Vertex buffer */
    {
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizeof(quadVerts);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (vkCreateBuffer(device, &bci, NULL, &vtxBuf) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, vtxBuf, &memReq);
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        if (vkAllocateMemory(device, &mai, NULL, &vtxMem) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        if (vkBindBufferMemory(device, vtxBuf, vtxMem, 0) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        void *mapped = NULL;
        if (vkMapMemory(device, vtxMem, 0, sizeof(quadVerts), 0, &mapped) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        memcpy(mapped, quadVerts, sizeof(quadVerts));
        vkUnmapMemory(device, vtxMem);
    }

    /* Command pool + buffer + fence */
    {
        VkCommandPoolCreateInfo cpci;
        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &cpci, NULL, &cmdPool) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

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
        if (vkCreateFence(device, &fci, NULL, &fence) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
    }

    printf("Displaying texture... close window to exit.\n");

    /* Render loop */
    uint32_t frame = 0;
    BOOL running = TRUE;
    while (running)
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

        uint32_t imgIdx = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &imgIdx);

        VkCommandBufferBeginInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuf, &bi);

        VkRenderingAttachmentInfo ca;
        memset(&ca, 0, sizeof(ca));
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = swapViews[imgIdx];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipLayout, 0, 1, &descSet, 0, NULL);

        VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, {innerW, innerH} };
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        VkDeviceSize vtxOffset = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOffset);
        vkCmdDraw(cmdBuf, 6, 1, 0, 0);

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
    if (texPixels) free_texture(texPixels);
    if (device) vkDeviceWaitIdle(device);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (cmdBuf) vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (pipLayout) vkDestroyPipelineLayout(device, pipLayout, NULL);
    if (fragMod) vkDestroyShaderModule(device, fragMod, NULL);
    if (vertMod) vkDestroyShaderModule(device, vertMod, NULL);
    if (descPool) vkDestroyDescriptorPool(device, descPool, NULL);
    if (descLayout) vkDestroyDescriptorSetLayout(device, descLayout, NULL);
    if (texSampler) vkDestroySampler(device, texSampler, NULL);
    if (texView) vkDestroyImageView(device, texView, NULL);
    if (texMem) vkFreeMemory(device, texMem, NULL);
    if (texImage) vkDestroyImage(device, texImage, NULL);
    if (vtxMem) vkFreeMemory(device, vtxMem, NULL);
    if (vtxBuf) vkDestroyBuffer(device, vtxBuf, NULL);
    if (swapViews) {
        uint32_t i;
        for (i = 0; i < imgCount; i++)
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
