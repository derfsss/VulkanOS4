/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_descriptor.c -- Descriptor set layout, pool, allocation, and update
**
** Provides the standard Vulkan resource binding mechanism so shaders
** can access uniform buffers (e.g., MVP matrices) and combined image
** samplers.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Sampler                                                                  */
/****************************************************************************/

VkResult swvk_CreateSampler(VkDevice device,
                             const VkSamplerCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkSampler *pSampler)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pSampler)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKSampler *sampler = (SWVKSampler *)IExec->AllocVecTags(
        sizeof(SWVKSampler),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sampler)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    sampler->magFilter    = pCreateInfo->magFilter;
    sampler->minFilter    = pCreateInfo->minFilter;
    sampler->addressModeU = pCreateInfo->addressModeU;
    sampler->addressModeV = pCreateInfo->addressModeV;

    *pSampler = (VkSampler)(uintptr_t)sampler;

    D(("[software_vk] Sampler created: mag=%lu, min=%lu, addrU=%lu, addrV=%lu\n",
                       (unsigned long)sampler->magFilter,
                       (unsigned long)sampler->minFilter,
                       (unsigned long)sampler->addressModeU,
                       (unsigned long)sampler->addressModeV));

    return VK_SUCCESS;
}

void swvk_DestroySampler(VkDevice device,
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
/* Descriptor set layout                                                    */
/****************************************************************************/

VkResult swvk_CreateDescriptorSetLayout(VkDevice device,
                                         const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                         const VkAllocationCallbacks *pAllocator,
                                         VkDescriptorSetLayout *pSetLayout)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pSetLayout)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKDescriptorSetLayout *layout = (SWVKDescriptorSetLayout *)IExec->AllocVecTags(
        sizeof(SWVKDescriptorSetLayout),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    layout->bindingCount = pCreateInfo->bindingCount;
    if (layout->bindingCount > SWVK_MAX_DESCRIPTOR_BINDINGS)
    {
        D(("[software_vk] WARNING: descriptor bindings clamped (%u > %u)\n",
                           (unsigned)layout->bindingCount, (unsigned)SWVK_MAX_DESCRIPTOR_BINDINGS));
        layout->bindingCount = SWVK_MAX_DESCRIPTOR_BINDINGS;
    }

    if (pCreateInfo->pBindings)
    {
        for (uint32_t i = 0; i < layout->bindingCount; i++)
            layout->bindings[i] = pCreateInfo->pBindings[i];
    }

    *pSetLayout = (VkDescriptorSetLayout)(uintptr_t)layout;

    D(("[software_vk] Descriptor set layout created: %lu bindings\n",
                       (unsigned long)layout->bindingCount));

    return VK_SUCCESS;
}

void swvk_DestroyDescriptorSetLayout(VkDevice device,
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

VkResult swvk_CreateDescriptorPool(VkDevice device,
                                    const VkDescriptorPoolCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkDescriptorPool *pDescriptorPool)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKDescriptorPool *pool = (SWVKDescriptorPool *)IExec->AllocVecTags(
        sizeof(SWVKDescriptorPool),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->maxSets = pCreateInfo->maxSets;

    *pDescriptorPool = (VkDescriptorPool)(uintptr_t)pool;

    D(("[software_vk] Descriptor pool created: maxSets=%lu\n",
                       (unsigned long)pool->maxSets));

    return VK_SUCCESS;
}

void swvk_DestroyDescriptorPool(VkDevice device,
                                 VkDescriptorPool descriptorPool,
                                 const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (descriptorPool == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)descriptorPool);
}

/****************************************************************************/
/* Descriptor pool reset                                                    */
/****************************************************************************/

VkResult swvk_ResetDescriptorPool(VkDevice device,
                                   VkDescriptorPool descriptorPool,
                                   VkDescriptorPoolResetFlags flags)
{
    (void)device; (void)descriptorPool; (void)flags;
    /* Known limitation: pool doesn't track allocated sets, so we
    ** cannot free them. Just accept the call. */
    return VK_SUCCESS;
}

/****************************************************************************/
/* Descriptor set allocation                                                */
/****************************************************************************/

VkResult swvk_AllocateDescriptorSets(VkDevice device,
                                      const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                      VkDescriptorSet *pDescriptorSets)
{
    (void)device;

    if (!pAllocateInfo || !pDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++)
    {
        SWVKDescriptorSet *set = (SWVKDescriptorSet *)IExec->AllocVecTags(
            sizeof(SWVKDescriptorSet),
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
        if (pAllocateInfo->pSetLayouts && pAllocateInfo->pSetLayouts[i] != VK_NULL_HANDLE)
        {
            SWVKDescriptorSetLayout *layout =
                (SWVKDescriptorSetLayout *)(uintptr_t)pAllocateInfo->pSetLayouts[i];
            set->bindingCount = layout->bindingCount;
            for (uint32_t b = 0; b < set->bindingCount; b++)
                set->bindings[b].type = layout->bindings[b].descriptorType;
        }

        pDescriptorSets[i] = (VkDescriptorSet)(uintptr_t)set;
    }

    D(("[software_vk] Allocated %lu descriptor sets\n",
                       (unsigned long)pAllocateInfo->descriptorSetCount));

    return VK_SUCCESS;
}

VkResult swvk_FreeDescriptorSets(VkDevice device,
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

void swvk_UpdateDescriptorSets(VkDevice device,
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

        SWVKDescriptorSet *set = (SWVKDescriptorSet *)(uintptr_t)write->dstSet;
        uint32_t binding = write->dstBinding;

        if (binding >= SWVK_MAX_DESCRIPTOR_BINDINGS)
        {
            D(("[software_vk] WARNING: descriptor binding %lu out of range\n",
                               (unsigned long)binding));
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
                (SWVKBuffer *)(uintptr_t)write->pBufferInfo[0].buffer;
            set->bindings[binding].offset = write->pBufferInfo[0].offset;
            set->bindings[binding].range  = write->pBufferInfo[0].range;

            D(("[software_vk] Descriptor set binding %lu: buffer, offset=%lu, range=%lu\n",
                               (unsigned long)binding,
                               (unsigned long)write->pBufferInfo[0].offset,
                               (unsigned long)write->pBufferInfo[0].range));
        }

        /* For combined image sampler descriptors */
        if (write->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            write->pImageInfo)
        {
            const VkDescriptorImageInfo *imgInfo = write->pImageInfo;
            set->bindings[binding].imageView =
                (SWVKImageView *)(uintptr_t)imgInfo->imageView;
            set->bindings[binding].sampler =
                (SWVKSampler *)(uintptr_t)imgInfo->sampler;

            D(("[software_vk] Descriptor set binding %lu: combined image sampler\n",
                               (unsigned long)binding));
        }
    }
}

/****************************************************************************/
/* Sampler YCbCr conversion stubs                                           */
/****************************************************************************/

VkResult swvk_CreateSamplerYcbcrConversion(VkDevice device,
                                             const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
                                             const VkAllocationCallbacks *pAllocator,
                                             VkSamplerYcbcrConversion *pYcbcrConversion)
{
    (void)device; (void)pCreateInfo; (void)pAllocator; (void)pYcbcrConversion;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

void swvk_DestroySamplerYcbcrConversion(VkDevice device,
                                          VkSamplerYcbcrConversion ycbcrConversion,
                                          const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)ycbcrConversion; (void)pAllocator;
}

/****************************************************************************/
/* Descriptor update template stubs                                         */
/****************************************************************************/

VkResult swvk_CreateDescriptorUpdateTemplate(VkDevice device,
                                                const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                                const VkAllocationCallbacks *pAllocator,
                                                VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
    (void)device; (void)pCreateInfo; (void)pAllocator; (void)pDescriptorUpdateTemplate;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

void swvk_DestroyDescriptorUpdateTemplate(VkDevice device,
                                             VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                             const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)descriptorUpdateTemplate; (void)pAllocator;
}

void swvk_UpdateDescriptorSetWithTemplate(VkDevice device,
                                            VkDescriptorSet descriptorSet,
                                            VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                            const void *pData)
{
    (void)device; (void)descriptorSet; (void)descriptorUpdateTemplate; (void)pData;
}

/****************************************************************************/
/* Descriptor set layout support                                            */
/****************************************************************************/

void swvk_GetDescriptorSetLayoutSupport(VkDevice device,
                                          const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                          VkDescriptorSetLayoutSupport *pSupport)
{
    (void)device; (void)pCreateInfo;
    if (pSupport)
        pSupport->supported = VK_TRUE;
}
