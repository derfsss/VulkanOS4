/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_device.c -- Device, memory, buffer, image, sampler management
**
** CPU-side metadata for Vulkan resources. Actual GPU resources (VBOs,
** textures, samplers) are created lazily when the rendering pipeline
** needs them.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "ogles2vk_internal.h"

/****************************************************************************/
/* Helper: bytes per pixel for a VkFormat                                   */
/****************************************************************************/

static uint32_t ogles2vk_FormatBytesPerPixel(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        return 4;
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    case VK_FORMAT_D16_UNORM:
        return 2;
    case VK_FORMAT_D32_SFLOAT:
        return 4;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 8;
    default:
        return 4;
    }
}

/****************************************************************************/
/* Device creation                                                          */
/****************************************************************************/

VkResult ogles2vk_CreateDevice(VkPhysicalDevice physicalDevice,
                             const VkDeviceCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkDevice *pDevice)
{
    (void)pAllocator;

    if (!physicalDevice || !pCreateInfo || !pDevice)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKDevice *dev = (OGLES2VKDevice *)IExec->AllocVecTags(
        sizeof(OGLES2VKDevice),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!dev)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    dev->physicalDevice = (OGLES2VKPhysicalDevice *)physicalDevice;
    dev->queue.familyIndex = 0;
    dev->queue.queueIndex  = 0;
    dev->glContext = NULL;  /* Created later by WSI */

    *pDevice = (VkDevice)dev;

    IExec->DebugPrintF("[ogles2_vk] Device created\n");

    return VK_SUCCESS;
}

void ogles2vk_DestroyDevice(VkDevice device,
                          const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (!device) return;

    /* Clean up OGLES2 context if active */
    ogles2vk_ShutdownOGLES2Context((OGLES2VKDevice *)device);

    IExec->FreeVec((APTR)device);

    IExec->DebugPrintF("[ogles2_vk] Device destroyed\n");
}

void ogles2vk_GetDeviceQueue(VkDevice device,
                           uint32_t queueFamilyIndex,
                           uint32_t queueIndex,
                           VkQueue *pQueue)
{
    (void)queueFamilyIndex;
    (void)queueIndex;

    if (!device || !pQueue)
        return;

    OGLES2VKDevice *dev = (OGLES2VKDevice *)device;
    *pQueue = (VkQueue)&dev->queue;
}

/****************************************************************************/
/* Memory allocation                                                        */
/****************************************************************************/

VkResult ogles2vk_AllocateMemory(VkDevice device,
                               const VkMemoryAllocateInfo *pAllocateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkDeviceMemory *pMemory)
{
    (void)device;
    (void)pAllocator;

    if (!pAllocateInfo || !pMemory)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pAllocateInfo->allocationSize == 0)
    {
        *pMemory = VK_NULL_HANDLE;
        return VK_SUCCESS;
    }

    if (pAllocateInfo->allocationSize > 0xFFFFFFFFULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    OGLES2VKMemory *mem = (OGLES2VKMemory *)IExec->AllocVecTags(
        sizeof(OGLES2VKMemory),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mem)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    mem->data = IExec->AllocVecTags(
        (uint32_t)pAllocateInfo->allocationSize,
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mem->data)
    {
        IExec->FreeVec(mem);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    mem->size            = pAllocateInfo->allocationSize;
    mem->mapped          = NULL;
    mem->memoryTypeIndex = pAllocateInfo->memoryTypeIndex;

    *pMemory = (VkDeviceMemory)(uintptr_t)mem;

    return VK_SUCCESS;
}

void ogles2vk_FreeMemory(VkDevice device,
                       VkDeviceMemory memory,
                       const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (memory == VK_NULL_HANDLE)
        return;

    OGLES2VKMemory *mem = (OGLES2VKMemory *)(uintptr_t)memory;

    if (mem->data)
        IExec->FreeVec(mem->data);
    IExec->FreeVec(mem);
}

VkResult ogles2vk_MapMemory(VkDevice device,
                          VkDeviceMemory memory,
                          VkDeviceSize offset,
                          VkDeviceSize size,
                          VkMemoryMapFlags flags,
                          void **ppData)
{
    (void)device;
    (void)flags;

    if (memory == VK_NULL_HANDLE || !ppData)
        return VK_ERROR_MEMORY_MAP_FAILED;

    OGLES2VKMemory *mem = (OGLES2VKMemory *)(uintptr_t)memory;

    if (!mem->data)
        return VK_ERROR_MEMORY_MAP_FAILED;

    if (offset >= mem->size)
        return VK_ERROR_MEMORY_MAP_FAILED;

    if (size != VK_WHOLE_SIZE && (offset + size) > mem->size)
        return VK_ERROR_MEMORY_MAP_FAILED;

    *ppData = (uint8_t *)mem->data + offset;
    mem->mapped = *ppData;

    return VK_SUCCESS;
}

void ogles2vk_UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    (void)device;

    if (memory == VK_NULL_HANDLE)
        return;

    OGLES2VKMemory *mem = (OGLES2VKMemory *)(uintptr_t)memory;
    mem->mapped = NULL;
}

/****************************************************************************/
/* Buffer creation                                                          */
/****************************************************************************/

VkResult ogles2vk_CreateBuffer(VkDevice device,
                             const VkBufferCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkBuffer *pBuffer)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKBuffer *buf = (OGLES2VKBuffer *)IExec->AllocVecTags(
        sizeof(OGLES2VKBuffer),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!buf)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    buf->size        = pCreateInfo->size;
    buf->usage       = pCreateInfo->usage;
    buf->boundMemory = NULL;
    buf->boundOffset = 0;

    *pBuffer = (VkBuffer)(uintptr_t)buf;

    return VK_SUCCESS;
}

void ogles2vk_DestroyBuffer(VkDevice device,
                          VkBuffer buffer,
                          const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (buffer == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)buffer);
}

void ogles2vk_GetBufferMemoryRequirements(VkDevice device,
                                        VkBuffer buffer,
                                        VkMemoryRequirements *pMemoryRequirements)
{
    (void)device;

    if (!pMemoryRequirements || buffer == VK_NULL_HANDLE)
        return;

    OGLES2VKBuffer *buf = (OGLES2VKBuffer *)(uintptr_t)buffer;

    pMemoryRequirements->size      = buf->size;
    pMemoryRequirements->alignment = 256;
    /* Bits 0 and 1: both memory types (device-local and host-visible) */
    pMemoryRequirements->memoryTypeBits = 0x3;
}

VkResult ogles2vk_BindBufferMemory(VkDevice device,
                                 VkBuffer buffer,
                                 VkDeviceMemory memory,
                                 VkDeviceSize memoryOffset)
{
    (void)device;

    if (buffer == VK_NULL_HANDLE || memory == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    OGLES2VKBuffer *buf = (OGLES2VKBuffer *)(uintptr_t)buffer;
    OGLES2VKMemory *mem = (OGLES2VKMemory *)(uintptr_t)memory;

    buf->boundMemory = mem;
    buf->boundOffset = memoryOffset;

    return VK_SUCCESS;
}

/****************************************************************************/
/* Image creation                                                           */
/****************************************************************************/

VkResult ogles2vk_CreateImage(VkDevice device,
                            const VkImageCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkImage *pImage)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pImage)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKImage *img = (OGLES2VKImage *)IExec->AllocVecTags(
        sizeof(OGLES2VKImage),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!img)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    img->width       = pCreateInfo->extent.width;
    img->height      = pCreateInfo->extent.height;
    img->depth       = pCreateInfo->extent.depth;
    img->mipLevels   = pCreateInfo->mipLevels;
    img->arrayLayers = pCreateInfo->arrayLayers;
    img->format      = pCreateInfo->format;
    img->imageType   = pCreateInfo->imageType;
    img->usage       = pCreateInfo->usage;
    img->tiling      = pCreateInfo->tiling;
    img->boundMemory = NULL;
    img->boundOffset = 0;

    /* Compute memory size: width * height * depth * bpp * layers
    ** Note: mip chain memory not included (only base level supported) */
    uint32_t bpp = ogles2vk_FormatBytesPerPixel(pCreateInfo->format);
    VkDeviceSize baseSize = (VkDeviceSize)img->width * img->height *
                            img->depth * bpp;
    img->memorySize = baseSize * img->arrayLayers;

    *pImage = (VkImage)(uintptr_t)img;

    return VK_SUCCESS;
}

void ogles2vk_DestroyImage(VkDevice device,
                         VkImage image,
                         const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (image == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)image);
}

void ogles2vk_GetImageMemoryRequirements(VkDevice device,
                                       VkImage image,
                                       VkMemoryRequirements *pMemoryRequirements)
{
    (void)device;

    if (!pMemoryRequirements || image == VK_NULL_HANDLE)
        return;

    OGLES2VKImage *img = (OGLES2VKImage *)(uintptr_t)image;

    pMemoryRequirements->size      = img->memorySize;
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 0x3;
}

VkResult ogles2vk_BindImageMemory(VkDevice device,
                                VkImage image,
                                VkDeviceMemory memory,
                                VkDeviceSize memoryOffset)
{
    (void)device;

    if (image == VK_NULL_HANDLE || memory == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    OGLES2VKImage *img = (OGLES2VKImage *)(uintptr_t)image;
    OGLES2VKMemory *mem = (OGLES2VKMemory *)(uintptr_t)memory;

    img->boundMemory = mem;
    img->boundOffset = memoryOffset;

    return VK_SUCCESS;
}

/****************************************************************************/
/* Image view creation                                                      */
/****************************************************************************/

VkResult ogles2vk_CreateImageView(VkDevice device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImageView *pView)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pView)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKImageView *view = (OGLES2VKImageView *)IExec->AllocVecTags(
        sizeof(OGLES2VKImageView),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!view)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    view->image            = (OGLES2VKImage *)(uintptr_t)pCreateInfo->image;
    view->viewType         = pCreateInfo->viewType;
    view->format           = pCreateInfo->format;
    view->subresourceRange = pCreateInfo->subresourceRange;

    *pView = (VkImageView)(uintptr_t)view;

    return VK_SUCCESS;
}

void ogles2vk_DestroyImageView(VkDevice device,
                              VkImageView imageView,
                              const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (imageView == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)imageView);
}

/****************************************************************************/
/* Sampler creation                                                         */
/****************************************************************************/

VkResult ogles2vk_CreateSampler(VkDevice device,
                              const VkSamplerCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkSampler *pSampler)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pSampler)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKSampler *sampler = (OGLES2VKSampler *)IExec->AllocVecTags(
        sizeof(OGLES2VKSampler),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sampler)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    sampler->magFilter       = pCreateInfo->magFilter;
    sampler->minFilter       = pCreateInfo->minFilter;
    sampler->mipmapMode      = pCreateInfo->mipmapMode;
    sampler->addressModeU    = pCreateInfo->addressModeU;
    sampler->addressModeV    = pCreateInfo->addressModeV;
    sampler->addressModeW    = pCreateInfo->addressModeW;
    sampler->maxAnisotropy   = pCreateInfo->maxAnisotropy;
    sampler->anisotropyEnable = pCreateInfo->anisotropyEnable;

    *pSampler = (VkSampler)(uintptr_t)sampler;

    return VK_SUCCESS;
}

void ogles2vk_DestroySampler(VkDevice device,
                           VkSampler sampler,
                           const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (sampler == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)sampler);
}

/****************************************************************************/
/* Vulkan 1.1 buffer/image wrappers                                         */
/****************************************************************************/

void ogles2vk_GetBufferMemoryRequirements2(VkDevice device,
                                         const VkBufferMemoryRequirementsInfo2 *pInfo,
                                         VkMemoryRequirements2 *pMemoryRequirements)
{
    ogles2vk_GetBufferMemoryRequirements(device, pInfo->buffer,
        &pMemoryRequirements->memoryRequirements);
}

void ogles2vk_GetImageMemoryRequirements2(VkDevice device,
                                        const VkImageMemoryRequirementsInfo2 *pInfo,
                                        VkMemoryRequirements2 *pMemoryRequirements)
{
    ogles2vk_GetImageMemoryRequirements(device, pInfo->image,
        &pMemoryRequirements->memoryRequirements);
}

VkResult ogles2vk_BindBufferMemory2(VkDevice device,
                                  uint32_t bindInfoCount,
                                  const VkBindBufferMemoryInfo *pBindInfos)
{
    for (uint32_t i = 0; i < bindInfoCount; i++)
    {
        VkResult result = ogles2vk_BindBufferMemory(device,
            pBindInfos[i].buffer,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS)
            return result;
    }
    return VK_SUCCESS;
}

VkResult ogles2vk_BindImageMemory2(VkDevice device,
                                 uint32_t bindInfoCount,
                                 const VkBindImageMemoryInfo *pBindInfos)
{
    for (uint32_t i = 0; i < bindInfoCount; i++)
    {
        VkResult result = ogles2vk_BindImageMemory(device,
            pBindInfos[i].image,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS)
            return result;
    }
    return VK_SUCCESS;
}

/****************************************************************************/
/* Descriptor set layout                                                    */
/****************************************************************************/

VkResult ogles2vk_CreateDescriptorSetLayout(VkDevice device,
                                          const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                          const VkAllocationCallbacks *pAllocator,
                                          VkDescriptorSetLayout *pSetLayout)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pSetLayout)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKDescriptorSetLayout *layout = (OGLES2VKDescriptorSetLayout *)IExec->AllocVecTags(
        sizeof(OGLES2VKDescriptorSetLayout),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    layout->bindingCount = pCreateInfo->bindingCount;
    if (layout->bindingCount > OGLES2VK_MAX_DESCRIPTOR_BINDINGS)
    {
        IExec->DebugPrintF("[ogles2_vk] WARNING: descriptor bindings clamped (%u > %u)\n",
                           (unsigned)layout->bindingCount,
                           (unsigned)OGLES2VK_MAX_DESCRIPTOR_BINDINGS);
        layout->bindingCount = OGLES2VK_MAX_DESCRIPTOR_BINDINGS;
    }

    if (pCreateInfo->pBindings)
    {
        for (uint32_t i = 0; i < layout->bindingCount; i++)
            layout->bindings[i] = pCreateInfo->pBindings[i];
    }

    *pSetLayout = (VkDescriptorSetLayout)(uintptr_t)layout;

    IExec->DebugPrintF("[ogles2_vk] Descriptor set layout created: %lu bindings\n",
                       (unsigned long)layout->bindingCount);

    return VK_SUCCESS;
}

void ogles2vk_DestroyDescriptorSetLayout(VkDevice device,
                                       VkDescriptorSetLayout descriptorSetLayout,
                                       const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (descriptorSetLayout == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)descriptorSetLayout);
}

/****************************************************************************/
/* Descriptor pool                                                          */
/****************************************************************************/

VkResult ogles2vk_CreateDescriptorPool(VkDevice device,
                                     const VkDescriptorPoolCreateInfo *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkDescriptorPool *pDescriptorPool)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKDescriptorPool *pool = (OGLES2VKDescriptorPool *)IExec->AllocVecTags(
        sizeof(OGLES2VKDescriptorPool),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->maxSets = pCreateInfo->maxSets;

    *pDescriptorPool = (VkDescriptorPool)(uintptr_t)pool;

    IExec->DebugPrintF("[ogles2_vk] Descriptor pool created: maxSets=%lu\n",
                       (unsigned long)pool->maxSets);

    return VK_SUCCESS;
}

void ogles2vk_DestroyDescriptorPool(VkDevice device,
                                  VkDescriptorPool descriptorPool,
                                  const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (descriptorPool == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)descriptorPool);
}

VkResult ogles2vk_ResetDescriptorPool(VkDevice device,
                                    VkDescriptorPool descriptorPool,
                                    VkDescriptorPoolResetFlags flags)
{
    (void)device;
    (void)descriptorPool;
    (void)flags;
    /* Known limitation: pool doesn't track allocated sets, so we
    ** cannot free them. Just accept the call. */
    return VK_SUCCESS;
}

/****************************************************************************/
/* Descriptor set allocation                                                */
/****************************************************************************/

VkResult ogles2vk_AllocateDescriptorSets(VkDevice device,
                                       const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                       VkDescriptorSet *pDescriptorSets)
{
    (void)device;

    if (!pAllocateInfo || !pDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
    {
        OGLES2VKDescriptorSet *set = (OGLES2VKDescriptorSet *)IExec->AllocVecTags(
            sizeof(OGLES2VKDescriptorSet),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!set)
        {
            /* Free already-allocated sets in this batch */
            for (uint32_t j = 0; j < i; j++)
            {
                if (pDescriptorSets[j] != VK_NULL_HANDLE)
                    IExec->FreeVec((APTR)(uintptr_t)pDescriptorSets[j]);
                pDescriptorSets[j] = VK_NULL_HANDLE;
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        /* Copy binding layout info from the corresponding set layout */
        if (pAllocateInfo->pSetLayouts &&
            pAllocateInfo->pSetLayouts[i] != VK_NULL_HANDLE)
        {
            OGLES2VKDescriptorSetLayout *layout =
                (OGLES2VKDescriptorSetLayout *)(uintptr_t)pAllocateInfo->pSetLayouts[i];
            set->bindingCount = layout->bindingCount;
            for (uint32_t b = 0; b < set->bindingCount; b++)
                set->bindings[b].type = layout->bindings[b].descriptorType;
        }

        pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)set;
    }

    IExec->DebugPrintF("[ogles2_vk] Allocated %lu descriptor sets\n",
                       (unsigned long)pAllocateInfo->descriptorSetCount);

    return VK_SUCCESS;
}

VkResult ogles2vk_FreeDescriptorSets(VkDevice device,
                                   VkDescriptorPool descriptorPool,
                                   uint32_t descriptorSetCount,
                                   const VkDescriptorSet *pDescriptorSets)
{
    (void)device;
    (void)descriptorPool;

    if (!pDescriptorSets)
        return VK_SUCCESS;

    for (uint32_t i = 0; i < descriptorSetCount; i++)
    {
        if (pDescriptorSets[i] != VK_NULL_HANDLE)
            IExec->FreeVec((APTR)(uintptr_t)pDescriptorSets[i]);
    }

    return VK_SUCCESS;
}

/****************************************************************************/
/* Descriptor set update                                                    */
/****************************************************************************/

void ogles2vk_UpdateDescriptorSets(VkDevice device,
                                 uint32_t descriptorWriteCount,
                                 const VkWriteDescriptorSet *pDescriptorWrites,
                                 uint32_t descriptorCopyCount,
                                 const void *pDescriptorCopies)
{
    (void)device;
    (void)descriptorCopyCount;
    (void)pDescriptorCopies;

    if (!pDescriptorWrites)
        return;

    for (uint32_t i = 0; i < descriptorWriteCount; i++)
    {
        const VkWriteDescriptorSet *write = &pDescriptorWrites[i];

        if (write->dstSet == VK_NULL_HANDLE)
            continue;

        OGLES2VKDescriptorSet *set = (OGLES2VKDescriptorSet *)(uintptr_t)write->dstSet;
        uint32_t binding = write->dstBinding;

        if (binding >= OGLES2VK_MAX_DESCRIPTOR_BINDINGS)
        {
            IExec->DebugPrintF("[ogles2_vk] WARNING: descriptor binding %lu out of range\n",
                               (unsigned long)binding);
            continue;
        }

        /* Ensure bindingCount covers this binding */
        if (binding >= set->bindingCount)
            set->bindingCount = binding + 1;

        set->bindings[binding].type = write->descriptorType;

        /* For uniform buffer descriptors, copy buffer info */
        if ((write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
             write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
             write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
             write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
            write->pBufferInfo)
        {
            set->bindings[binding].buffer =
                (OGLES2VKBuffer *)(uintptr_t)write->pBufferInfo[0].buffer;
            set->bindings[binding].offset = write->pBufferInfo[0].offset;
            set->bindings[binding].range  = write->pBufferInfo[0].range;

            IExec->DebugPrintF("[ogles2_vk] Descriptor binding %lu: buffer, "
                               "offset=%lu, range=%lu\n",
                               (unsigned long)binding,
                               (unsigned long)write->pBufferInfo[0].offset,
                               (unsigned long)write->pBufferInfo[0].range);
        }

        /* For combined image sampler descriptors */
        if (write->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            write->pImageInfo)
        {
            const VkDescriptorImageInfo *imgInfo = write->pImageInfo;
            set->bindings[binding].imageView =
                (OGLES2VKImageView *)(uintptr_t)imgInfo->imageView;
            set->bindings[binding].sampler =
                (OGLES2VKSampler *)(uintptr_t)imgInfo->sampler;

            IExec->DebugPrintF("[ogles2_vk] Descriptor binding %lu: combined image sampler\n",
                               (unsigned long)binding);
        }
    }
}

/****************************************************************************/
/* 100% API coverage: Device queue 2                                        */
/****************************************************************************/

void ogles2vk_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue)
{
    ogles2vk_GetDeviceQueue(device, pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueue);
}

/****************************************************************************/
/* 100% API coverage: Memory stubs                                          */
/****************************************************************************/

void ogles2vk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize *pCommittedMemoryInBytes)
{ (void)device; (void)memory; if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = 0; }

uint64_t ogles2vk_GetDeviceMemoryOpaqueCaptureAddress(VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{ (void)device; (void)pInfo; return 0; }

/****************************************************************************/
/* 100% API coverage: Image subresource layout                              */
/****************************************************************************/

/* Use the canonical ogles2vk_FormatBytesPerPixel() defined above */

void ogles2vk_GetImageSubresourceLayout(VkDevice device, VkImage image,
                                       const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout)
{
    (void)device; (void)pSubresource;
    if (image == VK_NULL_HANDLE || !pLayout) return;
    OGLES2VKImage *img = (OGLES2VKImage *)(uintptr_t)image;
    uint32_t bpp = ogles2vk_FormatBytesPerPixel(img->format);
    uint32_t rowPitch = img->width * bpp;
    pLayout->offset = 0;
    pLayout->size = img->memorySize;
    pLayout->rowPitch = rowPitch;
    pLayout->arrayPitch = rowPitch * img->height;
    pLayout->depthPitch = rowPitch * img->height;
}

/****************************************************************************/
/* 100% API coverage: Buffer view                                           */
/****************************************************************************/

VkResult ogles2vk_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkBufferView *pView)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pView) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKBufferView *view = (OGLES2VKBufferView *)IExec->AllocVecTags(sizeof(OGLES2VKBufferView),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pView = (VkBufferView)(uintptr_t)view;
    return VK_SUCCESS;
}

void ogles2vk_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (bufferView != VK_NULL_HANDLE) IExec->FreeVec((APTR)(uintptr_t)bufferView);
}

/****************************************************************************/
/* 100% API coverage: Buffer device address                                 */
/****************************************************************************/

VkDeviceAddress ogles2vk_GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{
    (void)device;
    if (!pInfo || pInfo->buffer == VK_NULL_HANDLE) return 0;
    return (VkDeviceAddress)((OGLES2VKBuffer *)(uintptr_t)pInfo->buffer)->boundOffset;
}

uint64_t ogles2vk_GetBufferOpaqueCaptureAddress(VkDevice device, const VkBufferDeviceAddressInfo *pInfo)
{ (void)device; (void)pInfo; return 0; }

/****************************************************************************/
/* 100% API coverage: Sparse memory stubs                                   */
/****************************************************************************/

void ogles2vk_GetImageSparseMemoryRequirements(VkDevice device, VkImage image,
    uint32_t *pCount, VkSparseImageMemoryRequirements *pReqs)
{ (void)device; (void)image; (void)pReqs; if (pCount) *pCount = 0; }

void ogles2vk_GetImageSparseMemoryRequirements2(VkDevice device,
    const VkImageSparseMemoryRequirementsInfo2 *pInfo, uint32_t *pCount,
    VkSparseImageMemoryRequirements2 *pReqs)
{ (void)device; (void)pInfo; (void)pReqs; if (pCount) *pCount = 0; }

/****************************************************************************/
/* 100% API coverage: Descriptor stubs                                      */
/****************************************************************************/

VkResult ogles2vk_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo *pCI,
    const VkAllocationCallbacks *pA, VkSamplerYcbcrConversion *pOut)
{ (void)device; (void)pCI; (void)pA; (void)pOut; return VK_ERROR_FEATURE_NOT_PRESENT; }

void ogles2vk_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcr, const VkAllocationCallbacks *pA)
{ (void)device; (void)ycbcr; (void)pA; }

VkResult ogles2vk_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *pCI,
    const VkAllocationCallbacks *pA, VkDescriptorUpdateTemplate *pOut)
{ (void)device; (void)pCI; (void)pA; (void)pOut; return VK_ERROR_FEATURE_NOT_PRESENT; }

void ogles2vk_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate tmpl, const VkAllocationCallbacks *pA)
{ (void)device; (void)tmpl; (void)pA; }

void ogles2vk_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set,
    VkDescriptorUpdateTemplate tmpl, const void *pData)
{ (void)device; (void)set; (void)tmpl; (void)pData; }

void ogles2vk_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCI,
    VkDescriptorSetLayoutSupport *pSupport)
{ (void)device; (void)pCI; if (pSupport) pSupport->supported = VK_TRUE; }
