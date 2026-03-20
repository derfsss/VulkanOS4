/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_memory.c -- Device memory allocation using system RAM
**
** The software ICD has a single memory type: host-visible, host-coherent
** system RAM. All memory is allocated via AmigaOS AllocVecTags and is
** directly accessible by the CPU.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <stdint.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Memory allocation                                                        */
/****************************************************************************/

VkResult swvk_AllocateMemory(VkDevice device,
                             const VkMemoryAllocateInfo *pAllocateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkDeviceMemory *pMemory)
{
    (void)device;
    (void)pAllocator;

    if (pAllocateInfo->allocationSize == 0)
    {
        *pMemory = VK_NULL_HANDLE;
        return VK_SUCCESS;
    }

    /* Guard against 64-to-32 bit truncation for AllocVecTags */
    if (pAllocateInfo->allocationSize > 0xFFFFFFFFULL)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    SWVKMemory *mem = (SWVKMemory *)IExec->AllocVecTags(
        sizeof(SWVKMemory),
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

    mem->size   = pAllocateInfo->allocationSize;
    mem->mapped = NULL;

    *pMemory = (VkDeviceMemory)(uintptr_t)mem;

    IExec->DebugPrintF("[software_vk] Memory allocated: %lu bytes\n",
                       (unsigned long)pAllocateInfo->allocationSize);

    return VK_SUCCESS;
}

/****************************************************************************/
/* Memory deallocation                                                      */
/****************************************************************************/

void swvk_FreeMemory(VkDevice device,
                     VkDeviceMemory memory,
                     const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (memory == VK_NULL_HANDLE)
        return;

    SWVKMemory *mem = (SWVKMemory *)(uintptr_t)memory;

    if (mem->data)
        IExec->FreeVec(mem->data);

    IExec->FreeVec(mem);
}

/****************************************************************************/
/* Memory mapping                                                           */
/* Since all memory is host-visible system RAM, mapping simply returns      */
/* a pointer into the allocated data buffer at the requested offset.        */
/****************************************************************************/

VkResult swvk_MapMemory(VkDevice device,
                        VkDeviceMemory memory,
                        VkDeviceSize offset,
                        VkDeviceSize size,
                        VkMemoryMapFlags flags,
                        void **ppData)
{
    (void)device;
    (void)flags;

    if (memory == VK_NULL_HANDLE)
        return VK_ERROR_MEMORY_MAP_FAILED;

    SWVKMemory *mem = (SWVKMemory *)(uintptr_t)memory;

    if (!mem->data)
        return VK_ERROR_MEMORY_MAP_FAILED;

    if (offset >= mem->size)
        return VK_ERROR_MEMORY_MAP_FAILED;

    /* VK_WHOLE_SIZE means map from offset to end of allocation */
    if (size != VK_WHOLE_SIZE && (offset + size) > mem->size)
        return VK_ERROR_MEMORY_MAP_FAILED;

    *ppData = (uint8_t *)mem->data + offset;
    mem->mapped = *ppData;

    return VK_SUCCESS;
}

/****************************************************************************/
/* Memory unmapping                                                         */
/* No-op for host-coherent memory -- the pointer remains valid until the    */
/* memory is freed.                                                         */
/****************************************************************************/

void swvk_UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    (void)device;

    if (memory == VK_NULL_HANDLE)
        return;

    SWVKMemory *mem = (SWVKMemory *)(uintptr_t)memory;
    mem->mapped = NULL;
}

/****************************************************************************/
/* Flush/Invalidate mapped memory                                           */
/* No-op for HOST_COHERENT memory -- everything is visible immediately.     */
/****************************************************************************/

VkResult swvk_FlushMappedMemoryRanges(VkDevice device,
                                       uint32_t memoryRangeCount,
                                       const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS; /* HOST_COHERENT: no-op */
}

VkResult swvk_InvalidateMappedMemoryRanges(VkDevice device,
                                            uint32_t memoryRangeCount,
                                            const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS; /* HOST_COHERENT: no-op */
}

/****************************************************************************/
/* Device memory commitment                                                 */
/****************************************************************************/

void swvk_GetDeviceMemoryCommitment(VkDevice device,
                                      VkDeviceMemory memory,
                                      VkDeviceSize *pCommittedMemoryInBytes)
{
    (void)device; (void)memory;
    /* All memory is committed in software renderer */
    if (pCommittedMemoryInBytes)
        *pCommittedMemoryInBytes = 0;
}

/****************************************************************************/
/* Opaque capture address                                                   */
/****************************************************************************/

uint64_t swvk_GetDeviceMemoryOpaqueCaptureAddress(VkDevice device,
                                                     const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
    (void)device; (void)pInfo;
    return 0;
}
