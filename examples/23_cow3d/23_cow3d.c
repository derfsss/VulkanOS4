/*
** Vulkan Cow3D -- AmigaOS 4
**
** Port of Alain Thellier's Cow3D demo to the Vulkan API.
** Renders a textured rotating 3D cow model (2914 vertices, 5813 triangles)
** loaded from the classic Cow3D dataset, with the cow texture loaded from
** a raw RGBA file.
**
** Demonstrates:
** - Large indexed mesh rendering (17439 indices, 32-bit)
** - External texture loading (256x256 RAW RGBA files)
** - Cosmos background (drawn first, cow rendered on top)
** - Two graphics pipelines (background + cow with depth)
** - Combined image sampler descriptors (cow + cosmos textures)
** - mat4 MVP push constant
** - Depth buffer for self-occlusion
** - Backface culling
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o cow3d 23_cow3d.c -lvulkan_loader -lauto -lm
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../common/vk_math.h"

#include "shaders/cow_vert_spv.h"
#include "shaders/cow_frag_spv.h"

#define WIN_WIDTH  640
#define WIN_HEIGHT 480

#define COW_SIZE    90.0f

#define TEX_SIZE   256
#define TEX_FILE   "Cow_256X256X32.RAW"
#define COSMOS_FILE "Cosmos_256X256X32.RAW"

/*------------------------------------------------------------------------
** Cow model data -- included from the original Cow3D project
** Points are stored as: U V X Y Z (5 floats per vertex)
** Indices are stored as: 3 ULONG per triangle
**----------------------------------------------------------------------*/
#define DISPLAYW 640
#define DISPLAYH 480

/* Forward-declare arrays from the data header */
#define pointsCount     2914
#define trianglesCount  5813

/* We only need the cow data, not the quad */
static float cow_points[pointsCount * 5];
static uint32_t cow_indices[trianglesCount * 3];

/*------------------------------------------------------------------------
** Vertex format for Vulkan: position (vec3) + uv (vec2) = 20 bytes
** The original cow data has no normals, so we skip them.
**----------------------------------------------------------------------*/
struct CowVertex {
    float pos[3];
    float uv[2];
};

/*------------------------------------------------------------------------
** Cosmos background quad -- fullscreen NDC quad (4 verts, 2 tris)
** Matches original Cow3D: drawn after cow with additive blending.
**----------------------------------------------------------------------*/
static const struct CowVertex cosmosQuadVerts[4] = {
    { {-1.0f, +1.0f, 0.0f}, {0.0f, 1.0f} },  /* bottom-left  */
    { {+1.0f, +1.0f, 0.0f}, {1.0f, 1.0f} },  /* bottom-right */
    { {+1.0f, -1.0f, 0.0f}, {1.0f, 0.0f} },  /* top-right    */
    { {-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f} },  /* top-left     */
};
static const uint32_t cosmosQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

/*------------------------------------------------------------------------
** Helper: create buffer + allocate memory + bind
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

/*------------------------------------------------------------------------
** Load raw RGBA texture from file
**----------------------------------------------------------------------*/
static uint8_t *loadRawTexture(const char *filename, int w, int h)
{
    size_t size = (size_t)w * h * 4;
    uint8_t *pixels = (uint8_t *)malloc(size);
    if (!pixels) return NULL;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("ERROR: Cannot open texture file '%s'\n", filename);
        free(pixels);
        return NULL;
    }

    size_t read = fread(pixels, 1, size, f);
    fclose(f);

    if (read != size) {
        printf("ERROR: Texture file too short (%u of %u bytes)\n",
               (unsigned)read, (unsigned)size);
        free(pixels);
        return NULL;
    }

    return pixels;
}

/*------------------------------------------------------------------------
** Convert cow points from U,V,X,Y,Z format to CowVertex format
**----------------------------------------------------------------------*/
static struct CowVertex *convertCowVertices(const float *srcPoints,
    uint32_t count, float scale)
{
    struct CowVertex *verts = (struct CowVertex *)malloc(
        count * sizeof(struct CowVertex));
    if (!verts) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        const float *src = &srcPoints[i * 5];
        float u = src[0];
        float v = src[1];
        float x = src[2];
        float y = src[3];
        float z = src[4];

        /* Apply same transform as ReadIncludedObjectNova:
           scale != 1.0 -> negate z */
        verts[i].pos[0] = scale * x;
        verts[i].pos[1] = scale * y;
        verts[i].pos[2] = scale * (-z);
        verts[i].uv[0]  = u;
        verts[i].uv[1]  = v;
    }

    return verts;
}

/*========================================================================
** MAIN
**======================================================================*/
int main(int argc, char **argv)
{
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

    /* Pipeline */
    VkShaderModule   vertMod      = VK_NULL_HANDLE;
    VkShaderModule   fragMod      = VK_NULL_HANDLE;
    VkPipelineLayout pipLayout    = VK_NULL_HANDLE;
    VkPipeline       pipeline     = VK_NULL_HANDLE;

    /* Geometry buffers */
    VkBuffer         vtxBuf       = VK_NULL_HANDLE;
    VkDeviceMemory   vtxMem       = VK_NULL_HANDLE;
    VkBuffer         idxBuf       = VK_NULL_HANDLE;
    VkDeviceMemory   idxMem       = VK_NULL_HANDLE;

    /* Texture */
    VkImage               texImage     = VK_NULL_HANDLE;
    VkDeviceMemory        texMem       = VK_NULL_HANDLE;
    VkImageView           texView      = VK_NULL_HANDLE;
    VkSampler             texSampler   = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout   = VK_NULL_HANDLE;
    VkDescriptorPool      descPool     = VK_NULL_HANDLE;
    VkDescriptorSet       descSet      = VK_NULL_HANDLE;

    /* Cosmos background */
    VkImage          cosmosImage  = VK_NULL_HANDLE;
    VkDeviceMemory   cosmosMem    = VK_NULL_HANDLE;
    VkImageView      cosmosView   = VK_NULL_HANDLE;
    VkDescriptorSet  cosmosDescSet = VK_NULL_HANDLE;
    VkPipeline       cosmosPipeline = VK_NULL_HANDLE;
    VkBuffer         cosmosVtxBuf  = VK_NULL_HANDLE;
    VkDeviceMemory   cosmosVtxMem  = VK_NULL_HANDLE;
    VkBuffer         cosmosIdxBuf  = VK_NULL_HANDLE;
    VkDeviceMemory   cosmosIdxMem  = VK_NULL_HANDLE;

    /* Depth buffer */
    VkImage          depthImage   = VK_NULL_HANDLE;
    VkDeviceMemory   depthMem     = VK_NULL_HANDLE;
    VkImageView      depthView    = VK_NULL_HANDLE;

    /* Window */
    struct Screen   *screen       = NULL;
    struct Window   *window       = NULL;
    VkImage         *swapImgs     = NULL;
    VkImageView     *swapViews    = NULL;
    uint32_t         imgCount     = 0;

    /* Converted vertex data */
    struct CowVertex *cowVerts    = NULL;
    uint8_t          *texPixels   = NULL;
    uint8_t          *cosmosPixels = NULL;

    int exitCode = 0;

    printf("=== Vulkan Cow3D ===\n");
    printf("Port of Alain Thellier's Cow3D demo to Vulkan\n");
    printf("  - %u vertices, %u triangles (%u indices)\n",
           pointsCount, trianglesCount, trianglesCount * 3);
    printf("  - Texture: %s (%dx%d RGBA)\n", TEX_FILE, TEX_SIZE, TEX_SIZE);
    printf("  - Cosmos background: %s (additive blend)\n", COSMOS_FILE);
    printf("  - Depth buffer, backface culling\n\n");

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /*=== Load cow data from the original header ===*/
    /* The data arrays are defined in the original Cow3D_Object.h.
       We include them here by copying the raw float/index data. */
    {
        /* Include the raw data - these are the original arrays */
        #define LOAD_COW_DATA
        static const float _cow_pts[] = {
            #include "cow_points.inc"
        };
        static const uint32_t _cow_idx[] = {
            #include "cow_indices.inc"
        };
        memcpy(cow_points, _cow_pts, sizeof(cow_points));
        memcpy(cow_indices, _cow_idx, sizeof(cow_indices));
        #undef LOAD_COW_DATA
    }

    /* Convert vertex format */
    cowVerts = convertCowVertices(cow_points, pointsCount, COW_SIZE);
    if (!cowVerts) { printf("ERROR: malloc vertices\n"); return 1; }

    /* Load texture from file */
    texPixels = loadRawTexture(TEX_FILE, TEX_SIZE, TEX_SIZE);
    if (!texPixels) {
        printf("WARNING: Using fallback checkerboard texture\n");
        texPixels = (uint8_t *)malloc(TEX_SIZE * TEX_SIZE * 4);
        if (!texPixels) { free(cowVerts); return 1; }
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                int idx = (y * TEX_SIZE + x) * 4;
                int check = ((x / 32) + (y / 32)) & 1;
                texPixels[idx+0] = check ? 180 : 60;
                texPixels[idx+1] = check ? 140 : 40;
                texPixels[idx+2] = check ? 80  : 20;
                texPixels[idx+3] = 255;
            }
        }
    }

    /* Load cosmos background texture */
    cosmosPixels = loadRawTexture(COSMOS_FILE, TEX_SIZE, TEX_SIZE);
    if (!cosmosPixels) {
        printf("WARNING: Using fallback starfield for cosmos\n");
        cosmosPixels = (uint8_t *)malloc(TEX_SIZE * TEX_SIZE * 4);
        if (!cosmosPixels) { free(cowVerts); free(texPixels); return 1; }
        memset(cosmosPixels, 0, TEX_SIZE * TEX_SIZE * 4);
        /* Scatter some stars */
        srand(42);
        for (int i = 0; i < 200; i++) {
            int x = rand() % TEX_SIZE, y = rand() % TEX_SIZE;
            int idx = (y * TEX_SIZE + x) * 4;
            uint8_t b = (uint8_t)(128 + rand() % 128);
            cosmosPixels[idx+0] = b;
            cosmosPixels[idx+1] = b;
            cosmosPixels[idx+2] = b;
            cosmosPixels[idx+3] = 255;
        }
    }

    /*=== Instance ===*/
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Vulkan Cow3D";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); exitCode = 1; goto cleanup; }

    /*=== Physical + logical device ===*/
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
    { printf("ERROR: vkCreateDevice\n"); exitCode = 1; goto cleanup; }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /*=== Window + Surface ===*/
    screen = LockPubScreen(NULL);
    if (!screen) { printf("ERROR: LockPubScreen\n"); exitCode = 1; goto cleanup; }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Cow3D",
        WA_Width,       WIN_WIDTH,
        WA_Height,      WIN_HEIGHT,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   (Tag)screen,
        TAG_DONE);
    if (!window) { printf("ERROR: OpenWindow\n"); exitCode = 1; goto cleanup; }

    VkAmigaSurfaceCreateInfoAMIGA sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
    sci.pScreen = screen;
    sci.pWindow = window;
    if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
    { printf("ERROR: vkCreateAmigaSurface\n"); exitCode = 1; goto cleanup; }

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
    { printf("ERROR: swapchain\n"); exitCode = 1; goto cleanup; }

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, NULL);
    swapImgs = (VkImage *)malloc(imgCount * sizeof(VkImage));
    if (!swapImgs) { exitCode = 1; goto cleanup; }
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImgs);

    swapViews = (VkImageView *)malloc(imgCount * sizeof(VkImageView));
    if (!swapViews) { exitCode = 1; goto cleanup; }
    memset(swapViews, 0, imgCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imgCount; i++)
    {
        VkImageViewCreateInfo vvci;
        memset(&vvci, 0, sizeof(vvci));
        vvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vvci.image = swapImgs[i];
        vvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vvci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vvci.subresourceRange.levelCount = 1;
        vvci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vvci, NULL, &swapViews[i]) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
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
        if (vkCreateImage(device, &dici, NULL, &depthImage) != VK_SUCCESS)
        { printf("ERROR: depth image\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, depthImage, &mr);
        VkMemoryAllocateInfo dmai;
        memset(&dmai, 0, sizeof(dmai));
        dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        dmai.allocationSize = mr.size;
        if (vkAllocateMemory(device, &dmai, NULL, &depthMem) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
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
        if (vkCreateImageView(device, &dvci, NULL, &depthView) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
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

    /*=== Texture ===*/
    printf("Creating %dx%d texture...\n", TEX_SIZE, TEX_SIZE);
    {
        VkImageCreateInfo tici;
        memset(&tici, 0, sizeof(tici));
        tici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        tici.imageType = VK_IMAGE_TYPE_2D;
        tici.format = VK_FORMAT_R8G8B8A8_UNORM;
        tici.extent.width = TEX_SIZE;
        tici.extent.height = TEX_SIZE;
        tici.extent.depth = 1;
        tici.mipLevels = 1;
        tici.arrayLayers = 1;
        tici.samples = VK_SAMPLE_COUNT_1_BIT;
        tici.tiling = VK_IMAGE_TILING_LINEAR;
        tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        tici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        if (vkCreateImage(device, &tici, NULL, &texImage) != VK_SUCCESS)
        { printf("ERROR: texture image\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements tmr;
        vkGetImageMemoryRequirements(device, texImage, &tmr);
        VkMemoryAllocateInfo tmai;
        memset(&tmai, 0, sizeof(tmai));
        tmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        tmai.allocationSize = tmr.size;
        if (vkAllocateMemory(device, &tmai, NULL, &texMem) != VK_SUCCESS)
        { printf("ERROR: texture memory\n"); exitCode = 1; goto cleanup; }
        vkBindImageMemory(device, texImage, texMem, 0);

        void *mapped;
        if (vkMapMemory(device, texMem, 0, tmr.size, 0, &mapped) == VK_SUCCESS)
        {
            memcpy(mapped, texPixels, TEX_SIZE * TEX_SIZE * 4);
            vkUnmapMemory(device, texMem);
        }

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
        { printf("ERROR: texture view\n"); exitCode = 1; goto cleanup; }

        VkSamplerCreateInfo saci;
        memset(&saci, 0, sizeof(saci));
        saci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        saci.magFilter = VK_FILTER_LINEAR;
        saci.minFilter = VK_FILTER_LINEAR;
        saci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        saci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        if (vkCreateSampler(device, &saci, NULL, &texSampler) != VK_SUCCESS)
        { printf("ERROR: sampler\n"); exitCode = 1; goto cleanup; }
    }
    printf("Cow texture loaded.\n");

    /*=== Cosmos texture ===*/
    printf("Creating cosmos texture...\n");
    {
        VkImageCreateInfo cici;
        memset(&cici, 0, sizeof(cici));
        cici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        cici.imageType = VK_IMAGE_TYPE_2D;
        cici.format = VK_FORMAT_R8G8B8A8_UNORM;
        cici.extent.width = TEX_SIZE;
        cici.extent.height = TEX_SIZE;
        cici.extent.depth = 1;
        cici.mipLevels = 1;
        cici.arrayLayers = 1;
        cici.samples = VK_SAMPLE_COUNT_1_BIT;
        cici.tiling = VK_IMAGE_TILING_LINEAR;
        cici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        cici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        if (vkCreateImage(device, &cici, NULL, &cosmosImage) != VK_SUCCESS)
        { printf("ERROR: cosmos image\n"); exitCode = 1; goto cleanup; }

        VkMemoryRequirements cmr;
        vkGetImageMemoryRequirements(device, cosmosImage, &cmr);
        VkMemoryAllocateInfo cmai;
        memset(&cmai, 0, sizeof(cmai));
        cmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        cmai.allocationSize = cmr.size;
        if (vkAllocateMemory(device, &cmai, NULL, &cosmosMem) != VK_SUCCESS)
        { printf("ERROR: cosmos memory\n"); exitCode = 1; goto cleanup; }
        vkBindImageMemory(device, cosmosImage, cosmosMem, 0);

        void *mapped;
        if (vkMapMemory(device, cosmosMem, 0, cmr.size, 0, &mapped) == VK_SUCCESS)
        {
            memcpy(mapped, cosmosPixels, TEX_SIZE * TEX_SIZE * 4);
            vkUnmapMemory(device, cosmosMem);
        }

        VkImageViewCreateInfo cvci;
        memset(&cvci, 0, sizeof(cvci));
        cvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cvci.image = cosmosImage;
        cvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cvci.format = VK_FORMAT_R8G8B8A8_UNORM;
        cvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cvci.subresourceRange.levelCount = 1;
        cvci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &cvci, NULL, &cosmosView) != VK_SUCCESS)
        { printf("ERROR: cosmos view\n"); exitCode = 1; goto cleanup; }
    }
    printf("Cosmos texture loaded.\n");

    /*=== Descriptor sets for cow + cosmos textures ===*/
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
        poolSize.descriptorCount = 2;

        VkDescriptorPoolCreateInfo dpci;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 2;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device, &dpci, NULL, &descPool) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        /* Cow texture descriptor set */
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

        /* Cosmos texture descriptor set */
        if (vkAllocateDescriptorSets(device, &dsai, &cosmosDescSet) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkDescriptorImageInfo cosmosImgInfo;
        memset(&cosmosImgInfo, 0, sizeof(cosmosImgInfo));
        cosmosImgInfo.sampler = texSampler;
        cosmosImgInfo.imageView = cosmosView;
        cosmosImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet cwds;
        memset(&cwds, 0, sizeof(cwds));
        cwds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cwds.dstSet = cosmosDescSet;
        cwds.dstBinding = 0;
        cwds.descriptorCount = 1;
        cwds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cwds.pImageInfo = &cosmosImgInfo;
        vkUpdateDescriptorSets(device, 1, &cwds, 0, NULL);
    }

    /*=== Vertex buffer ===*/
    {
        VkDeviceSize vtxSize = pointsCount * sizeof(struct CowVertex);
        printf("Uploading vertex buffer (%u bytes, %u vertices)...\n",
               (unsigned)vtxSize, pointsCount);
        if (createBuffer(device, vtxSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         &vtxBuf, &vtxMem) != 0)
        { printf("ERROR: vertex buffer\n"); exitCode = 1; goto cleanup; }
        if (uploadBuffer(device, vtxMem, cowVerts, vtxSize) != 0)
        { printf("ERROR: vertex upload\n"); exitCode = 1; goto cleanup; }
    }

    /*=== Index buffer (32-bit indices) ===*/
    {
        VkDeviceSize idxSize = trianglesCount * 3 * sizeof(uint32_t);
        printf("Uploading index buffer (%u bytes, %u indices)...\n",
               (unsigned)idxSize, trianglesCount * 3);
        if (createBuffer(device, idxSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         &idxBuf, &idxMem) != 0)
        { printf("ERROR: index buffer\n"); exitCode = 1; goto cleanup; }
        if (uploadBuffer(device, idxMem, cow_indices, idxSize) != 0)
        { printf("ERROR: index upload\n"); exitCode = 1; goto cleanup; }
    }

    /*=== Cosmos quad buffers ===*/
    {
        VkDeviceSize cvSize = sizeof(cosmosQuadVerts);
        if (createBuffer(device, cvSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         &cosmosVtxBuf, &cosmosVtxMem) != 0)
        { printf("ERROR: cosmos vertex buffer\n"); exitCode = 1; goto cleanup; }
        if (uploadBuffer(device, cosmosVtxMem, cosmosQuadVerts, cvSize) != 0)
        { printf("ERROR: cosmos vertex upload\n"); exitCode = 1; goto cleanup; }

        VkDeviceSize ciSize = sizeof(cosmosQuadIndices);
        if (createBuffer(device, ciSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         &cosmosIdxBuf, &cosmosIdxMem) != 0)
        { printf("ERROR: cosmos index buffer\n"); exitCode = 1; goto cleanup; }
        if (uploadBuffer(device, cosmosIdxMem, cosmosQuadIndices, ciSize) != 0)
        { printf("ERROR: cosmos index upload\n"); exitCode = 1; goto cleanup; }
    }

    /*=== Shader modules ===*/
    {
        VkShaderModuleCreateInfo smci;
        memset(&smci, 0, sizeof(smci));
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = cow_vert_spv_len;
        smci.pCode = (const uint32_t *)cow_vert_spv;
        if (vkCreateShaderModule(device, &smci, NULL, &vertMod) != VK_SUCCESS)
        { printf("ERROR: vert shader\n"); exitCode = 1; goto cleanup; }
        smci.codeSize = cow_frag_spv_len;
        smci.pCode = (const uint32_t *)cow_frag_spv;
        if (vkCreateShaderModule(device, &smci, NULL, &fragMod) != VK_SUCCESS)
        { printf("ERROR: frag shader\n"); exitCode = 1; goto cleanup; }
    }

    /*=== Pipeline layout: push constant (mat4 MVP) + descriptor set (texture) ===*/
    {
        VkPushConstantRange pcRange;
        memset(&pcRange, 0, sizeof(pcRange));
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0;
        pcRange.size = 64; /* sizeof(mat4) */

        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &descLayout;
        if (vkCreatePipelineLayout(device, &plci, NULL, &pipLayout) != VK_SUCCESS)
        { printf("ERROR: pipeline layout\n"); exitCode = 1; goto cleanup; }
    }

    /*=== Graphics pipeline ===*/
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

        /* Vertex input: pos(vec3) + uv(vec2) = 20 bytes */
        VkVertexInputBindingDescription vbd;
        memset(&vbd, 0, sizeof(vbd));
        vbd.binding = 0;
        vbd.stride = sizeof(struct CowVertex);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vad[2];
        memset(vad, 0, sizeof(vad));
        vad[0].location = 0; vad[0].binding = 0;
        vad[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vad[0].offset = 0; /* pos */
        vad[1].location = 1; vad[1].binding = 0;
        vad[1].format = VK_FORMAT_R32G32_SFLOAT;
        vad[1].offset = 12; /* uv (after 3 floats) */

        VkPipelineVertexInputStateCreateInfo vis;
        memset(&vis, 0, sizeof(vis));
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount = 1;
        vis.pVertexBindingDescriptions = &vbd;
        vis.vertexAttributeDescriptionCount = 2;
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
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
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
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
        gpci.layout = pipLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline) != VK_SUCCESS)
        { printf("ERROR: pipeline\n"); exitCode = 1; goto cleanup; }

        /* Cosmos pipeline: opaque background, no depth test, no cull */
        VkPipelineDepthStencilStateCreateInfo cosmosDs;
        memset(&cosmosDs, 0, sizeof(cosmosDs));
        cosmosDs.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        cosmosDs.depthTestEnable = VK_FALSE;
        cosmosDs.depthWriteEnable = VK_FALSE;

        VkPipelineRasterizationStateCreateInfo cosmosRs;
        memset(&cosmosRs, 0, sizeof(cosmosRs));
        cosmosRs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        cosmosRs.polygonMode = VK_POLYGON_MODE_FILL;
        cosmosRs.cullMode = VK_CULL_MODE_NONE;
        cosmosRs.lineWidth = 1.0f;

        gpci.pRasterizationState = &cosmosRs;
        gpci.pDepthStencilState = &cosmosDs;
        gpci.pColorBlendState = &cb;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &cosmosPipeline) != VK_SUCCESS)
        { printf("ERROR: cosmos pipeline\n"); exitCode = 1; goto cleanup; }
    }

    printf("Rendering cow... close window to exit.\n");

    /*=== Render loop ===*/
    float angleY = 0.0f;
    uint32_t frame = 0;
    uint32_t fpsFrameCount = 0;
    clock_t startClock = clock();
    clock_t fpsClockStart = startClock;
    char titleBuf[64];
    BOOL running = TRUE;

    while (running)
    {
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

        /* Compute MVP matrix -- orthographic projection matching original */
        angleY += 0.02f;
        if (angleY > 2.0f * (float)M_PI) angleY -= 2.0f * (float)M_PI;

        float rotY[16], model[16];
        vkm_mat4_rotate_y(rotY, angleY);
        vkm_mat4_identity(model);
        vkm_mat4_multiply(model, rotY, model);

        float view[16];
        vkm_mat4_translate(view, 0.0f, 0.0f, -200.0f);

        float modelView[16];
        vkm_mat4_multiply(modelView, view, model);

        /* Orthographic projection matching original: +/-COW_SIZE on each axis */
        float proj[16];
        vkm_mat4_identity(proj);
        {
            float l = -COW_SIZE, r = COW_SIZE;
            float b = -COW_SIZE, t = COW_SIZE;
            float n = -500.0f, f = 500.0f;
            proj[0]  =  2.0f / (r - l);
            proj[5]  =  2.0f / (t - b);
            proj[10] = -2.0f / (f - n);
            proj[12] = -(r + l) / (r - l);
            proj[13] = -(t + b) / (t - b);
            proj[14] = -(f + n) / (f - n);
        }

        float mvp[16];
        vkm_mat4_multiply(mvp, proj, modelView);

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
        ca.clearValue.color.float32[0] = 0.0f;
        ca.clearValue.color.float32[1] = 0.0f;
        ca.clearValue.color.float32[2] = 0.0f;
        ca.clearValue.color.float32[3] = 1.0f;

        /* Depth attachment */
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

        VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, {innerW, innerH} };
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        /* 1) Draw cosmos background (opaque fullscreen quad, no depth) */
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, cosmosPipeline);

        float identity[16];
        vkm_mat4_identity(identity);
        vkCmdPushConstants(cmdBuf, pipLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 64, identity);

        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipLayout, 0, 1, &cosmosDescSet, 0, NULL);

        VkDeviceSize vtxOffset = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &cosmosVtxBuf, &vtxOffset);
        vkCmdBindIndexBuffer(cmdBuf, cosmosIdxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmdBuf, 6, 1, 0, 0, 0);

        /* 2) Draw cow on top (opaque, depth test for self-occlusion) */
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdPushConstants(cmdBuf, pipLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 64, mvp);

        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipLayout, 0, 1, &descSet, 0, NULL);

        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOffset);
        vkCmdBindIndexBuffer(cmdBuf, idxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmdBuf, trianglesCount * 3, 1, 0, 0, 0);

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
        fpsFrameCount++;

        /* Update FPS in window title approximately every second */
        {
            clock_t now = clock();
            double elapsed = (double)(now - fpsClockStart) / (double)CLOCKS_PER_SEC;
            if (elapsed >= 1.0)
            {
                double fps = (double)fpsFrameCount / elapsed;
                snprintf(titleBuf, sizeof(titleBuf), "Vulkan Cow3D - %.2f FPS", fps);
                SetWindowTitles(window, titleBuf, (STRPTR)~0);
                fpsFrameCount = 0;
                fpsClockStart = now;
            }
        }
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
    if (fence) vkDestroyFence(device, fence, NULL);
    if (cmdBuf) vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, NULL);
    if (cosmosPipeline) vkDestroyPipeline(device, cosmosPipeline, NULL);
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (pipLayout) vkDestroyPipelineLayout(device, pipLayout, NULL);
    if (fragMod) vkDestroyShaderModule(device, fragMod, NULL);
    if (vertMod) vkDestroyShaderModule(device, vertMod, NULL);
    if (descPool) vkDestroyDescriptorPool(device, descPool, NULL);
    if (descLayout) vkDestroyDescriptorSetLayout(device, descLayout, NULL);
    if (texSampler) vkDestroySampler(device, texSampler, NULL);
    if (cosmosView) vkDestroyImageView(device, cosmosView, NULL);
    if (cosmosMem) vkFreeMemory(device, cosmosMem, NULL);
    if (cosmosImage) vkDestroyImage(device, cosmosImage, NULL);
    if (texView) vkDestroyImageView(device, texView, NULL);
    if (texMem) vkFreeMemory(device, texMem, NULL);
    if (texImage) vkDestroyImage(device, texImage, NULL);
    if (cosmosIdxBuf) vkDestroyBuffer(device, cosmosIdxBuf, NULL);
    if (cosmosIdxMem) vkFreeMemory(device, cosmosIdxMem, NULL);
    if (cosmosVtxBuf) vkDestroyBuffer(device, cosmosVtxBuf, NULL);
    if (cosmosVtxMem) vkFreeMemory(device, cosmosVtxMem, NULL);
    if (idxBuf) vkDestroyBuffer(device, idxBuf, NULL);
    if (idxMem) vkFreeMemory(device, idxMem, NULL);
    if (vtxBuf) vkDestroyBuffer(device, vtxBuf, NULL);
    if (vtxMem) vkFreeMemory(device, vtxMem, NULL);
    if (depthView) vkDestroyImageView(device, depthView, NULL);
    if (depthMem) vkFreeMemory(device, depthMem, NULL);
    if (depthImage) vkDestroyImage(device, depthImage, NULL);
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
    if (cowVerts) free(cowVerts);
    if (texPixels) free(texPixels);
    if (cosmosPixels) free(cosmosPixels);

    printf("Done (exit %d)\n", exitCode);
    return exitCode;
}
