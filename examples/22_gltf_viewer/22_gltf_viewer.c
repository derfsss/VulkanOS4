/*
** Vulkan glTF Viewer -- AmigaOS 4
**
** Demonstrates loading 3D models from glTF 2.0
** files using cgltf (https://github.com/jkuhlmann/cgltf, Johannes
** Kuhlmann, MIT license). Falls back to a built-in procedural cube
** if cgltf is not available or the model file is not found.
**
** Also demonstrates:
** - Vertex buffers with position + normal
** - Index buffers (TRIANGLE_LIST)
** - mat4 MVP push constants (via vk_math.h)
** - Backface culling, rotating model
**
** To enable glTF loading, download cgltf.h into external/cgltf/
** and compile with -DUSE_CGLTF. Without it, a procedural cube
** is rendered instead.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o gltf_viewer gltf_viewer.c -lvulkan_loader -lauto -lm
**
** With cgltf:
**          ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__ -DUSE_CGLTF
**          -I../../external/cgltf
**          -o gltf_viewer gltf_viewer.c -lvulkan_loader -lauto -lm
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

#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"
#include "../common/vkex_loop.h"

#ifdef USE_CGLTF
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#endif

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

struct Vertex { float pos[3]; float nor[3]; };

/*------------------------------------------------------------------------
** Built-in fallback cube (same geometry as 13_solid_cube)
**----------------------------------------------------------------------*/
static const struct Vertex g_cubeVerts[] = {
    /* Front */
    {{-0.5f,-0.5f,-0.5f},{0,0,-1}}, {{0.5f,-0.5f,-0.5f},{0,0,-1}},
    {{0.5f,0.5f,-0.5f},{0,0,-1}},   {{-0.5f,0.5f,-0.5f},{0,0,-1}},
    /* Back */
    {{0.5f,-0.5f,0.5f},{0,0,1}},    {{-0.5f,-0.5f,0.5f},{0,0,1}},
    {{-0.5f,0.5f,0.5f},{0,0,1}},    {{0.5f,0.5f,0.5f},{0,0,1}},
    /* Right */
    {{0.5f,-0.5f,-0.5f},{1,0,0}},   {{0.5f,-0.5f,0.5f},{1,0,0}},
    {{0.5f,0.5f,0.5f},{1,0,0}},     {{0.5f,0.5f,-0.5f},{1,0,0}},
    /* Left */
    {{-0.5f,-0.5f,0.5f},{-1,0,0}},  {{-0.5f,-0.5f,-0.5f},{-1,0,0}},
    {{-0.5f,0.5f,-0.5f},{-1,0,0}},  {{-0.5f,0.5f,0.5f},{-1,0,0}},
    /* Top */
    {{-0.5f,0.5f,-0.5f},{0,1,0}},   {{0.5f,0.5f,-0.5f},{0,1,0}},
    {{0.5f,0.5f,0.5f},{0,1,0}},     {{-0.5f,0.5f,0.5f},{0,1,0}},
    /* Bottom */
    {{-0.5f,-0.5f,0.5f},{0,-1,0}},  {{0.5f,-0.5f,0.5f},{0,-1,0}},
    {{0.5f,-0.5f,-0.5f},{0,-1,0}},  {{-0.5f,-0.5f,-0.5f},{0,-1,0}},
};
static const uint16_t g_cubeIndices[] = {
    0,1,2,2,3,0, 4,5,6,6,7,4, 8,9,10,10,11,8,
    12,13,14,14,15,12, 16,17,18,18,19,16, 20,21,22,22,23,20,
};
#define CUBE_VCOUNT (sizeof(g_cubeVerts) / sizeof(g_cubeVerts[0]))
#define CUBE_ICOUNT (sizeof(g_cubeIndices) / sizeof(g_cubeIndices[0]))

/*------------------------------------------------------------------------
** Model data container
**----------------------------------------------------------------------*/
typedef struct ModelData {
    struct Vertex *vertices;
    uint16_t      *indices;
    uint32_t       vertCount;
    uint32_t       idxCount;
    int            allocated; /* 1 if vertices/indices were malloc'd */
} ModelData;

/*------------------------------------------------------------------------
** Load model from glTF file (requires cgltf) or use fallback cube
**----------------------------------------------------------------------*/
static int load_model(const char *filename, ModelData *model)
{
    memset(model, 0, sizeof(*model));

#ifdef USE_CGLTF
    if (filename)
    {
        cgltf_options options;
        cgltf_data *data = NULL;
        cgltf_result result;

        memset(&options, 0, sizeof(options));
        result = cgltf_parse_file(&options, filename, &data);
        if (result != cgltf_result_success)
        {
            printf("Could not parse '%s' (error %d)\n", filename, (int)result);
            goto fallback;
        }

        result = cgltf_load_buffers(&options, data, filename);
        if (result != cgltf_result_success)
        {
            printf("Could not load buffers for '%s'\n", filename);
            cgltf_free(data);
            goto fallback;
        }

        /* Extract first mesh, first primitive */
        if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
        {
            printf("No meshes found in '%s'\n", filename);
            cgltf_free(data);
            goto fallback;
        }

        cgltf_primitive *prim = &data->meshes[0].primitives[0];
        cgltf_accessor *posAcc = NULL;
        cgltf_accessor *norAcc = NULL;
        cgltf_size ai;

        for (ai = 0; ai < prim->attributes_count; ai++)
        {
            if (prim->attributes[ai].type == cgltf_attribute_type_position)
                posAcc = prim->attributes[ai].data;
            else if (prim->attributes[ai].type == cgltf_attribute_type_normal)
                norAcc = prim->attributes[ai].data;
        }

        if (!posAcc)
        {
            printf("No position attribute in '%s'\n", filename);
            cgltf_free(data);
            goto fallback;
        }

        model->vertCount = (uint32_t)posAcc->count;
        if (model->vertCount > 0xFFFF)
        {
            printf("Too many vertices (%u) in '%s'\n", model->vertCount, filename);
            cgltf_free(data);
            goto fallback;
        }

        model->vertices = (struct Vertex *)malloc(model->vertCount * sizeof(struct Vertex));
        if (!model->vertices)
        {
            cgltf_free(data);
            goto fallback;
        }

        /* Read positions and normals */
        {
            cgltf_size vi;
            for (vi = 0; vi < posAcc->count; vi++)
            {
                cgltf_accessor_read_float(posAcc, vi, model->vertices[vi].pos, 3);
                if (norAcc && vi < norAcc->count)
                    cgltf_accessor_read_float(norAcc, vi, model->vertices[vi].nor, 3);
                else
                {
                    model->vertices[vi].nor[0] = 0.0f;
                    model->vertices[vi].nor[1] = 1.0f;
                    model->vertices[vi].nor[2] = 0.0f;
                }
            }
        }

        /* Read indices */
        if (prim->indices)
        {
            model->idxCount = (uint32_t)prim->indices->count;
            model->indices = (uint16_t *)malloc(model->idxCount * sizeof(uint16_t));
            if (!model->indices)
            {
                free(model->vertices);
                cgltf_free(data);
                goto fallback;
            }
            {
                cgltf_size ii;
                for (ii = 0; ii < prim->indices->count; ii++)
                    model->indices[ii] = (uint16_t)cgltf_accessor_read_index(prim->indices, ii);
            }
        }
        else
        {
            /* Non-indexed: generate sequential indices */
            uint32_t ii;
            model->idxCount = model->vertCount;
            model->indices = (uint16_t *)malloc(model->idxCount * sizeof(uint16_t));
            if (!model->indices)
            {
                free(model->vertices);
                cgltf_free(data);
                goto fallback;
            }
            for (ii = 0; ii < model->idxCount; ii++)
                model->indices[ii] = (uint16_t)ii;
        }

        model->allocated = 1;
        printf("Loaded '%s': %u vertices, %u indices\n",
               filename, model->vertCount, model->idxCount);
        cgltf_free(data);
        return 0;
    }
fallback:
#else
    (void)filename;
    printf("cgltf not available (compile with -DUSE_CGLTF).\n");
#endif

    printf("Using built-in cube model.\n");
    model->vertCount = CUBE_VCOUNT;
    model->idxCount = CUBE_ICOUNT;
    model->vertices = (struct Vertex *)malloc(CUBE_VCOUNT * sizeof(struct Vertex));
    model->indices = (uint16_t *)malloc(CUBE_ICOUNT * sizeof(uint16_t));
    if (!model->vertices || !model->indices)
    {
        free(model->vertices);
        free(model->indices);
        model->vertices = NULL;
        model->indices = NULL;
        return -1;
    }
    memcpy(model->vertices, g_cubeVerts, CUBE_VCOUNT * sizeof(struct Vertex));
    memcpy(model->indices, g_cubeIndices, CUBE_ICOUNT * sizeof(uint16_t));
    model->allocated = 1;
    return 0;
}

static void free_model(ModelData *model)
{
    if (model->allocated)
    {
        free(model->vertices);
        free(model->indices);
    }
    memset(model, 0, sizeof(*model));
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
    VkBuffer         idxBuf    = VK_NULL_HANDLE;
    VkDeviceMemory   idxMem    = VK_NULL_HANDLE;
    struct Screen   *screen    = NULL;
    struct Window   *window    = NULL;
    VkImage         *swapImgs  = NULL;
    VkImageView     *swapViews = NULL;
    uint32_t         imgCount  = 0;
    int exitCode = 0;
    ModelData model;

    const char *modelFile = vkex_positional(argc, argv, 0);

    printf("=== Vulkan glTF Viewer ===\n");
    if (modelFile)
        printf("Model file: %s\n", modelFile);

    if (!IVulkan) { printf("ERROR: No vulkan.library\n"); return 1; }

    /* Load model */
    memset(&model, 0, sizeof(model));
    if (load_model(modelFile, &model) != 0)
    { printf("ERROR: model loading failed\n"); return 1; }
    printf("Model: %u vertices, %u indices\n", model.vertCount, model.idxCount);

    /* Instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "glTF Viewer";
    ai.apiVersion = VK_API_VERSION_1_3;

    const char *iExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_AMIGA_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iExts;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
    { printf("ERROR: vkCreateInstance\n"); free_model(&model); return 1; }

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
        WA_Title, (Tag)"Vulkan glTF Viewer",
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

    /* Shaders */
    {
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
    }

    /* Pipeline layout with mat4 push constant */
    {
        VkPushConstantRange pcRange;
        memset(&pcRange, 0, sizeof(pcRange));
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.size = 64;
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
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

        VkVertexInputAttributeDescription viads[2];
        memset(viads, 0, sizeof(viads));
        viads[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        viads[1].location = 1;
        viads[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        viads[1].offset = 12;

        VkPipelineVertexInputStateCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vibd;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = viads;

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
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
        VkDeviceSize vbSize = model.vertCount * sizeof(struct Vertex);
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = vbSize;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (vkCreateBuffer(device, &bci, NULL, &vtxBuf) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, vtxBuf, &mr);
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        if (vkAllocateMemory(device, &mai, NULL, &vtxMem) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        if (vkBindBufferMemory(device, vtxBuf, vtxMem, 0) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        void *mapped = NULL;
        if (vkMapMemory(device, vtxMem, 0, vbSize, 0, &mapped) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        memcpy(mapped, model.vertices, (size_t)vbSize);
        vkUnmapMemory(device, vtxMem);
    }

    /* Index buffer */
    {
        VkDeviceSize ibSize = model.idxCount * sizeof(uint16_t);
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = ibSize;
        bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (vkCreateBuffer(device, &bci, NULL, &idxBuf) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, idxBuf, &mr);
        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        if (vkAllocateMemory(device, &mai, NULL, &idxMem) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        if (vkBindBufferMemory(device, idxBuf, idxMem, 0) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }

        void *mapped = NULL;
        if (vkMapMemory(device, idxMem, 0, ibSize, 0, &mapped) != VK_SUCCESS)
        { exitCode = 1; goto cleanup; }
        memcpy(mapped, model.indices, (size_t)ibSize);
        vkUnmapMemory(device, idxMem);
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

    printf("Rendering model (%u tris)... close window to exit.\n",
           model.idxCount / 3);

    /* Render loop */
    float angleY = 0.0f;
    uint32_t frame = 0;
    clock_t startClock = clock();
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

        /* Compute MVP */
        angleY += 0.015f;
        if (angleY > 2.0f * (float)M_PI) angleY -= 2.0f * (float)M_PI;

        float rotY[16], rotX[16], rot[16], proj[16], mvp[16];
        vkm_mat4_rotate_y(rotY, angleY);
        vkm_mat4_rotate_x(rotX, 0.5f);
        vkm_mat4_multiply(rot, rotX, rotY);

        float aspect = (float)innerW / (float)innerH;
        vkm_mat4_perspective(proj, (float)(M_PI / 4.0), aspect, 0.1f, 10.0f);

        float view[16], modelView[16];
        vkm_mat4_translate(view, 0.0f, 0.0f, -3.0f);
        vkm_mat4_multiply(modelView, view, rot);
        vkm_mat4_multiply(mvp, proj, modelView);

        /* Record */
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
        ca.clearValue.color.float32[0] = 0.05f;
        ca.clearValue.color.float32[1] = 0.05f;
        ca.clearValue.color.float32[2] = 0.1f;
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
        vkCmdPushConstants(cmdBuf, pipLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp);

        VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, {innerW, innerH} };
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        VkDeviceSize vtxOff = 0;
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOff);
        vkCmdBindIndexBuffer(cmdBuf, idxBuf, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmdBuf, model.idxCount, 1, 0, 0, 0);

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
    if (pipeline) vkDestroyPipeline(device, pipeline, NULL);
    if (pipLayout) vkDestroyPipelineLayout(device, pipLayout, NULL);
    if (fragMod) vkDestroyShaderModule(device, fragMod, NULL);
    if (vertMod) vkDestroyShaderModule(device, vertMod, NULL);
    if (idxBuf) vkDestroyBuffer(device, idxBuf, NULL);
    if (idxMem) vkFreeMemory(device, idxMem, NULL);
    if (vtxBuf) vkDestroyBuffer(device, vtxBuf, NULL);
    if (vtxMem) vkFreeMemory(device, vtxMem, NULL);
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

    free_model(&model);
    printf("Done (exit %d)\n", exitCode);
    return exitCode;
}
