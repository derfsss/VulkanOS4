/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_sync.c -- Synchronisation primitives (fences, semaphores)
**
** The software ICD executes everything synchronously on the CPU.
** Fences are signalled immediately during vkQueueSubmit.
** Semaphores are no-ops (single queue, no GPU concurrency).
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>

#include <stdint.h>
#include <string.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Fence implementation                                                     */
/****************************************************************************/

VkResult swvk_CreateFence(VkDevice device,
                           const VkFenceCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkFence *pFence)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pFence)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKFence *fence = (SWVKFence *)IExec->AllocVecTags(
        sizeof(SWVKFence),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!fence)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* VK_FENCE_CREATE_SIGNALED_BIT means the fence starts signalled */
    fence->signalled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
                       ? VK_TRUE : VK_FALSE;

    *pFence = (VkFence)(uintptr_t)fence;

    return VK_SUCCESS;
}

void swvk_DestroyFence(VkDevice device, VkFence fence,
                        const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (fence != VK_NULL_HANDLE)
        IExec->FreeVec((APTR)(uintptr_t)fence);
}

VkResult swvk_WaitForFences(VkDevice device,
                             uint32_t fenceCount,
                             const VkFence *pFences,
                             VkBool32 waitAll,
                             uint64_t timeout)
{
    (void)device;
    (void)timeout;

    /* Software renderer is synchronous -- fences are signalled immediately
    ** during vkQueueSubmit. If any fence is not signalled, it means it
    ** was never submitted (application error), but we still return success
    ** since timeout=0 is valid. */

    if (waitAll)
    {
        for (uint32_t i = 0; i < fenceCount; i++)
        {
            if (pFences[i] == VK_NULL_HANDLE)
                continue;
            SWVKFence *f = (SWVKFence *)(uintptr_t)pFences[i];
            if (!f->signalled)
                return VK_TIMEOUT;
        }
        return VK_SUCCESS;
    }
    else
    {
        /* Wait for any one fence */
        for (uint32_t i = 0; i < fenceCount; i++)
        {
            if (pFences[i] == VK_NULL_HANDLE)
                continue;
            SWVKFence *f = (SWVKFence *)(uintptr_t)pFences[i];
            if (f->signalled)
                return VK_SUCCESS;
        }
        return VK_TIMEOUT;
    }
}

VkResult swvk_ResetFences(VkDevice device,
                           uint32_t fenceCount,
                           const VkFence *pFences)
{
    (void)device;

    for (uint32_t i = 0; i < fenceCount; i++)
    {
        if (pFences[i] == VK_NULL_HANDLE)
            continue;
        SWVKFence *f = (SWVKFence *)(uintptr_t)pFences[i];
        f->signalled = VK_FALSE;
    }

    return VK_SUCCESS;
}

/****************************************************************************/
/* Fence status query                                                       */
/****************************************************************************/

VkResult swvk_GetFenceStatus(VkDevice device, VkFence fence)
{
    (void)device;

    if (fence == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKFence *f = (SWVKFence *)(uintptr_t)fence;
    return f->signalled ? VK_SUCCESS : VK_NOT_READY;
}

/****************************************************************************/
/* Semaphore implementation (no-op for software renderer)                   */
/****************************************************************************/

VkResult swvk_CreateSemaphore(VkDevice device,
                               const VkSemaphoreCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSemaphore *pSemaphore)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    SWVKSemaphore *sem = (SWVKSemaphore *)IExec->AllocVecTags(
        sizeof(SWVKSemaphore),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sem)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pSemaphore = (VkSemaphore)(uintptr_t)sem;

    return VK_SUCCESS;
}

void swvk_DestroySemaphore(VkDevice device, VkSemaphore semaphore,
                            const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (semaphore != VK_NULL_HANDLE)
        IExec->FreeVec((APTR)(uintptr_t)semaphore);
}

/****************************************************************************/
/* Event implementation                                                     */
/****************************************************************************/

VkResult swvk_CreateEvent(VkDevice device,
                           const VkEventCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkEvent *pEvent)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (!pEvent)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKEvent *event = (SWVKEvent *)IExec->AllocVecTags(
        sizeof(SWVKEvent),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!event)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    event->signalled = VK_FALSE;
    *pEvent = (VkEvent)(uintptr_t)event;

    return VK_SUCCESS;
}

void swvk_DestroyEvent(VkDevice device, VkEvent event,
                        const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (event != VK_NULL_HANDLE)
        IExec->FreeVec((APTR)(uintptr_t)event);
}

VkResult swvk_GetEventStatus(VkDevice device, VkEvent event)
{
    (void)device;

    if (event == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKEvent *e = (SWVKEvent *)(uintptr_t)event;
    return e->signalled ? VK_EVENT_SET : VK_EVENT_RESET;
}

VkResult swvk_SetEvent(VkDevice device, VkEvent event)
{
    (void)device;

    if (event == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKEvent *e = (SWVKEvent *)(uintptr_t)event;
    e->signalled = VK_TRUE;
    return VK_SUCCESS;
}

VkResult swvk_ResetEvent(VkDevice device, VkEvent event)
{
    (void)device;

    if (event == VK_NULL_HANDLE)
        return VK_ERROR_UNKNOWN;

    SWVKEvent *e = (SWVKEvent *)(uintptr_t)event;
    e->signalled = VK_FALSE;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Private data slots                                                       */
/****************************************************************************/

VkResult swvk_CreatePrivateDataSlot(VkDevice device,
                                      const VkPrivateDataSlotCreateInfo *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator,
                                      VkPrivateDataSlot *pPrivateDataSlot)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (!pPrivateDataSlot)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKPrivateDataSlot *slot = (SWVKPrivateDataSlot *)IExec->AllocVecTags(
        sizeof(SWVKPrivateDataSlot),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!slot)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pPrivateDataSlot = (VkPrivateDataSlot)(uintptr_t)slot;
    return VK_SUCCESS;
}

void swvk_DestroyPrivateDataSlot(VkDevice device,
                                   VkPrivateDataSlot privateDataSlot,
                                   const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (privateDataSlot != VK_NULL_HANDLE)
        IExec->FreeVec((APTR)(uintptr_t)privateDataSlot);
}

VkResult swvk_SetPrivateData(VkDevice device,
                               VkObjectType objectType,
                               uint64_t objectHandle,
                               VkPrivateDataSlot privateDataSlot,
                               uint64_t data)
{
    (void)device; (void)objectType; (void)objectHandle;
    (void)privateDataSlot; (void)data;
    return VK_SUCCESS;
}

void swvk_GetPrivateData(VkDevice device,
                           VkObjectType objectType,
                           uint64_t objectHandle,
                           VkPrivateDataSlot privateDataSlot,
                           uint64_t *pData)
{
    (void)device; (void)objectType; (void)objectHandle;
    (void)privateDataSlot;
    if (pData) *pData = 0;
}

/****************************************************************************/
/* Timeline semaphore stubs                                                 */
/****************************************************************************/

VkResult swvk_GetSemaphoreCounterValue(VkDevice device,
                                         VkSemaphore semaphore,
                                         uint64_t *pValue)
{
    (void)device; (void)semaphore;
    if (pValue) *pValue = 0;
    return VK_SUCCESS;
}

VkResult swvk_WaitSemaphores(VkDevice device,
                               const VkSemaphoreWaitInfo *pWaitInfo,
                               uint64_t timeout)
{
    (void)device; (void)pWaitInfo; (void)timeout;
    return VK_SUCCESS;
}

VkResult swvk_SignalSemaphore(VkDevice device,
                                const VkSemaphoreSignalInfo *pSignalInfo)
{
    (void)device; (void)pSignalInfo;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Query pool implementation                                                */
/****************************************************************************/

VkResult swvk_CreateQueryPool(VkDevice device,
                                const VkQueryPoolCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkQueryPool *pQueryPool)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pQueryPool)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKQueryPool *pool = (SWVKQueryPool *)IExec->AllocVecTags(
        sizeof(SWVKQueryPool),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->type  = pCreateInfo->queryType;
    pool->count = pCreateInfo->queryCount;

    if (pool->count > 0)
    {
        pool->results = (uint64_t *)IExec->AllocVecTags(
            pool->count * sizeof(uint64_t),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!pool->results)
        {
            IExec->FreeVec(pool);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    *pQueryPool = (VkQueryPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void swvk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                             const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (queryPool == VK_NULL_HANDLE)
        return;

    SWVKQueryPool *pool = (SWVKQueryPool *)(uintptr_t)queryPool;
    if (pool->results)
        IExec->FreeVec(pool->results);
    IExec->FreeVec(pool);
}

VkResult swvk_GetQueryPoolResults(VkDevice device,
                                    VkQueryPool queryPool,
                                    uint32_t firstQuery,
                                    uint32_t queryCount,
                                    size_t dataSize,
                                    void *pData,
                                    VkDeviceSize stride,
                                    VkQueryResultFlags flags)
{
    (void)device;
    (void)flags;

    if (queryPool == VK_NULL_HANDLE || !pData)
        return VK_ERROR_UNKNOWN;

    SWVKQueryPool *pool = (SWVKQueryPool *)(uintptr_t)queryPool;
    uint8_t *dst = (uint8_t *)pData;

    for (uint32_t i = 0; i < queryCount; i++)
    {
        uint32_t idx = firstQuery + i;
        if (idx < pool->count && (size_t)((dst - (uint8_t *)pData) + sizeof(uint64_t)) <= dataSize)
        {
            memcpy(dst, &pool->results[idx], sizeof(uint64_t));
        }
        dst += stride;
    }

    return VK_SUCCESS;
}

void swvk_ResetQueryPool(VkDevice device,
                           VkQueryPool queryPool,
                           uint32_t firstQuery,
                           uint32_t queryCount)
{
    (void)device;

    if (queryPool == VK_NULL_HANDLE)
        return;

    SWVKQueryPool *pool = (SWVKQueryPool *)(uintptr_t)queryPool;

    for (uint32_t i = 0; i < queryCount; i++)
    {
        uint32_t idx = firstQuery + i;
        if (idx < pool->count)
            pool->results[idx] = 0;
    }
}

/****************************************************************************/
/* Queue bind sparse stub                                                   */
/****************************************************************************/

VkResult swvk_QueueBindSparse(VkQueue queue,
                                uint32_t bindInfoCount,
                                const VkBindSparseInfo *pBindInfo,
                                VkFence fence)
{
    (void)queue; (void)bindInfoCount; (void)pBindInfo; (void)fence;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}
