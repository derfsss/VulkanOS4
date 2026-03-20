/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_instance.c -- Instance management and physical device queries
**
** Functions here use standard Vulkan calling convention (no APICALL,
** no Self parameter). They are returned by vk_icdGetInstanceProcAddr
** and stored in VulkanIFace by the loader.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Physical device singleton                                                */
/* Represents the CPU as a Vulkan physical device.                          */
/****************************************************************************/

SWVKPhysicalDevice g_physDevice =
{
    .properties = {
        .apiVersion    = VK_API_VERSION_1_3,
        .driverVersion = VK_MAKE_API_VERSION(0, LIBVER, LIBREV, 0),
        .vendorID      = 0,        /* No PCI vendor */
        .deviceID      = 0,        /* No PCI device */
        .deviceType    = VK_PHYSICAL_DEVICE_TYPE_CPU,
        .deviceName    = "AmigaOS Software Renderer",
        .pipelineCacheUUID = {0},
        .limits = {
            .maxImageDimension1D                    = 4096,
            .maxImageDimension2D                    = 4096,
            .maxImageDimension3D                    = 256,
            .maxImageDimensionCube                  = 4096,
            .maxImageArrayLayers                    = 256,
            .maxTexelBufferElements                 = 65536,
            .maxUniformBufferRange                  = 16384,
            .maxStorageBufferRange                  = 65536,
            .maxPushConstantsSize                   = 128,
            .maxMemoryAllocationCount               = 4096,
            .maxSamplerAllocationCount              = 4000,
            .bufferImageGranularity                 = 1,
            .sparseAddressSpaceSize                 = 0,
            .maxBoundDescriptorSets                 = 4,
            .maxPerStageDescriptorSamplers          = 16,
            .maxPerStageDescriptorUniformBuffers    = 12,
            .maxPerStageDescriptorStorageBuffers    = 4,
            .maxPerStageDescriptorSampledImages     = 16,
            .maxPerStageDescriptorStorageImages     = 4,
            .maxPerStageDescriptorInputAttachments  = 4,
            .maxPerStageResources                   = 128,
            .maxDescriptorSetSamplers               = 96,
            .maxDescriptorSetUniformBuffers         = 72,
            .maxDescriptorSetUniformBuffersDynamic  = 8,
            .maxDescriptorSetStorageBuffers         = 24,
            .maxDescriptorSetStorageBuffersDynamic  = 4,
            .maxDescriptorSetSampledImages          = 96,
            .maxDescriptorSetStorageImages          = 24,
            .maxDescriptorSetInputAttachments       = 4,
            .maxVertexInputAttributes               = 16,
            .maxVertexInputBindings                 = 16,
            .maxVertexInputAttributeOffset          = 2047,
            .maxVertexInputBindingStride             = 2048,
            .maxVertexOutputComponents              = 64,
            .maxTessellationGenerationLevel         = 0,
            .maxTessellationPatchSize               = 0,
            .maxTessellationControlPerVertexInputComponents  = 0,
            .maxTessellationControlPerVertexOutputComponents = 0,
            .maxTessellationControlPerPatchOutputComponents  = 0,
            .maxTessellationControlTotalOutputComponents     = 0,
            .maxTessellationEvaluationInputComponents       = 0,
            .maxTessellationEvaluationOutputComponents      = 0,
            .maxGeometryShaderInvocations           = 0,
            .maxGeometryInputComponents             = 0,
            .maxGeometryOutputComponents            = 0,
            .maxGeometryOutputVertices              = 0,
            .maxGeometryTotalOutputComponents       = 0,
            .maxFragmentInputComponents             = 64,
            .maxFragmentOutputAttachments            = 4,
            .maxFragmentDualSrcAttachments          = 0,
            .maxFragmentCombinedOutputResources     = 4,
            .maxComputeSharedMemorySize             = 16384,
            .maxComputeWorkGroupCount               = {65535, 65535, 65535},
            .maxComputeWorkGroupInvocations         = 128,
            .maxComputeWorkGroupSize                = {128, 128, 64},
            .subPixelPrecisionBits                  = 4,
            .subTexelPrecisionBits                  = 4,
            .mipmapPrecisionBits                    = 4,
            .maxDrawIndexedIndexValue               = 0xFFFFFFFF,
            .maxDrawIndirectCount                   = 1,
            .maxSamplerLodBias                      = 2.0f,
            .maxSamplerAnisotropy                   = 1.0f,
            .maxViewports                           = 1,
            .maxViewportDimensions                  = {4096, 4096},
            .viewportBoundsRange                    = {-8192.0f, 8191.0f},
            .viewportSubPixelBits                   = 0,
            .minMemoryMapAlignment                  = 64,
            .minTexelBufferOffsetAlignment          = 256,
            .minUniformBufferOffsetAlignment        = 256,
            .minStorageBufferOffsetAlignment        = 256,
            .minTexelOffset                         = -8,
            .maxTexelOffset                         = 7,
            .minTexelGatherOffset                   = 0,
            .maxTexelGatherOffset                   = 0,
            .minInterpolationOffset                 = 0.0f,
            .maxInterpolationOffset                 = 0.0f,
            .subPixelInterpolationOffsetBits        = 0,
            .maxFramebufferWidth                    = 4096,
            .maxFramebufferHeight                   = 4096,
            .maxFramebufferLayers                   = 1,
            .framebufferColorSampleCounts           = VK_SAMPLE_COUNT_1_BIT,
            .framebufferDepthSampleCounts           = VK_SAMPLE_COUNT_1_BIT,
            .framebufferStencilSampleCounts         = VK_SAMPLE_COUNT_1_BIT,
            .framebufferNoAttachmentsSampleCounts   = VK_SAMPLE_COUNT_1_BIT,
            .maxColorAttachments                    = 4,
            .sampledImageColorSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
            .sampledImageIntegerSampleCounts        = VK_SAMPLE_COUNT_1_BIT,
            .sampledImageDepthSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
            .sampledImageStencilSampleCounts        = VK_SAMPLE_COUNT_1_BIT,
            .storageImageSampleCounts               = VK_SAMPLE_COUNT_1_BIT,
            .maxSampleMaskWords                     = 1,
            .timestampComputeAndGraphics            = VK_FALSE,
            .timestampPeriod                        = 0.0f,
            .maxClipDistances                       = 0,
            .maxCullDistances                       = 0,
            .maxCombinedClipAndCullDistances        = 0,
            .discreteQueuePriorities                = 2,
            .pointSizeRange                         = {1.0f, 1.0f},
            .lineWidthRange                         = {1.0f, 1.0f},
            .pointSizeGranularity                   = 0.0f,
            .lineWidthGranularity                   = 0.0f,
            .strictLines                            = VK_FALSE,
            .standardSampleLocations                = VK_TRUE,
            .optimalBufferCopyOffsetAlignment       = 1,
            .optimalBufferCopyRowPitchAlignment      = 1,
            .nonCoherentAtomSize                    = 256,
        },
        .sparseProperties = {0},
    },
    .features = {
        /* Conservative feature set for software renderer.
        ** Only enable what we actually implement. */
        .robustBufferAccess                 = VK_FALSE,
        .fullDrawIndexUint32                = VK_TRUE,
        .imageCubeArray                     = VK_FALSE,
        .independentBlend                   = VK_FALSE,
        .geometryShader                     = VK_FALSE,
        .tessellationShader                 = VK_FALSE,
        .sampleRateShading                  = VK_FALSE,
        .dualSrcBlend                       = VK_FALSE,
        .logicOp                            = VK_FALSE,
        .multiDrawIndirect                  = VK_FALSE,
        .drawIndirectFirstInstance           = VK_FALSE,
        .depthClamp                         = VK_FALSE,
        .depthBiasClamp                     = VK_FALSE,
        .fillModeNonSolid                   = VK_FALSE,
        .depthBounds                        = VK_FALSE,
        .wideLines                          = VK_FALSE,
        .largePoints                        = VK_FALSE,
        .alphaToOne                         = VK_FALSE,
        .multiViewport                      = VK_FALSE,
        .samplerAnisotropy                  = VK_FALSE,
        .textureCompressionETC2             = VK_FALSE,
        .textureCompressionASTC_LDR         = VK_FALSE,
        .textureCompressionBC               = VK_FALSE,
        .occlusionQueryPrecise              = VK_FALSE,
        .pipelineStatisticsQuery            = VK_FALSE,
        .vertexPipelineStoresAndAtomics     = VK_FALSE,
        .fragmentStoresAndAtomics           = VK_FALSE,
        .shaderTessellationAndGeometryPointSize = VK_FALSE,
        .shaderImageGatherExtended          = VK_FALSE,
        .shaderStorageImageExtendedFormats  = VK_FALSE,
        .shaderStorageImageMultisample      = VK_FALSE,
        .shaderStorageImageReadWithoutFormat = VK_FALSE,
        .shaderStorageImageWriteWithoutFormat = VK_FALSE,
        .shaderUniformBufferArrayDynamicIndexing = VK_FALSE,
        .shaderSampledImageArrayDynamicIndexing = VK_FALSE,
        .shaderStorageBufferArrayDynamicIndexing = VK_FALSE,
        .shaderStorageImageArrayDynamicIndexing = VK_FALSE,
        .shaderClipDistance                 = VK_FALSE,
        .shaderCullDistance                 = VK_FALSE,
        .shaderFloat64                      = VK_FALSE,
        .shaderInt64                        = VK_FALSE,
        .shaderInt16                        = VK_FALSE,
        .shaderResourceResidency            = VK_FALSE,
        .shaderResourceMinLod               = VK_FALSE,
        .sparseBinding                      = VK_FALSE,
        .sparseResidencyBuffer              = VK_FALSE,
        .sparseResidencyImage2D             = VK_FALSE,
        .sparseResidencyImage3D             = VK_FALSE,
        .sparseResidency2Samples            = VK_FALSE,
        .sparseResidency4Samples            = VK_FALSE,
        .sparseResidency8Samples            = VK_FALSE,
        .sparseResidency16Samples           = VK_FALSE,
        .sparseResidencyAliased             = VK_FALSE,
        .variableMultisampleRate            = VK_FALSE,
        .inheritedQueries                   = VK_FALSE,
    },
    .memoryProperties = {
        /* Single memory type: host-visible, host-coherent system RAM */
        .memoryTypeCount = 1,
        .memoryTypes = {
            [0] = {
                .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                .heapIndex = 0,
            },
        },
        .memoryHeapCount = 1,
        .memoryHeaps = {
            [0] = {
                .size  = 256ULL * 1024 * 1024,  /* 256 MB */
                .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
            },
        },
    },
    .queueFamilyProperties = {
        /* Single queue family: graphics + transfer (no compute) */
        .queueFlags                    = VK_QUEUE_GRAPHICS_BIT |
                                         VK_QUEUE_TRANSFER_BIT,
        .queueCount                    = 1,
        .timestampValidBits            = 0,
        .minImageTransferGranularity   = {1, 1, 1},
    },
};

/****************************************************************************/
/* Instance creation                                                        */
/****************************************************************************/

VkResult swvk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkInstance *pInstance)
{
    (void)pAllocator;

    SWVKInstance *inst = (SWVKInstance *)IExec->AllocVecTags(
        sizeof(SWVKInstance),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!inst)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    inst->apiVersion = (pCreateInfo->pApplicationInfo != NULL)
        ? pCreateInfo->pApplicationInfo->apiVersion
        : VK_API_VERSION_1_3;

    *pInstance = (VkInstance)inst;

    IExec->DebugPrintF("[software_vk] Instance created (API %lu.%lu)\n",
                       (unsigned long)VK_API_VERSION_MAJOR(inst->apiVersion),
                       (unsigned long)VK_API_VERSION_MINOR(inst->apiVersion));

    return VK_SUCCESS;
}

void swvk_DestroyInstance(VkInstance instance,
                          const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (instance != VK_NULL_HANDLE)
    {
        IExec->FreeVec((APTR)instance);
        IExec->DebugPrintF("[software_vk] Instance destroyed\n");
    }
}

VkResult swvk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
    *pApiVersion = VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

VkResult swvk_EnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    (void)pLayerName;

    /* The ICD has no instance extensions of its own.
    ** VK_KHR_swapchain is a device extension (reported by
    ** vkEnumerateDeviceExtensionProperties, not implemented yet).
    ** VK_KHR_surface / VK_AMIGA_surface are loader-owned. */
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Physical device enumeration                                              */
/****************************************************************************/

VkResult swvk_EnumeratePhysicalDevices(VkInstance instance,
                                       uint32_t *pPhysicalDeviceCount,
                                       VkPhysicalDevice *pPhysicalDevices)
{
    (void)instance;

    if (pPhysicalDevices == NULL)
    {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceCount >= 1)
    {
        pPhysicalDevices[0] = (VkPhysicalDevice)&g_physDevice;
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    /* Buffer too small */
    *pPhysicalDeviceCount = 0;
    return VK_INCOMPLETE;
}

/****************************************************************************/
/* Physical device property queries                                         */
/****************************************************************************/

void swvk_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                      VkPhysicalDeviceProperties *pProperties)
{
    SWVKPhysicalDevice *pd = (SWVKPhysicalDevice *)physicalDevice;
    memcpy(pProperties, &pd->properties, sizeof(VkPhysicalDeviceProperties));
}

void swvk_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceFeatures *pFeatures)
{
    SWVKPhysicalDevice *pd = (SWVKPhysicalDevice *)physicalDevice;
    memcpy(pFeatures, &pd->features, sizeof(VkPhysicalDeviceFeatures));
}

void swvk_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties)
{
    SWVKPhysicalDevice *pd = (SWVKPhysicalDevice *)physicalDevice;

    if (pQueueFamilyProperties == NULL)
    {
        *pQueueFamilyPropertyCount = 1;
        return;
    }

    if (*pQueueFamilyPropertyCount >= 1)
    {
        memcpy(&pQueueFamilyProperties[0], &pd->queueFamilyProperties,
               sizeof(VkQueueFamilyProperties));
        *pQueueFamilyPropertyCount = 1;
    }
    else
    {
        *pQueueFamilyPropertyCount = 0;
    }
}

void swvk_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    SWVKPhysicalDevice *pd = (SWVKPhysicalDevice *)physicalDevice;
    memcpy(pMemoryProperties, &pd->memoryProperties,
           sizeof(VkPhysicalDeviceMemoryProperties));
}

void swvk_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties *pFormatProperties)
{
    (void)physicalDevice;

    memset(pFormatProperties, 0, sizeof(VkFormatProperties));

    /* Report format support for commonly used formats.
    ** The software renderer supports linear tiling for all formats
    ** (it's all in system RAM anyway). */
    switch (format)
    {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
            pFormatProperties->linearTilingFeatures =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                VK_FORMAT_FEATURE_BLIT_DST_BIT |
                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            pFormatProperties->optimalTilingFeatures =
                pFormatProperties->linearTilingFeatures;
            break;

        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32_SFLOAT:
            pFormatProperties->bufferFeatures =
                VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
            break;

        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM:
            pFormatProperties->optimalTilingFeatures =
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
            break;

        default:
            break;
    }
}

/****************************************************************************/
/* vkGetPhysicalDeviceImageFormatProperties                                 */
/****************************************************************************/

VkResult swvk_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties)
{
    (void)physicalDevice;
    (void)tiling;
    (void)flags;

    memset(pImageFormatProperties, 0, sizeof(VkImageFormatProperties));

    /* Check if the format is one we support */
    VkBool32 supported = VK_FALSE;
    switch (format)
    {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM:
            supported = VK_TRUE;
            break;
        default:
            break;
    }

    if (!supported)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    /* Return basic capabilities for supported formats */
    pImageFormatProperties->maxExtent.width  = 4096;
    pImageFormatProperties->maxExtent.height = (type >= VK_IMAGE_TYPE_2D) ? 4096 : 1;
    pImageFormatProperties->maxExtent.depth  = (type >= VK_IMAGE_TYPE_3D) ? 256 : 1;
    pImageFormatProperties->maxMipLevels     = 1;
    pImageFormatProperties->maxArrayLayers   = 1;
    pImageFormatProperties->sampleCounts     = VK_SAMPLE_COUNT_1_BIT;
    pImageFormatProperties->maxResourceSize  = 256ULL * 1024 * 1024;

    (void)usage;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Vulkan 1.1/1.2/1.3 wrapper functions                                    */
/* These "2" variants wrap the v1 functions and walk the pNext chain.       */
/****************************************************************************/

void swvk_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceProperties2 *pProperties)
{
    swvk_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);
    /* Walk pNext chain -- for now, leave pNext structs unfilled (zeroed by caller) */
}

void swvk_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                      VkPhysicalDeviceFeatures2 *pFeatures)
{
    swvk_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
    /* Walk pNext chain -- for now, leave pNext structs unfilled */
}

void swvk_GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
    if (pQueueFamilyProperties == NULL)
    {
        /* Just return count */
        swvk_GetPhysicalDeviceQueueFamilyProperties(physicalDevice,
            pQueueFamilyPropertyCount, NULL);
        return;
    }

    /* Wrap the v1 call into a temporary v1 struct */
    uint32_t count = *pQueueFamilyPropertyCount;
    VkQueueFamilyProperties tempProps;
    uint32_t tempCount = 1;
    swvk_GetPhysicalDeviceQueueFamilyProperties(physicalDevice,
        &tempCount, &tempProps);

    if (count >= 1 && tempCount >= 1)
    {
        pQueueFamilyProperties[0].queueFamilyProperties = tempProps;
        *pQueueFamilyPropertyCount = 1;
    }
    else
    {
        *pQueueFamilyPropertyCount = 0;
    }
}

void swvk_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
    swvk_GetPhysicalDeviceMemoryProperties(physicalDevice,
        &pMemoryProperties->memoryProperties);
}

void swvk_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2 *pFormatProperties)
{
    swvk_GetPhysicalDeviceFormatProperties(physicalDevice, format,
        &pFormatProperties->formatProperties);
}

/****************************************************************************/
/* Physical device external properties stubs                                */
/****************************************************************************/

void swvk_GetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
    VkExternalBufferProperties *pExternalBufferProperties)
{
    (void)physicalDevice; (void)pExternalBufferInfo;
    if (pExternalBufferProperties)
    {
        memset(&pExternalBufferProperties->externalMemoryProperties, 0,
               sizeof(VkExternalMemoryProperties));
    }
}

void swvk_GetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties)
{
    (void)physicalDevice; (void)pExternalFenceInfo;
    if (pExternalFenceProperties)
    {
        pExternalFenceProperties->exportFromImportedHandleTypes = 0;
        pExternalFenceProperties->compatibleHandleTypes         = 0;
        pExternalFenceProperties->externalFenceFeatures         = 0;
    }
}

void swvk_GetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
    (void)physicalDevice; (void)pExternalSemaphoreInfo;
    if (pExternalSemaphoreProperties)
    {
        pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
        pExternalSemaphoreProperties->compatibleHandleTypes         = 0;
        pExternalSemaphoreProperties->externalSemaphoreFeatures     = 0;
    }
}

/****************************************************************************/
/* Physical device group enumeration                                        */
/****************************************************************************/

VkResult swvk_EnumeratePhysicalDeviceGroups(
    VkInstance instance,
    uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties)
{
    (void)instance;

    if (pPhysicalDeviceGroupProperties == NULL)
    {
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceGroupCount >= 1)
    {
        memset(&pPhysicalDeviceGroupProperties[0], 0,
               sizeof(VkPhysicalDeviceGroupProperties));
        pPhysicalDeviceGroupProperties[0].sType = (VkStructureType)1000070000;
        pPhysicalDeviceGroupProperties[0].physicalDeviceCount = 1;
        pPhysicalDeviceGroupProperties[0].physicalDevices[0]  = (VkPhysicalDevice)&g_physDevice;
        pPhysicalDeviceGroupProperties[0].subsetAllocation    = VK_FALSE;
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }

    *pPhysicalDeviceGroupCount = 0;
    return VK_INCOMPLETE;
}

/****************************************************************************/
/* Physical device tool properties                                          */
/****************************************************************************/

VkResult swvk_GetPhysicalDeviceToolProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pToolCount,
    void *pToolProperties)
{
    (void)physicalDevice; (void)pToolProperties;
    if (pToolCount) *pToolCount = 0;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Physical device image format properties 2                                */
/****************************************************************************/

VkResult swvk_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
    VkImageFormatProperties2 *pImageFormatProperties)
{
    return swvk_GetPhysicalDeviceImageFormatProperties(
        physicalDevice,
        pImageFormatInfo->format,
        pImageFormatInfo->type,
        pImageFormatInfo->tiling,
        pImageFormatInfo->usage,
        pImageFormatInfo->flags,
        &pImageFormatProperties->imageFormatProperties);
}

/****************************************************************************/
/* Render area granularity                                                   */
/****************************************************************************/

void swvk_GetRenderAreaGranularity(VkDevice device,
                                     VkRenderPass renderPass,
                                     VkExtent2D *pGranularity)
{
    (void)device; (void)renderPass;
    if (pGranularity)
    {
        pGranularity->width  = 1;
        pGranularity->height = 1;
    }
}

/****************************************************************************/
/* Sparse image format properties stubs                                     */
/****************************************************************************/

void swvk_GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *pProperties)
{
    (void)physicalDevice; (void)format; (void)type; (void)samples;
    (void)usage; (void)tiling; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

void swvk_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2 *pProperties)
{
    (void)physicalDevice; (void)pFormatInfo; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}
