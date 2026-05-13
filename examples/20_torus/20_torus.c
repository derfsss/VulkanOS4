/*
** Vulkan Torus -- AmigaOS 4
**
** Renders a textured rotating torus over a rippling starfield background.
** The torus rotates on both X and Y axes, has a procedural checkerboard
** texture, and directional lighting. FPS is displayed in the window title
** bar via Intuition (SetWindowTitles) every second, independent of the
** Vulkan render loop.
**
** API functions exercised:
**   Two graphics pipelines per frame (starfield + torus)
**   Descriptor sets with combined image sampler (texture)
**   Push constants (mat4 MVP + float time = 68 bytes)
**   Indexed drawing (torus ~768 vertices, ~4608 indices)
**   Procedural geometry generation (torus parametric equations)
**   Depth testing (torus self-occlusion)
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o torus torus.c -lvulkan_loader -lauto -lm
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>
#include <devices/timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../common/vk_math.h"

/* Embedded SPIR-V shaders */
#include "shaders/torus_vert_spv.h"
#include "shaders/torus_frag_spv.h"
#include "shaders/stars_vert_spv.h"
#include "shaders/stars_frag_spv.h"
#include "../common/vkex_loop.h"

#define WIN_WIDTH  640
#define WIN_HEIGHT 480

/*------------------------------------------------------------------------
** Torus geometry parameters
**----------------------------------------------------------------------*/
#define TORUS_MAJOR_R    0.5f   /* Distance from centre to tube centre */
#define TORUS_MINOR_R    0.2f   /* Tube radius */
#define TORUS_SEGMENTS   32     /* Around the ring */
#define TORUS_SIDES      24     /* Around the tube */
#define TORUS_VCOUNT     (TORUS_SEGMENTS * TORUS_SIDES)
#define TORUS_ICOUNT     (TORUS_SEGMENTS * TORUS_SIDES * 6)

#define TEX_WIDTH   64
#define TEX_HEIGHT  64

/*------------------------------------------------------------------------
** Vertex format: position (vec3) + normal (vec3) + uv (vec2) = 32 bytes
**----------------------------------------------------------------------*/
typedef struct TorusVertex {
    float pos[3];
    float normal[3];
    float uv[2];
} TorusVertex;

/*------------------------------------------------------------------------
** Generate torus mesh
**----------------------------------------------------------------------*/
static void generate_torus(TorusVertex *verts, uint16_t *indices)
{
    int v = 0;
    for (int i = 0; i < TORUS_SEGMENTS; i++)
    {
        float u_angle = (float)i / (float)TORUS_SEGMENTS * 2.0f * (float)M_PI;
        float cu = cosf(u_angle), su = sinf(u_angle);

        for (int j = 0; j < TORUS_SIDES; j++)
        {
            float v_angle = (float)j / (float)TORUS_SIDES * 2.0f * (float)M_PI;
            float cv = cosf(v_angle), sv = sinf(v_angle);

            /* Position */
            float x = (TORUS_MAJOR_R + TORUS_MINOR_R * cv) * cu;
            float y = TORUS_MINOR_R * sv;
            float z = (TORUS_MAJOR_R + TORUS_MINOR_R * cv) * su;
            verts[v].pos[0] = x;
            verts[v].pos[1] = y;
            verts[v].pos[2] = z;

            /* Normal (points outward from tube centre) */
            verts[v].normal[0] = cv * cu;
            verts[v].normal[1] = sv;
            verts[v].normal[2] = cv * su;

            /* UV */
            verts[v].uv[0] = (float)i / (float)TORUS_SEGMENTS;
            verts[v].uv[1] = (float)j / (float)TORUS_SIDES;

            v++;
        }
    }

    /* Indices: two triangles per quad */
    int idx = 0;
    for (int i = 0; i < TORUS_SEGMENTS; i++)
    {
        int nextI = (i + 1) % TORUS_SEGMENTS;
        for (int j = 0; j < TORUS_SIDES; j++)
        {
            int nextJ = (j + 1) % TORUS_SIDES;
            int a = i * TORUS_SIDES + j;
            int b = nextI * TORUS_SIDES + j;
            int c2 = nextI * TORUS_SIDES + nextJ;
            int d = i * TORUS_SIDES + nextJ;

            indices[idx++] = (uint16_t)a;
            indices[idx++] = (uint16_t)b;
            indices[idx++] = (uint16_t)c2;
            indices[idx++] = (uint16_t)a;
            indices[idx++] = (uint16_t)c2;
            indices[idx++] = (uint16_t)d;
        }
    }
}

/*------------------------------------------------------------------------
** Generate procedural checkerboard texture (RGBA)
**----------------------------------------------------------------------*/
static void generate_texture(uint8_t *pixels, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int idx = (y * w + x) * 4;
            int check = ((x / 8) + (y / 8)) & 1;
            if (check)
            {
                /* Orange-gold */
                pixels[idx+0] = 220;
                pixels[idx+1] = 160;
                pixels[idx+2] = 40;
                pixels[idx+3] = 255;
            }
            else
            {
                /* Dark brown */
                pixels[idx+0] = 60;
                pixels[idx+1] = 30;
                pixels[idx+2] = 10;
                pixels[idx+3] = 255;
            }
        }
    }
}

/*------------------------------------------------------------------------
** Helper: create shader module from embedded SPIR-V
**----------------------------------------------------------------------*/
static VkShaderModule createShaderModule(VkDevice device,
    const unsigned char *code, unsigned int codeLen)
{
    VkShaderModuleCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = codeLen;
    ci.pCode    = (const uint32_t *)code;

    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, NULL, &mod);
    return mod;
}

/*------------------------------------------------------------------------
** Helper: create buffer + memory + bind
**----------------------------------------------------------------------*/
static int createBuffer(VkDevice device, VkDeviceSize size,
    VkBufferUsageFlags usage, VkBuffer *pBuf, VkDeviceMemory *pMem)
{
    VkBufferCreateInfo bci;
    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;

    if (vkCreateBuffer(device, &bci, NULL, pBuf) != VK_SUCCESS)
        return -1;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, *pBuf, &req);

    VkMemoryAllocateInfo mai;
    memset(&mai, 0, sizeof(mai));
    mai.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;

    if (vkAllocateMemory(device, &mai, NULL, pMem) != VK_SUCCESS)
        return -1;

    return (vkBindBufferMemory(device, *pBuf, *pMem, 0) == VK_SUCCESS) ? 0 : -1;
}

/*------------------------------------------------------------------------
** Helper: upload data to buffer via map/memcpy/unmap
**----------------------------------------------------------------------*/
static int uploadBuffer(VkDevice device, VkDeviceMemory mem,
    const void *data, VkDeviceSize size)
{
    void *mapped = NULL;
    if (vkMapMemory(device, mem, 0, size, 0, &mapped) != VK_SUCCESS)
        return -1;
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, mem);
    return 0;
}

/*========================================================================
** MAIN
**======================================================================*/
int main(int argc, char **argv)
{
int    vkex_dur = vkex_duration_secs(argc, argv);    time_t vkex_t0  = time(NULL);
    (void)argc; (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    /* Vulkan objects */
    VkInstance       instance     = VK_NULL_HANDLE;
    VkDevice         device       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface      = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain    = VK_NULL_HANDLE;
    VkCommandPool    cmdPool      = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf       = VK_NULL_HANDLE;
    VkFence          fence        = VK_NULL_HANDLE;

    /* Torus pipeline */
    VkShaderModule   torusVert    = VK_NULL_HANDLE;
    VkShaderModule   torusFrag    = VK_NULL_HANDLE;
    VkPipelineLayout torusLayout  = VK_NULL_HANDLE;
    VkPipeline       torusPipe    = VK_NULL_HANDLE;
    VkBuffer         torusVB      = VK_NULL_HANDLE;
    VkDeviceMemory   torusVBMem   = VK_NULL_HANDLE;
    VkBuffer         torusIB      = VK_NULL_HANDLE;
    VkDeviceMemory   torusIBMem   = VK_NULL_HANDLE;

    /* Texture */
    VkImage          texImage     = VK_NULL_HANDLE;
    VkDeviceMemory   texMem       = VK_NULL_HANDLE;
    VkImageView      texView      = VK_NULL_HANDLE;
    VkSampler        texSampler   = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool     = VK_NULL_HANDLE;
    VkDescriptorSet  descSet      = VK_NULL_HANDLE;

    /* Stars pipeline */
    VkShaderModule   starsVert    = VK_NULL_HANDLE;
    VkShaderModule   starsFrag    = VK_NULL_HANDLE;
    VkPipelineLayout starsLayout  = VK_NULL_HANDLE;
    VkPipeline       starsPipe    = VK_NULL_HANDLE;
    VkBuffer         starsVB      = VK_NULL_HANDLE;
    VkDeviceMemory   starsVBMem   = VK_NULL_HANDLE;

    /* Depth buffer */
    VkImage          depthImage   = VK_NULL_HANDLE;
    VkDeviceMemory   depthMem     = VK_NULL_HANDLE;
    VkImageView      depthView    = VK_NULL_HANDLE;

    /* Window */
    struct Screen   *screen       = NULL;
    struct Window   *window       = NULL;
    VkImage         *images       = NULL;
    VkImageView     *views        = NULL;
    uint32_t         imgCount     = 0;
    int exitCode = 0;

    printf("=== Vulkan Torus Demo ===\n");

    if (!IVulkan) { printf("vulkan.library not found\n"); return 1; }

    /*=== Instance ===*/
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Vulkan Torus";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("vkCreateInstance failed\n"); return 1; }

    /*=== Physical + logical device ===*/
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
    { printf("vkCreateDevice failed\n"); exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /*=== Window + Surface ===*/
    screen = LockPubScreen(NULL);
    if (!screen) { exitCode = 1; goto cleanup; }
    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Torus",
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

    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen;
    sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    { exitCode = 1; goto cleanup; }

    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    /*=== Swapchain ===*/
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
    images = (VkImage *)malloc(imgCount * sizeof(VkImage));
    if (!images) { printf("Out of memory\n"); exitCode = 1; goto cleanup; }
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, images);

    views = (VkImageView *)malloc(imgCount * sizeof(VkImageView));
    if (!views) { printf("Out of memory\n"); exitCode = 1; goto cleanup; }
    memset(views, 0, imgCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imgCount; i++)
    {
        VkImageViewCreateInfo vvci;
        memset(&vvci, 0, sizeof(vvci));
        vvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vvci.image = images[i];
        vvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vvci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vvci.subresourceRange.levelCount = 1;
        vvci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vvci, NULL, &views[i]);
    }

    /*=== Depth buffer ===*/
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
        vkCreateImage(device, &dici, NULL, &depthImage);

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, depthImage, &mr);
        VkMemoryAllocateInfo dmai;
        memset(&dmai, 0, sizeof(dmai));
        dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        dmai.allocationSize = mr.size;
        vkAllocateMemory(device, &dmai, NULL, &depthMem);
        vkBindImageMemory(device, depthImage, depthMem, 0);

        VkImageViewCreateInfo dvci;
        memset(&dvci, 0, sizeof(dvci));
        dvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvci.image = depthImage;
        dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvci.format = VK_FORMAT_D32_SFLOAT;
        dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dvci.subresourceRange.levelCount = 1;
        dvci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &dvci, NULL, &depthView);
    }

    /*=== Command pool + buffer + fence ===*/
    {
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
    }

    /*=== Texture (procedural checkerboard) ===*/
    {
        uint8_t *texPixels = (uint8_t *)malloc(TEX_WIDTH * TEX_HEIGHT * 4);
        if (!texPixels) { printf("Out of memory\n"); exitCode = 1; goto cleanup; }
        generate_texture(texPixels, TEX_WIDTH, TEX_HEIGHT);

        /* Create image */
        VkImageCreateInfo tici;
        memset(&tici, 0, sizeof(tici));
        tici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        tici.imageType = VK_IMAGE_TYPE_2D;
        tici.format = VK_FORMAT_R8G8B8A8_UNORM;
        tici.extent.width = TEX_WIDTH;
        tici.extent.height = TEX_HEIGHT;
        tici.extent.depth = 1;
        tici.mipLevels = 1;
        tici.arrayLayers = 1;
        tici.samples = VK_SAMPLE_COUNT_1_BIT;
        tici.tiling = VK_IMAGE_TILING_LINEAR;
        tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        vkCreateImage(device, &tici, NULL, &texImage);

        VkMemoryRequirements tmr;
        vkGetImageMemoryRequirements(device, texImage, &tmr);
        VkMemoryAllocateInfo tmai;
        memset(&tmai, 0, sizeof(tmai));
        tmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        tmai.allocationSize = tmr.size;
        vkAllocateMemory(device, &tmai, NULL, &texMem);
        vkBindImageMemory(device, texImage, texMem, 0);

        /* Upload pixel data */
        void *mapped;
        if (vkMapMemory(device, texMem, 0, tmr.size, 0, &mapped) == VK_SUCCESS)
        {
            memcpy(mapped, texPixels, TEX_WIDTH * TEX_HEIGHT * 4);
            vkUnmapMemory(device, texMem);
        }
        free(texPixels);

        /* Image view */
        VkImageViewCreateInfo tvci;
        memset(&tvci, 0, sizeof(tvci));
        tvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        tvci.image = texImage;
        tvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        tvci.format = VK_FORMAT_R8G8B8A8_UNORM;
        tvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        tvci.subresourceRange.levelCount = 1;
        tvci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &tvci, NULL, &texView);

        /* Sampler */
        VkSamplerCreateInfo saci;
        memset(&saci, 0, sizeof(saci));
        saci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        saci.magFilter = VK_FILTER_LINEAR;
        saci.minFilter = VK_FILTER_LINEAR;
        saci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkCreateSampler(device, &saci, NULL, &texSampler);
    }

    /*=== Descriptor set for texture ===*/
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
        vkCreateDescriptorSetLayout(device, &dlci, NULL, &descLayout);

        VkDescriptorPoolSize poolSize;
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo dpci;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &dpci, NULL, &descPool);

        VkDescriptorSetAllocateInfo dsai;
        memset(&dsai, 0, sizeof(dsai));
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &descLayout;
        vkAllocateDescriptorSets(device, &dsai, &descSet);

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

    /*=== Shader modules ===*/
    torusVert = createShaderModule(device, torus_vert_spv, torus_vert_spv_len);
    torusFrag = createShaderModule(device, torus_frag_spv, torus_frag_spv_len);
    starsVert = createShaderModule(device, stars_vert_spv, stars_vert_spv_len);
    starsFrag = createShaderModule(device, stars_frag_spv, stars_frag_spv_len);

    /*=== Push constant range (shared: mat4 + float = 68 bytes) ===*/
    VkPushConstantRange pcRange;
    memset(&pcRange, 0, sizeof(pcRange));
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = 68; /* mat4(64) + float(4) */

    /*=== Torus pipeline layout (with descriptor set) ===*/
    {
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &descLayout;
        vkCreatePipelineLayout(device, &plci, NULL, &torusLayout);
    }

    /*=== Stars pipeline layout (no descriptor set) ===*/
    {
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &plci, NULL, &starsLayout);
    }

    /*=== Torus graphics pipeline ===*/
    {
        VkPipelineShaderStageCreateInfo stages[2];
        memset(stages, 0, sizeof(stages));
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = torusVert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = torusFrag;
        stages[1].pName = "main";

        /* Vertex input: pos(vec3) + normal(vec3) + uv(vec2) */
        VkVertexInputBindingDescription vbd;
        memset(&vbd, 0, sizeof(vbd));
        vbd.binding = 0;
        vbd.stride = sizeof(TorusVertex);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vad[3];
        memset(vad, 0, sizeof(vad));
        vad[0].location = 0; vad[0].binding = 0;
        vad[0].format = VK_FORMAT_R32G32B32_SFLOAT; vad[0].offset = 0;
        vad[1].location = 1; vad[1].binding = 0;
        vad[1].format = VK_FORMAT_R32G32B32_SFLOAT; vad[1].offset = 12;
        vad[2].location = 2; vad[2].binding = 0;
        vad[2].format = VK_FORMAT_R32G32_SFLOAT; vad[2].offset = 24;

        VkPipelineVertexInputStateCreateInfo vis;
        memset(&vis, 0, sizeof(vis));
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount = 1;
        vis.pVertexBindingDescriptions = &vbd;
        vis.vertexAttributeDescriptionCount = 3;
        vis.pVertexAttributeDescriptions = vad;

        VkPipelineInputAssemblyStateCreateInfo ias;
        memset(&ias, 0, sizeof(ias));
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vps;
        memset(&vps, 0, sizeof(vps));
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1;
        vps.scissorCount = 1;

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

        VkPipelineDepthStencilStateCreateInfo ds;
        memset(&ds, 0, sizeof(ds));
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba;
        memset(&cba, 0, sizeof(cba));
        cba.colorWriteMask = 0xF;

        VkPipelineColorBlendStateCreateInfo cb;
        memset(&cb, 0, sizeof(cb));
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn;
        memset(&dyn, 0, sizeof(dyn));
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkPipelineRenderingCreateInfo pri;
        memset(&pri, 0, sizeof(pri));
        pri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pri.colorAttachmentCount = 1;
        VkFormat colFmt = VK_FORMAT_B8G8R8A8_UNORM;
        pri.pColorAttachmentFormats = &colFmt;
        pri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo gpci;
        memset(&gpci, 0, sizeof(gpci));
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.pNext = &pri;
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vis;
        gpci.pInputAssemblyState = &ias;
        gpci.pViewportState = &vps;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pDepthStencilState = &ds;
        gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dyn;
        gpci.layout = torusLayout;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &torusPipe);
    }

    /*=== Stars graphics pipeline ===*/
    {
        VkPipelineShaderStageCreateInfo stages[2];
        memset(stages, 0, sizeof(stages));
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = starsVert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = starsFrag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vbd;
        memset(&vbd, 0, sizeof(vbd));
        vbd.stride = 8; /* vec2 */

        VkVertexInputAttributeDescription vad;
        memset(&vad, 0, sizeof(vad));
        vad.format = VK_FORMAT_R32G32_SFLOAT;

        VkPipelineVertexInputStateCreateInfo vis;
        memset(&vis, 0, sizeof(vis));
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount = 1;
        vis.pVertexBindingDescriptions = &vbd;
        vis.vertexAttributeDescriptionCount = 1;
        vis.pVertexAttributeDescriptions = &vad;

        VkPipelineInputAssemblyStateCreateInfo ias;
        memset(&ias, 0, sizeof(ias));
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vps;
        memset(&vps, 0, sizeof(vps));
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.scissorCount = 1;

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

        VkPipelineDepthStencilStateCreateInfo ds;
        memset(&ds, 0, sizeof(ds));
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState cba;
        memset(&cba, 0, sizeof(cba));
        cba.colorWriteMask = 0xF;

        VkPipelineColorBlendStateCreateInfo cb;
        memset(&cb, 0, sizeof(cb));
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn;
        memset(&dyn, 0, sizeof(dyn));
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkPipelineRenderingCreateInfo pri;
        memset(&pri, 0, sizeof(pri));
        pri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pri.colorAttachmentCount = 1;
        VkFormat colFmt = VK_FORMAT_B8G8R8A8_UNORM;
        pri.pColorAttachmentFormats = &colFmt;
        pri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo gpci;
        memset(&gpci, 0, sizeof(gpci));
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.pNext = &pri;
        gpci.stageCount = 2; gpci.pStages = stages;
        gpci.pVertexInputState = &vis; gpci.pInputAssemblyState = &ias;
        gpci.pViewportState = &vps; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms; gpci.pDepthStencilState = &ds;
        gpci.pColorBlendState = &cb; gpci.pDynamicState = &dyn;
        gpci.layout = starsLayout;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &starsPipe);
    }

    /*=== Generate and upload torus geometry ===*/
    {
        TorusVertex *tv = (TorusVertex *)malloc(TORUS_VCOUNT * sizeof(TorusVertex));
        uint16_t *ti = (uint16_t *)malloc(TORUS_ICOUNT * sizeof(uint16_t));
        if (!tv || !ti) { free(tv); free(ti); printf("Out of memory\n"); exitCode = 1; goto cleanup; }
        generate_torus(tv, ti);

        if (createBuffer(device, TORUS_VCOUNT * sizeof(TorusVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &torusVB, &torusVBMem) < 0)
        { free(tv); free(ti); printf("Torus VB failed\n"); exitCode = 1; goto cleanup; }
        uploadBuffer(device, torusVBMem, tv, TORUS_VCOUNT * sizeof(TorusVertex));

        if (createBuffer(device, TORUS_ICOUNT * sizeof(uint16_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &torusIB, &torusIBMem) < 0)
        { free(tv); free(ti); printf("Torus IB failed\n"); exitCode = 1; goto cleanup; }
        uploadBuffer(device, torusIBMem, ti, TORUS_ICOUNT * sizeof(uint16_t));

        printf("Torus: %d vertices, %d indices\n", TORUS_VCOUNT, TORUS_ICOUNT);
        free(tv);
        free(ti);
    }

    /*=== Stars fullscreen quad ===*/
    {
        float quadVerts[] = {
            -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,
            -1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f,
        };
        if (createBuffer(device, sizeof(quadVerts),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &starsVB, &starsVBMem) < 0)
        { printf("Stars VB failed\n"); exitCode = 1; goto cleanup; }
        uploadBuffer(device, starsVBMem, quadVerts, sizeof(quadVerts));
    }

    /*=== Render loop ===*/
    printf("Rendering... close window to exit.\n");
    float angleX = 0.0f, angleY = 0.0f, time = 0.0f;
    uint32_t frame = 0;
    uint32_t fpsFrameCount = 0;
    clock_t fpsClockStart = clock();
    char titleBuf[64];

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

        uint32_t idx = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            VK_NULL_HANDLE, VK_NULL_HANDLE, &idx);

        /* Build MVP matrix */
        float proj[16], rotX[16], rotY[16], model[16], mvp[16];
        float aspect = (float)innerW / (float)innerH;
        vkm_mat4_perspective(proj, 45.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);

        vkm_mat4_rotate_x(rotX, angleX);
        vkm_mat4_rotate_y(rotY, angleY);
        vkm_mat4_multiply(model, rotY, rotX);

        /* Translate back (camera at z=0, torus at z=-1.5) */
        float translate[16];
        vkm_mat4_translate(translate, 0.0f, 0.0f, -1.5f);

        float view_model[16];
        vkm_mat4_multiply(view_model, translate, model);
        vkm_mat4_multiply(mvp, proj, view_model);

        /* Push constant data: mat4 + float */
        struct { float mvp[16]; float time; } pc;
        memcpy(pc.mvp, mvp, 64);
        pc.time = time;

        /* Record command buffer */
        VkCommandBufferBeginInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmdBuf, &bi);

        VkViewport vp;
        memset(&vp, 0, sizeof(vp));
        vp.width = (float)innerW;
        vp.height = (float)innerH;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);

        VkRect2D sc;
        memset(&sc, 0, sizeof(sc));
        sc.extent.width = innerW;
        sc.extent.height = innerH;
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        /* Begin rendering with colour + depth */
        VkRenderingAttachmentInfo ca;
        memset(&ca, 0, sizeof(ca));
        ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ca.imageView = views[idx];
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color.float32[3] = 1.0f;

        VkRenderingAttachmentInfo da;
        memset(&da, 0, sizeof(da));
        da.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        da.imageView = depthView;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        da.clearValue.depthStencil.depth = 1.0f;

        VkRenderingInfo ri;
        memset(&ri, 0, sizeof(ri));
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent.width = innerW;
        ri.renderArea.extent.height = innerH;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        ri.pDepthAttachment = &da;

        vkCmdBeginRendering(cmdBuf, &ri);

        /* --- Draw 1: Starfield background --- */
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, starsPipe);
        vkCmdPushConstants(cmdBuf, starsLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, 68, &pc);
        VkDeviceSize starsOff = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &starsVB, &starsOff);
        vkCmdDraw(cmdBuf, 6, 1, 0, 0);

        /* --- Draw 2: Textured torus --- */
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, torusPipe);
        vkCmdPushConstants(cmdBuf, torusLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, 68, &pc);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
            torusLayout, 0, 1, &descSet, 0, NULL);
        VkDeviceSize torusOff = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &torusVB, &torusOff);
        vkCmdBindIndexBuffer(cmdBuf, torusIB, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmdBuf, TORUS_ICOUNT, 1, 0, 0, 0);

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
        pi.pImageIndices = &idx;
        vkQueuePresentKHR(queue, &pi);

        /* Update rotation + time */
        angleX += 0.012f;
        angleY += 0.018f;
        time += 0.016f;
        frame++;
        fpsFrameCount++;

        /* Update FPS in window title approximately every second */
        {
            clock_t now = clock();
            double elapsed = (double)(now - fpsClockStart) / (double)CLOCKS_PER_SEC;
            if (elapsed >= 1.0)
            {
                double fps = (double)fpsFrameCount / elapsed;
                snprintf(titleBuf, sizeof(titleBuf), "Vulkan Torus - %.2f FPS", fps);
                SetWindowTitles(window, titleBuf, (STRPTR)~0);
                fpsFrameCount = 0;
                fpsClockStart = now;
            }
        }
    }

    printf("Rendered %u frames.\n", frame);

cleanup:
    if (device) vkDeviceWaitIdle(device);

    /* Stars pipeline */
    if (starsPipe)   vkDestroyPipeline(device, starsPipe, NULL);
    if (starsLayout) vkDestroyPipelineLayout(device, starsLayout, NULL);
    if (starsFrag)   vkDestroyShaderModule(device, starsFrag, NULL);
    if (starsVert)   vkDestroyShaderModule(device, starsVert, NULL);
    if (starsVB)     vkDestroyBuffer(device, starsVB, NULL);
    if (starsVBMem)  vkFreeMemory(device, starsVBMem, NULL);

    /* Torus pipeline */
    if (torusPipe)   vkDestroyPipeline(device, torusPipe, NULL);
    if (torusLayout) vkDestroyPipelineLayout(device, torusLayout, NULL);
    if (torusFrag)   vkDestroyShaderModule(device, torusFrag, NULL);
    if (torusVert)   vkDestroyShaderModule(device, torusVert, NULL);
    if (torusIB)     vkDestroyBuffer(device, torusIB, NULL);
    if (torusIBMem)  vkFreeMemory(device, torusIBMem, NULL);
    if (torusVB)     vkDestroyBuffer(device, torusVB, NULL);
    if (torusVBMem)  vkFreeMemory(device, torusVBMem, NULL);

    /* Texture */
    if (descPool)    vkDestroyDescriptorPool(device, descPool, NULL);
    if (descLayout)  vkDestroyDescriptorSetLayout(device, descLayout, NULL);
    if (texSampler)  vkDestroySampler(device, texSampler, NULL);
    if (texView)     vkDestroyImageView(device, texView, NULL);
    if (texMem)      vkFreeMemory(device, texMem, NULL);
    if (texImage)    vkDestroyImage(device, texImage, NULL);

    /* Depth */
    if (depthView)   vkDestroyImageView(device, depthView, NULL);
    if (depthMem)    vkFreeMemory(device, depthMem, NULL);
    if (depthImage)  vkDestroyImage(device, depthImage, NULL);

    /* Core */
    if (fence)       vkDestroyFence(device, fence, NULL);
    if (cmdBuf)      vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool)     vkDestroyCommandPool(device, cmdPool, NULL);
    if (views) {
        for (uint32_t i = 0; i < imgCount; i++)
            if (views[i]) vkDestroyImageView(device, views[i], NULL);
        free(views);
    }
    if (images)      free(images);
    if (swapchain)   vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device)      vkDestroyDevice(device, NULL);
    if (surface)     vkDestroySurfaceKHR(instance, surface, NULL);
    if (window)      CloseWindow(window);
    if (screen)      UnlockPubScreen(NULL, screen);
    if (instance)    vkDestroyInstance(instance, NULL);
    return exitCode;
}
