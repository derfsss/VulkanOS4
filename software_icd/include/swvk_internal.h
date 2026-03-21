#ifndef SWVK_INTERNAL_H
#define SWVK_INTERNAL_H

/*
** swvk_internal.h -- Internal structures for software_vk.library
**
** This is the software Vulkan ICD for AmigaOS 4. It implements
** Vulkan rendering in software (CPU only, no GPU required).
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

/*------------------------------------------------------------------------
** Version info
**----------------------------------------------------------------------*/
#define LIBNAME     "software_vk.library"
#define LIBVER      1
#define LIBREV      2
#define LIBVSTRING  "software_vk.library 1.2 (21.03.2026)"

/*------------------------------------------------------------------------
** Library base structure
**----------------------------------------------------------------------*/
struct SWVKLibBase
{
    struct Library          lib_Lib;
    BPTR                    lib_SegList;
    struct ExecIFace       *lib_IExec;
};

/*------------------------------------------------------------------------
** Instance state
**----------------------------------------------------------------------*/
typedef struct SWVKInstance
{
    uint32_t apiVersion;
} SWVKInstance;

/*------------------------------------------------------------------------
** Physical device state (singleton -- CPU software renderer)
**----------------------------------------------------------------------*/
typedef struct SWVKPhysicalDevice
{
    VkPhysicalDeviceProperties        properties;
    VkPhysicalDeviceFeatures          features;
    VkPhysicalDeviceMemoryProperties  memoryProperties;
    VkQueueFamilyProperties           queueFamilyProperties;
} SWVKPhysicalDevice;

/*------------------------------------------------------------------------
** Memory allocation state
**----------------------------------------------------------------------*/
typedef struct SWVKMemory
{
    void        *data;          /* Allocated system RAM */
    VkDeviceSize size;          /* Allocation size in bytes */
    void        *mapped;        /* Mapped pointer (NULL if not mapped) */
} SWVKMemory;

/*------------------------------------------------------------------------
** Buffer state
**----------------------------------------------------------------------*/
typedef struct SWVKBuffer
{
    VkDeviceSize       size;
    VkBufferUsageFlags usage;
    SWVKMemory        *boundMemory;     /* NULL until bound */
    VkDeviceSize       boundOffset;
} SWVKBuffer;

/*------------------------------------------------------------------------
** Image state
**----------------------------------------------------------------------*/
typedef struct SWVKImage
{
    uint32_t      width;
    uint32_t      height;
    uint32_t      depth;
    uint32_t      mipLevels;
    uint32_t      arrayLayers;
    VkFormat      format;
    VkImageType   imageType;
    VkImageLayout currentLayout;
    VkImageUsageFlags usage;
    SWVKMemory   *boundMemory;          /* NULL until bound */
    VkDeviceSize  boundOffset;
    VkDeviceSize  memorySize;           /* Computed size in bytes */
} SWVKImage;

/*------------------------------------------------------------------------
** Image view state
**----------------------------------------------------------------------*/
typedef struct SWVKImageView
{
    SWVKImage          *image;
    VkFormat            format;
    VkImageViewType     viewType;
    VkComponentMapping  components;
    VkImageSubresourceRange subresourceRange;
} SWVKImageView;

/*------------------------------------------------------------------------
** Shader module state
**----------------------------------------------------------------------*/
struct SpvModule;  /* Forward declaration -- defined in swvk_spirv.h */

typedef struct SWVKShaderModule
{
    uint32_t         *code;       /* Byte-swapped SPIR-V words */
    uint32_t          wordCount;
    struct SpvModule *parsed;     /* Parsed SPIR-V module */
} SWVKShaderModule;

/*------------------------------------------------------------------------
** Sampler state
**----------------------------------------------------------------------*/
typedef struct SWVKSampler
{
    VkFilter              magFilter;
    VkFilter              minFilter;
    VkSamplerAddressMode  addressModeU;
    VkSamplerAddressMode  addressModeV;
} SWVKSampler;

/*------------------------------------------------------------------------
** Descriptor set types
**----------------------------------------------------------------------*/
#define SWVK_MAX_DESCRIPTOR_BINDINGS 8
#define SWVK_MAX_DESCRIPTOR_SETS     4

typedef struct SWVKDescriptorSetLayout
{
    uint32_t                       bindingCount;
    VkDescriptorSetLayoutBinding   bindings[SWVK_MAX_DESCRIPTOR_BINDINGS];
} SWVKDescriptorSetLayout;

typedef struct SWVKDescriptorPool
{
    uint32_t maxSets;
} SWVKDescriptorPool;

/* Per-binding descriptor data */
typedef struct SWVKDescriptorBinding
{
    VkDescriptorType  type;
    /* For uniform buffers */
    SWVKBuffer       *buffer;
    VkDeviceSize      offset;
    VkDeviceSize      range;
    /* For combined image sampler */
    SWVKImageView    *imageView;
    SWVKSampler      *sampler;
} SWVKDescriptorBinding;

typedef struct SWVKDescriptorSet
{
    SWVKDescriptorBinding bindings[SWVK_MAX_DESCRIPTOR_BINDINGS];
    uint32_t              bindingCount;
} SWVKDescriptorSet;

/*------------------------------------------------------------------------
** Pipeline layout
**----------------------------------------------------------------------*/
#define SWVK_MAX_PUSH_CONSTANT_SIZE 128
#define SWVK_MAX_PUSH_CONSTANT_RANGES 4

typedef struct SWVKPipelineLayout
{
    uint32_t             pushConstantRangeCount;
    VkPushConstantRange  pushConstantRanges[SWVK_MAX_PUSH_CONSTANT_RANGES];
    uint32_t             setLayoutCount;
    SWVKDescriptorSetLayout *setLayouts[SWVK_MAX_DESCRIPTOR_SETS];
} SWVKPipelineLayout;

/*------------------------------------------------------------------------
** Pipeline cache (no-op)
**----------------------------------------------------------------------*/
typedef struct SWVKPipelineCache
{
    uint32_t placeholder;
} SWVKPipelineCache;

/*------------------------------------------------------------------------
** Graphics pipeline
**----------------------------------------------------------------------*/
#define SWVK_MAX_VERTEX_BINDINGS    8
#define SWVK_MAX_VERTEX_ATTRIBUTES 16

typedef struct SWVKPipeline
{
    SWVKShaderModule  *vertShader;
    SWVKShaderModule  *fragShader;
    SWVKPipelineLayout *layout;

    /* Vertex input */
    uint32_t           vertexBindingCount;
    VkVertexInputBindingDescription vertexBindings[SWVK_MAX_VERTEX_BINDINGS];
    uint32_t           vertexAttributeCount;
    VkVertexInputAttributeDescription vertexAttributes[SWVK_MAX_VERTEX_ATTRIBUTES];

    /* Input assembly */
    VkPrimitiveTopology topology;

    /* Rasterisation */
    VkCullModeFlags    cullMode;
    VkFrontFace        frontFace;
    VkPolygonMode      polygonMode;
    float              lineWidth;

    /* Color blend */
    VkBool32           blendEnable;

    /* Depth testing */
    VkBool32           depthTestEnable;
    VkBool32           depthWriteEnable;
    VkCompareOp        depthCompareOp;

    /* Rendering format (Vulkan 1.3 dynamic rendering) */
    VkFormat           colorAttachmentFormat;
    VkFormat           depthAttachmentFormat;
} SWVKPipeline;

/*------------------------------------------------------------------------
** Render pass / framebuffer state
**----------------------------------------------------------------------*/
#define SWVK_MAX_ATTACHMENTS 8

typedef struct SWVKRenderPass
{
    uint32_t              attachmentCount;
    VkAttachmentDescription attachments[SWVK_MAX_ATTACHMENTS];
    /* Single subpass */
    uint32_t              colorAttachmentCount;
    VkAttachmentReference colorAttachments[SWVK_MAX_ATTACHMENTS];
    VkAttachmentReference depthAttachment;
    VkBool32              hasDepth;
} SWVKRenderPass;

typedef struct SWVKFramebuffer
{
    SWVKImageView *attachments[SWVK_MAX_ATTACHMENTS];
    uint32_t       attachmentCount;
    uint32_t       width;
    uint32_t       height;
} SWVKFramebuffer;

/*------------------------------------------------------------------------
** Command buffer types
**----------------------------------------------------------------------*/
#define SWVK_MAX_COMMANDS 256

typedef enum SWVKCommandType
{
    SWVK_CMD_BIND_PIPELINE,
    SWVK_CMD_SET_VIEWPORT,
    SWVK_CMD_SET_SCISSOR,
    SWVK_CMD_DRAW,
    SWVK_CMD_BEGIN_RENDERING,
    SWVK_CMD_END_RENDERING,
    SWVK_CMD_PUSH_CONSTANTS,
    SWVK_CMD_BIND_VERTEX_BUFFERS,
    SWVK_CMD_BIND_INDEX_BUFFER,
    SWVK_CMD_DRAW_INDEXED,
    SWVK_CMD_BIND_DESCRIPTOR_SETS,
    /* Legacy render pass */
    SWVK_CMD_BEGIN_RENDER_PASS,
    SWVK_CMD_END_RENDER_PASS,
    SWVK_CMD_NEXT_SUBPASS,
    SWVK_CMD_PIPELINE_BARRIER,
    /* Dynamic state */
    SWVK_CMD_SET_CULL_MODE,
    SWVK_CMD_SET_FRONT_FACE,
    SWVK_CMD_SET_PRIMITIVE_TOPOLOGY,
    SWVK_CMD_SET_DEPTH_TEST_ENABLE,
    SWVK_CMD_SET_DEPTH_WRITE_ENABLE,
    SWVK_CMD_SET_DEPTH_COMPARE_OP,
    SWVK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE,
    SWVK_CMD_SET_STENCIL_TEST_ENABLE,
    SWVK_CMD_SET_STENCIL_OP,
    SWVK_CMD_SET_RASTERIZER_DISCARD_ENABLE,
    SWVK_CMD_SET_DEPTH_BIAS_ENABLE,
    SWVK_CMD_SET_PRIMITIVE_RESTART_ENABLE,
    /* Transfer commands */
    SWVK_CMD_COPY_BUFFER,
    SWVK_CMD_COPY_BUFFER_TO_IMAGE,
    SWVK_CMD_COPY_IMAGE_TO_BUFFER,
    SWVK_CMD_COPY_IMAGE,
    SWVK_CMD_FILL_BUFFER,
    SWVK_CMD_UPDATE_BUFFER,
    /* Dynamic state (additional) */
    SWVK_CMD_SET_LINE_WIDTH,
    SWVK_CMD_SET_DEPTH_BIAS,
    SWVK_CMD_SET_BLEND_CONSTANTS,
    SWVK_CMD_SET_DEPTH_BOUNDS_VAL,
    SWVK_CMD_SET_STENCIL_COMPARE_MASK,
    SWVK_CMD_SET_STENCIL_WRITE_MASK,
    SWVK_CMD_SET_STENCIL_REFERENCE,
    /* Indirect draw */
    SWVK_CMD_DRAW_INDIRECT,
    SWVK_CMD_DRAW_INDEXED_INDIRECT,
} SWVKCommandType;

typedef struct SWVKCommand
{
    SWVKCommandType type;
    union {
        struct {
            SWVKPipeline *pipeline;
        } bindPipeline;

        struct {
            VkViewport viewport;
        } setViewport;

        struct {
            VkRect2D scissor;
        } setScissor;

        struct {
            uint32_t vertexCount;
            uint32_t instanceCount;
            uint32_t firstVertex;
            uint32_t firstInstance;
        } draw;

        struct {
            SWVKImageView  *colorAttachment;
            VkAttachmentLoadOp loadOp;
            VkAttachmentStoreOp storeOp;
            VkClearValue   clearValue;
            VkRect2D       renderArea;
            /* Depth attachment */
            SWVKImageView  *depthAttachment;
            VkAttachmentLoadOp depthLoadOp;
            VkClearValue   depthClearValue;
        } beginRendering;

        struct {
            uint32_t offset;
            uint32_t size;
            uint8_t  data[SWVK_MAX_PUSH_CONSTANT_SIZE];
        } pushConstants;

        struct {
            uint32_t    firstBinding;
            uint32_t    bindingCount;
            SWVKBuffer *buffers[SWVK_MAX_VERTEX_BINDINGS];
            VkDeviceSize offsets[SWVK_MAX_VERTEX_BINDINGS];
        } bindVertexBuffers;

        struct {
            SWVKBuffer  *buffer;
            VkDeviceSize offset;
            VkIndexType  indexType;
        } bindIndexBuffer;

        struct {
            uint32_t indexCount;
            uint32_t instanceCount;
            uint32_t firstIndex;
            int32_t  vertexOffset;
            uint32_t firstInstance;
        } drawIndexed;

        struct {
            SWVKDescriptorSet *sets[SWVK_MAX_DESCRIPTOR_SETS];
            uint32_t firstSet;
            uint32_t setCount;
        } bindDescriptorSets;

        /* Legacy render pass */
        struct {
            SWVKRenderPass  *renderPass;
            SWVKFramebuffer *framebuffer;
            VkRect2D         renderArea;
            VkClearValue     clearValues[SWVK_MAX_ATTACHMENTS];
            uint32_t         clearValueCount;
        } beginRenderPass;

        /* Dynamic state (scalar values) */
        struct {
            VkCullModeFlags    cullMode;
        } setCullMode;
        struct {
            VkFrontFace        frontFace;
        } setFrontFace;
        struct {
            VkPrimitiveTopology topology;
        } setPrimitiveTopology;
        struct {
            VkBool32           enable;
        } setDepthTestEnable;
        struct {
            VkBool32           enable;
        } setDepthWriteEnable;
        struct {
            VkCompareOp        compareOp;
        } setDepthCompareOp;
        struct {
            VkBool32           enable;
        } setBoolEnable;     /* Shared by depth bounds, stencil, rasterizer discard, depth bias, primitive restart */

        /* Transfer commands */
        struct {
            SWVKBuffer  *src;
            SWVKBuffer  *dst;
            VkDeviceSize srcOffset;
            VkDeviceSize dstOffset;
            VkDeviceSize size;
        } copyBuffer;

        struct {
            SWVKBuffer  *srcBuffer;
            SWVKImage   *dstImage;
            VkDeviceSize bufferOffset;
            uint32_t     bufferRowLength;
            VkOffset3D   imageOffset;
            VkExtent3D   imageExtent;
        } copyBufferToImage;

        struct {
            SWVKImage   *srcImage;
            SWVKBuffer  *dstBuffer;
            VkDeviceSize bufferOffset;
            uint32_t     bufferRowLength;
            VkOffset3D   imageOffset;
            VkExtent3D   imageExtent;
        } copyImageToBuffer;

        struct {
            SWVKImage  *src;
            SWVKImage  *dst;
            VkOffset3D  srcOffset;
            VkOffset3D  dstOffset;
            VkExtent3D  extent;
        } copyImage;

        struct {
            SWVKBuffer  *buffer;
            VkDeviceSize offset;
            VkDeviceSize size;
            uint32_t     data;
        } fillBuffer;

        struct {
            SWVKBuffer  *buffer;
            VkDeviceSize offset;
            VkDeviceSize size;
            uint8_t      data[64]; /* Max 64 bytes for inline update */
        } updateBuffer;

        /* Dynamic state (additional) */
        struct {
            float lineWidth;
        } setLineWidth;
        struct {
            float constantFactor;
            float clamp;
            float slopeFactor;
        } setDepthBias;
        struct {
            float constants[4];
        } setBlendConstants;
        struct {
            float minBounds;
            float maxBounds;
        } setDepthBoundsVal;
        struct {
            VkStencilFaceFlags faceMask;
            uint32_t           mask;
        } setStencilMask;  /* Shared by compare mask and write mask */
        struct {
            VkStencilFaceFlags faceMask;
            uint32_t           reference;
        } setStencilRef;

        /* Indirect draw */
        struct {
            SWVKBuffer  *buffer;
            VkDeviceSize offset;
            uint32_t     drawCount;
            uint32_t     stride;
        } drawIndirect;
    };
} SWVKCommand;

typedef struct SWVKCommandPool
{
    uint32_t queueFamilyIndex;
} SWVKCommandPool;

typedef struct SWVKCommandBuffer
{
    SWVKCommand   commands[SWVK_MAX_COMMANDS];
    uint32_t      commandCount;
    uint32_t      recording;    /* 1 if between Begin/End */
} SWVKCommandBuffer;

/*------------------------------------------------------------------------
** Fence state
**----------------------------------------------------------------------*/
typedef struct SWVKFence
{
    VkBool32 signalled;
} SWVKFence;

/*------------------------------------------------------------------------
** Semaphore state (no-op for software renderer)
**----------------------------------------------------------------------*/
typedef struct SWVKSemaphore
{
    uint32_t placeholder;       /* No real state needed */
} SWVKSemaphore;

/*------------------------------------------------------------------------
** Event state
**----------------------------------------------------------------------*/
typedef struct SWVKEvent
{
    VkBool32 signalled;
} SWVKEvent;

/*------------------------------------------------------------------------
** Query pool state
**----------------------------------------------------------------------*/
typedef struct SWVKQueryPool
{
    VkQueryType  type;
    uint32_t     count;
    uint64_t    *results;
} SWVKQueryPool;

/*------------------------------------------------------------------------
** Buffer view state (placeholder)
**----------------------------------------------------------------------*/
typedef struct SWVKBufferView
{
    uint32_t placeholder;
} SWVKBufferView;

/*------------------------------------------------------------------------
** Private data slot state (placeholder)
**----------------------------------------------------------------------*/
typedef struct SWVKPrivateDataSlot
{
    uint32_t placeholder;
} SWVKPrivateDataSlot;

/*------------------------------------------------------------------------
** Swapchain state
**----------------------------------------------------------------------*/
#define SWVK_MAX_SWAPCHAIN_IMAGES 3

struct LoaderSurface;  /* Forward declaration -- defined in loader_wsi.c */

typedef struct SWVKSwapchain
{
    struct SWVKDevice       *device;
    struct LoaderSurface    *surface;
    uint32_t                 width;
    uint32_t                 height;
    VkFormat                 format;
    uint32_t                 imageCount;
    struct SWVKImage        *images[SWVK_MAX_SWAPCHAIN_IMAGES];
    struct SWVKMemory       *memory[SWVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t                 currentImage;
} SWVKSwapchain;

/*------------------------------------------------------------------------
** Queue state
**----------------------------------------------------------------------*/
typedef struct SWVKQueue
{
    struct SWVKDevice *device;
    uint32_t           familyIndex;
    uint32_t           queueIndex;
} SWVKQueue;

/*------------------------------------------------------------------------
** Device state
**----------------------------------------------------------------------*/
typedef struct SWVKDevice
{
    SWVKInstance *instance;
    SWVKQueue    graphicsQueue;
} SWVKDevice;

/*------------------------------------------------------------------------
** Externs -- shared across ICD source files
**----------------------------------------------------------------------*/
extern struct ExecIFace      *IExec;
extern SWVKPhysicalDevice     g_physDevice;

/*------------------------------------------------------------------------
** ICD Vulkan function implementations
** Standard Vulkan calling convention -- NO APICALL, NO Self parameter.
** These are returned as PFN_vkVoidFunction by vk_icdGetInstanceProcAddr.
**----------------------------------------------------------------------*/

/* Instance */
VkResult swvk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
void     swvk_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator);
VkResult swvk_EnumerateInstanceVersion(uint32_t *pApiVersion);
VkResult swvk_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);

/* Physical device enumeration */
VkResult swvk_EnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);

/* Physical device queries */
void swvk_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
void swvk_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
void swvk_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
void swvk_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
void swvk_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties);

/* Device */
VkResult swvk_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
void     swvk_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator);
void     swvk_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
VkResult swvk_DeviceWaitIdle(VkDevice device);
VkResult swvk_QueueWaitIdle(VkQueue queue);

/* Memory */
VkResult swvk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
void     swvk_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
VkResult swvk_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData);
void     swvk_UnmapMemory(VkDevice device, VkDeviceMemory memory);

/* Buffer */
VkResult swvk_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
void     swvk_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
void     swvk_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
VkResult swvk_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);

/* Image */
VkResult swvk_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage);
void     swvk_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
void     swvk_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
VkResult swvk_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);

/* Image view */
VkResult swvk_CreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView);
void     swvk_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);

/* Sampler */
VkResult swvk_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
void     swvk_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);

/* Shader module */
VkResult swvk_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
void     swvk_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);

/* Pipeline layout */
VkResult swvk_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
void     swvk_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);

/* Pipeline cache */
VkResult swvk_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache);
void     swvk_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator);

/* Descriptor sets */
VkResult swvk_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
void     swvk_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator);
VkResult swvk_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
void     swvk_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
VkResult swvk_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
VkResult swvk_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);
void     swvk_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);

/* Graphics pipeline */
VkResult swvk_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
void     swvk_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);

/* Command pool */
VkResult swvk_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
void     swvk_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);

/* Command buffer */
VkResult swvk_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
void     swvk_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
VkResult swvk_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
VkResult swvk_EndCommandBuffer(VkCommandBuffer commandBuffer);

/* Command recording */
void swvk_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
void swvk_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports);
void swvk_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors);
void swvk_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void swvk_CmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo);
void swvk_CmdEndRendering(VkCommandBuffer commandBuffer);
void swvk_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues);
void swvk_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
void swvk_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
void swvk_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void swvk_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);

/* Queue submit (draw execution) */
VkResult swvk_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);

/* Synchronisation */
VkResult swvk_CreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
void     swvk_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
VkResult swvk_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
VkResult swvk_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
VkResult swvk_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore);
void     swvk_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator);

/* Device extension enumeration */
VkResult swvk_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);

/* WSI surface queries */
VkResult swvk_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported);
VkResult swvk_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
VkResult swvk_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats);
VkResult swvk_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes);

/* WSI swapchain */
VkResult swvk_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
void     swvk_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
VkResult swvk_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages);
VkResult swvk_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);
VkResult swvk_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);

/* Stubs */
VkResult swvk_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
VkResult swvk_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
VkResult swvk_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
void     swvk_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers);
VkResult swvk_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
void     swvk_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags);
VkResult swvk_GetFenceStatus(VkDevice device, VkFence fence);
VkResult swvk_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData);
VkResult swvk_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches);
VkResult swvk_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);

/* Legacy render pass */
VkResult swvk_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
void     swvk_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
VkResult swvk_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
void     swvk_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
void     swvk_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents);
void     swvk_CmdEndRenderPass(VkCommandBuffer commandBuffer);
void     swvk_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents);

/* Vulkan 1.3 dynamic state */
void swvk_CmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode);
void swvk_CmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace);
void swvk_CmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology);
void swvk_CmdSetViewportWithCount(VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport *pViewports);
void swvk_CmdSetScissorWithCount(VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D *pScissors);
void swvk_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes, const VkDeviceSize *pStrides);
void swvk_CmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable);
void swvk_CmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable);
void swvk_CmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp);
void swvk_CmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable);
void swvk_CmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable);
void swvk_CmdSetStencilOp(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
void swvk_CmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable);
void swvk_CmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable);
void swvk_CmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable);

/* Transfer commands */
void swvk_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions);
void swvk_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
void swvk_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions);
void swvk_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions);
void swvk_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data);
void swvk_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData);

/* Vulkan 1.1/1.2/1.3 wrappers */
void swvk_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties);
void swvk_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures);
void swvk_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2 *pQueueFamilyProperties);
void swvk_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
void swvk_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2 *pFormatProperties);
void swvk_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pMemoryRequirements);
void swvk_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pMemoryRequirements);
VkResult swvk_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos);
VkResult swvk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos);
void swvk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer, const void *pDependencyInfo);
VkResult swvk_QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence);
VkResult swvk_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *pImageFormatProperties);

/* Stub functions */
PFN_vkVoidFunction swvk_GetDeviceProcAddr(VkDevice device, const char *pName);
void     swvk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes);
void     swvk_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout);
void     swvk_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D *pGranularity);
void     swvk_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue);
void     swvk_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, VkDescriptorSetLayoutSupport *pSupport);
VkResult swvk_GetPhysicalDeviceToolProperties(VkPhysicalDevice physicalDevice, uint32_t *pToolCount, void *pToolProperties);
VkResult swvk_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo, VkImageFormatProperties2 *pImageFormatProperties);
void     swvk_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo, VkExternalBufferProperties *pExternalBufferProperties);
void     swvk_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo, VkExternalFenceProperties *pExternalFenceProperties);
void     swvk_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo, VkExternalSemaphoreProperties *pExternalSemaphoreProperties);
VkResult swvk_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties);
VkResult swvk_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
VkResult swvk_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBufferView *pView);
void     swvk_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator);
VkResult swvk_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSamplerYcbcrConversion *pYcbcrConversion);
void     swvk_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks *pAllocator);
VkResult swvk_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate);
void     swvk_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks *pAllocator);
void     swvk_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData);
VkResult swvk_CreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPrivateDataSlot *pPrivateDataSlot);
void     swvk_DestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot, const VkAllocationCallbacks *pAllocator);
VkResult swvk_SetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t data);
void     swvk_GetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkPrivateDataSlot privateDataSlot, uint64_t *pData);
VkDeviceAddress swvk_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo);
uint64_t swvk_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo);
uint64_t swvk_GetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo);
VkResult swvk_GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *pValue);
VkResult swvk_WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout);
VkResult swvk_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo);
void     swvk_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t *pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements *pSparseMemoryRequirements);
void     swvk_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo, uint32_t *pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements);
void     swvk_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t *pPropertyCount, VkSparseImageFormatProperties *pProperties);
void     swvk_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo, uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties);

/* Event functions */
VkResult swvk_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkEvent *pEvent);
void     swvk_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator);
VkResult swvk_GetEventStatus(VkDevice device, VkEvent event);
VkResult swvk_SetEvent(VkDevice device, VkEvent event);
VkResult swvk_ResetEvent(VkDevice device, VkEvent event);

/* Query pool functions */
VkResult swvk_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool);
void     swvk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator);
VkResult swvk_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags);
void     swvk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
VkResult swvk_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo *pBindInfo, VkFence fence);

/* Render pass 2 */
VkResult swvk_CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);

/* Dynamic state + clear commands */
void swvk_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
void swvk_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
void swvk_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]);
void swvk_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds);
void swvk_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask);
void swvk_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask);
void swvk_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference);
void swvk_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
void swvk_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
void swvk_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects);
void swvk_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter);
void swvk_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve *pRegions);

/* Vulkan 1.3 "2" command variants */
void swvk_CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo);
void swvk_CmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo);
void swvk_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo);
void swvk_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo);
void swvk_CmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo);
void swvk_CmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo);

/* Command recording (no-ops, events, queries, render pass 2) */
void swvk_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask);
void swvk_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void swvk_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void swvk_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
void swvk_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
void swvk_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
void swvk_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
void swvk_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers);
void swvk_CmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event, const VkDependencyInfo *pDependencyInfo);
void swvk_CmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags2 stageMask);
void swvk_CmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents, const VkDependencyInfo *pDependencyInfos);
void swvk_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags);
void swvk_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
void swvk_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
void swvk_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);
void swvk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query);
void swvk_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
void swvk_CmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, const VkSubpassBeginInfo *pSubpassBeginInfo);
void swvk_CmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo, const VkSubpassEndInfo *pSubpassEndInfo);
void swvk_CmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo);

/* Indirect draw */
void swvk_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void swvk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void swvk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);
void swvk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride);

/* Graphics library cleanup (called from ICD expunge) */
void swvk_CloseGraphics(void);

#endif /* SWVK_INTERNAL_H */
