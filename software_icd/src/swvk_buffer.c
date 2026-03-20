/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_buffer.c -- Buffer, image, and image view management
**
** Buffers and images are metadata wrappers. The actual data lives in
** VkDeviceMemory (system RAM) and is associated via vkBind*Memory.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Helper: compute bytes per pixel for a VkFormat                           */
/****************************************************************************/

static uint32_t swvk_FormatBytesPerPixel(VkFormat format)
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
            return 4;  /* Safe fallback */
    }
}

/****************************************************************************/
/* Buffer creation                                                          */
/****************************************************************************/

VkResult swvk_CreateBuffer(VkDevice device,
                           const VkBufferCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkBuffer *pBuffer)
{
    (void)device;
    (void)pAllocator;

    SWVKBuffer *buf = (SWVKBuffer *)IExec->AllocVecTags(
        sizeof(SWVKBuffer),
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

void swvk_DestroyBuffer(VkDevice device,
                        VkBuffer buffer,
                        const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (buffer == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)buffer);
}

/****************************************************************************/
/* Buffer memory requirements                                               */
/****************************************************************************/

void swvk_GetBufferMemoryRequirements(VkDevice device,
                                      VkBuffer buffer,
                                      VkMemoryRequirements *pMemoryRequirements)
{
    (void)device;

    SWVKBuffer *buf = (SWVKBuffer *)(uintptr_t)buffer;

    pMemoryRequirements->size      = buf->size;
    pMemoryRequirements->alignment = 256;   /* Conservative alignment */
    /* Bit 0 = memory type index 0 (our only memory type) */
    pMemoryRequirements->memoryTypeBits = 1;
}

/****************************************************************************/
/* Buffer memory binding                                                    */
/****************************************************************************/

VkResult swvk_BindBufferMemory(VkDevice device,
                               VkBuffer buffer,
                               VkDeviceMemory memory,
                               VkDeviceSize memoryOffset)
{
    (void)device;

    /* Defensive check -- Vulkan spec says these must be valid handles */
    if (buffer == VK_NULL_HANDLE || memory == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKBuffer *buf = (SWVKBuffer *)(uintptr_t)buffer;
    SWVKMemory *mem = (SWVKMemory *)(uintptr_t)memory;

    buf->boundMemory = mem;
    buf->boundOffset = memoryOffset;

    return VK_SUCCESS;
}

/****************************************************************************/
/* Image creation                                                           */
/****************************************************************************/

VkResult swvk_CreateImage(VkDevice device,
                          const VkImageCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkImage *pImage)
{
    (void)device;
    (void)pAllocator;

    SWVKImage *img = (SWVKImage *)IExec->AllocVecTags(
        sizeof(SWVKImage),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!img)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    img->width         = pCreateInfo->extent.width;
    img->height        = pCreateInfo->extent.height;
    img->depth         = pCreateInfo->extent.depth;
    img->mipLevels     = pCreateInfo->mipLevels;
    img->arrayLayers   = pCreateInfo->arrayLayers;
    img->format        = pCreateInfo->format;
    img->imageType     = pCreateInfo->imageType;
    img->currentLayout = pCreateInfo->initialLayout;
    img->usage         = pCreateInfo->usage;
    img->boundMemory   = NULL;
    img->boundOffset   = 0;

    /* Compute memory size: width * height * depth * bpp * layers * mips
    ** Currently, only a single mip level is supported. */
    uint32_t bpp = swvk_FormatBytesPerPixel(pCreateInfo->format);
    img->memorySize = (VkDeviceSize)pCreateInfo->extent.width *
                      pCreateInfo->extent.height *
                      pCreateInfo->extent.depth *
                      bpp *
                      pCreateInfo->arrayLayers;

    *pImage = (VkImage)(uintptr_t)img;

    return VK_SUCCESS;
}

void swvk_DestroyImage(VkDevice device,
                       VkImage image,
                       const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (image == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)image);
}

/****************************************************************************/
/* Image memory requirements                                                */
/****************************************************************************/

void swvk_GetImageMemoryRequirements(VkDevice device,
                                     VkImage image,
                                     VkMemoryRequirements *pMemoryRequirements)
{
    (void)device;

    SWVKImage *img = (SWVKImage *)(uintptr_t)image;

    pMemoryRequirements->size      = img->memorySize;
    pMemoryRequirements->alignment = 256;   /* Conservative alignment */
    /* Bit 0 = memory type index 0 (our only memory type) */
    pMemoryRequirements->memoryTypeBits = 1;
}

/****************************************************************************/
/* Image memory binding                                                     */
/****************************************************************************/

VkResult swvk_BindImageMemory(VkDevice device,
                              VkImage image,
                              VkDeviceMemory memory,
                              VkDeviceSize memoryOffset)
{
    (void)device;

    /* Defensive check -- Vulkan spec says these must be valid handles */
    if (image == VK_NULL_HANDLE || memory == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKImage  *img = (SWVKImage *)(uintptr_t)image;
    SWVKMemory *mem = (SWVKMemory *)(uintptr_t)memory;

    img->boundMemory = mem;
    img->boundOffset = memoryOffset;

    return VK_SUCCESS;
}

/****************************************************************************/
/* Image view creation                                                      */
/****************************************************************************/

VkResult swvk_CreateImageView(VkDevice device,
                              const VkImageViewCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkImageView *pView)
{
    (void)device;
    (void)pAllocator;

    SWVKImageView *view = (SWVKImageView *)IExec->AllocVecTags(
        sizeof(SWVKImageView),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!view)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    view->image     = (SWVKImage *)(uintptr_t)pCreateInfo->image;
    view->format    = pCreateInfo->format;
    view->viewType  = pCreateInfo->viewType;
    view->components = pCreateInfo->components;
    view->subresourceRange = pCreateInfo->subresourceRange;

    *pView = (VkImageView)(uintptr_t)view;

    return VK_SUCCESS;
}

void swvk_DestroyImageView(VkDevice device,
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
/* Vulkan 1.1 "2" wrappers                                                 */
/****************************************************************************/

void swvk_GetBufferMemoryRequirements2(VkDevice device,
                                        const VkBufferMemoryRequirementsInfo2 *pInfo,
                                        VkMemoryRequirements2 *pMemoryRequirements)
{
    swvk_GetBufferMemoryRequirements(device, pInfo->buffer,
        &pMemoryRequirements->memoryRequirements);
}

void swvk_GetImageMemoryRequirements2(VkDevice device,
                                       const VkImageMemoryRequirementsInfo2 *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements)
{
    swvk_GetImageMemoryRequirements(device, pInfo->image,
        &pMemoryRequirements->memoryRequirements);
}

VkResult swvk_BindBufferMemory2(VkDevice device,
                                 uint32_t bindInfoCount,
                                 const VkBindBufferMemoryInfo *pBindInfos)
{
    for (uint32_t i = 0; i < bindInfoCount; i++)
    {
        VkResult result = swvk_BindBufferMemory(device,
            pBindInfos[i].buffer,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS)
            return result;
    }
    return VK_SUCCESS;
}

VkResult swvk_BindImageMemory2(VkDevice device,
                                uint32_t bindInfoCount,
                                const VkBindImageMemoryInfo *pBindInfos)
{
    for (uint32_t i = 0; i < bindInfoCount; i++)
    {
        VkResult result = swvk_BindImageMemory(device,
            pBindInfos[i].image,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS)
            return result;
    }
    return VK_SUCCESS;
}

/****************************************************************************/
/* Image subresource layout                                                 */
/****************************************************************************/

void swvk_GetImageSubresourceLayout(VkDevice device,
                                      VkImage image,
                                      const VkImageSubresource *pSubresource,
                                      VkSubresourceLayout *pLayout)
{
    (void)device;
    (void)pSubresource;

    if (image == VK_NULL_HANDLE || !pLayout)
        return;

    SWVKImage *img = (SWVKImage *)(uintptr_t)image;

    uint32_t bpp = swvk_FormatBytesPerPixel(img->format);
    uint32_t rowPitch = img->width * bpp;

    pLayout->offset     = 0;
    pLayout->size       = img->memorySize;
    pLayout->rowPitch   = rowPitch;
    pLayout->arrayPitch = rowPitch * img->height;
    pLayout->depthPitch = rowPitch * img->height;
}

/****************************************************************************/
/* Buffer view (placeholder)                                                */
/****************************************************************************/

VkResult swvk_CreateBufferView(VkDevice device,
                                 const VkBufferViewCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkBufferView *pView)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (!pView)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKBufferView *view = (SWVKBufferView *)IExec->AllocVecTags(
        sizeof(SWVKBufferView),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!view)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pView = (VkBufferView)(uintptr_t)view;
    return VK_SUCCESS;
}

void swvk_DestroyBufferView(VkDevice device, VkBufferView bufferView,
                              const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (bufferView != VK_NULL_HANDLE)
        IExec->FreeVec((APTR)(uintptr_t)bufferView);
}

/****************************************************************************/
/* Buffer device address                                                    */
/****************************************************************************/

VkDeviceAddress swvk_GetBufferDeviceAddress(VkDevice device,
                                              const VkBufferDeviceAddressInfo *pInfo)
{
    (void)device;

    if (!pInfo || pInfo->buffer == VK_NULL_HANDLE)
        return 0;

    SWVKBuffer *buf = (SWVKBuffer *)(uintptr_t)pInfo->buffer;
    return (VkDeviceAddress)buf->boundOffset;
}

uint64_t swvk_GetBufferOpaqueCaptureAddress(VkDevice device,
                                              const VkBufferDeviceAddressInfo *pInfo)
{
    (void)device; (void)pInfo;
    return 0;
}

/****************************************************************************/
/* Sparse memory requirements stubs                                         */
/****************************************************************************/

void swvk_GetImageSparseMemoryRequirements(VkDevice device,
                                             VkImage image,
                                             uint32_t *pSparseMemoryRequirementCount,
                                             VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
    (void)device; (void)image; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount)
        *pSparseMemoryRequirementCount = 0;
}

void swvk_GetImageSparseMemoryRequirements2(VkDevice device,
                                               const VkImageSparseMemoryRequirementsInfo2 *pInfo,
                                               uint32_t *pSparseMemoryRequirementCount,
                                               VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount)
        *pSparseMemoryRequirementCount = 0;
}
