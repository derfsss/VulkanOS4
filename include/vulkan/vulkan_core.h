#ifndef VULKAN_CORE_H
#define VULKAN_CORE_H

/*
** Vulkan 1.3 Core API - Types, Enumerations, and Structures
** AmigaOS 4 Platform
**
** Vulkan structures use standard C alignment (natural packing).
** Do NOT apply #pragma pack(2) or #pragma amiga-align.
*/

#include <vulkan/vk_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------
** Vulkan version macros
**----------------------------------------------------------------------*/
#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | \
     (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

#define VK_API_VERSION_VARIANT(version) ((uint32_t)(version) >> 29U)
#define VK_API_VERSION_MAJOR(version)   (((uint32_t)(version) >> 22U) & 0x7FU)
#define VK_API_VERSION_MINOR(version)   (((uint32_t)(version) >> 12U) & 0x3FFU)
#define VK_API_VERSION_PATCH(version)   ((uint32_t)(version) & 0xFFFU)

#define VK_API_VERSION_1_0  VK_MAKE_API_VERSION(0, 1, 0, 0)
#define VK_API_VERSION_1_1  VK_MAKE_API_VERSION(0, 1, 1, 0)
#define VK_API_VERSION_1_2  VK_MAKE_API_VERSION(0, 1, 2, 0)
#define VK_API_VERSION_1_3  VK_MAKE_API_VERSION(0, 1, 3, 0)

#define VK_HEADER_VERSION        275
#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION)

/* Deprecated version macros (pre-1.2.175) -- needed by VMA and other third-party libs */
#define VK_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
#define VK_VERSION_MAJOR(version)   (((uint32_t)(version) >> 22U) & 0x7FU)
#define VK_VERSION_MINOR(version)   (((uint32_t)(version) >> 12U) & 0x3FFU)
#define VK_VERSION_PATCH(version)   ((uint32_t)(version) & 0xFFFU)

/*------------------------------------------------------------------------
** Constants
**----------------------------------------------------------------------*/
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE  256
#define VK_UUID_SIZE                      16
#define VK_MAX_MEMORY_TYPES               32
#define VK_MAX_MEMORY_HEAPS               16
#define VK_MAX_EXTENSION_NAME_SIZE        256
#define VK_MAX_DESCRIPTION_SIZE           256
#define VK_WHOLE_SIZE                     (~0ULL)
#define VK_ATTACHMENT_UNUSED              (~0U)
#define VK_QUEUE_FAMILY_IGNORED           (~0U)
#define VK_SUBPASS_EXTERNAL               (~0U)
#define VK_TRUE                           1
#define VK_FALSE                          0

#define VK_NULL_HANDLE 0

/*------------------------------------------------------------------------
** Handle definitions
**----------------------------------------------------------------------*/
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;

/* Dispatchable handles (pointers - contain dispatch table) */
VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkCommandBuffer)

/* Non-dispatchable handles (uint64_t - opaque identifiers) */
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkEvent)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkQueryPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBufferView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSamplerYcbcrConversion)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorUpdateTemplate)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPrivateDataSlot)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR)

/*------------------------------------------------------------------------
** Fundamental type definitions
**----------------------------------------------------------------------*/
typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef uint64_t VkFlags64;
typedef uint64_t VkDeviceAddress;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleMask;

/*------------------------------------------------------------------------
** Result codes
**----------------------------------------------------------------------*/
typedef enum VkResult {
    VK_SUCCESS                                  = 0,
    VK_NOT_READY                                = 1,
    VK_TIMEOUT                                  = 2,
    VK_EVENT_SET                                = 3,
    VK_EVENT_RESET                              = 4,
    VK_INCOMPLETE                               = 5,
    VK_SUBOPTIMAL_KHR                           = 1000001003,
    VK_ERROR_OUT_OF_HOST_MEMORY                 = -1,
    VK_ERROR_OUT_OF_DEVICE_MEMORY               = -2,
    VK_ERROR_INITIALIZATION_FAILED              = -3,
    VK_ERROR_DEVICE_LOST                        = -4,
    VK_ERROR_MEMORY_MAP_FAILED                  = -5,
    VK_ERROR_LAYER_NOT_PRESENT                  = -6,
    VK_ERROR_EXTENSION_NOT_PRESENT              = -7,
    VK_ERROR_FEATURE_NOT_PRESENT                = -8,
    VK_ERROR_INCOMPATIBLE_DRIVER                = -9,
    VK_ERROR_TOO_MANY_OBJECTS                   = -10,
    VK_ERROR_FORMAT_NOT_SUPPORTED               = -11,
    VK_ERROR_FRAGMENTED_POOL                    = -12,
    VK_ERROR_UNKNOWN                            = -13,
    VK_ERROR_OUT_OF_POOL_MEMORY                 = -1000069000,
    VK_ERROR_INVALID_EXTERNAL_HANDLE            = -1000072003,
    VK_ERROR_FRAGMENTATION                      = -1000161000,
    VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS      = -1000257000,
    VK_PIPELINE_COMPILE_REQUIRED                = 1000297000,
    VK_ERROR_SURFACE_LOST_KHR                   = -1000000000,
    VK_ERROR_NATIVE_WINDOW_IN_USE_KHR           = -1000000001,
    VK_ERROR_OUT_OF_DATE_KHR                    = -1000001004,
    VK_RESULT_MAX_ENUM                          = 0x7FFFFFFF
} VkResult;

/*------------------------------------------------------------------------
** Structure type enum
**----------------------------------------------------------------------*/
typedef enum VkStructureType {
    VK_STRUCTURE_TYPE_APPLICATION_INFO                              = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                          = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO                      = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO                            = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO                                   = 4,
    VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE                            = 6,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO                          = 5,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO                             = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO                         = 9,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO                            = 12,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO                             = 14,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO                        = 15,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO                     = 16,
    VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO                    = 17,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO             = 18,
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO       = 19,
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO     = 20,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO           = 22,
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO      = 23,
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO        = 24,
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO      = 25,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO        = 26,
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO            = 27,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO                   = 30,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO                       = 38,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO                      = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO                  = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO                     = 42,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO                        = 43,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO                       = 37,
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO                 = 28,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR                    = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR                             = 1000001001,
    /* Vulkan 1.3 dynamic rendering */
    VK_STRUCTURE_TYPE_RENDERING_INFO                               = 1000044000,
    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO                    = 1000044001,
    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO               = 1000044002,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES   = 1000044003,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES          = 53,
    /* AmigaOS WSI extension */
    /* Descriptor sets */
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO            = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO                  = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO                 = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET                         = 35,
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO                          = 31,
    VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO                    = 1000400000,
    /* Vulkan 1.1 / 1.2 / 1.3 "2" wrapper structs */
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2                  = 1000059000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2                = 1000059001,
    VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2                         = 1000059002,
    VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2                   = 1000059005,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2         = 1000059006,
    VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2           = 1000146000,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2            = 1000146001,
    VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2                       = 1000146003,
    VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO                     = 1000157000,
    VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO                      = 1000157001,
    VK_STRUCTURE_TYPE_SUBMIT_INFO_2                               = 1000314004,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO                  = 1000314005,
    VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO                       = 1000314006,
    VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2                   = 1000059003,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2         = 1000059004,
    /* 100% API coverage: additional sType values */
    VK_STRUCTURE_TYPE_EVENT_CREATE_INFO                            = 10,
    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO                       = 11,
    VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO                      = 13,
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO                 = 29,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2                          = 1000145003,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT                = 1000168001,
    VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO       = 1000085000,
    VK_STRUCTURE_TYPE_PRIVATE_DATA_SLOT_CREATE_INFO                = 1000295002,
    VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO                   = 1000244001,
    VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO    = 1000257004,
    VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO                          = 1000207004,
    VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO                        = 1000207005,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2                    = 1000109004,
    VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO                            = 1000109005,
    VK_STRUCTURE_TYPE_SUBPASS_END_INFO                              = 1000109006,
    VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2                            = 1000337000,
    VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2                             = 1000337001,
    VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2                   = 1000337002,
    VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2                   = 1000337003,
    VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2                             = 1000337004,
    VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2                          = 1000337005,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO         = 1000071002,
    VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES                   = 1000071003,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO          = 1000112000,
    VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES                    = 1000112001,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO      = 1000076000,
    VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES                = 1000076001,
    VK_STRUCTURE_TYPE_MAX_ENUM                                     = 0x7FFFFFFF
} VkStructureType;

/* Generic pNext chain traversal structures */
typedef struct VkBaseInStructure {
    VkStructureType                    sType;
    const struct VkBaseInStructure*    pNext;
} VkBaseInStructure;

typedef struct VkBaseOutStructure {
    VkStructureType                    sType;
    struct VkBaseOutStructure*         pNext;
} VkBaseOutStructure;

/*------------------------------------------------------------------------
** Enumerations
**----------------------------------------------------------------------*/
typedef enum VkPhysicalDeviceType {
    VK_PHYSICAL_DEVICE_TYPE_OTHER          = 0,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   = 2,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    = 3,
    VK_PHYSICAL_DEVICE_TYPE_CPU            = 4,
    VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM       = 0x7FFFFFFF
} VkPhysicalDeviceType;

typedef enum VkFormat {
    VK_FORMAT_UNDEFINED                = 0,
    VK_FORMAT_R8G8B8A8_UNORM           = 37,
    VK_FORMAT_R8G8B8A8_SRGB            = 43,
    VK_FORMAT_B8G8R8A8_UNORM           = 44,
    VK_FORMAT_B8G8R8A8_SRGB            = 50,
    VK_FORMAT_D16_UNORM                = 124,
    VK_FORMAT_D32_SFLOAT               = 126,
    VK_FORMAT_D24_UNORM_S8_UINT        = 129,
    VK_FORMAT_D32_SFLOAT_S8_UINT       = 130,
    VK_FORMAT_R32_SFLOAT               = 100,
    VK_FORMAT_R32G32_SFLOAT            = 103,
    VK_FORMAT_R32G32B32_SFLOAT         = 106,
    VK_FORMAT_R32G32B32A32_SFLOAT      = 109,
    VK_FORMAT_MAX_ENUM                 = 0x7FFFFFFF
} VkFormat;

typedef enum VkImageLayout {
    VK_IMAGE_LAYOUT_UNDEFINED                    = 0,
    VK_IMAGE_LAYOUT_GENERAL                      = 1,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL      = 2,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL      = 5,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL         = 6,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL         = 7,
    VK_IMAGE_LAYOUT_PREINITIALIZED               = 8,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR              = 1000001002,
    VK_IMAGE_LAYOUT_MAX_ENUM                     = 0x7FFFFFFF
} VkImageLayout;

typedef enum VkImageType {
    VK_IMAGE_TYPE_1D       = 0,
    VK_IMAGE_TYPE_2D       = 1,
    VK_IMAGE_TYPE_3D       = 2,
    VK_IMAGE_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkImageType;

typedef enum VkImageTiling {
    VK_IMAGE_TILING_OPTIMAL  = 0,
    VK_IMAGE_TILING_LINEAR   = 1,
    VK_IMAGE_TILING_MAX_ENUM = 0x7FFFFFFF
} VkImageTiling;

typedef enum VkImageViewType {
    VK_IMAGE_VIEW_TYPE_2D       = 1,
    VK_IMAGE_VIEW_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkImageViewType;

typedef enum VkSharingMode {
    VK_SHARING_MODE_EXCLUSIVE  = 0,
    VK_SHARING_MODE_CONCURRENT = 1,
    VK_SHARING_MODE_MAX_ENUM   = 0x7FFFFFFF
} VkSharingMode;

typedef enum VkComponentSwizzle {
    VK_COMPONENT_SWIZZLE_IDENTITY = 0,
    VK_COMPONENT_SWIZZLE_ZERO     = 1,
    VK_COMPONENT_SWIZZLE_ONE      = 2,
    VK_COMPONENT_SWIZZLE_R        = 3,
    VK_COMPONENT_SWIZZLE_G        = 4,
    VK_COMPONENT_SWIZZLE_B        = 5,
    VK_COMPONENT_SWIZZLE_A        = 6,
    VK_COMPONENT_SWIZZLE_MAX_ENUM = 0x7FFFFFFF
} VkComponentSwizzle;

typedef enum VkImageAspectFlagBits {
    VK_IMAGE_ASPECT_COLOR_BIT   = 0x00000001,
    VK_IMAGE_ASPECT_DEPTH_BIT   = 0x00000002,
    VK_IMAGE_ASPECT_STENCIL_BIT = 0x00000004,
    VK_IMAGE_ASPECT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkImageAspectFlagBits;
typedef VkFlags VkImageAspectFlags;

typedef enum VkPipelineBindPoint {
    VK_PIPELINE_BIND_POINT_GRAPHICS  = 0,
    VK_PIPELINE_BIND_POINT_COMPUTE   = 1,
    VK_PIPELINE_BIND_POINT_MAX_ENUM  = 0x7FFFFFFF
} VkPipelineBindPoint;

typedef enum VkCommandBufferLevel {
    VK_COMMAND_BUFFER_LEVEL_PRIMARY   = 0,
    VK_COMMAND_BUFFER_LEVEL_SECONDARY = 1,
    VK_COMMAND_BUFFER_LEVEL_MAX_ENUM  = 0x7FFFFFFF
} VkCommandBufferLevel;

typedef enum VkCommandBufferUsageFlagBits {
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT      = 0x00000001,
    VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT = 0x00000002,
    VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT     = 0x00000004,
    VK_COMMAND_BUFFER_USAGE_FLAG_BITS_MAX_ENUM       = 0x7FFFFFFF
} VkCommandBufferUsageFlagBits;
typedef VkFlags VkCommandBufferUsageFlags;

typedef enum VkPrimitiveTopology {
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST     = 0,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST      = 1,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP     = 2,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST  = 3,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 4,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN   = 5,
    VK_PRIMITIVE_TOPOLOGY_MAX_ENUM       = 0x7FFFFFFF
} VkPrimitiveTopology;

typedef enum VkPolygonMode {
    VK_POLYGON_MODE_FILL      = 0,
    VK_POLYGON_MODE_LINE      = 1,
    VK_POLYGON_MODE_POINT     = 2,
    VK_POLYGON_MODE_MAX_ENUM  = 0x7FFFFFFF
} VkPolygonMode;

typedef enum VkCullModeFlagBits {
    VK_CULL_MODE_NONE           = 0,
    VK_CULL_MODE_FRONT_BIT      = 0x00000001,
    VK_CULL_MODE_BACK_BIT       = 0x00000002,
    VK_CULL_MODE_FRONT_AND_BACK = 0x00000003,
    VK_CULL_MODE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkCullModeFlagBits;
typedef VkFlags VkCullModeFlags;

typedef enum VkFrontFace {
    VK_FRONT_FACE_COUNTER_CLOCKWISE = 0,
    VK_FRONT_FACE_CLOCKWISE         = 1,
    VK_FRONT_FACE_MAX_ENUM          = 0x7FFFFFFF
} VkFrontFace;

typedef enum VkCompareOp {
    VK_COMPARE_OP_NEVER            = 0,
    VK_COMPARE_OP_LESS             = 1,
    VK_COMPARE_OP_EQUAL            = 2,
    VK_COMPARE_OP_LESS_OR_EQUAL    = 3,
    VK_COMPARE_OP_GREATER          = 4,
    VK_COMPARE_OP_NOT_EQUAL        = 5,
    VK_COMPARE_OP_GREATER_OR_EQUAL = 6,
    VK_COMPARE_OP_ALWAYS           = 7,
    VK_COMPARE_OP_MAX_ENUM         = 0x7FFFFFFF
} VkCompareOp;

typedef enum VkFilter {
    VK_FILTER_NEAREST  = 0,
    VK_FILTER_LINEAR   = 1,
    VK_FILTER_MAX_ENUM = 0x7FFFFFFF
} VkFilter;

typedef enum VkSamplerMipmapMode {
    VK_SAMPLER_MIPMAP_MODE_NEAREST  = 0,
    VK_SAMPLER_MIPMAP_MODE_LINEAR   = 1,
    VK_SAMPLER_MIPMAP_MODE_MAX_ENUM = 0x7FFFFFFF
} VkSamplerMipmapMode;

typedef enum VkSamplerAddressMode {
    VK_SAMPLER_ADDRESS_MODE_REPEAT               = 0,
    VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT      = 1,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE        = 2,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER      = 3,
    VK_SAMPLER_ADDRESS_MODE_MAX_ENUM             = 0x7FFFFFFF
} VkSamplerAddressMode;

typedef enum VkBorderColor {
    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK = 0,
    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK      = 2,
    VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE      = 4,
    VK_BORDER_COLOR_MAX_ENUM                = 0x7FFFFFFF
} VkBorderColor;

typedef enum VkSampleCountFlagBits {
    VK_SAMPLE_COUNT_1_BIT  = 0x00000001,
    VK_SAMPLE_COUNT_MAX_ENUM = 0x7FFFFFFF
} VkSampleCountFlagBits;
typedef VkFlags VkSampleCountFlags;

typedef enum VkBlendFactor {
    VK_BLEND_FACTOR_ZERO                = 0,
    VK_BLEND_FACTOR_ONE                 = 1,
    VK_BLEND_FACTOR_SRC_ALPHA           = 6,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7,
    VK_BLEND_FACTOR_MAX_ENUM            = 0x7FFFFFFF
} VkBlendFactor;

typedef enum VkBlendOp {
    VK_BLEND_OP_ADD      = 0,
    VK_BLEND_OP_MAX_ENUM = 0x7FFFFFFF
} VkBlendOp;

typedef enum VkColorComponentFlagBits {
    VK_COLOR_COMPONENT_R_BIT = 0x00000001,
    VK_COLOR_COMPONENT_G_BIT = 0x00000002,
    VK_COLOR_COMPONENT_B_BIT = 0x00000004,
    VK_COLOR_COMPONENT_A_BIT = 0x00000008,
    VK_COLOR_COMPONENT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkColorComponentFlagBits;
typedef VkFlags VkColorComponentFlags;

typedef enum VkDynamicState {
    VK_DYNAMIC_STATE_VIEWPORT                    = 0,
    VK_DYNAMIC_STATE_SCISSOR                     = 1,
    VK_DYNAMIC_STATE_LINE_WIDTH                  = 2,
    VK_DYNAMIC_STATE_DEPTH_BIAS                  = 3,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS             = 4,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS                = 5,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK        = 6,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK          = 7,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE           = 8,
    /* Vulkan 1.3 extended dynamic state */
    VK_DYNAMIC_STATE_CULL_MODE                   = 1000267000,
    VK_DYNAMIC_STATE_FRONT_FACE                  = 1000267001,
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY           = 1000267002,
    VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT          = 1000267003,
    VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT           = 1000267004,
    VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE  = 1000267005,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE            = 1000267006,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE           = 1000267007,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP             = 1000267008,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE     = 1000267009,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE          = 1000267010,
    VK_DYNAMIC_STATE_STENCIL_OP                   = 1000267011,
    VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE    = 1000377001,
    VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE            = 1000377002,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE     = 1000377004,
    VK_DYNAMIC_STATE_MAX_ENUM                    = 0x7FFFFFFF
} VkDynamicState;

typedef enum VkShaderStageFlagBits {
    VK_SHADER_STAGE_VERTEX_BIT   = 0x00000001,
    VK_SHADER_STAGE_FRAGMENT_BIT = 0x00000010,
    VK_SHADER_STAGE_COMPUTE_BIT  = 0x00000020,
    VK_SHADER_STAGE_ALL_GRAPHICS = 0x0000001F,
    VK_SHADER_STAGE_ALL          = 0x7FFFFFFF,
    VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkShaderStageFlagBits;
typedef VkFlags VkShaderStageFlags;

typedef enum VkIndexType {
    VK_INDEX_TYPE_UINT16 = 0,
    VK_INDEX_TYPE_UINT32 = 1,
    VK_INDEX_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkIndexType;

typedef enum VkVertexInputRate {
    VK_VERTEX_INPUT_RATE_VERTEX   = 0,
    VK_VERTEX_INPUT_RATE_INSTANCE = 1,
    VK_VERTEX_INPUT_RATE_MAX_ENUM = 0x7FFFFFFF
} VkVertexInputRate;

typedef enum VkAttachmentLoadOp {
    VK_ATTACHMENT_LOAD_OP_LOAD      = 0,
    VK_ATTACHMENT_LOAD_OP_CLEAR     = 1,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2,
    VK_ATTACHMENT_LOAD_OP_MAX_ENUM  = 0x7FFFFFFF
} VkAttachmentLoadOp;

typedef enum VkAttachmentStoreOp {
    VK_ATTACHMENT_STORE_OP_STORE     = 0,
    VK_ATTACHMENT_STORE_OP_DONT_CARE = 1,
    VK_ATTACHMENT_STORE_OP_MAX_ENUM  = 0x7FFFFFFF
} VkAttachmentStoreOp;

typedef enum VkLogicOp {
    VK_LOGIC_OP_COPY     = 3,
    VK_LOGIC_OP_MAX_ENUM = 0x7FFFFFFF
} VkLogicOp;

typedef enum VkResolveModeFlagBits {
    VK_RESOLVE_MODE_NONE = 0,
    VK_RESOLVE_MODE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkResolveModeFlagBits;

typedef enum VkQueueFlagBits {
    VK_QUEUE_GRAPHICS_BIT       = 0x00000001,
    VK_QUEUE_COMPUTE_BIT        = 0x00000002,
    VK_QUEUE_TRANSFER_BIT       = 0x00000004,
    VK_QUEUE_SPARSE_BINDING_BIT = 0x00000008,
    VK_QUEUE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkQueueFlagBits;
typedef VkFlags VkQueueFlags;

typedef enum VkMemoryPropertyFlagBits {
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT        = 0x00000001,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT        = 0x00000002,
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT       = 0x00000004,
    VK_MEMORY_PROPERTY_HOST_CACHED_BIT         = 0x00000008,
    VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT    = 0x00000010,
    VK_MEMORY_PROPERTY_PROTECTED_BIT           = 0x00000020,
    VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM      = 0x7FFFFFFF
} VkMemoryPropertyFlagBits;
typedef VkFlags VkMemoryPropertyFlags;

typedef enum VkMemoryHeapFlagBits {
    VK_MEMORY_HEAP_DEVICE_LOCAL_BIT       = 0x00000001,
    VK_MEMORY_HEAP_FLAG_BITS_MAX_ENUM     = 0x7FFFFFFF
} VkMemoryHeapFlagBits;
typedef VkFlags VkMemoryHeapFlags;

typedef enum VkFenceCreateFlagBits {
    VK_FENCE_CREATE_SIGNALED_BIT       = 0x00000001,
    VK_FENCE_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkFenceCreateFlagBits;
typedef VkFlags VkFenceCreateFlags;

typedef enum VkPipelineStageFlagBits {
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT            = 0x00000001,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 0x00000400,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT         = 0x00002000,
    VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM         = 0x7FFFFFFF
} VkPipelineStageFlagBits;
typedef VkFlags VkPipelineStageFlags;

typedef enum VkDescriptorType {
    VK_DESCRIPTOR_TYPE_SAMPLER                  = 0,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER   = 1,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE            = 2,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE            = 3,
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER     = 4,
    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER     = 5,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER           = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER           = 7,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC   = 8,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC   = 9,
    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT         = 10,
    VK_DESCRIPTOR_TYPE_MAX_ENUM                 = 0x7FFFFFFF
} VkDescriptorType;

/* Flag types used in create info structs */
typedef VkFlags VkInstanceCreateFlags;
typedef VkFlags VkDeviceCreateFlags;
typedef VkFlags VkDeviceQueueCreateFlags;
typedef VkFlags VkMemoryMapFlags;
typedef VkFlags VkBufferCreateFlags;
typedef VkFlags VkBufferUsageFlags;
typedef VkFlags VkImageCreateFlags;
typedef VkFlags VkImageUsageFlags;
typedef VkFlags VkPipelineCreateFlags;
typedef VkFlags VkPipelineCacheCreateFlags;
typedef VkFlags VkPipelineShaderStageCreateFlags;
typedef VkFlags VkPipelineVertexInputStateCreateFlags;
typedef VkFlags VkPipelineInputAssemblyStateCreateFlags;
typedef VkFlags VkPipelineViewportStateCreateFlags;
typedef VkFlags VkPipelineRasterizationStateCreateFlags;
typedef VkFlags VkPipelineMultisampleStateCreateFlags;
typedef VkFlags VkPipelineDepthStencilStateCreateFlags;
typedef VkFlags VkPipelineColorBlendStateCreateFlags;
typedef VkFlags VkPipelineDynamicStateCreateFlags;
typedef VkFlags VkPipelineLayoutCreateFlags;
typedef VkFlags VkCommandPoolCreateFlags;
typedef VkFlags VkCommandPoolResetFlags;
typedef VkFlags VkCommandBufferUsageFlags;
typedef VkFlags VkCommandBufferResetFlags;
typedef VkFlags VkDescriptorSetLayoutCreateFlags;
typedef VkFlags VkDescriptorPoolCreateFlags;
typedef VkFlags VkDescriptorPoolResetFlags;
typedef VkFlags VkDependencyFlags;
typedef VkFlags VkRenderingFlags;
typedef VkFlags VkFormatFeatureFlags;
typedef VkFlags VkAccessFlags;
typedef VkFlags VkStencilFaceFlags;

/* Format feature flag bits */
#define VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT             0x00000001
#define VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT          0x00000080
#define VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT    0x00000100
#define VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT  0x00000200
#define VK_FORMAT_FEATURE_BLIT_SRC_BIT                  0x00000400
#define VK_FORMAT_FEATURE_BLIT_DST_BIT                  0x00000800
#define VK_FORMAT_FEATURE_TRANSFER_SRC_BIT              0x00004000
#define VK_FORMAT_FEATURE_TRANSFER_DST_BIT              0x00008000
#define VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT             0x00000040

/* Buffer usage flag bits */
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT  0x00000001
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT  0x00000002
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 0x00000080
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT  0x00000040
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT  0x00000010
#define VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT 0x00000100
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  0x00000020

/* Image usage flag bits */
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT         0x00000001
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT         0x00000002
#define VK_IMAGE_USAGE_SAMPLED_BIT              0x00000004
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT     0x00000010
#define VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 0x00000020
#define VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT     0x00000040
#define VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT         0x00000080

/* Command pool create flag bits */
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x00000002

/* Stencil face flags (used by vkCmdSetStencilOp) */
typedef enum VkStencilFaceFlagBits {
    VK_STENCIL_FACE_FRONT_BIT          = 0x00000001,
    VK_STENCIL_FACE_BACK_BIT           = 0x00000002,
    VK_STENCIL_FACE_FRONT_AND_BACK     = 0x00000003,
    VK_STENCIL_FACE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkStencilFaceFlagBits;

typedef enum VkStencilOp {
    VK_STENCIL_OP_KEEP                = 0,
    VK_STENCIL_OP_ZERO                = 1,
    VK_STENCIL_OP_REPLACE             = 2,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP = 3,
    VK_STENCIL_OP_DECREMENT_AND_CLAMP = 4,
    VK_STENCIL_OP_INVERT              = 5,
    VK_STENCIL_OP_INCREMENT_AND_WRAP  = 6,
    VK_STENCIL_OP_DECREMENT_AND_WRAP  = 7,
    VK_STENCIL_OP_MAX_ENUM            = 0x7FFFFFFF
} VkStencilOp;

typedef enum VkSubpassContents {
    VK_SUBPASS_CONTENTS_INLINE                    = 0,
    VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS = 1,
    VK_SUBPASS_CONTENTS_MAX_ENUM                  = 0x7FFFFFFF
} VkSubpassContents;

/*------------------------------------------------------------------------
** Basic structures
**----------------------------------------------------------------------*/
typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} VkExtent3D;

typedef struct VkOffset2D {
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkOffset3D {
    int32_t x;
    int32_t y;
    int32_t z;
} VkOffset3D;

typedef struct VkRect2D {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

typedef struct VkComponentMapping {
    VkComponentSwizzle r;
    VkComponentSwizzle g;
    VkComponentSwizzle b;
    VkComponentSwizzle a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
    VkImageAspectFlags aspectMask;
    uint32_t           baseMipLevel;
    uint32_t           levelCount;
    uint32_t           baseArrayLayer;
    uint32_t           layerCount;
} VkImageSubresourceRange;

typedef union VkClearColorValue {
    float    float32[4];
    int32_t  int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue {
    float    depth;
    uint32_t stencil;
} VkClearDepthStencilValue;

typedef union VkClearValue {
    VkClearColorValue        color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;

/*------------------------------------------------------------------------
** Allocation callbacks (application-provided allocators)
**----------------------------------------------------------------------*/
typedef enum VkSystemAllocationScope {
    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND  = 0,
    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT   = 1,
    VK_SYSTEM_ALLOCATION_SCOPE_CACHE    = 2,
    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE   = 3,
    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE = 4,
    VK_SYSTEM_ALLOCATION_SCOPE_MAX_ENUM = 0x7FFFFFFF
} VkSystemAllocationScope;

typedef enum VkInternalAllocationType {
    VK_INTERNAL_ALLOCATION_TYPE_EXECUTABLE = 0,
    VK_INTERNAL_ALLOCATION_TYPE_MAX_ENUM   = 0x7FFFFFFF
} VkInternalAllocationType;

typedef void* (VKAPI_PTR *PFN_vkAllocationFunction)(
    void* pUserData, size_t size, size_t alignment,
    VkSystemAllocationScope allocationScope);
typedef void* (VKAPI_PTR *PFN_vkReallocationFunction)(
    void* pUserData, void* pOriginal, size_t size, size_t alignment,
    VkSystemAllocationScope allocationScope);
typedef void (VKAPI_PTR *PFN_vkFreeFunction)(
    void* pUserData, void* pMemory);
typedef void (VKAPI_PTR *PFN_vkInternalAllocationNotification)(
    void* pUserData, size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope);
typedef void (VKAPI_PTR *PFN_vkInternalFreeNotification)(
    void* pUserData, size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope allocationScope);

typedef struct VkAllocationCallbacks {
    void*                                pUserData;
    PFN_vkAllocationFunction             pfnAllocation;
    PFN_vkReallocationFunction           pfnReallocation;
    PFN_vkFreeFunction                   pfnFree;
    PFN_vkInternalAllocationNotification pfnInternalAllocation;
    PFN_vkInternalFreeNotification       pfnInternalFree;
} VkAllocationCallbacks;

/*------------------------------------------------------------------------
** Instance and device structures
**----------------------------------------------------------------------*/
typedef struct VkApplicationInfo {
    VkStructureType sType;
    const void*     pNext;
    const char*     pApplicationName;
    uint32_t        applicationVersion;
    const char*     pEngineName;
    uint32_t        engineVersion;
    uint32_t        apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType            sType;
    const void*                pNext;
    VkInstanceCreateFlags      flags;
    const VkApplicationInfo*   pApplicationInfo;
    uint32_t                   enabledLayerCount;
    const char* const*         ppEnabledLayerNames;
    uint32_t                   enabledExtensionCount;
    const char* const*         ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkExtensionProperties {
    char     extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkLayerProperties {
    char     layerName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char     description[VK_MAX_DESCRIPTION_SIZE];
} VkLayerProperties;

typedef struct VkFormatProperties {
    VkFormatFeatureFlags linearTilingFeatures;
    VkFormatFeatureFlags optimalTilingFeatures;
    VkFormatFeatureFlags bufferFeatures;
} VkFormatProperties;

typedef struct VkQueueFamilyProperties {
    VkQueueFlags queueFlags;
    uint32_t     queueCount;
    uint32_t     timestampValidBits;
    VkExtent3D   minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkMemoryType {
    VkMemoryPropertyFlags propertyFlags;
    uint32_t              heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize    size;
    VkMemoryHeapFlags flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t     memoryTypeCount;
    VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES];
    uint32_t     memoryHeapCount;
    VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS];
} VkPhysicalDeviceMemoryProperties;

/* PhysicalDeviceLimits -- large struct, all fields needed for spec compliance */
typedef struct VkPhysicalDeviceLimits {
    uint32_t maxImageDimension1D;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxTexelBufferElements;
    uint32_t maxUniformBufferRange;
    uint32_t maxStorageBufferRange;
    uint32_t maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount;
    uint32_t maxSamplerAllocationCount;
    VkDeviceSize bufferImageGranularity;
    VkDeviceSize sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments;
    uint32_t maxPerStageResources;
    uint32_t maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers;
    uint32_t maxDescriptorSetUniformBuffersDynamic;
    uint32_t maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic;
    uint32_t maxDescriptorSetSampledImages;
    uint32_t maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments;
    uint32_t maxVertexInputAttributes;
    uint32_t maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset;
    uint32_t maxVertexInputBindingStride;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel;
    uint32_t maxTessellationPatchSize;
    uint32_t maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents;
    uint32_t maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents;
    uint32_t maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryShaderInvocations;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryOutputVertices;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentOutputAttachments;
    uint32_t maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources;
    uint32_t maxComputeSharedMemorySize;
    uint32_t maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations;
    uint32_t maxComputeWorkGroupSize[3];
    uint32_t subPixelPrecisionBits;
    uint32_t subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits;
    uint32_t maxDrawIndexedIndexValue;
    uint32_t maxDrawIndirectCount;
    float    maxSamplerLodBias;
    float    maxSamplerAnisotropy;
    uint32_t maxViewports;
    uint32_t maxViewportDimensions[2];
    float    viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t   minMemoryMapAlignment;
    VkDeviceSize minTexelBufferOffsetAlignment;
    VkDeviceSize minUniformBufferOffsetAlignment;
    VkDeviceSize minStorageBufferOffsetAlignment;
    int32_t  minTexelOffset;
    uint32_t maxTexelOffset;
    int32_t  minTexelGatherOffset;
    uint32_t maxTexelGatherOffset;
    float    minInterpolationOffset;
    float    maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    uint32_t maxFramebufferLayers;
    VkSampleCountFlags framebufferColorSampleCounts;
    VkSampleCountFlags framebufferDepthSampleCounts;
    VkSampleCountFlags framebufferStencilSampleCounts;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    VkSampleCountFlags sampledImageColorSampleCounts;
    VkSampleCountFlags sampledImageIntegerSampleCounts;
    VkSampleCountFlags sampledImageDepthSampleCounts;
    VkSampleCountFlags sampledImageStencilSampleCounts;
    VkSampleCountFlags storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    VkBool32 timestampComputeAndGraphics;
    float    timestampPeriod;
    uint32_t maxClipDistances;
    uint32_t maxCullDistances;
    uint32_t maxCombinedClipAndCullDistances;
    uint32_t discreteQueuePriorities;
    float    pointSizeRange[2];
    float    lineWidthRange[2];
    float    pointSizeGranularity;
    float    lineWidthGranularity;
    VkBool32 strictLines;
    VkBool32 standardSampleLocations;
    VkDeviceSize optimalBufferCopyOffsetAlignment;
    VkDeviceSize optimalBufferCopyRowPitchAlignment;
    VkDeviceSize nonCoherentAtomSize;
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceSparseProperties {
    VkBool32 residencyStandard2DBlockShape;
    VkBool32 residencyStandard2DMultisampleBlockShape;
    VkBool32 residencyStandard3DBlockShape;
    VkBool32 residencyAlignedMipSize;
    VkBool32 residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties {
    uint32_t                       apiVersion;
    uint32_t                       driverVersion;
    uint32_t                       vendorID;
    uint32_t                       deviceID;
    VkPhysicalDeviceType           deviceType;
    char                           deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t                        pipelineCacheUUID[VK_UUID_SIZE];
    VkPhysicalDeviceLimits         limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

/* PhysicalDeviceFeatures -- all fields needed for feature queries */
typedef struct VkPhysicalDeviceFeatures {
    VkBool32 robustBufferAccess;
    VkBool32 fullDrawIndexUint32;
    VkBool32 imageCubeArray;
    VkBool32 independentBlend;
    VkBool32 geometryShader;
    VkBool32 tessellationShader;
    VkBool32 sampleRateShading;
    VkBool32 dualSrcBlend;
    VkBool32 logicOp;
    VkBool32 multiDrawIndirect;
    VkBool32 drawIndirectFirstInstance;
    VkBool32 depthClamp;
    VkBool32 depthBiasClamp;
    VkBool32 fillModeNonSolid;
    VkBool32 depthBounds;
    VkBool32 wideLines;
    VkBool32 largePoints;
    VkBool32 alphaToOne;
    VkBool32 multiViewport;
    VkBool32 samplerAnisotropy;
    VkBool32 textureCompressionETC2;
    VkBool32 textureCompressionASTC_LDR;
    VkBool32 textureCompressionBC;
    VkBool32 occlusionQueryPrecise;
    VkBool32 pipelineStatisticsQuery;
    VkBool32 vertexPipelineStoresAndAtomics;
    VkBool32 fragmentStoresAndAtomics;
    VkBool32 shaderTessellationAndGeometryPointSize;
    VkBool32 shaderImageGatherExtended;
    VkBool32 shaderStorageImageExtendedFormats;
    VkBool32 shaderStorageImageMultisample;
    VkBool32 shaderStorageImageReadWithoutFormat;
    VkBool32 shaderStorageImageWriteWithoutFormat;
    VkBool32 shaderUniformBufferArrayDynamicIndexing;
    VkBool32 shaderSampledImageArrayDynamicIndexing;
    VkBool32 shaderStorageBufferArrayDynamicIndexing;
    VkBool32 shaderStorageImageArrayDynamicIndexing;
    VkBool32 shaderClipDistance;
    VkBool32 shaderCullDistance;
    VkBool32 shaderFloat64;
    VkBool32 shaderInt64;
    VkBool32 shaderInt16;
    VkBool32 shaderResourceResidency;
    VkBool32 shaderResourceMinLod;
    VkBool32 sparseBinding;
    VkBool32 sparseResidencyBuffer;
    VkBool32 sparseResidencyImage2D;
    VkBool32 sparseResidencyImage3D;
    VkBool32 sparseResidency2Samples;
    VkBool32 sparseResidency4Samples;
    VkBool32 sparseResidency8Samples;
    VkBool32 sparseResidency16Samples;
    VkBool32 sparseResidencyAliased;
    VkBool32 variableMultisampleRate;
    VkBool32 inheritedQueries;
} VkPhysicalDeviceFeatures;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType            sType;
    const void*                pNext;
    VkDeviceQueueCreateFlags   flags;
    uint32_t                   queueFamilyIndex;
    uint32_t                   queueCount;
    const float*               pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType                    sType;
    const void*                        pNext;
    VkDeviceCreateFlags                flags;
    uint32_t                           queueCreateInfoCount;
    const VkDeviceQueueCreateInfo*     pQueueCreateInfos;
    uint32_t                           enabledLayerCount;
    const char* const*                 ppEnabledLayerNames;
    uint32_t                           enabledExtensionCount;
    const char* const*                 ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures*    pEnabledFeatures;
} VkDeviceCreateInfo;

/*------------------------------------------------------------------------
** Memory, buffer, image structures
**----------------------------------------------------------------------*/
typedef struct VkMemoryAllocateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkDeviceSize    allocationSize;
    uint32_t        memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t     memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkBufferCreateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkBufferCreateFlags flags;
    VkDeviceSize       size;
    VkBufferUsageFlags usage;
    VkSharingMode      sharingMode;
    uint32_t           queueFamilyIndexCount;
    const uint32_t*    pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkImageCreateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkImageCreateFlags flags;
    VkImageType        imageType;
    VkFormat           format;
    VkExtent3D         extent;
    uint32_t           mipLevels;
    uint32_t           arrayLayers;
    VkSampleCountFlagBits samples;
    VkImageTiling      tiling;
    VkImageUsageFlags  usage;
    VkSharingMode      sharingMode;
    uint32_t           queueFamilyIndexCount;
    const uint32_t*    pQueueFamilyIndices;
    VkImageLayout      initialLayout;
} VkImageCreateInfo;

typedef struct VkImageViewCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkFlags                  flags;
    VkImage                  image;
    VkImageViewType          viewType;
    VkFormat                 format;
    VkComponentMapping       components;
    VkImageSubresourceRange  subresourceRange;
} VkImageViewCreateInfo;

/*------------------------------------------------------------------------
** Shader and pipeline structures
**----------------------------------------------------------------------*/
typedef struct VkShaderModuleCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    size_t          codeSize;
    const uint32_t* pCode;
} VkShaderModuleCreateInfo;

typedef struct VkSpecializationMapEntry {
    uint32_t constantID;
    uint32_t offset;
    size_t   size;
} VkSpecializationMapEntry;

typedef struct VkSpecializationInfo {
    uint32_t                          mapEntryCount;
    const VkSpecializationMapEntry*   pMapEntries;
    size_t                            dataSize;
    const void*                       pData;
} VkSpecializationInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    VkStructureType                    sType;
    const void*                        pNext;
    VkPipelineShaderStageCreateFlags   flags;
    VkShaderStageFlagBits              stage;
    VkShaderModule                     module;
    const char*                        pName;
    const VkSpecializationInfo*        pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkVertexInputBindingDescription {
    uint32_t        binding;
    uint32_t        stride;
    VkVertexInputRate inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
    uint32_t location;
    uint32_t binding;
    VkFormat format;
    uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo {
    VkStructureType                              sType;
    const void*                                  pNext;
    VkPipelineVertexInputStateCreateFlags        flags;
    uint32_t                                     vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription*       pVertexBindingDescriptions;
    uint32_t                                     vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription*     pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    VkStructureType                           sType;
    const void*                               pNext;
    VkPipelineInputAssemblyStateCreateFlags   flags;
    VkPrimitiveTopology                       topology;
    VkBool32                                  primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
    VkStructureType                       sType;
    const void*                           pNext;
    VkPipelineViewportStateCreateFlags    flags;
    uint32_t                              viewportCount;
    const VkViewport*                     pViewports;
    uint32_t                              scissorCount;
    const VkRect2D*                       pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
    VkStructureType                            sType;
    const void*                                pNext;
    VkPipelineRasterizationStateCreateFlags    flags;
    VkBool32                                   depthClampEnable;
    VkBool32                                   rasterizerDiscardEnable;
    VkPolygonMode                              polygonMode;
    VkCullModeFlags                            cullMode;
    VkFrontFace                                frontFace;
    VkBool32                                   depthBiasEnable;
    float                                      depthBiasConstantFactor;
    float                                      depthBiasClamp;
    float                                      depthBiasSlopeFactor;
    float                                      lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType                          sType;
    const void*                              pNext;
    VkPipelineMultisampleStateCreateFlags    flags;
    VkSampleCountFlagBits                    rasterizationSamples;
    VkBool32                                 sampleShadingEnable;
    float                                    minSampleShading;
    const VkSampleMask*                      pSampleMask;
    VkBool32                                 alphaToCoverageEnable;
    VkBool32                                 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkStencilOpState {
    uint32_t   failOp;
    uint32_t   passOp;
    uint32_t   depthFailOp;
    VkCompareOp compareOp;
    uint32_t   compareMask;
    uint32_t   writeMask;
    uint32_t   reference;
} VkStencilOpState;

typedef struct VkPipelineDepthStencilStateCreateInfo {
    VkStructureType                           sType;
    const void*                               pNext;
    VkPipelineDepthStencilStateCreateFlags    flags;
    VkBool32                                  depthTestEnable;
    VkBool32                                  depthWriteEnable;
    VkCompareOp                               depthCompareOp;
    VkBool32                                  depthBoundsTestEnable;
    VkBool32                                  stencilTestEnable;
    VkStencilOpState                          front;
    VkStencilOpState                          back;
    float                                     minDepthBounds;
    float                                     maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32              blendEnable;
    VkBlendFactor         srcColorBlendFactor;
    VkBlendFactor         dstColorBlendFactor;
    VkBlendOp             colorBlendOp;
    VkBlendFactor         srcAlphaBlendFactor;
    VkBlendFactor         dstAlphaBlendFactor;
    VkBlendOp             alphaBlendOp;
    VkColorComponentFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    VkStructureType                              sType;
    const void*                                  pNext;
    VkPipelineColorBlendStateCreateFlags         flags;
    VkBool32                                     logicOpEnable;
    VkLogicOp                                    logicOp;
    uint32_t                                     attachmentCount;
    const VkPipelineColorBlendAttachmentState*   pAttachments;
    float                                        blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineDynamicStateCreateInfo {
    VkStructureType                      sType;
    const void*                          pNext;
    VkPipelineDynamicStateCreateFlags    flags;
    uint32_t                             dynamicStateCount;
    const VkDynamicState*                pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkPushConstantRange {
    VkShaderStageFlags    stageFlags;
    uint32_t              offset;
    uint32_t              size;
} VkPushConstantRange;

typedef struct VkPipelineLayoutCreateInfo {
    VkStructureType                 sType;
    const void*                     pNext;
    VkPipelineLayoutCreateFlags     flags;
    uint32_t                        setLayoutCount;
    const VkDescriptorSetLayout*    pSetLayouts;
    uint32_t                        pushConstantRangeCount;
    const VkPushConstantRange*      pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkPipelineCacheCreateInfo {
    VkStructureType              sType;
    const void*                  pNext;
    VkPipelineCacheCreateFlags   flags;
    size_t                       initialDataSize;
    const void*                  pInitialData;
} VkPipelineCacheCreateInfo;

/* Descriptor set types */
typedef struct VkDescriptorSetLayoutBinding {
    uint32_t              binding;
    VkDescriptorType      descriptorType;
    uint32_t              descriptorCount;
    VkShaderStageFlags    stageFlags;
    const VkSampler*      pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType                        sType;
    const void*                            pNext;
    VkDescriptorSetLayoutCreateFlags       flags;
    uint32_t                               bindingCount;
    const VkDescriptorSetLayoutBinding*    pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDescriptorPoolSize {
    VkDescriptorType    type;
    uint32_t            descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo {
    VkStructureType                sType;
    const void*                    pNext;
    VkDescriptorPoolCreateFlags    flags;
    uint32_t                       maxSets;
    uint32_t                       poolSizeCount;
    const VkDescriptorPoolSize*    pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetAllocateInfo {
    VkStructureType                 sType;
    const void*                     pNext;
    VkDescriptorPool                descriptorPool;
    uint32_t                        descriptorSetCount;
    const VkDescriptorSetLayout*    pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer        buffer;
    VkDeviceSize    offset;
    VkDeviceSize    range;
} VkDescriptorBufferInfo;

typedef struct VkDescriptorImageInfo {
    VkSampler       sampler;
    VkImageView     imageView;
    VkImageLayout   imageLayout;
} VkDescriptorImageInfo;

typedef struct VkSamplerCreateInfo {
    VkStructureType         sType;
    const void*             pNext;
    VkFlags                 flags;
    VkFilter                magFilter;
    VkFilter                minFilter;
    VkSamplerMipmapMode     mipmapMode;
    VkSamplerAddressMode    addressModeU;
    VkSamplerAddressMode    addressModeV;
    VkSamplerAddressMode    addressModeW;
    float                   mipLodBias;
    VkBool32                anisotropyEnable;
    float                   maxAnisotropy;
    VkBool32                compareEnable;
    VkCompareOp             compareOp;
    float                   minLod;
    float                   maxLod;
    VkBorderColor           borderColor;
    VkBool32                unnormalizedCoordinates;
} VkSamplerCreateInfo;

typedef struct VkWriteDescriptorSet {
    VkStructureType                  sType;
    const void*                      pNext;
    VkDescriptorSet                  dstSet;
    uint32_t                         dstBinding;
    uint32_t                         dstArrayElement;
    uint32_t                         descriptorCount;
    VkDescriptorType                 descriptorType;
    const VkDescriptorImageInfo*     pImageInfo;
    const VkDescriptorBufferInfo*    pBufferInfo;
    const void*                      pTexelBufferView;
} VkWriteDescriptorSet;

/* Vulkan 1.3 pipeline rendering (for dynamic rendering) */
typedef struct VkPipelineRenderingCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    uint32_t        viewMask;
    uint32_t        colorAttachmentCount;
    const VkFormat* pColorAttachmentFormats;
    VkFormat        depthAttachmentFormat;
    VkFormat        stencilAttachmentFormat;
} VkPipelineRenderingCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType                                  sType;
    const void*                                      pNext;
    VkPipelineCreateFlags                            flags;
    uint32_t                                         stageCount;
    const VkPipelineShaderStageCreateInfo*            pStages;
    const VkPipelineVertexInputStateCreateInfo*       pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo*     pInputAssemblyState;
    const void*                                      pTessellationState;
    const VkPipelineViewportStateCreateInfo*          pViewportState;
    const VkPipelineRasterizationStateCreateInfo*     pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo*       pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo*      pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo*        pColorBlendState;
    const VkPipelineDynamicStateCreateInfo*           pDynamicState;
    VkPipelineLayout                                 layout;
    VkRenderPass                                     renderPass;
    uint32_t                                         subpass;
    VkPipeline                                       basePipelineHandle;
    int32_t                                          basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

/*------------------------------------------------------------------------
** Command buffer structures
**----------------------------------------------------------------------*/
typedef struct VkCommandPoolCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkCommandPoolCreateFlags flags;
    uint32_t                 queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    VkStructureType      sType;
    const void*          pNext;
    VkCommandPool        commandPool;
    VkCommandBufferLevel level;
    uint32_t             commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferInheritanceInfo {
    VkStructureType  sType;
    const void*      pNext;
    VkRenderPass     renderPass;
    uint32_t         subpass;
    VkFramebuffer    framebuffer;
    VkBool32         occlusionQueryEnable;
    VkFlags          queryFlags;
    VkFlags          pipelineStatistics;
} VkCommandBufferInheritanceInfo;

typedef struct VkCommandBufferBeginInfo {
    VkStructureType                      sType;
    const void*                          pNext;
    VkCommandBufferUsageFlags            flags;
    const VkCommandBufferInheritanceInfo* pInheritanceInfo;
} VkCommandBufferBeginInfo;

/*------------------------------------------------------------------------
** Vulkan 1.3 dynamic rendering
**----------------------------------------------------------------------*/
typedef struct VkRenderingAttachmentInfo {
    VkStructureType        sType;
    const void*            pNext;
    VkImageView            imageView;
    VkImageLayout          imageLayout;
    VkResolveModeFlagBits  resolveMode;
    VkImageView            resolveImageView;
    VkImageLayout          resolveImageLayout;
    VkAttachmentLoadOp     loadOp;
    VkAttachmentStoreOp    storeOp;
    VkClearValue           clearValue;
} VkRenderingAttachmentInfo;

typedef struct VkRenderingInfo {
    VkStructureType                  sType;
    const void*                      pNext;
    VkRenderingFlags                 flags;
    VkRect2D                         renderArea;
    uint32_t                         layerCount;
    uint32_t                         viewMask;
    uint32_t                         colorAttachmentCount;
    const VkRenderingAttachmentInfo* pColorAttachments;
    const VkRenderingAttachmentInfo* pDepthAttachment;
    const VkRenderingAttachmentInfo* pStencilAttachment;
} VkRenderingInfo;

/*------------------------------------------------------------------------
** Synchronisation structures
**----------------------------------------------------------------------*/
typedef struct VkFenceCreateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkFenceCreateFlags flags;
} VkFenceCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
} VkSemaphoreCreateInfo;

typedef struct VkSubmitInfo {
    VkStructureType             sType;
    const void*                 pNext;
    uint32_t                    waitSemaphoreCount;
    const VkSemaphore*          pWaitSemaphores;
    const VkPipelineStageFlags* pWaitDstStageMask;
    uint32_t                    commandBufferCount;
    const VkCommandBuffer*      pCommandBuffers;
    uint32_t                    signalSemaphoreCount;
    const VkSemaphore*          pSignalSemaphores;
} VkSubmitInfo;

/*------------------------------------------------------------------------
** Mapped memory range (for vkFlush/InvalidateMappedMemoryRanges)
**----------------------------------------------------------------------*/
typedef struct VkMappedMemoryRange {
    VkStructureType sType;
    const void*     pNext;
    VkDeviceMemory  memory;
    VkDeviceSize    offset;
    VkDeviceSize    size;
} VkMappedMemoryRange;

/*------------------------------------------------------------------------
** Legacy render pass structures (Vulkan 1.0 compatibility)
**----------------------------------------------------------------------*/
typedef struct VkAttachmentDescription {
    VkFlags                flags;
    VkFormat               format;
    VkSampleCountFlagBits  samples;
    VkAttachmentLoadOp     loadOp;
    VkAttachmentStoreOp    storeOp;
    VkAttachmentLoadOp     stencilLoadOp;
    VkAttachmentStoreOp    stencilStoreOp;
    VkImageLayout          initialLayout;
    VkImageLayout          finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
    uint32_t      attachment;
    VkImageLayout layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags                       flags;
    VkPipelineBindPoint           pipelineBindPoint;
    uint32_t                      inputAttachmentCount;
    const VkAttachmentReference*  pInputAttachments;
    uint32_t                      colorAttachmentCount;
    const VkAttachmentReference*  pColorAttachments;
    const VkAttachmentReference*  pResolveAttachments;
    const VkAttachmentReference*  pDepthStencilAttachment;
    uint32_t                      preserveAttachmentCount;
    const uint32_t*               pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
    uint32_t            srcSubpass;
    uint32_t            dstSubpass;
    VkPipelineStageFlags srcStageMask;
    VkPipelineStageFlags dstStageMask;
    VkAccessFlags        srcAccessMask;
    VkAccessFlags        dstAccessMask;
    VkDependencyFlags    dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo {
    VkStructureType                sType;
    const void*                    pNext;
    VkFlags                        flags;
    uint32_t                       attachmentCount;
    const VkAttachmentDescription* pAttachments;
    uint32_t                       subpassCount;
    const VkSubpassDescription*    pSubpasses;
    uint32_t                       dependencyCount;
    const VkSubpassDependency*     pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    VkRenderPass    renderPass;
    uint32_t        attachmentCount;
    const VkImageView* pAttachments;
    uint32_t        width;
    uint32_t        height;
    uint32_t        layers;
} VkFramebufferCreateInfo;

typedef struct VkRenderPassBeginInfo {
    VkStructureType  sType;
    const void*      pNext;
    VkRenderPass     renderPass;
    VkFramebuffer    framebuffer;
    VkRect2D         renderArea;
    uint32_t         clearValueCount;
    const VkClearValue* pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkClearRect {
    VkRect2D rect;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkClearRect;

/*------------------------------------------------------------------------
** KHR Surface / Swapchain structures
**----------------------------------------------------------------------*/
#define VK_KHR_SURFACE_SPEC_VERSION      25
#define VK_KHR_SURFACE_EXTENSION_NAME    "VK_KHR_surface"
#define VK_KHR_SWAPCHAIN_SPEC_VERSION    70
#define VK_KHR_SWAPCHAIN_EXTENSION_NAME  "VK_KHR_swapchain"

typedef enum VkPresentModeKHR {
    VK_PRESENT_MODE_IMMEDIATE_KHR    = 0,
    VK_PRESENT_MODE_MAILBOX_KHR     = 1,
    VK_PRESENT_MODE_FIFO_KHR        = 2,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
    VK_PRESENT_MODE_MAX_ENUM_KHR    = 0x7FFFFFFF
} VkPresentModeKHR;

typedef enum VkColorSpaceKHR {
    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR  = 0,
    VK_COLOR_SPACE_MAX_ENUM_KHR        = 0x7FFFFFFF
} VkColorSpaceKHR;

typedef enum VkCompositeAlphaFlagBitsKHR {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR          = 0x00000001,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR  = 0x00000002,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR = 0x00000004,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR         = 0x00000008,
    VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR  = 0x7FFFFFFF
} VkCompositeAlphaFlagBitsKHR;
typedef VkFlags VkCompositeAlphaFlagsKHR;

typedef enum VkSurfaceTransformFlagBitsKHR {
    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR = 0x00000001,
    VK_SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM_KHR = 0x7FFFFFFF
} VkSurfaceTransformFlagBitsKHR;
typedef VkFlags VkSurfaceTransformFlagsKHR;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t                       minImageCount;
    uint32_t                       maxImageCount;
    VkExtent2D                     currentExtent;
    VkExtent2D                     minImageExtent;
    VkExtent2D                     maxImageExtent;
    uint32_t                       maxImageArrayLayers;
    VkSurfaceTransformFlagsKHR     supportedTransforms;
    VkSurfaceTransformFlagBitsKHR  currentTransform;
    VkCompositeAlphaFlagsKHR       supportedCompositeAlpha;
    VkImageUsageFlags              supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    VkFormat       format;
    VkColorSpaceKHR colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType                sType;
    const void*                    pNext;
    VkFlags                        flags;
    VkSurfaceKHR                   surface;
    uint32_t                       minImageCount;
    VkFormat                       imageFormat;
    VkColorSpaceKHR                imageColorSpace;
    VkExtent2D                     imageExtent;
    uint32_t                       imageArrayLayers;
    VkImageUsageFlags              imageUsage;
    VkSharingMode                  imageSharingMode;
    uint32_t                       queueFamilyIndexCount;
    const uint32_t*                pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR  preTransform;
    VkCompositeAlphaFlagBitsKHR    compositeAlpha;
    VkPresentModeKHR               presentMode;
    VkBool32                       clipped;
    VkSwapchainKHR                 oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkPresentInfoKHR {
    VkStructureType       sType;
    const void*           pNext;
    uint32_t              waitSemaphoreCount;
    const VkSemaphore*    pWaitSemaphores;
    uint32_t              swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const uint32_t*       pImageIndices;
    VkResult*             pResults;
} VkPresentInfoKHR;

/*------------------------------------------------------------------------
** Function pointer typedefs (PFN_vk* types)
** Used for vkGetInstanceProcAddr / vkGetDeviceProcAddr return values
**----------------------------------------------------------------------*/
typedef void (VKAPI_PTR *PFN_vkVoidFunction)(void);

/* Global / Instance functions */
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef void     (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateInstanceVersion)(uint32_t*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, VkExtensionProperties*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateInstanceLayerProperties)(uint32_t*, VkLayerProperties*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceFeatures)(VkPhysicalDevice, VkPhysicalDeviceFeatures*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceFormatProperties)(VkPhysicalDevice, VkFormat, VkFormatProperties*);
typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetDeviceProcAddr)(VkDevice, const char*);

/* Device functions */
typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDevice)(VkDevice, const VkAllocationCallbacks*);
typedef void     (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (VKAPI_PTR *PFN_vkDeviceWaitIdle)(VkDevice);
typedef VkResult (VKAPI_PTR *PFN_vkQueueWaitIdle)(VkQueue);

/* Memory */
typedef VkResult (VKAPI_PTR *PFN_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
typedef void     (VKAPI_PTR *PFN_vkFreeMemory)(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**);
typedef void     (VKAPI_PTR *PFN_vkUnmapMemory)(VkDevice, VkDeviceMemory);

/* Buffer / Image */
typedef VkResult (VKAPI_PTR *PFN_vkCreateBuffer)(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
typedef void     (VKAPI_PTR *PFN_vkDestroyBuffer)(VkDevice, VkBuffer, const VkAllocationCallbacks*);
typedef void     (VKAPI_PTR *PFN_vkGetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements*);
typedef VkResult (VKAPI_PTR *PFN_vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_PTR *PFN_vkCreateImage)(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
typedef void     (VKAPI_PTR *PFN_vkDestroyImage)(VkDevice, VkImage, const VkAllocationCallbacks*);
typedef void     (VKAPI_PTR *PFN_vkGetImageMemoryRequirements)(VkDevice, VkImage, VkMemoryRequirements*);
typedef VkResult (VKAPI_PTR *PFN_vkBindImageMemory)(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_PTR *PFN_vkCreateImageView)(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
typedef void     (VKAPI_PTR *PFN_vkDestroyImageView)(VkDevice, VkImageView, const VkAllocationCallbacks*);

/* Sampler */
typedef VkResult (VKAPI_PTR *PFN_vkCreateSampler)(VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*);
typedef void     (VKAPI_PTR *PFN_vkDestroySampler)(VkDevice, VkSampler, const VkAllocationCallbacks*);

/* Shader / Pipeline */
typedef VkResult (VKAPI_PTR *PFN_vkCreateShaderModule)(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*);
typedef void     (VKAPI_PTR *PFN_vkDestroyShaderModule)(VkDevice, VkShaderModule, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkCreatePipelineLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
typedef void     (VKAPI_PTR *PFN_vkDestroyPipelineLayout)(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateGraphicsPipelines)(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
typedef void     (VKAPI_PTR *PFN_vkDestroyPipeline)(VkDevice, VkPipeline, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkCreatePipelineCache)(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*);
typedef void     (VKAPI_PTR *PFN_vkDestroyPipelineCache)(VkDevice, VkPipelineCache, const VkAllocationCallbacks*);

/* Descriptor sets */
typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorSetLayout)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDescriptorSetLayout)(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorPool)(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDescriptorPool)(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateDescriptorSets)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
typedef VkResult (VKAPI_PTR *PFN_vkFreeDescriptorSets)(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*);
typedef void     (VKAPI_PTR *PFN_vkUpdateDescriptorSets)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
typedef void     (VKAPI_PTR *PFN_vkCmdBindDescriptorSets)(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);

/* Command buffer */
typedef VkResult (VKAPI_PTR *PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*);
typedef void     (VKAPI_PTR *PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef void     (VKAPI_PTR *PFN_vkFreeCommandBuffers)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
typedef VkResult (VKAPI_PTR *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef VkResult (VKAPI_PTR *PFN_vkEndCommandBuffer)(VkCommandBuffer);

/* Command recording */
typedef void (VKAPI_PTR *PFN_vkCmdBindPipeline)(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
typedef void (VKAPI_PTR *PFN_vkCmdSetViewport)(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
typedef void (VKAPI_PTR *PFN_vkCmdSetScissor)(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
typedef void (VKAPI_PTR *PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdBeginRendering)(VkCommandBuffer, const VkRenderingInfo*);
typedef void (VKAPI_PTR *PFN_vkCmdEndRendering)(VkCommandBuffer);
typedef void (VKAPI_PTR *PFN_vkCmdPushConstants)(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdBindVertexBuffers)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
typedef void (VKAPI_PTR *PFN_vkCmdBindIndexBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndexed)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);

/* Synchronisation */
typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*);
typedef void     (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice, VkFence, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
typedef VkResult (VKAPI_PTR *PFN_vkResetFences)(VkDevice, uint32_t, const VkFence*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*);
typedef void     (VKAPI_PTR *PFN_vkDestroySemaphore)(VkDevice, VkSemaphore, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);

/* WSI */
typedef void     (VKAPI_PTR *PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
typedef void     (VKAPI_PTR *PFN_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
typedef VkResult (VKAPI_PTR *PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
typedef VkResult (VKAPI_PTR *PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);

/* Device extension enumeration */
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);

/* Memory, command buffer, and pipeline cache management */
typedef VkResult (VKAPI_PTR *PFN_vkFlushMappedMemoryRanges)(VkDevice, uint32_t, const VkMappedMemoryRange*);
typedef VkResult (VKAPI_PTR *PFN_vkInvalidateMappedMemoryRanges)(VkDevice, uint32_t, const VkMappedMemoryRange*);
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandBuffer)(VkCommandBuffer, VkCommandBufferResetFlags);
typedef void     (VKAPI_PTR *PFN_vkCmdPipelineBarrier)(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandPool)(VkDevice, VkCommandPool, VkCommandPoolResetFlags);
typedef void     (VKAPI_PTR *PFN_vkTrimCommandPool)(VkDevice, VkCommandPool, VkFlags);
typedef VkResult (VKAPI_PTR *PFN_vkGetFenceStatus)(VkDevice, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkGetPipelineCacheData)(VkDevice, VkPipelineCache, size_t*, void*);
typedef VkResult (VKAPI_PTR *PFN_vkMergePipelineCaches)(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*);
typedef VkResult (VKAPI_PTR *PFN_vkResetDescriptorPool)(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);

/* Legacy render pass */
typedef VkResult (VKAPI_PTR *PFN_vkCreateRenderPass)(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
typedef void     (VKAPI_PTR *PFN_vkDestroyRenderPass)(VkDevice, VkRenderPass, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateFramebuffer)(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
typedef void     (VKAPI_PTR *PFN_vkDestroyFramebuffer)(VkDevice, VkFramebuffer, const VkAllocationCallbacks*);
typedef void     (VKAPI_PTR *PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
typedef void     (VKAPI_PTR *PFN_vkCmdEndRenderPass)(VkCommandBuffer);
typedef void     (VKAPI_PTR *PFN_vkCmdNextSubpass)(VkCommandBuffer, VkSubpassContents);

/* Vulkan 1.3 dynamic state */
typedef void     (VKAPI_PTR *PFN_vkCmdSetCullMode)(VkCommandBuffer, VkCullModeFlags);
typedef void     (VKAPI_PTR *PFN_vkCmdSetFrontFace)(VkCommandBuffer, VkFrontFace);
typedef void     (VKAPI_PTR *PFN_vkCmdSetPrimitiveTopology)(VkCommandBuffer, VkPrimitiveTopology);
typedef void     (VKAPI_PTR *PFN_vkCmdSetViewportWithCount)(VkCommandBuffer, uint32_t, const VkViewport*);
typedef void     (VKAPI_PTR *PFN_vkCmdSetScissorWithCount)(VkCommandBuffer, uint32_t, const VkRect2D*);
typedef void     (VKAPI_PTR *PFN_vkCmdBindVertexBuffers2)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*, const VkDeviceSize*, const VkDeviceSize*);
typedef void     (VKAPI_PTR *PFN_vkCmdSetDepthTestEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetDepthWriteEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetDepthCompareOp)(VkCommandBuffer, VkCompareOp);
typedef void     (VKAPI_PTR *PFN_vkCmdSetDepthBoundsTestEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetStencilTestEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetStencilOp)(VkCommandBuffer, VkStencilFaceFlags, VkStencilOp, VkStencilOp, VkStencilOp, VkCompareOp);
typedef void     (VKAPI_PTR *PFN_vkCmdSetRasterizerDiscardEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetDepthBiasEnable)(VkCommandBuffer, VkBool32);
typedef void     (VKAPI_PTR *PFN_vkCmdSetPrimitiveRestartEnable)(VkCommandBuffer, VkBool32);

/*------------------------------------------------------------------------
** Transfer command structures
**----------------------------------------------------------------------*/
typedef struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
} VkBufferCopy;

typedef struct VkImageSubresourceLayers {
    VkImageAspectFlags aspectMask;
    uint32_t           mipLevel;
    uint32_t           baseArrayLayer;
    uint32_t           layerCount;
} VkImageSubresourceLayers;

typedef struct VkBufferImageCopy {
    VkDeviceSize             bufferOffset;
    uint32_t                 bufferRowLength;
    uint32_t                 bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D               imageOffset;
    VkExtent3D               imageExtent;
} VkBufferImageCopy;

typedef struct VkImageCopy {
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffset;
    VkExtent3D               extent;
} VkImageCopy;

typedef struct VkImageBlit {
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffsets[2];
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffsets[2];
} VkImageBlit;

/* Transfer command PFN types */
typedef void (VKAPI_PTR *PFN_vkCmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyBufferToImage)(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyImageToBuffer)(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyImage)(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdFillBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void*);

/*------------------------------------------------------------------------
** Vulkan 1.1/1.2/1.3 "2" wrapper structures
**----------------------------------------------------------------------*/
typedef struct VkPhysicalDeviceProperties2 {
    VkStructureType            sType;
    void*                      pNext;
    VkPhysicalDeviceProperties properties;
} VkPhysicalDeviceProperties2;

typedef struct VkPhysicalDeviceFeatures2 {
    VkStructureType          sType;
    void*                    pNext;
    VkPhysicalDeviceFeatures features;
} VkPhysicalDeviceFeatures2;

typedef struct VkQueueFamilyProperties2 {
    VkStructureType       sType;
    void*                 pNext;
    VkQueueFamilyProperties queueFamilyProperties;
} VkQueueFamilyProperties2;

typedef struct VkPhysicalDeviceMemoryProperties2 {
    VkStructureType                  sType;
    void*                            pNext;
    VkPhysicalDeviceMemoryProperties memoryProperties;
} VkPhysicalDeviceMemoryProperties2;

typedef struct VkFormatProperties2 {
    VkStructureType    sType;
    void*              pNext;
    VkFormatProperties formatProperties;
} VkFormatProperties2;

typedef struct VkBufferMemoryRequirementsInfo2 {
    VkStructureType sType;
    const void*     pNext;
    VkBuffer        buffer;
} VkBufferMemoryRequirementsInfo2;

typedef struct VkImageMemoryRequirementsInfo2 {
    VkStructureType sType;
    const void*     pNext;
    VkImage         image;
} VkImageMemoryRequirementsInfo2;

typedef struct VkMemoryRequirements2 {
    VkStructureType      sType;
    void*                pNext;
    VkMemoryRequirements memoryRequirements;
} VkMemoryRequirements2;

typedef struct VkBindBufferMemoryInfo {
    VkStructureType sType;
    const void*     pNext;
    VkBuffer        buffer;
    VkDeviceMemory  memory;
    VkDeviceSize    memoryOffset;
} VkBindBufferMemoryInfo;

typedef struct VkBindImageMemoryInfo {
    VkStructureType sType;
    const void*     pNext;
    VkImage         image;
    VkDeviceMemory  memory;
    VkDeviceSize    memoryOffset;
} VkBindImageMemoryInfo;

typedef struct VkSemaphoreSubmitInfo {
    VkStructureType        sType;
    const void*            pNext;
    VkSemaphore            semaphore;
    uint64_t               value;
    VkPipelineStageFlags   stageMask;
    uint32_t               deviceIndex;
} VkSemaphoreSubmitInfo;

typedef struct VkCommandBufferSubmitInfo {
    VkStructureType  sType;
    const void*      pNext;
    VkCommandBuffer  commandBuffer;
    uint32_t         deviceMask;
} VkCommandBufferSubmitInfo;

typedef struct VkSubmitInfo2 {
    VkStructureType                  sType;
    const void*                      pNext;
    VkFlags                          flags;
    uint32_t                         waitSemaphoreInfoCount;
    const VkSemaphoreSubmitInfo*     pWaitSemaphoreInfos;
    uint32_t                         commandBufferInfoCount;
    const VkCommandBufferSubmitInfo* pCommandBufferInfos;
    uint32_t                         signalSemaphoreInfoCount;
    const VkSemaphoreSubmitInfo*     pSignalSemaphoreInfos;
} VkSubmitInfo2;

typedef struct VkImageFormatProperties {
    VkExtent3D         maxExtent;
    uint32_t           maxMipLevels;
    uint32_t           maxArrayLayers;
    VkSampleCountFlags sampleCounts;
    VkDeviceSize       maxResourceSize;
} VkImageFormatProperties;

typedef struct VkPhysicalDeviceImageFormatInfo2 {
    VkStructureType    sType;
    const void*        pNext;
    VkFormat           format;
    VkImageType        type;
    VkImageTiling      tiling;
    VkImageUsageFlags  usage;
    VkImageCreateFlags flags;
} VkPhysicalDeviceImageFormatInfo2;

typedef struct VkImageFormatProperties2 {
    VkStructureType          sType;
    void*                    pNext;
    VkImageFormatProperties  imageFormatProperties;
} VkImageFormatProperties2;

/*------------------------------------------------------------------------
** Additional Vulkan types for full API coverage
**----------------------------------------------------------------------*/

/* Vulkan 1.0 types */
typedef VkFlags VkQueryControlFlags;
typedef VkFlags VkQueryResultFlags;

typedef enum VkQueryType {
    VK_QUERY_TYPE_OCCLUSION = 0,
    VK_QUERY_TYPE_PIPELINE_STATISTICS = 1,
    VK_QUERY_TYPE_TIMESTAMP = 2,
} VkQueryType;

typedef enum VkObjectType {
    VK_OBJECT_TYPE_UNKNOWN = 0,
    VK_OBJECT_TYPE_INSTANCE = 1,
    VK_OBJECT_TYPE_PHYSICAL_DEVICE = 2,
    VK_OBJECT_TYPE_DEVICE = 3,
    VK_OBJECT_TYPE_QUEUE = 4,
    VK_OBJECT_TYPE_SEMAPHORE = 5,
    VK_OBJECT_TYPE_COMMAND_BUFFER = 6,
    VK_OBJECT_TYPE_FENCE = 7,
    VK_OBJECT_TYPE_DEVICE_MEMORY = 8,
    VK_OBJECT_TYPE_BUFFER = 9,
    VK_OBJECT_TYPE_IMAGE = 10,
    VK_OBJECT_TYPE_EVENT = 11,
    VK_OBJECT_TYPE_QUERY_POOL = 12,
    VK_OBJECT_TYPE_BUFFER_VIEW = 13,
    VK_OBJECT_TYPE_IMAGE_VIEW = 14,
    VK_OBJECT_TYPE_PIPELINE_LAYOUT = 17,
    VK_OBJECT_TYPE_RENDER_PASS = 18,
    VK_OBJECT_TYPE_PIPELINE = 19,
    VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT = 20,
    VK_OBJECT_TYPE_SAMPLER = 21,
    VK_OBJECT_TYPE_DESCRIPTOR_POOL = 22,
    VK_OBJECT_TYPE_DESCRIPTOR_SET = 23,
    VK_OBJECT_TYPE_FRAMEBUFFER = 24,
    VK_OBJECT_TYPE_COMMAND_POOL = 25,
} VkObjectType;

typedef struct VkImageSubresource {
    VkImageAspectFlags aspectMask;
    uint32_t           mipLevel;
    uint32_t           arrayLayer;
} VkImageSubresource;

typedef struct VkSubresourceLayout {
    VkDeviceSize offset;
    VkDeviceSize size;
    VkDeviceSize rowPitch;
    VkDeviceSize arrayPitch;
    VkDeviceSize depthPitch;
} VkSubresourceLayout;

typedef struct VkEventCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
} VkEventCreateInfo;

typedef struct VkQueryPoolCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    VkQueryType     queryType;
    uint32_t        queryCount;
    VkFlags         pipelineStatistics;
} VkQueryPoolCreateInfo;

typedef struct VkBufferViewCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    VkBuffer        buffer;
    VkFormat        format;
    VkDeviceSize    offset;
    VkDeviceSize    range;
} VkBufferViewCreateInfo;

typedef struct VkComputePipelineCreateInfo {
    VkStructureType                    sType;
    const void*                        pNext;
    VkPipelineCreateFlags              flags;
    VkPipelineShaderStageCreateInfo    stage;
    VkPipelineLayout                   layout;
    VkPipeline                         basePipelineHandle;
    int32_t                            basePipelineIndex;
} VkComputePipelineCreateInfo;

typedef struct VkDrawIndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
} VkDrawIndirectCommand;

typedef struct VkDrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
} VkDrawIndexedIndirectCommand;

typedef struct VkImageResolve {
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffset;
    VkExtent3D               extent;
} VkImageResolve;

typedef struct VkClearAttachment {
    VkImageAspectFlags aspectMask;
    uint32_t           colorAttachment;
    VkClearValue       clearValue;
} VkClearAttachment;

typedef struct VkSparseImageFormatProperties {
    VkImageAspectFlags       aspectMask;
    VkExtent3D               imageGranularity;
    VkFlags                  flags;
} VkSparseImageFormatProperties;

typedef struct VkSparseImageMemoryRequirements {
    VkSparseImageFormatProperties formatProperties;
    uint32_t                      imageMipTailFirstLod;
    VkDeviceSize                  imageMipTailSize;
    VkDeviceSize                  imageMipTailOffset;
    VkDeviceSize                  imageMipTailStride;
} VkSparseImageMemoryRequirements;

typedef struct VkSparseMemoryBind {
    VkDeviceSize          resourceOffset;
    VkDeviceSize          size;
    VkDeviceMemory        memory;
    VkDeviceSize          memoryOffset;
    VkFlags               flags;
} VkSparseMemoryBind;

typedef struct VkSparseBufferMemoryBindInfo {
    VkBuffer                buffer;
    uint32_t                bindCount;
    const VkSparseMemoryBind* pBinds;
} VkSparseBufferMemoryBindInfo;

typedef struct VkSparseImageOpaqueMemoryBindInfo {
    VkImage                 image;
    uint32_t                bindCount;
    const VkSparseMemoryBind* pBinds;
} VkSparseImageOpaqueMemoryBindInfo;

typedef struct VkSparseImageMemoryBind {
    VkImageSubresource      subresource;
    VkOffset3D              offset;
    VkExtent3D              extent;
    VkDeviceMemory          memory;
    VkDeviceSize            memoryOffset;
    VkFlags                 flags;
} VkSparseImageMemoryBind;

typedef struct VkSparseImageMemoryBindInfo {
    VkImage                          image;
    uint32_t                         bindCount;
    const VkSparseImageMemoryBind*   pBinds;
} VkSparseImageMemoryBindInfo;

typedef struct VkBindSparseInfo {
    VkStructureType                          sType;
    const void*                              pNext;
    uint32_t                                 waitSemaphoreCount;
    const VkSemaphore*                       pWaitSemaphores;
    uint32_t                                 bufferBindCount;
    const VkSparseBufferMemoryBindInfo*      pBufferBinds;
    uint32_t                                 imageOpaqueBindCount;
    const VkSparseImageOpaqueMemoryBindInfo* pImageOpaqueBinds;
    uint32_t                                 imageBindCount;
    const VkSparseImageMemoryBindInfo*       pImageBinds;
    uint32_t                                 signalSemaphoreCount;
    const VkSemaphore*                       pSignalSemaphores;
} VkBindSparseInfo;

/* Vulkan 1.1 types */
typedef struct VkDeviceQueueInfo2 {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        queueFamilyIndex;
    uint32_t        queueIndex;
} VkDeviceQueueInfo2;

typedef struct VkDescriptorSetLayoutSupport {
    VkStructureType sType;
    void*           pNext;
    VkBool32        supported;
} VkDescriptorSetLayoutSupport;

typedef struct VkPhysicalDeviceGroupProperties {
    VkStructureType    sType;
    void*              pNext;
    uint32_t           physicalDeviceCount;
    VkPhysicalDevice   physicalDevices[32];
    VkBool32           subsetAllocation;
} VkPhysicalDeviceGroupProperties;

typedef struct VkExternalMemoryProperties {
    VkFlags externalMemoryFeatures;
    VkFlags exportFromImportedHandleTypes;
    VkFlags compatibleHandleTypes;
} VkExternalMemoryProperties;

typedef struct VkPhysicalDeviceExternalBufferInfo {
    VkStructureType       sType;
    const void*           pNext;
    VkFlags               flags;
    VkBufferUsageFlags    usage;
    VkFlags               handleType;
} VkPhysicalDeviceExternalBufferInfo;

typedef struct VkExternalBufferProperties {
    VkStructureType            sType;
    void*                      pNext;
    VkExternalMemoryProperties externalMemoryProperties;
} VkExternalBufferProperties;

typedef struct VkPhysicalDeviceExternalFenceInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         handleType;
} VkPhysicalDeviceExternalFenceInfo;

typedef struct VkExternalFenceProperties {
    VkStructureType sType;
    void*           pNext;
    VkFlags         exportFromImportedHandleTypes;
    VkFlags         compatibleHandleTypes;
    VkFlags         externalFenceFeatures;
} VkExternalFenceProperties;

typedef struct VkPhysicalDeviceExternalSemaphoreInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         handleType;
} VkPhysicalDeviceExternalSemaphoreInfo;

typedef struct VkExternalSemaphoreProperties {
    VkStructureType sType;
    void*           pNext;
    VkFlags         exportFromImportedHandleTypes;
    VkFlags         compatibleHandleTypes;
    VkFlags         externalSemaphoreFeatures;
} VkExternalSemaphoreProperties;

typedef struct VkSamplerYcbcrConversionCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFormat        format;
    uint32_t        ycbcrModel;
    uint32_t        ycbcrRange;
    VkComponentMapping components;
    uint32_t        xChromaOffset;
    uint32_t        yChromaOffset;
    VkFilter        chromaFilter;
    VkBool32        forceExplicitReconstruction;
} VkSamplerYcbcrConversionCreateInfo;

typedef struct VkDescriptorUpdateTemplateCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        descriptorUpdateEntryCount;
    const void*     pDescriptorUpdateEntries;
    uint32_t        templateType;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineBindPoint   pipelineBindPoint;
    VkPipelineLayout      pipelineLayout;
    uint32_t              set;
} VkDescriptorUpdateTemplateCreateInfo;

typedef struct VkImageSparseMemoryRequirementsInfo2 {
    VkStructureType sType;
    const void*     pNext;
    VkImage         image;
} VkImageSparseMemoryRequirementsInfo2;

typedef struct VkSparseImageMemoryRequirements2 {
    VkStructureType                  sType;
    void*                            pNext;
    VkSparseImageMemoryRequirements  memoryRequirements;
} VkSparseImageMemoryRequirements2;

typedef struct VkPhysicalDeviceSparseImageFormatInfo2 {
    VkStructureType          sType;
    const void*              pNext;
    VkFormat                 format;
    VkImageType              type;
    VkSampleCountFlagBits    samples;
    VkImageUsageFlags        usage;
    VkImageTiling            tiling;
} VkPhysicalDeviceSparseImageFormatInfo2;

typedef struct VkSparseImageFormatProperties2 {
    VkStructureType                sType;
    void*                          pNext;
    VkSparseImageFormatProperties  properties;
} VkSparseImageFormatProperties2;

/* Vulkan 1.2 types */
typedef struct VkBufferDeviceAddressInfo {
    VkStructureType sType;
    const void*     pNext;
    VkBuffer        buffer;
} VkBufferDeviceAddressInfo;

typedef struct VkDeviceMemoryOpaqueCaptureAddressInfo {
    VkStructureType sType;
    const void*     pNext;
    VkDeviceMemory  memory;
} VkDeviceMemoryOpaqueCaptureAddressInfo;

typedef struct VkSemaphoreWaitInfo {
    VkStructureType     sType;
    const void*         pNext;
    VkFlags             flags;
    uint32_t            semaphoreCount;
    const VkSemaphore*  pSemaphores;
    const uint64_t*     pValues;
} VkSemaphoreWaitInfo;

typedef struct VkSemaphoreSignalInfo {
    VkStructureType sType;
    const void*     pNext;
    VkSemaphore     semaphore;
    uint64_t        value;
} VkSemaphoreSignalInfo;

typedef struct VkAttachmentDescription2 {
    VkStructureType       sType;
    const void*           pNext;
    VkFlags               flags;
    VkFormat              format;
    VkSampleCountFlagBits samples;
    VkAttachmentLoadOp    loadOp;
    VkAttachmentStoreOp   storeOp;
    VkAttachmentLoadOp    stencilLoadOp;
    VkAttachmentStoreOp   stencilStoreOp;
    VkImageLayout         initialLayout;
    VkImageLayout         finalLayout;
} VkAttachmentDescription2;

typedef struct VkAttachmentReference2 {
    VkStructureType    sType;
    const void*        pNext;
    uint32_t           attachment;
    VkImageLayout      layout;
    VkImageAspectFlags aspectMask;
} VkAttachmentReference2;

typedef struct VkSubpassDescription2 {
    VkStructureType                  sType;
    const void*                      pNext;
    VkFlags                          flags;
    VkPipelineBindPoint              pipelineBindPoint;
    uint32_t                         viewMask;
    uint32_t                         inputAttachmentCount;
    const VkAttachmentReference2*    pInputAttachments;
    uint32_t                         colorAttachmentCount;
    const VkAttachmentReference2*    pColorAttachments;
    const VkAttachmentReference2*    pResolveAttachments;
    const VkAttachmentReference2*    pDepthStencilAttachment;
    uint32_t                         preserveAttachmentCount;
    const uint32_t*                  pPreserveAttachments;
} VkSubpassDescription2;

typedef struct VkRenderPassCreateInfo2 {
    VkStructureType                    sType;
    const void*                        pNext;
    VkFlags                            flags;
    uint32_t                           attachmentCount;
    const VkAttachmentDescription2*    pAttachments;
    uint32_t                           subpassCount;
    const VkSubpassDescription2*       pSubpasses;
    uint32_t                           dependencyCount;
    const void*                        pDependencies;
    uint32_t                           correlatedViewMaskCount;
    const uint32_t*                    pCorrelatedViewMasks;
} VkRenderPassCreateInfo2;

typedef struct VkSubpassBeginInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkSubpassContents  contents;
} VkSubpassBeginInfo;

typedef struct VkSubpassEndInfo {
    VkStructureType sType;
    const void*     pNext;
} VkSubpassEndInfo;

/* Vulkan 1.3 types */
typedef VkFlags64 VkPipelineStageFlags2;

typedef struct VkPrivateDataSlotCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
} VkPrivateDataSlotCreateInfo;

typedef struct VkMemoryBarrier2 {
    VkStructureType       sType;
    const void*           pNext;
    VkPipelineStageFlags2 srcStageMask;
    VkFlags64             srcAccessMask;
    VkPipelineStageFlags2 dstStageMask;
    VkFlags64             dstAccessMask;
} VkMemoryBarrier2;

typedef struct VkDependencyInfo {
    VkStructureType         sType;
    const void*             pNext;
    VkFlags                 dependencyFlags;
    uint32_t                memoryBarrierCount;
    const VkMemoryBarrier2* pMemoryBarriers;
    uint32_t                bufferMemoryBarrierCount;
    const void*             pBufferMemoryBarriers;
    uint32_t                imageMemoryBarrierCount;
    const void*             pImageMemoryBarriers;
} VkDependencyInfo;

typedef struct VkBufferCopy2 {
    VkStructureType sType;
    const void*     pNext;
    VkDeviceSize    srcOffset;
    VkDeviceSize    dstOffset;
    VkDeviceSize    size;
} VkBufferCopy2;

typedef struct VkCopyBufferInfo2 {
    VkStructureType       sType;
    const void*           pNext;
    VkBuffer              srcBuffer;
    VkBuffer              dstBuffer;
    uint32_t              regionCount;
    const VkBufferCopy2*  pRegions;
} VkCopyBufferInfo2;

typedef struct VkImageCopy2 {
    VkStructureType          sType;
    const void*              pNext;
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffset;
    VkExtent3D               extent;
} VkImageCopy2;

typedef struct VkCopyImageInfo2 {
    VkStructureType       sType;
    const void*           pNext;
    VkImage               srcImage;
    VkImageLayout         srcImageLayout;
    VkImage               dstImage;
    VkImageLayout         dstImageLayout;
    uint32_t              regionCount;
    const VkImageCopy2*   pRegions;
} VkCopyImageInfo2;

typedef struct VkBufferImageCopy2 {
    VkStructureType          sType;
    const void*              pNext;
    VkDeviceSize             bufferOffset;
    uint32_t                 bufferRowLength;
    uint32_t                 bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D               imageOffset;
    VkExtent3D               imageExtent;
} VkBufferImageCopy2;

typedef struct VkCopyBufferToImageInfo2 {
    VkStructureType             sType;
    const void*                 pNext;
    VkBuffer                    srcBuffer;
    VkImage                     dstImage;
    VkImageLayout               dstImageLayout;
    uint32_t                    regionCount;
    const VkBufferImageCopy2*   pRegions;
} VkCopyBufferToImageInfo2;

typedef struct VkCopyImageToBufferInfo2 {
    VkStructureType             sType;
    const void*                 pNext;
    VkImage                     srcImage;
    VkImageLayout               srcImageLayout;
    VkBuffer                    dstBuffer;
    uint32_t                    regionCount;
    const VkBufferImageCopy2*   pRegions;
} VkCopyImageToBufferInfo2;

typedef struct VkImageBlit2 {
    VkStructureType          sType;
    const void*              pNext;
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffsets[2];
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffsets[2];
} VkImageBlit2;

typedef struct VkBlitImageInfo2 {
    VkStructureType       sType;
    const void*           pNext;
    VkImage               srcImage;
    VkImageLayout         srcImageLayout;
    VkImage               dstImage;
    VkImageLayout         dstImageLayout;
    uint32_t              regionCount;
    const VkImageBlit2*   pRegions;
    VkFilter              filter;
} VkBlitImageInfo2;

typedef struct VkImageResolve2 {
    VkStructureType          sType;
    const void*              pNext;
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D               srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D               dstOffset;
    VkExtent3D               extent;
} VkImageResolve2;

typedef struct VkResolveImageInfo2 {
    VkStructureType         sType;
    const void*             pNext;
    VkImage                 srcImage;
    VkImageLayout           srcImageLayout;
    VkImage                 dstImage;
    VkImageLayout           dstImageLayout;
    uint32_t                regionCount;
    const VkImageResolve2*  pRegions;
} VkResolveImageInfo2;

/* PFN types for Vulkan 1.1/1.2/1.3 wrapper functions */
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties2)(VkPhysicalDevice, VkPhysicalDeviceProperties2*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceFeatures2)(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceQueueFamilyProperties2)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties2*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceMemoryProperties2)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceFormatProperties2)(VkPhysicalDevice, VkFormat, VkFormatProperties2*);
typedef void     (VKAPI_PTR *PFN_vkGetBufferMemoryRequirements2)(VkDevice, const VkBufferMemoryRequirementsInfo2*, VkMemoryRequirements2*);
typedef void     (VKAPI_PTR *PFN_vkGetImageMemoryRequirements2)(VkDevice, const VkImageMemoryRequirementsInfo2*, VkMemoryRequirements2*);
typedef VkResult (VKAPI_PTR *PFN_vkBindBufferMemory2)(VkDevice, uint32_t, const VkBindBufferMemoryInfo*);
typedef VkResult (VKAPI_PTR *PFN_vkBindImageMemory2)(VkDevice, uint32_t, const VkBindImageMemoryInfo*);
typedef void     (VKAPI_PTR *PFN_vkCmdPipelineBarrier2)(VkCommandBuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit2)(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceImageFormatProperties)(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties*);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_CORE_H */
