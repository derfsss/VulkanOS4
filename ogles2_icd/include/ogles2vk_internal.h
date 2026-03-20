#ifndef OGLES2VK_INTERNAL_H
#define OGLES2VK_INTERNAL_H

/*
** ogles2vk_internal.h -- Internal structures for ogles2_vk.library
**
** This is the W3D Nova hardware-accelerated Vulkan ICD for AmigaOS 4.
** It implements Vulkan rendering by translating to the Warp3DNova API.
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

/*------------------------------------------------------------------------
** Version info
**----------------------------------------------------------------------*/
#define LIBNAME     "ogles2_vk.library"
#define LIBVER      1
#define LIBREV      1
#define LIBVSTRING  "ogles2_vk.library 1.1 (19.03.2026)"

/*------------------------------------------------------------------------
** Library base structure
**----------------------------------------------------------------------*/
struct OGLES2VKLibBase
{
    struct Library          lib_Lib;
    BPTR                    lib_SegList;
    struct ExecIFace       *lib_IExec;
};

/*------------------------------------------------------------------------
** Instance state
**----------------------------------------------------------------------*/
typedef struct OGLES2VKInstance
{
    uint32_t apiVersion;
} OGLES2VKInstance;

/*------------------------------------------------------------------------
** Physical device state (singleton -- first W3D Nova GPU)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKPhysicalDevice
{
    VkPhysicalDeviceProperties        properties;
    VkPhysicalDeviceFeatures          features;
    VkPhysicalDeviceMemoryProperties  memoryProperties;
    VkQueueFamilyProperties           queueFamilyProperties;
    /* W3D Nova state */
    void *w3dGpu;  /* W3DN_Gpu* -- stored as void* to avoid header dependency */
} OGLES2VKPhysicalDevice;

/*------------------------------------------------------------------------
** Queue state
**----------------------------------------------------------------------*/
typedef struct OGLES2VKQueue
{
    uint32_t familyIndex;
    uint32_t queueIndex;
} OGLES2VKQueue;

/*------------------------------------------------------------------------
** Device state
** OGLES2 context is NOT created here -- it requires a window from WSI.
** The context pointer will be set when the first swapchain is created.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKDevice
{
    OGLES2VKPhysicalDevice *physicalDevice;
    OGLES2VKQueue           queue;
    void                *glContext;   /* OGLES2 context (aglCreateContextTags2) */
} OGLES2VKDevice;

/*------------------------------------------------------------------------
** Memory allocation
** All allocations use system RAM (host-visible, host-coherent).
** GPU VRAM is managed internally by W3D Nova when resources are created.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKMemory
{
    void        *data;
    VkDeviceSize size;
    void        *mapped;
    uint32_t     memoryTypeIndex;
} OGLES2VKMemory;

/*------------------------------------------------------------------------
** Buffer state
** CPU-side metadata. GL VBOs are created lazily at draw time.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKBuffer
{
    VkDeviceSize        size;
    VkBufferUsageFlags  usage;
    OGLES2VKMemory        *boundMemory;
    VkDeviceSize        boundOffset;
} OGLES2VKBuffer;

/*------------------------------------------------------------------------
** Image state
** CPU-side metadata. GL textures are created lazily at draw time.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKImage
{
    uint32_t            width;
    uint32_t            height;
    uint32_t            depth;
    uint32_t            mipLevels;
    uint32_t            arrayLayers;
    VkFormat            format;
    VkImageType         imageType;
    VkImageUsageFlags   usage;
    VkImageTiling       tiling;
    OGLES2VKMemory        *boundMemory;
    VkDeviceSize        boundOffset;
    VkDeviceSize        memorySize;
} OGLES2VKImage;

/*------------------------------------------------------------------------
** Image view state
**----------------------------------------------------------------------*/
typedef struct OGLES2VKImageView
{
    OGLES2VKImage             *image;
    VkImageViewType         viewType;
    VkFormat                format;
    VkImageSubresourceRange subresourceRange;
} OGLES2VKImageView;

/*------------------------------------------------------------------------
** Sampler state
** Records filter/wrap modes. GL sampler state applied at draw time.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKSampler
{
    VkFilter               magFilter;
    VkFilter               minFilter;
    VkSamplerMipmapMode    mipmapMode;
    VkSamplerAddressMode   addressModeU;
    VkSamplerAddressMode   addressModeV;
    VkSamplerAddressMode   addressModeW;
    float                  maxAnisotropy;
    VkBool32               anisotropyEnable;
} OGLES2VKSampler;

/*------------------------------------------------------------------------
** Descriptor set layout
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_DESCRIPTOR_BINDINGS 8

typedef struct OGLES2VKDescriptorSetLayout
{
    uint32_t                       bindingCount;
    VkDescriptorSetLayoutBinding   bindings[OGLES2VK_MAX_DESCRIPTOR_BINDINGS];
} OGLES2VKDescriptorSetLayout;

/*------------------------------------------------------------------------
** Descriptor pool
**----------------------------------------------------------------------*/
typedef struct OGLES2VKDescriptorPool
{
    uint32_t maxSets;
} OGLES2VKDescriptorPool;

/*------------------------------------------------------------------------
** Descriptor binding
** Stores either a buffer reference or an image/sampler pair.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKDescriptorBinding
{
    VkDescriptorType  type;
    /* Uniform buffer */
    OGLES2VKBuffer      *buffer;
    VkDeviceSize      offset;
    VkDeviceSize      range;
    /* Combined image sampler */
    OGLES2VKImageView   *imageView;
    OGLES2VKSampler     *sampler;
} OGLES2VKDescriptorBinding;

/*------------------------------------------------------------------------
** Descriptor set
**----------------------------------------------------------------------*/
typedef struct OGLES2VKDescriptorSet
{
    OGLES2VKDescriptorBinding bindings[OGLES2VK_MAX_DESCRIPTOR_BINDINGS];
    uint32_t               bindingCount;
} OGLES2VKDescriptorSet;

/*------------------------------------------------------------------------
** Shader module
** Stores SPIR-V code. GPU compilation happens lazily at first draw.
**----------------------------------------------------------------------*/
typedef struct OGLES2VKShaderModule
{
    uint32_t       *codeOrig;    /* Original SPIR-V bytes (LE) */
    uint32_t        wordCount;
    uint32_t        codeSize;    /* Size in bytes */
    uint32_t        glShader;    /* GLuint shader handle (0 = not compiled) */
} OGLES2VKShaderModule;

/*------------------------------------------------------------------------
** Pipeline layout
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_PUSH_CONSTANT_RANGES 4
#define OGLES2VK_MAX_DESCRIPTOR_SETS      4

typedef struct OGLES2VKPipelineLayout
{
    uint32_t             pushConstantRangeCount;
    VkPushConstantRange  pushConstantRanges[OGLES2VK_MAX_PUSH_CONSTANT_RANGES];
    uint32_t             setLayoutCount;
    void                *setLayouts[OGLES2VK_MAX_DESCRIPTOR_SETS];
} OGLES2VKPipelineLayout;

/*------------------------------------------------------------------------
** Pipeline cache (no-op)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKPipelineCache
{
    uint32_t placeholder;
} OGLES2VKPipelineCache;

/*------------------------------------------------------------------------
** SPIR-V to GLSL transpile result (SPIRV-Cross)
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_SAMPLERS 8

typedef struct OGLES2VKTranspileResult
{
    char    *glsl;                /* GLSL source (AllocVecTags, caller frees) */
    /* Push constant vec4 array */
    char     pcArrayName[64];    /* "PushConstants" or "" if none */
    uint32_t pcArraySize;        /* Number of vec4s (0 = no push constants) */
    /* Sampler info from reflection */
    uint32_t samplerCount;
    struct {
        char     name[64];       /* Uniform name in GLSL (e.g. "tex") */
        uint32_t set;
        uint32_t binding;
    } samplers[OGLES2VK_MAX_SAMPLERS];
} OGLES2VKTranspileResult;

/*------------------------------------------------------------------------
** Graphics pipeline
** Stores shader refs + full pipeline state. GL shader program
** created lazily at first draw.
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_VERTEX_BINDINGS    8
#define OGLES2VK_MAX_VERTEX_ATTRIBUTES 16

typedef struct OGLES2VKPipeline
{
    OGLES2VKShaderModule      *vertShader;
    OGLES2VKShaderModule      *fragShader;
    OGLES2VKPipelineLayout    *layout;
    uint32_t                glProgram;    /* GLuint program (0 = not linked) */

    /* Vertex input */
    uint32_t                vertexBindingCount;
    VkVertexInputBindingDescription vertexBindings[OGLES2VK_MAX_VERTEX_BINDINGS];
    uint32_t                vertexAttributeCount;
    VkVertexInputAttributeDescription vertexAttributes[OGLES2VK_MAX_VERTEX_ATTRIBUTES];

    /* Input assembly */
    VkPrimitiveTopology     topology;

    /* Rasterisation */
    VkCullModeFlags         cullMode;
    VkFrontFace             frontFace;
    VkPolygonMode           polygonMode;
    float                   lineWidth;

    /* Color blend */
    VkBool32                blendEnable;

    /* Depth testing */
    VkBool32                depthTestEnable;
    VkBool32                depthWriteEnable;
    VkCompareOp             depthCompareOp;

    /* Rendering format (Vulkan 1.3 dynamic rendering) */
    VkFormat                colorAttachmentFormat;
    VkFormat                depthAttachmentFormat;

    /* Push constant array (SPIRV-Cross flatten-ubo) */
    int32_t                 pcArrayLoc;      /* glGetUniformLocation for vec4 array */
    uint32_t                pcArraySize;     /* N vec4s to upload (0 = none) */

    /* Sampler locations (indexed by slot, from transpile result) */
    int32_t                 samplerLocs[OGLES2VK_MAX_SAMPLERS];
    uint32_t                samplerCount;

} OGLES2VKPipeline;

/*------------------------------------------------------------------------
** Command buffer types
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_COMMANDS          256
#define OGLES2VK_MAX_PUSH_CONSTANT_SIZE 128
#define OGLES2VK_MAX_ATTACHMENTS        8

typedef enum OGLES2VKCommandType
{
    OGLES2VK_CMD_BIND_PIPELINE,
    OGLES2VK_CMD_SET_VIEWPORT,
    OGLES2VK_CMD_SET_SCISSOR,
    OGLES2VK_CMD_DRAW,
    OGLES2VK_CMD_BEGIN_RENDERING,
    OGLES2VK_CMD_END_RENDERING,
    OGLES2VK_CMD_PUSH_CONSTANTS,
    OGLES2VK_CMD_BIND_VERTEX_BUFFERS,
    OGLES2VK_CMD_BIND_INDEX_BUFFER,
    OGLES2VK_CMD_DRAW_INDEXED,
    OGLES2VK_CMD_BIND_DESCRIPTOR_SETS,
    OGLES2VK_CMD_BEGIN_RENDER_PASS,
    OGLES2VK_CMD_END_RENDER_PASS,
    OGLES2VK_CMD_NEXT_SUBPASS,
    OGLES2VK_CMD_PIPELINE_BARRIER,
    OGLES2VK_CMD_SET_CULL_MODE,
    OGLES2VK_CMD_SET_FRONT_FACE,
    OGLES2VK_CMD_SET_PRIMITIVE_TOPOLOGY,
    OGLES2VK_CMD_SET_DEPTH_TEST_ENABLE,
    OGLES2VK_CMD_SET_DEPTH_WRITE_ENABLE,
    OGLES2VK_CMD_SET_DEPTH_COMPARE_OP,
    OGLES2VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE,
    OGLES2VK_CMD_SET_STENCIL_TEST_ENABLE,
    OGLES2VK_CMD_SET_STENCIL_OP,
    OGLES2VK_CMD_SET_RASTERIZER_DISCARD_ENABLE,
    OGLES2VK_CMD_SET_DEPTH_BIAS_ENABLE,
    OGLES2VK_CMD_SET_PRIMITIVE_RESTART_ENABLE,
    OGLES2VK_CMD_COPY_BUFFER,
    OGLES2VK_CMD_COPY_BUFFER_TO_IMAGE,
    OGLES2VK_CMD_COPY_IMAGE_TO_BUFFER,
    OGLES2VK_CMD_COPY_IMAGE,
    OGLES2VK_CMD_FILL_BUFFER,
    OGLES2VK_CMD_UPDATE_BUFFER,
    /* 100% API coverage: Dynamic state */
    OGLES2VK_CMD_SET_LINE_WIDTH,
    OGLES2VK_CMD_SET_DEPTH_BIAS,
    OGLES2VK_CMD_SET_BLEND_CONSTANTS,
    OGLES2VK_CMD_SET_DEPTH_BOUNDS_VAL,
    OGLES2VK_CMD_SET_STENCIL_COMPARE_MASK,
    OGLES2VK_CMD_SET_STENCIL_WRITE_MASK,
    OGLES2VK_CMD_SET_STENCIL_REFERENCE,
    /* 100% API coverage: Indirect draw */
    OGLES2VK_CMD_DRAW_INDIRECT,
    OGLES2VK_CMD_DRAW_INDEXED_INDIRECT,
} OGLES2VKCommandType;

typedef struct OGLES2VKCommand
{
    OGLES2VKCommandType type;
    union {
        struct { OGLES2VKPipeline *pipeline; } bindPipeline;
        struct { VkViewport viewport; } setViewport;
        struct { VkRect2D scissor; } setScissor;
        struct { uint32_t vertexCount, instanceCount, firstVertex, firstInstance; } draw;
        struct {
            OGLES2VKImageView  *colorAttachment;
            VkAttachmentLoadOp loadOp;
            VkAttachmentStoreOp storeOp;
            VkClearValue   clearValue;
            VkRect2D       renderArea;
            OGLES2VKImageView *depthAttachment;
            VkAttachmentLoadOp depthLoadOp;
            VkClearValue   depthClearValue;
        } beginRendering;
        struct { uint32_t offset, size; uint8_t data[OGLES2VK_MAX_PUSH_CONSTANT_SIZE]; } pushConstants;
        struct {
            uint32_t     firstBinding, bindingCount;
            OGLES2VKBuffer *buffers[OGLES2VK_MAX_VERTEX_BINDINGS];
            VkDeviceSize offsets[OGLES2VK_MAX_VERTEX_BINDINGS];
        } bindVertexBuffers;
        struct { OGLES2VKBuffer *buffer; VkDeviceSize offset; VkIndexType indexType; } bindIndexBuffer;
        struct { uint32_t indexCount, instanceCount, firstIndex; int32_t vertexOffset; uint32_t firstInstance; } drawIndexed;
        struct { void *sets[OGLES2VK_MAX_DESCRIPTOR_SETS]; uint32_t firstSet, setCount; } bindDescriptorSets;
        struct { void *renderPass; void *framebuffer; VkRect2D renderArea; VkClearValue clearValues[OGLES2VK_MAX_ATTACHMENTS]; uint32_t clearValueCount; } beginRenderPass;
        struct { VkCullModeFlags cullMode; } setCullMode;
        struct { VkFrontFace frontFace; } setFrontFace;
        struct { VkPrimitiveTopology topology; } setPrimitiveTopology;
        struct { VkBool32 enable; } setDepthTestEnable;
        struct { VkBool32 enable; } setDepthWriteEnable;
        struct { VkCompareOp compareOp; } setDepthCompareOp;
        struct { VkBool32 enable; } setBoolEnable;
        struct { VkStencilFaceFlags faceMask; uint32_t failOp, passOp, depthFailOp, compareOp; } setStencilOp;
        struct { OGLES2VKBuffer *src, *dst; VkDeviceSize srcOffset, dstOffset, size; } copyBuffer;
        struct { OGLES2VKBuffer *srcBuffer; OGLES2VKImage *dstImage; VkDeviceSize bufferOffset; uint32_t bufferRowLength; VkOffset3D imageOffset; VkExtent3D imageExtent; } copyBufferToImage;
        struct { OGLES2VKImage *srcImage; OGLES2VKBuffer *dstBuffer; VkDeviceSize bufferOffset; uint32_t bufferRowLength; VkOffset3D imageOffset; VkExtent3D imageExtent; } copyImageToBuffer;
        struct { OGLES2VKImage *src, *dst; VkOffset3D srcOffset, dstOffset; VkExtent3D extent; } copyImage;
        struct { OGLES2VKBuffer *buffer; VkDeviceSize offset, size; uint32_t data; } fillBuffer;
        struct { OGLES2VKBuffer *buffer; VkDeviceSize offset, size; uint8_t data[64]; } updateBuffer;
        /* 100% API coverage: Dynamic state */
        struct { float lineWidth; } setLineWidth;
        struct { float constantFactor, clamp, slopeFactor; } setDepthBias;
        struct { float constants[4]; } setBlendConstants;
        struct { float minBounds, maxBounds; } setDepthBoundsVal;
        struct { VkStencilFaceFlags faceMask; uint32_t mask; } setStencilMask;
        struct { VkStencilFaceFlags faceMask; uint32_t reference; } setStencilRef;
        /* 100% API coverage: Indirect draw */
        struct { OGLES2VKBuffer *buffer; VkDeviceSize offset; uint32_t drawCount, stride; } drawIndirect;
    };
} OGLES2VKCommand;

typedef struct OGLES2VKCommandPool
{
    uint32_t queueFamilyIndex;
} OGLES2VKCommandPool;

typedef struct OGLES2VKCommandBuffer
{
    OGLES2VKCommand commands[OGLES2VK_MAX_COMMANDS];
    uint32_t     commandCount;
    uint32_t     recording;
} OGLES2VKCommandBuffer;

/*------------------------------------------------------------------------
** Synchronisation
**----------------------------------------------------------------------*/
typedef struct OGLES2VKFence
{
    VkBool32 signalled;
} OGLES2VKFence;

typedef struct OGLES2VKSemaphore
{
    uint32_t placeholder;
} OGLES2VKSemaphore;

/*------------------------------------------------------------------------
** Event state (100% API coverage)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKEvent
{
    VkBool32 signalled;
} OGLES2VKEvent;

/*------------------------------------------------------------------------
** Query pool state (100% API coverage)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKQueryPool
{
    VkQueryType  type;
    uint32_t     count;
    uint64_t    *results;
} OGLES2VKQueryPool;

/*------------------------------------------------------------------------
** Buffer view state (100% API coverage -- placeholder)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKBufferView
{
    uint32_t placeholder;
} OGLES2VKBufferView;

/*------------------------------------------------------------------------
** Private data slot state (100% API coverage -- placeholder)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKPrivateDataSlot
{
    uint32_t placeholder;
} OGLES2VKPrivateDataSlot;

/*------------------------------------------------------------------------
** Render pass state (100% API coverage)
**----------------------------------------------------------------------*/
typedef struct OGLES2VKRenderPass
{
    uint32_t              attachmentCount;
    VkAttachmentDescription attachments[OGLES2VK_MAX_ATTACHMENTS];
    uint32_t              colorAttachmentCount;
    VkAttachmentReference colorAttachments[OGLES2VK_MAX_ATTACHMENTS];
    VkAttachmentReference depthAttachment;
    VkBool32              hasDepth;
} OGLES2VKRenderPass;

typedef struct OGLES2VKFramebuffer
{
    OGLES2VKImageView *attachments[OGLES2VK_MAX_ATTACHMENTS];
    uint32_t       attachmentCount;
    uint32_t       width;
    uint32_t       height;
} OGLES2VKFramebuffer;

/*------------------------------------------------------------------------
** Externs -- shared across ICD source files
**----------------------------------------------------------------------*/
extern struct ExecIFace       *IExec;
extern OGLES2VKPhysicalDevice     g_physDevice;

/*------------------------------------------------------------------------
** ICD Vulkan function implementations
** Standard Vulkan calling convention -- NO APICALL, NO Self parameter.
** These are returned as PFN_vkVoidFunction by vk_icdGetInstanceProcAddr.
**----------------------------------------------------------------------*/

/* Instance */
VkResult ogles2vk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
void     ogles2vk_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_EnumerateInstanceVersion(uint32_t *pApiVersion);
VkResult ogles2vk_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);

/* Physical device */
VkResult ogles2vk_EnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
void ogles2vk_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
void ogles2vk_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
void ogles2vk_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
void ogles2vk_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
void ogles2vk_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties);
VkResult ogles2vk_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);

/* Device */
VkResult ogles2vk_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
void     ogles2vk_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator);
void     ogles2vk_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
VkResult ogles2vk_DeviceWaitIdle(VkDevice device);
VkResult ogles2vk_QueueWaitIdle(VkQueue queue);

/* Memory */
VkResult ogles2vk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
void     ogles2vk_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData);
void     ogles2vk_UnmapMemory(VkDevice device, VkDeviceMemory memory);

/* Buffer */
VkResult ogles2vk_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
void     ogles2vk_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
void     ogles2vk_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
VkResult ogles2vk_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);

/* Image */
VkResult ogles2vk_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage);
void     ogles2vk_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
void     ogles2vk_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
VkResult ogles2vk_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult ogles2vk_CreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView);
void     ogles2vk_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);

/* Sampler */
VkResult ogles2vk_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
void     ogles2vk_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);

/* Descriptor sets */
VkResult ogles2vk_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
void     ogles2vk_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
void     ogles2vk_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
VkResult ogles2vk_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);
VkResult ogles2vk_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);
void     ogles2vk_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);

/* Shader module */
VkResult ogles2vk_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
void     ogles2vk_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);

/* Pipeline layout */
VkResult ogles2vk_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
void     ogles2vk_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);

/* Pipeline cache */
VkResult ogles2vk_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache);
void     ogles2vk_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator);

/* Graphics pipeline */
VkResult ogles2vk_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
void     ogles2vk_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);

/* Command pool/buffer */
VkResult ogles2vk_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
void     ogles2vk_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
void     ogles2vk_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
VkResult ogles2vk_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
VkResult ogles2vk_EndCommandBuffer(VkCommandBuffer commandBuffer);
VkResult ogles2vk_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
void     ogles2vk_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
void     ogles2vk_TrimCommandPool(VkDevice device, VkCommandPool commandPool, uint32_t flags);

/* Command recording */
void ogles2vk_CmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bindPoint, VkPipeline pipeline);
void ogles2vk_CmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkViewport *pViewports);
void ogles2vk_CmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkRect2D *pScissors);
void ogles2vk_CmdDraw(VkCommandBuffer cb, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void ogles2vk_CmdDrawIndexed(VkCommandBuffer cb, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void ogles2vk_CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *pRenderingInfo);
void ogles2vk_CmdEndRendering(VkCommandBuffer cb);
void ogles2vk_CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues);
void ogles2vk_CmdBindVertexBuffers(VkCommandBuffer cb, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
void ogles2vk_CmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
void ogles2vk_CmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount, const VkDescriptorSet *pSets, uint32_t dynOffsetCount, const uint32_t *pDynOffsets);
void ogles2vk_CmdBeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo *pBegin, VkSubpassContents contents);
void ogles2vk_CmdEndRenderPass(VkCommandBuffer cb);
void ogles2vk_CmdNextSubpass(VkCommandBuffer cb, VkSubpassContents contents);
void ogles2vk_CmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkDependencyFlags deps, uint32_t memBarrierCount, const void *pMemBarriers, uint32_t bufBarrierCount, const void *pBufBarriers, uint32_t imgBarrierCount, const void *pImgBarriers);
void ogles2vk_CmdPipelineBarrier2(VkCommandBuffer cb, const void *pDependencyInfo);
void ogles2vk_CmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, uint32_t regionCount, const VkBufferCopy *pRegions);
void ogles2vk_CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, VkImageLayout dstLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
void ogles2vk_CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkBuffer dst, uint32_t regionCount, const VkBufferImageCopy *pRegions);
void ogles2vk_CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, uint32_t regionCount, const VkImageCopy *pRegions);
void ogles2vk_CmdFillBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, uint32_t data);
void ogles2vk_CmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, const void *pData);

/* Dynamic state */
void ogles2vk_CmdSetCullMode(VkCommandBuffer cb, VkCullModeFlags cullMode);
void ogles2vk_CmdSetFrontFace(VkCommandBuffer cb, VkFrontFace frontFace);
void ogles2vk_CmdSetPrimitiveTopology(VkCommandBuffer cb, VkPrimitiveTopology topology);
void ogles2vk_CmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count, const VkViewport *pViewports);
void ogles2vk_CmdSetScissorWithCount(VkCommandBuffer cb, uint32_t count, const VkRect2D *pScissors);
void ogles2vk_CmdBindVertexBuffers2(VkCommandBuffer cb, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes, const VkDeviceSize *pStrides);
void ogles2vk_CmdSetDepthTestEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetDepthWriteEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetDepthCompareOp(VkCommandBuffer cb, VkCompareOp compareOp);
void ogles2vk_CmdSetDepthBoundsTestEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetStencilTestEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
void ogles2vk_CmdSetRasterizerDiscardEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetDepthBiasEnable(VkCommandBuffer cb, VkBool32 enable);
void ogles2vk_CmdSetPrimitiveRestartEnable(VkCommandBuffer cb, VkBool32 enable);

/* Synchronisation */
VkResult ogles2vk_CreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
void     ogles2vk_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
VkResult ogles2vk_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
VkResult ogles2vk_GetFenceStatus(VkDevice device, VkFence fence);
VkResult ogles2vk_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore);
void     ogles2vk_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator);

/* Queue submission */
VkResult ogles2vk_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
VkResult ogles2vk_QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence);

/* Vulkan 1.1/1.2/1.3 wrappers */
void ogles2vk_GetPhysicalDeviceProperties2(VkPhysicalDevice physDev, VkPhysicalDeviceProperties2 *pProperties);
void ogles2vk_GetPhysicalDeviceFeatures2(VkPhysicalDevice physDev, VkPhysicalDeviceFeatures2 *pFeatures);
void ogles2vk_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physDev, uint32_t *pCount, VkQueueFamilyProperties2 *pProps);
void ogles2vk_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physDev, VkPhysicalDeviceMemoryProperties2 *pMemProps);
void ogles2vk_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physDev, VkFormat format, VkFormatProperties2 *pFormatProps);
void ogles2vk_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pReqs);
void ogles2vk_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pReqs);
VkResult ogles2vk_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos);
VkResult ogles2vk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos);
VkResult ogles2vk_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physDev, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *pProps);

/* Stubs for spec compliance */
VkResult ogles2vk_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
VkResult ogles2vk_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);

/* WSI cleanup */
void ogles2vk_CloseGraphics(void);

/* SPIR-V to GLSL transpiler (SPIRV-Cross) */
int ogles2vk_SPIRV2GLSL(const uint32_t *leCode, uint32_t wordCount, uint32_t codeSize, int isVertex, OGLES2VKTranspileResult *result);

/* Command execution (OGLES2 backend) */
void ogles2vk_ExecuteCommandBuffer(OGLES2VKDevice *dev, OGLES2VKCommandBuffer *cmd);
int  ogles2vk_InitOGLES2Context(OGLES2VKDevice *dev, void *window);
void ogles2vk_ShutdownOGLES2Context(OGLES2VKDevice *dev);

/*------------------------------------------------------------------------
** Swapchain state
**----------------------------------------------------------------------*/
#define OGLES2VK_MAX_SWAPCHAIN_IMAGES 3

/* Forward declare loader surface (defined in loader_internal.h) */
struct LoaderSurface
{
    void    *screen;    /* struct Screen* */
    void    *window;    /* struct Window* */
    uint32_t width;
    uint32_t height;
};

typedef struct OGLES2VKSwapchain
{
    OGLES2VKDevice          *device;
    struct LoaderSurface *surface;
    uint32_t              width;
    uint32_t              height;
    VkFormat              format;
    uint32_t              imageCount;
    OGLES2VKImage           *images[OGLES2VK_MAX_SWAPCHAIN_IMAGES];
    OGLES2VKMemory          *memory[OGLES2VK_MAX_SWAPCHAIN_IMAGES];
    uint32_t              currentImage;
} OGLES2VKSwapchain;

/* WSI functions */
VkResult ogles2vk_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physDev, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported);
VkResult ogles2vk_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physDev, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pCapabilities);
VkResult ogles2vk_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physDev, VkSurfaceKHR surface, uint32_t *pCount, VkSurfaceFormatKHR *pFormats);
VkResult ogles2vk_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physDev, VkSurfaceKHR surface, uint32_t *pCount, VkPresentModeKHR *pModes);
VkResult ogles2vk_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
void     ogles2vk_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pCount, VkImage *pImages);
VkResult ogles2vk_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);
VkResult ogles2vk_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);

/* 100% API coverage: Stub functions */
PFN_vkVoidFunction ogles2vk_GetDeviceProcAddr(VkDevice device, const char *pName);
void     ogles2vk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes);
void     ogles2vk_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout);
void     ogles2vk_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity);
void     ogles2vk_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue);
void     ogles2vk_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, VkDescriptorSetLayoutSupport *pSupport);
VkResult ogles2vk_GetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t *pToolCount, void *pToolProperties);
VkResult ogles2vk_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo, VkImageFormatProperties2 *pImageFormatProperties);
void     ogles2vk_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo, VkExternalBufferProperties *pExternalBufferProperties);
void     ogles2vk_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo, VkExternalFenceProperties *pExternalFenceProperties);
void     ogles2vk_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo, VkExternalSemaphoreProperties *pExternalSemaphoreProperties);
VkResult ogles2vk_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties);
VkResult ogles2vk_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
VkResult ogles2vk_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBufferView *pView);
void     ogles2vk_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSamplerYcbcrConversion *pYcbcrConversion);
void     ogles2vk_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate);
void     ogles2vk_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks *pAllocator);
void     ogles2vk_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData);
VkResult ogles2vk_CreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPrivateDataSlot *pPrivateDataSlot);
void     ogles2vk_DestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_SetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data);
void     ogles2vk_GetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t *pData);
VkDeviceAddress ogles2vk_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo);
uint64_t ogles2vk_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo);
uint64_t ogles2vk_GetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo);
VkResult ogles2vk_GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *pValue);
VkResult ogles2vk_WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout);
VkResult ogles2vk_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo);
VkResult ogles2vk_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData);
VkResult ogles2vk_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches);
void     ogles2vk_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t *pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements *pSparseMemoryRequirements);
void     ogles2vk_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo, uint32_t *pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements);
void     ogles2vk_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t *pPropertyCount, VkSparseImageFormatProperties *pProperties);
void     ogles2vk_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo, uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties);

/* 100% API coverage: Events */
VkResult ogles2vk_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkEvent *pEvent);
void     ogles2vk_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_GetEventStatus(VkDevice device, VkEvent event);
VkResult ogles2vk_SetEvent(VkDevice device, VkEvent event);
VkResult ogles2vk_ResetEvent(VkDevice device, VkEvent event);

/* 100% API coverage: Query pools */
VkResult ogles2vk_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool);
void     ogles2vk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags);
void     ogles2vk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
VkResult ogles2vk_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo *pBindInfo, VkFence fence);

/* 100% API coverage: Render pass + framebuffer */
VkResult ogles2vk_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
void     ogles2vk_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
void     ogles2vk_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
VkResult ogles2vk_CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);

/* 100% API coverage: Dynamic state + clear commands */
void ogles2vk_CmdSetLineWidth(VkCommandBuffer cb, float lineWidth);
void ogles2vk_CmdSetDepthBias(VkCommandBuffer cb, float constantFactor, float clamp, float slopeFactor);
void ogles2vk_CmdSetBlendConstants(VkCommandBuffer cb, const float blendConstants[4]);
void ogles2vk_CmdSetDepthBounds(VkCommandBuffer cb, float minDepthBounds, float maxDepthBounds);
void ogles2vk_CmdSetStencilCompareMask(VkCommandBuffer cb, VkStencilFaceFlags faceMask, uint32_t compareMask);
void ogles2vk_CmdSetStencilWriteMask(VkCommandBuffer cb, VkStencilFaceFlags faceMask, uint32_t writeMask);
void ogles2vk_CmdSetStencilReference(VkCommandBuffer cb, VkStencilFaceFlags faceMask, uint32_t reference);
void ogles2vk_CmdClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
void ogles2vk_CmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
void ogles2vk_CmdClearAttachments(VkCommandBuffer cb, uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects);
void ogles2vk_CmdBlitImage(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter);
void ogles2vk_CmdResolveImage(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve *pRegions);

/* 100% API coverage: Vulkan 1.3 "2" command variants */
void ogles2vk_CmdCopyBuffer2(VkCommandBuffer cb, const VkCopyBufferInfo2 *pCopyBufferInfo);
void ogles2vk_CmdCopyImage2(VkCommandBuffer cb, const VkCopyImageInfo2 *pCopyImageInfo);
void ogles2vk_CmdCopyBufferToImage2(VkCommandBuffer cb, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo);
void ogles2vk_CmdCopyImageToBuffer2(VkCommandBuffer cb, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo);
void ogles2vk_CmdBlitImage2(VkCommandBuffer cb, const VkBlitImageInfo2 *pBlitImageInfo);
void ogles2vk_CmdResolveImage2(VkCommandBuffer cb, const VkResolveImageInfo2 *pResolveImageInfo);

/* 100% API coverage: Command no-ops + events + queries + render pass 2 */
void ogles2vk_CmdSetDeviceMask(VkCommandBuffer cb, uint32_t deviceMask);
void ogles2vk_CmdDispatch(VkCommandBuffer cb, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void ogles2vk_CmdDispatchBase(VkCommandBuffer cb, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void ogles2vk_CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset);
void ogles2vk_CmdExecuteCommands(VkCommandBuffer cb, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
void ogles2vk_CmdSetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stageMask);
void ogles2vk_CmdResetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stageMask);
void ogles2vk_CmdWaitEvents(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers);
void ogles2vk_CmdSetEvent2(VkCommandBuffer cb, VkEvent event, const VkDependencyInfo *pDependencyInfo);
void ogles2vk_CmdResetEvent2(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags2 stageMask);
void ogles2vk_CmdWaitEvents2(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents, const VkDependencyInfo *pDependencyInfos);
void ogles2vk_CmdBeginQuery(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags);
void ogles2vk_CmdEndQuery(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t query);
void ogles2vk_CmdResetQueryPool(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
void ogles2vk_CmdWriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);
void ogles2vk_CmdWriteTimestamp2(VkCommandBuffer cb, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query);
void ogles2vk_CmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
void ogles2vk_CmdBeginRenderPass2(VkCommandBuffer cb, const VkRenderPassBeginInfo *pRenderPassBegin, const VkSubpassBeginInfo *pSubpassBeginInfo);
void ogles2vk_CmdNextSubpass2(VkCommandBuffer cb, const VkSubpassBeginInfo *pSubpassBeginInfo, const VkSubpassEndInfo *pSubpassEndInfo);
void ogles2vk_CmdEndRenderPass2(VkCommandBuffer cb, const VkSubpassEndInfo *pSubpassEndInfo);

/* 100% API coverage: Indirect draw */
void ogles2vk_CmdDrawIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void ogles2vk_CmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void ogles2vk_CmdDrawIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);
void ogles2vk_CmdDrawIndexedIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);

#endif /* OGLES2VK_INTERNAL_H */
