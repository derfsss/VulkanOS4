/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_sync.c -- Fence, semaphore, and queue submission
**
** Fences are simple booleans (execution is synchronous). Semaphores
** are no-ops (single queue). Queue submit executes command buffers
** synchronously and signals the fence.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <stdint.h>
#include <string.h>

#include "ogles2vk_internal.h"

/****************************************************************************/
/* Fence                                                                    */
/****************************************************************************/

VkResult ogles2vk_CreateFence(VkDevice device,
                            const VkFenceCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkFence *pFence)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pFence)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKFence *fence = (OGLES2VKFence *)IExec->AllocVecTags(
        sizeof(OGLES2VKFence),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!fence)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    fence->signalled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
                        ? VK_TRUE : VK_FALSE;

    *pFence = (VkFence)(uintptr_t)fence;
    return VK_SUCCESS;
}

void ogles2vk_DestroyFence(VkDevice device, VkFence fence,
                         const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;
    if (fence == VK_NULL_HANDLE) return;
    IExec->FreeVec((APTR)(uintptr_t)fence);
}

VkResult ogles2vk_WaitForFences(VkDevice device, uint32_t fenceCount,
                              const VkFence *pFences, VkBool32 waitAll,
                              uint64_t timeout)
{
    (void)device;
    (void)waitAll;
    (void)timeout;

    if (!pFences) return VK_ERROR_INITIALIZATION_FAILED;

    /* Synchronous execution -- all fences are immediately ready */
    for (uint32_t i = 0; i < fenceCount; i++)
    {
        if (pFences[i] != VK_NULL_HANDLE)
        {
            OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)pFences[i];
            f->signalled = VK_TRUE;
        }
    }

    return VK_SUCCESS;
}

VkResult ogles2vk_ResetFences(VkDevice device, uint32_t fenceCount,
                            const VkFence *pFences)
{
    (void)device;
    if (!pFences) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < fenceCount; i++)
    {
        if (pFences[i] != VK_NULL_HANDLE)
        {
            OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)pFences[i];
            f->signalled = VK_FALSE;
        }
    }

    return VK_SUCCESS;
}

VkResult ogles2vk_GetFenceStatus(VkDevice device, VkFence fence)
{
    (void)device;
    if (fence == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;

    OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)fence;
    return f->signalled ? VK_SUCCESS : VK_NOT_READY;
}

/****************************************************************************/
/* Semaphore (no-op)                                                        */
/****************************************************************************/

VkResult ogles2vk_CreateSemaphore(VkDevice device,
                                const VkSemaphoreCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkSemaphore *pSemaphore)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (!pSemaphore)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKSemaphore *sem = (OGLES2VKSemaphore *)IExec->AllocVecTags(
        sizeof(OGLES2VKSemaphore),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!sem)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pSemaphore = (VkSemaphore)(uintptr_t)sem;
    return VK_SUCCESS;
}

void ogles2vk_DestroySemaphore(VkDevice device, VkSemaphore semaphore,
                             const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;
    if (semaphore == VK_NULL_HANDLE) return;
    IExec->FreeVec((APTR)(uintptr_t)semaphore);
}

/****************************************************************************/
/* Queue submission                                                         */
/****************************************************************************/

VkResult ogles2vk_QueueSubmit(VkQueue queue, uint32_t submitCount,
                            const VkSubmitInfo *pSubmits, VkFence fence)
{
    /* Find the device from the queue handle */
    OGLES2VKQueue *q = (OGLES2VKQueue *)queue;
    OGLES2VKDevice *dev = NULL;

    /* The queue is embedded in the device struct -- recover the device
    ** pointer by subtracting the offset of the queue field. */
    if (q)
    {
        dev = (OGLES2VKDevice *)((uint8_t *)q -
              (uint8_t *)&((OGLES2VKDevice *)0)->queue);
    }

    /* Execute command buffers */
    if (pSubmits && dev)
    {
        for (uint32_t s = 0; s < submitCount; s++)
        {
            const VkSubmitInfo *si = &pSubmits[s];
            for (uint32_t c = 0; c < si->commandBufferCount; c++)
            {
                OGLES2VKCommandBuffer *cmd =
                    (OGLES2VKCommandBuffer *)si->pCommandBuffers[c];
                if (cmd)
                    ogles2vk_ExecuteCommandBuffer(dev, cmd);
            }
        }
    }

    /* Signal fence */
    if (fence != VK_NULL_HANDLE)
    {
        OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }

    return VK_SUCCESS;
}

VkResult ogles2vk_QueueSubmit2(VkQueue queue, uint32_t submitCount,
                             const VkSubmitInfo2 *pSubmits, VkFence fence)
{
    OGLES2VKQueue *q = (OGLES2VKQueue *)queue;
    OGLES2VKDevice *dev = NULL;

    if (q)
    {
        dev = (OGLES2VKDevice *)((uint8_t *)q -
              (uint8_t *)&((OGLES2VKDevice *)0)->queue);
    }

    /* Execute command buffers (Vulkan 1.3 submit info uses different struct) */
    if (pSubmits && dev)
    {
        for (uint32_t s = 0; s < submitCount; s++)
        {
            const VkSubmitInfo2 *si = &pSubmits[s];
            for (uint32_t c = 0; c < si->commandBufferInfoCount; c++)
            {
                OGLES2VKCommandBuffer *cmd =
                    (OGLES2VKCommandBuffer *)si->pCommandBufferInfos[c].commandBuffer;
                if (cmd)
                    ogles2vk_ExecuteCommandBuffer(dev, cmd);
            }
        }
    }

    if (fence != VK_NULL_HANDLE)
    {
        OGLES2VKFence *f = (OGLES2VKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }

    return VK_SUCCESS;
}

/****************************************************************************/
/* Event implementation (100% API coverage)                                 */
/****************************************************************************/

VkResult ogles2vk_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pEvent) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKEvent *event = (OGLES2VKEvent *)IExec->AllocVecTags(sizeof(OGLES2VKEvent),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!event) return VK_ERROR_OUT_OF_HOST_MEMORY;
    event->signalled = VK_FALSE;
    *pEvent = (VkEvent)(uintptr_t)event;
    return VK_SUCCESS;
}

void ogles2vk_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (event != VK_NULL_HANDLE) IExec->FreeVec((APTR)(uintptr_t)event);
}

VkResult ogles2vk_GetEventStatus(VkDevice device, VkEvent event)
{
    (void)device;
    if (event == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    OGLES2VKEvent *e = (OGLES2VKEvent *)(uintptr_t)event;
    return e->signalled ? VK_EVENT_SET : VK_EVENT_RESET;
}

VkResult ogles2vk_SetEvent(VkDevice device, VkEvent event)
{
    (void)device;
    if (event == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    ((OGLES2VKEvent *)(uintptr_t)event)->signalled = VK_TRUE;
    return VK_SUCCESS;
}

VkResult ogles2vk_ResetEvent(VkDevice device, VkEvent event)
{
    (void)device;
    if (event == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    ((OGLES2VKEvent *)(uintptr_t)event)->signalled = VK_FALSE;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Private data slots (100% API coverage)                                   */
/****************************************************************************/

VkResult ogles2vk_CreatePrivateDataSlot(VkDevice device, const VkPrivateDataSlotCreateInfo *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkPrivateDataSlot *pPrivateDataSlot)
{
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pPrivateDataSlot) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKPrivateDataSlot *slot = (OGLES2VKPrivateDataSlot *)IExec->AllocVecTags(sizeof(OGLES2VKPrivateDataSlot),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!slot) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pPrivateDataSlot = (VkPrivateDataSlot)(uintptr_t)slot;
    return VK_SUCCESS;
}

void ogles2vk_DestroyPrivateDataSlot(VkDevice device, VkPrivateDataSlot privateDataSlot,
                                   const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (privateDataSlot != VK_NULL_HANDLE) IExec->FreeVec((APTR)(uintptr_t)privateDataSlot);
}

VkResult ogles2vk_SetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                               VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot; (void)data;
    return VK_SUCCESS;
}

void ogles2vk_GetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                           VkPrivateDataSlot privateDataSlot, uint64_t *pData)
{
    (void)device; (void)objectType; (void)objectHandle; (void)privateDataSlot;
    if (pData) *pData = 0;
}

/****************************************************************************/
/* Timeline semaphore stubs (100% API coverage)                             */
/****************************************************************************/

VkResult ogles2vk_GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *pValue)
{ (void)device; (void)semaphore; if (pValue) *pValue = 0; return VK_SUCCESS; }

VkResult ogles2vk_WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout)
{ (void)device; (void)pWaitInfo; (void)timeout; return VK_SUCCESS; }

VkResult ogles2vk_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{ (void)device; (void)pSignalInfo; return VK_SUCCESS; }

/****************************************************************************/
/* Query pool implementation (100% API coverage)                            */
/****************************************************************************/

VkResult ogles2vk_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool)
{
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pQueryPool) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKQueryPool *pool = (OGLES2VKQueryPool *)IExec->AllocVecTags(sizeof(OGLES2VKQueryPool),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = pCreateInfo->queryType;
    pool->count = pCreateInfo->queryCount;
    if (pool->count > 0) {
        pool->results = (uint64_t *)IExec->AllocVecTags(pool->count * sizeof(uint64_t),
            AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
        if (!pool->results) { IExec->FreeVec(pool); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    }
    *pQueryPool = (VkQueryPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void ogles2vk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (queryPool == VK_NULL_HANDLE) return;
    OGLES2VKQueryPool *pool = (OGLES2VKQueryPool *)(uintptr_t)queryPool;
    if (pool->results) IExec->FreeVec(pool->results);
    IExec->FreeVec(pool);
}

VkResult ogles2vk_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                                    uint32_t queryCount, size_t dataSize, void *pData,
                                    VkDeviceSize stride, VkQueryResultFlags flags)
{
    (void)device; (void)flags;
    if (queryPool == VK_NULL_HANDLE || !pData) return VK_ERROR_UNKNOWN;
    OGLES2VKQueryPool *pool = (OGLES2VKQueryPool *)(uintptr_t)queryPool;
    uint8_t *dst = (uint8_t *)pData;
    for (uint32_t i = 0; i < queryCount; i++) {
        uint32_t idx = firstQuery + i;
        if (idx < pool->count && (size_t)((dst - (uint8_t *)pData) + sizeof(uint64_t)) <= dataSize)
            memcpy(dst, &pool->results[idx], sizeof(uint64_t));
        dst += stride;
    }
    return VK_SUCCESS;
}

void ogles2vk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    (void)device;
    if (queryPool == VK_NULL_HANDLE) return;
    OGLES2VKQueryPool *pool = (OGLES2VKQueryPool *)(uintptr_t)queryPool;
    for (uint32_t i = 0; i < queryCount; i++) {
        uint32_t idx = firstQuery + i;
        if (idx < pool->count) pool->results[idx] = 0;
    }
}

VkResult ogles2vk_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo *pBindInfo, VkFence fence)
{ (void)queue; (void)bindInfoCount; (void)pBindInfo; (void)fence; return VK_ERROR_FEATURE_NOT_PRESENT; }
