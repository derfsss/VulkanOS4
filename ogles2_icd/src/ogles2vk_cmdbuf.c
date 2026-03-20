/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_cmdbuf.c -- Command pool, command buffer, and command recording
**
** Commands are recorded into a fixed array and executed by translating
** to OGLES2 API calls during queue submission.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "ogles2vk_internal.h"

/****************************************************************************/
/* Helper: append a command to the command buffer                           */
/****************************************************************************/

static OGLES2VKCommand *ogles2vk_CmdAppend(OGLES2VKCommandBuffer *cmd, OGLES2VKCommandType type)
{
    if (cmd->commandCount >= OGLES2VK_MAX_COMMANDS)
    {
        IExec->DebugPrintF("[ogles2_vk] WARNING: command buffer full (%u/%u)\n",
                           (unsigned)cmd->commandCount, (unsigned)OGLES2VK_MAX_COMMANDS);
        return NULL;
    }

    OGLES2VKCommand *c = &cmd->commands[cmd->commandCount++];
    memset(c, 0, sizeof(OGLES2VKCommand));
    c->type = type;
    return c;
}

/****************************************************************************/
/* Command pool                                                             */
/****************************************************************************/

VkResult ogles2vk_CreateCommandPool(VkDevice device,
                                  const VkCommandPoolCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkCommandPool *pCommandPool)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pCommandPool)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKCommandPool *pool = (OGLES2VKCommandPool *)IExec->AllocVecTags(
        sizeof(OGLES2VKCommandPool),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->queueFamilyIndex = pCreateInfo->queueFamilyIndex;

    *pCommandPool = (VkCommandPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void ogles2vk_DestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                               const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;
    if (commandPool == VK_NULL_HANDLE) return;
    IExec->FreeVec((APTR)(uintptr_t)commandPool);
}

void ogles2vk_ResetCommandPool(VkDevice device, VkCommandPool commandPool,
                              VkCommandPoolResetFlags flags)
{
    (void)device;
    (void)commandPool;
    (void)flags;
}

void ogles2vk_TrimCommandPool(VkDevice device, VkCommandPool commandPool,
                             uint32_t flags)
{
    (void)device;
    (void)commandPool;
    (void)flags;
}

/****************************************************************************/
/* Command buffer allocation                                                */
/****************************************************************************/

VkResult ogles2vk_AllocateCommandBuffers(VkDevice device,
                                       const VkCommandBufferAllocateInfo *pAllocateInfo,
                                       VkCommandBuffer *pCommandBuffers)
{
    (void)device;

    if (!pAllocateInfo || !pCommandBuffers)
        return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++)
    {
        OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)IExec->AllocVecTags(
            sizeof(OGLES2VKCommandBuffer),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!cmd)
        {
            for (uint32_t j = 0; j < i; j++)
            {
                if (pCommandBuffers[j])
                    IExec->FreeVec((APTR)pCommandBuffers[j]);
                pCommandBuffers[j] = NULL;
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        pCommandBuffers[i] = (VkCommandBuffer)cmd;
    }

    return VK_SUCCESS;
}

void ogles2vk_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                               uint32_t commandBufferCount,
                               const VkCommandBuffer *pCommandBuffers)
{
    (void)device;
    (void)commandPool;

    if (!pCommandBuffers) return;

    for (uint32_t i = 0; i < commandBufferCount; i++)
    {
        if (pCommandBuffers[i])
            IExec->FreeVec((APTR)pCommandBuffers[i]);
    }
}

/****************************************************************************/
/* Command buffer lifecycle                                                 */
/****************************************************************************/

VkResult ogles2vk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
    (void)pBeginInfo;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)commandBuffer;
    cmd->commandCount = 0;
    cmd->recording    = 1;
    return VK_SUCCESS;
}

VkResult ogles2vk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)commandBuffer;
    cmd->recording = 0;

    IExec->DebugPrintF("[ogles2_vk] Command buffer recorded: %lu commands\n",
                       (unsigned long)cmd->commandCount);
    return VK_SUCCESS;
}

VkResult ogles2vk_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                                   VkCommandBufferResetFlags flags)
{
    (void)flags;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)commandBuffer;
    cmd->commandCount = 0;
    cmd->recording    = 0;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Command recording functions                                              */
/****************************************************************************/

void ogles2vk_CmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bindPoint,
                            VkPipeline pipeline)
{
    (void)bindPoint;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BIND_PIPELINE);
    if (c) c->bindPipeline.pipeline = (OGLES2VKPipeline *)(uintptr_t)pipeline;
}

void ogles2vk_CmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count,
                           const VkViewport *pViewports)
{
    (void)first;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pViewports || count == 0) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_VIEWPORT);
    if (c) c->setViewport.viewport = pViewports[0];
}

void ogles2vk_CmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count,
                          const VkRect2D *pScissors)
{
    (void)first;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pScissors || count == 0) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_SCISSOR);
    if (c) c->setScissor.scissor = pScissors[0];
}

void ogles2vk_CmdDraw(VkCommandBuffer cb, uint32_t vertexCount,
                    uint32_t instanceCount, uint32_t firstVertex,
                    uint32_t firstInstance)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_DRAW);
    if (c)
    {
        c->draw.vertexCount   = vertexCount;
        c->draw.instanceCount = instanceCount;
        c->draw.firstVertex   = firstVertex;
        c->draw.firstInstance = firstInstance;
    }
}

void ogles2vk_CmdDrawIndexed(VkCommandBuffer cb, uint32_t indexCount,
                           uint32_t instanceCount, uint32_t firstIndex,
                           int32_t vertexOffset, uint32_t firstInstance)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_DRAW_INDEXED);
    if (c)
    {
        c->drawIndexed.indexCount    = indexCount;
        c->drawIndexed.instanceCount = instanceCount;
        c->drawIndexed.firstIndex    = firstIndex;
        c->drawIndexed.vertexOffset  = vertexOffset;
        c->drawIndexed.firstInstance = firstInstance;
    }
}

void ogles2vk_CmdBeginRendering(VkCommandBuffer cb,
                              const VkRenderingInfo *pRenderingInfo)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pRenderingInfo) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BEGIN_RENDERING);
    if (!c) return;

    c->beginRendering.renderArea = pRenderingInfo->renderArea;

    if (pRenderingInfo->colorAttachmentCount > 0 && pRenderingInfo->pColorAttachments)
    {
        const VkRenderingAttachmentInfo *ca = &pRenderingInfo->pColorAttachments[0];
        c->beginRendering.colorAttachment = (OGLES2VKImageView *)(uintptr_t)ca->imageView;
        c->beginRendering.loadOp    = ca->loadOp;
        c->beginRendering.storeOp   = ca->storeOp;
        c->beginRendering.clearValue = ca->clearValue;
    }

    if (pRenderingInfo->pDepthAttachment)
    {
        const VkRenderingAttachmentInfo *da = pRenderingInfo->pDepthAttachment;
        c->beginRendering.depthAttachment = (OGLES2VKImageView *)(uintptr_t)da->imageView;
        c->beginRendering.depthLoadOp     = da->loadOp;
        c->beginRendering.depthClearValue = da->clearValue;
    }
}

void ogles2vk_CmdEndRendering(VkCommandBuffer cb)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_END_RENDERING);
}

void ogles2vk_CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                             VkShaderStageFlags stageFlags, uint32_t offset,
                             uint32_t size, const void *pValues)
{
    (void)layout;
    (void)stageFlags;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pValues || size == 0) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_PUSH_CONSTANTS);
    if (!c) return;

    if (size > OGLES2VK_MAX_PUSH_CONSTANT_SIZE)
        size = OGLES2VK_MAX_PUSH_CONSTANT_SIZE;
    c->pushConstants.offset = offset;
    c->pushConstants.size   = size;
    memcpy(c->pushConstants.data, pValues, size);
}

void ogles2vk_CmdBindVertexBuffers(VkCommandBuffer cb, uint32_t firstBinding,
                                 uint32_t bindingCount, const VkBuffer *pBuffers,
                                 const VkDeviceSize *pOffsets)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pBuffers || !pOffsets) return;
    if (firstBinding >= OGLES2VK_MAX_VERTEX_BINDINGS) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BIND_VERTEX_BUFFERS);
    if (!c) return;

    if (bindingCount > OGLES2VK_MAX_VERTEX_BINDINGS - firstBinding)
        bindingCount = OGLES2VK_MAX_VERTEX_BINDINGS - firstBinding;
    c->bindVertexBuffers.firstBinding = firstBinding;
    c->bindVertexBuffers.bindingCount = bindingCount;
    for (uint32_t i = 0; i < bindingCount; i++)
    {
        c->bindVertexBuffers.buffers[i] = (OGLES2VKBuffer *)(uintptr_t)pBuffers[i];
        c->bindVertexBuffers.offsets[i] = pOffsets[i];
    }
}

void ogles2vk_CmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buffer,
                                VkDeviceSize offset, VkIndexType indexType)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BIND_INDEX_BUFFER);
    if (c)
    {
        c->bindIndexBuffer.buffer    = (OGLES2VKBuffer *)(uintptr_t)buffer;
        c->bindIndexBuffer.offset    = offset;
        c->bindIndexBuffer.indexType = indexType;
    }
}

void ogles2vk_CmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bindPoint,
                                  VkPipelineLayout layout, uint32_t firstSet,
                                  uint32_t setCount, const VkDescriptorSet *pSets,
                                  uint32_t dynOffsetCount, const uint32_t *pDynOffsets)
{
    (void)bindPoint;
    (void)layout;
    (void)dynOffsetCount;
    (void)pDynOffsets;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pSets) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BIND_DESCRIPTOR_SETS);
    if (!c) return;

    if (firstSet >= OGLES2VK_MAX_DESCRIPTOR_SETS)
        return;
    if (firstSet + setCount > OGLES2VK_MAX_DESCRIPTOR_SETS)
        setCount = OGLES2VK_MAX_DESCRIPTOR_SETS - firstSet;
    c->bindDescriptorSets.firstSet = firstSet;
    c->bindDescriptorSets.setCount = setCount;
    for (uint32_t i = 0; i < setCount; i++)
        c->bindDescriptorSets.sets[i] = (void *)(uintptr_t)pSets[i];
}

/****************************************************************************/
/* Legacy render pass                                                       */
/****************************************************************************/

void ogles2vk_CmdBeginRenderPass(VkCommandBuffer cb,
                               const VkRenderPassBeginInfo *pBegin,
                               VkSubpassContents contents)
{
    (void)contents;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pBegin) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_BEGIN_RENDER_PASS);
    if (!c) return;

    c->beginRenderPass.renderPass  = (void *)(uintptr_t)pBegin->renderPass;
    c->beginRenderPass.framebuffer = (void *)(uintptr_t)pBegin->framebuffer;
    c->beginRenderPass.renderArea  = pBegin->renderArea;
    c->beginRenderPass.clearValueCount = pBegin->clearValueCount;
    if (c->beginRenderPass.clearValueCount > OGLES2VK_MAX_ATTACHMENTS)
        c->beginRenderPass.clearValueCount = OGLES2VK_MAX_ATTACHMENTS;
    if (pBegin->pClearValues)
    {
        for (uint32_t i = 0; i < c->beginRenderPass.clearValueCount; i++)
            c->beginRenderPass.clearValues[i] = pBegin->pClearValues[i];
    }
}

void ogles2vk_CmdEndRenderPass(VkCommandBuffer cb)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_END_RENDER_PASS);
}

void ogles2vk_CmdNextSubpass(VkCommandBuffer cb, VkSubpassContents contents)
{
    (void)contents;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_NEXT_SUBPASS);
}

/****************************************************************************/
/* Pipeline barrier (no-op record)                                          */
/****************************************************************************/

void ogles2vk_CmdPipelineBarrier(VkCommandBuffer cb,
    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
    VkDependencyFlags deps, uint32_t memBarrierCount,
    const void *pMemBarriers, uint32_t bufBarrierCount,
    const void *pBufBarriers, uint32_t imgBarrierCount,
    const void *pImgBarriers)
{
    (void)srcStage; (void)dstStage; (void)deps;
    (void)memBarrierCount; (void)pMemBarriers;
    (void)bufBarrierCount; (void)pBufBarriers;
    (void)imgBarrierCount; (void)pImgBarriers;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_PIPELINE_BARRIER);
}

void ogles2vk_CmdPipelineBarrier2(VkCommandBuffer cb,
                                const void *pDependencyInfo)
{
    (void)pDependencyInfo;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_PIPELINE_BARRIER);
}

/****************************************************************************/
/* Dynamic state (Vulkan 1.3)                                               */
/****************************************************************************/

void ogles2vk_CmdSetCullMode(VkCommandBuffer cb, VkCullModeFlags cullMode)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_CULL_MODE);
    if (c) c->setCullMode.cullMode = cullMode;
}

void ogles2vk_CmdSetFrontFace(VkCommandBuffer cb, VkFrontFace frontFace)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_FRONT_FACE);
    if (c) c->setFrontFace.frontFace = frontFace;
}

void ogles2vk_CmdSetPrimitiveTopology(VkCommandBuffer cb, VkPrimitiveTopology topology)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_PRIMITIVE_TOPOLOGY);
    if (c) c->setPrimitiveTopology.topology = topology;
}

void ogles2vk_CmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count,
                                    const VkViewport *pViewports)
{
    ogles2vk_CmdSetViewport(cb, 0, count, pViewports);
}

void ogles2vk_CmdSetScissorWithCount(VkCommandBuffer cb, uint32_t count,
                                   const VkRect2D *pScissors)
{
    ogles2vk_CmdSetScissor(cb, 0, count, pScissors);
}

void ogles2vk_CmdBindVertexBuffers2(VkCommandBuffer cb, uint32_t firstBinding,
                                  uint32_t bindingCount, const VkBuffer *pBuffers,
                                  const VkDeviceSize *pOffsets,
                                  const VkDeviceSize *pSizes,
                                  const VkDeviceSize *pStrides)
{
    (void)pSizes;
    (void)pStrides;
    ogles2vk_CmdBindVertexBuffers(cb, firstBinding, bindingCount, pBuffers, pOffsets);
}

void ogles2vk_CmdSetDepthTestEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_TEST_ENABLE);
    if (c) c->setDepthTestEnable.enable = enable;
}

void ogles2vk_CmdSetDepthWriteEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_WRITE_ENABLE);
    if (c) c->setDepthWriteEnable.enable = enable;
}

void ogles2vk_CmdSetDepthCompareOp(VkCommandBuffer cb, VkCompareOp compareOp)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_COMPARE_OP);
    if (c) c->setDepthCompareOp.compareOp = compareOp;
}

void ogles2vk_CmdSetDepthBoundsTestEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE);
    if (c) c->setBoolEnable.enable = enable;
}

void ogles2vk_CmdSetStencilTestEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_STENCIL_TEST_ENABLE);
    if (c) c->setBoolEnable.enable = enable;
}

void ogles2vk_CmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faceMask,
                            VkStencilOp failOp, VkStencilOp passOp,
                            VkStencilOp depthFailOp, VkCompareOp compareOp)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_STENCIL_OP);
    if (c)
    {
        c->setStencilOp.faceMask     = faceMask;
        c->setStencilOp.failOp       = (uint32_t)failOp;
        c->setStencilOp.passOp       = (uint32_t)passOp;
        c->setStencilOp.depthFailOp  = (uint32_t)depthFailOp;
        c->setStencilOp.compareOp    = (uint32_t)compareOp;
    }
}

void ogles2vk_CmdSetRasterizerDiscardEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_RASTERIZER_DISCARD_ENABLE);
    if (c) c->setBoolEnable.enable = enable;
}

void ogles2vk_CmdSetDepthBiasEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_BIAS_ENABLE);
    if (c) c->setBoolEnable.enable = enable;
}

void ogles2vk_CmdSetPrimitiveRestartEnable(VkCommandBuffer cb, VkBool32 enable)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_PRIMITIVE_RESTART_ENABLE);
    if (c) c->setBoolEnable.enable = enable;
}

/****************************************************************************/
/* Transfer commands                                                        */
/****************************************************************************/

void ogles2vk_CmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst,
                          uint32_t regionCount, const VkBufferCopy *pRegions)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pRegions) return;
    for (uint32_t i = 0; i < regionCount; i++)
    {
        OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_COPY_BUFFER);
        if (!c) return;
        c->copyBuffer.src       = (OGLES2VKBuffer *)(uintptr_t)src;
        c->copyBuffer.dst       = (OGLES2VKBuffer *)(uintptr_t)dst;
        c->copyBuffer.srcOffset = pRegions[i].srcOffset;
        c->copyBuffer.dstOffset = pRegions[i].dstOffset;
        c->copyBuffer.size      = pRegions[i].size;
    }
}

void ogles2vk_CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst,
                                 VkImageLayout dstLayout, uint32_t regionCount,
                                 const VkBufferImageCopy *pRegions)
{
    (void)dstLayout;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pRegions) return;
    for (uint32_t i = 0; i < regionCount; i++)
    {
        OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_COPY_BUFFER_TO_IMAGE);
        if (!c) return;
        c->copyBufferToImage.srcBuffer      = (OGLES2VKBuffer *)(uintptr_t)src;
        c->copyBufferToImage.dstImage       = (OGLES2VKImage *)(uintptr_t)dst;
        c->copyBufferToImage.bufferOffset   = pRegions[i].bufferOffset;
        c->copyBufferToImage.bufferRowLength = pRegions[i].bufferRowLength;
        c->copyBufferToImage.imageOffset    = pRegions[i].imageOffset;
        c->copyBufferToImage.imageExtent    = pRegions[i].imageExtent;
    }
}

void ogles2vk_CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src,
                                 VkImageLayout srcLayout, VkBuffer dst,
                                 uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
    (void)srcLayout;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pRegions) return;
    for (uint32_t i = 0; i < regionCount; i++)
    {
        OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_COPY_IMAGE_TO_BUFFER);
        if (!c) return;
        c->copyImageToBuffer.srcImage       = (OGLES2VKImage *)(uintptr_t)src;
        c->copyImageToBuffer.dstBuffer      = (OGLES2VKBuffer *)(uintptr_t)dst;
        c->copyImageToBuffer.bufferOffset   = pRegions[i].bufferOffset;
        c->copyImageToBuffer.bufferRowLength = pRegions[i].bufferRowLength;
        c->copyImageToBuffer.imageOffset    = pRegions[i].imageOffset;
        c->copyImageToBuffer.imageExtent    = pRegions[i].imageExtent;
    }
}

void ogles2vk_CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout,
                         VkImage dst, VkImageLayout dstLayout,
                         uint32_t regionCount, const VkImageCopy *pRegions)
{
    (void)srcLayout;
    (void)dstLayout;
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pRegions) return;
    for (uint32_t i = 0; i < regionCount; i++)
    {
        OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_COPY_IMAGE);
        if (!c) return;
        c->copyImage.src       = (OGLES2VKImage *)(uintptr_t)src;
        c->copyImage.dst       = (OGLES2VKImage *)(uintptr_t)dst;
        c->copyImage.srcOffset = pRegions[i].srcOffset;
        c->copyImage.dstOffset = pRegions[i].dstOffset;
        c->copyImage.extent    = pRegions[i].extent;
    }
}

void ogles2vk_CmdFillBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset,
                          VkDeviceSize size, uint32_t data)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_FILL_BUFFER);
    if (c)
    {
        c->fillBuffer.buffer = (OGLES2VKBuffer *)(uintptr_t)dst;
        c->fillBuffer.offset = offset;
        c->fillBuffer.size   = size;
        c->fillBuffer.data   = data;
    }
}

void ogles2vk_CmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset,
                            VkDeviceSize size, const void *pData)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    if (!pData || size == 0) return;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_UPDATE_BUFFER);
    if (!c) return;

    if (size > 64) size = 64;
    c->updateBuffer.buffer = (OGLES2VKBuffer *)(uintptr_t)dst;
    c->updateBuffer.offset = offset;
    c->updateBuffer.size   = size;
    memcpy(c->updateBuffer.data, pData, (uint32_t)size);
}

/****************************************************************************/
/* Memory flush/invalidate stubs (host-coherent, no-op)                     */
/****************************************************************************/

VkResult ogles2vk_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                        const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS;
}

VkResult ogles2vk_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                             const VkMappedMemoryRange *pMemoryRanges)
{
    (void)device; (void)memoryRangeCount; (void)pMemoryRanges;
    return VK_SUCCESS;
}

/****************************************************************************/
/* 100% API coverage: No-op command stubs                                    */
/****************************************************************************/

void ogles2vk_CmdSetDeviceMask(VkCommandBuffer cb, uint32_t deviceMask)
{ (void)cb; (void)deviceMask; }

void ogles2vk_CmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z)
{ (void)cb; (void)x; (void)y; (void)z; }

void ogles2vk_CmdDispatchBase(VkCommandBuffer cb, uint32_t bx, uint32_t by, uint32_t bz,
    uint32_t x, uint32_t y, uint32_t z)
{ (void)cb; (void)bx; (void)by; (void)bz; (void)x; (void)y; (void)z; }

void ogles2vk_CmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset)
{ (void)cb; (void)buffer; (void)offset; }

void ogles2vk_CmdExecuteCommands(VkCommandBuffer cb, uint32_t count, const VkCommandBuffer *pCBs)
{ (void)cb; (void)count; (void)pCBs; }

/****************************************************************************/
/* 100% API coverage: Event command no-ops                                   */
/****************************************************************************/

void ogles2vk_CmdSetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stageMask)
{ (void)cb; (void)event; (void)stageMask; }

void ogles2vk_CmdResetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stageMask)
{ (void)cb; (void)event; (void)stageMask; }

void ogles2vk_CmdWaitEvents(VkCommandBuffer cb, uint32_t eventCount, const VkEvent *pEvents,
    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
    uint32_t memCount, const void *pMem, uint32_t bufCount, const void *pBuf,
    uint32_t imgCount, const void *pImg)
{ (void)cb; (void)eventCount; (void)pEvents; (void)srcStage; (void)dstStage;
  (void)memCount; (void)pMem; (void)bufCount; (void)pBuf; (void)imgCount; (void)pImg; }

void ogles2vk_CmdSetEvent2(VkCommandBuffer cb, VkEvent event, const VkDependencyInfo *pDep)
{ (void)cb; (void)event; (void)pDep; }

void ogles2vk_CmdResetEvent2(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags2 stageMask)
{ (void)cb; (void)event; (void)stageMask; }

void ogles2vk_CmdWaitEvents2(VkCommandBuffer cb, uint32_t count, const VkEvent *pEvents, const VkDependencyInfo *pDeps)
{ (void)cb; (void)count; (void)pEvents; (void)pDeps; }

/****************************************************************************/
/* 100% API coverage: Query command no-ops                                   */
/****************************************************************************/

void ogles2vk_CmdBeginQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query, VkQueryControlFlags flags)
{ (void)cb; (void)pool; (void)query; (void)flags; }

void ogles2vk_CmdEndQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query)
{ (void)cb; (void)pool; (void)query; }

void ogles2vk_CmdResetQueryPool(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count)
{ (void)cb; (void)pool; (void)first; (void)count; }

static uint64_t ogles2vk_timestamp_counter = 0;

void ogles2vk_CmdWriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits stage, VkQueryPool pool, uint32_t query)
{
    (void)cb; (void)stage;
    if (pool == VK_NULL_HANDLE) return;
    OGLES2VKQueryPool *qp = (OGLES2VKQueryPool *)(uintptr_t)pool;
    if (query < qp->count) qp->results[query] = ++ogles2vk_timestamp_counter;
}

void ogles2vk_CmdWriteTimestamp2(VkCommandBuffer cb, VkPipelineStageFlags2 stage, VkQueryPool pool, uint32_t query)
{
    (void)cb; (void)stage;
    if (pool == VK_NULL_HANDLE) return;
    OGLES2VKQueryPool *qp = (OGLES2VKQueryPool *)(uintptr_t)pool;
    if (query < qp->count) qp->results[query] = ++ogles2vk_timestamp_counter;
}

void ogles2vk_CmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count,
    VkBuffer dstBuf, VkDeviceSize dstOff, VkDeviceSize stride, VkQueryResultFlags flags)
{ (void)cb; (void)pool; (void)first; (void)count; (void)dstBuf; (void)dstOff; (void)stride; (void)flags; }

/****************************************************************************/
/* 100% API coverage: Render pass 2 (forward to v1)                          */
/****************************************************************************/

void ogles2vk_CmdBeginRenderPass2(VkCommandBuffer cb, const VkRenderPassBeginInfo *pBegin, const VkSubpassBeginInfo *pSBI)
{ (void)pSBI; ogles2vk_CmdBeginRenderPass(cb, pBegin, VK_SUBPASS_CONTENTS_INLINE); }

void ogles2vk_CmdNextSubpass2(VkCommandBuffer cb, const VkSubpassBeginInfo *pSBI, const VkSubpassEndInfo *pSEI)
{ (void)pSBI; (void)pSEI; ogles2vk_CmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE); }

void ogles2vk_CmdEndRenderPass2(VkCommandBuffer cb, const VkSubpassEndInfo *pSEI)
{ (void)pSEI; ogles2vk_CmdEndRenderPass(cb); }

/****************************************************************************/
/* 100% API coverage: Dynamic state commands                                 */
/****************************************************************************/

void ogles2vk_CmdSetLineWidth(VkCommandBuffer cb, float lineWidth)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_LINE_WIDTH);
    if (c) c->setLineWidth.lineWidth = lineWidth;
}

void ogles2vk_CmdSetDepthBias(VkCommandBuffer cb, float cf, float clamp, float sf)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_BIAS);
    if (c) { c->setDepthBias.constantFactor = cf; c->setDepthBias.clamp = clamp; c->setDepthBias.slopeFactor = sf; }
}

void ogles2vk_CmdSetBlendConstants(VkCommandBuffer cb, const float bc[4])
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_BLEND_CONSTANTS);
    if (c && bc) { c->setBlendConstants.constants[0]=bc[0]; c->setBlendConstants.constants[1]=bc[1];
                   c->setBlendConstants.constants[2]=bc[2]; c->setBlendConstants.constants[3]=bc[3]; }
}

void ogles2vk_CmdSetDepthBounds(VkCommandBuffer cb, float minDB, float maxDB)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_DEPTH_BOUNDS_VAL);
    if (c) { c->setDepthBoundsVal.minBounds = minDB; c->setDepthBoundsVal.maxBounds = maxDB; }
}

void ogles2vk_CmdSetStencilCompareMask(VkCommandBuffer cb, VkStencilFaceFlags face, uint32_t mask)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_STENCIL_COMPARE_MASK);
    if (c) { c->setStencilMask.faceMask = face; c->setStencilMask.mask = mask; }
}

void ogles2vk_CmdSetStencilWriteMask(VkCommandBuffer cb, VkStencilFaceFlags face, uint32_t mask)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_STENCIL_WRITE_MASK);
    if (c) { c->setStencilMask.faceMask = face; c->setStencilMask.mask = mask; }
}

void ogles2vk_CmdSetStencilReference(VkCommandBuffer cb, VkStencilFaceFlags face, uint32_t ref)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_SET_STENCIL_REFERENCE);
    if (c) { c->setStencilRef.faceMask = face; c->setStencilRef.reference = ref; }
}

/****************************************************************************/
/* 100% API coverage: Clear commands (inline)                                */
/****************************************************************************/

void ogles2vk_CmdClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout,
    const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
    (void)cb; (void)layout; (void)rangeCount; (void)pRanges;
    if (image == VK_NULL_HANDLE || !pColor) return;
    OGLES2VKImage *img = (OGLES2VKImage *)(uintptr_t)image;
    if (!img->boundMemory || !img->boundMemory->data) return;
    uint8_t *pixels = (uint8_t *)img->boundMemory->data + img->boundOffset;
    uint32_t count = img->width * img->height * img->depth;
    uint8_t r=(uint8_t)(pColor->float32[0]*255.0f), g=(uint8_t)(pColor->float32[1]*255.0f),
            b=(uint8_t)(pColor->float32[2]*255.0f), a=(uint8_t)(pColor->float32[3]*255.0f);
    uint32_t argb = ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
    uint32_t *dst = (uint32_t *)pixels;
    for (uint32_t i = 0; i < count; i++) dst[i] = argb;
}

void ogles2vk_CmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout,
    const VkClearDepthStencilValue *pDS, uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
    (void)cb; (void)layout; (void)rangeCount; (void)pRanges;
    if (image == VK_NULL_HANDLE || !pDS) return;
    OGLES2VKImage *img = (OGLES2VKImage *)(uintptr_t)image;
    if (!img->boundMemory || !img->boundMemory->data) return;
    float *depth = (float *)((uint8_t *)img->boundMemory->data + img->boundOffset);
    uint32_t count = img->width * img->height;
    for (uint32_t i = 0; i < count; i++) depth[i] = pDS->depth;
}

void ogles2vk_CmdClearAttachments(VkCommandBuffer cb, uint32_t attCount, const VkClearAttachment *pAtts,
    uint32_t rectCount, const VkClearRect *pRects)
{ (void)cb; (void)attCount; (void)pAtts; (void)rectCount; (void)pRects; }

/****************************************************************************/
/* 100% API coverage: Blit/resolve (simplified memcpy)                       */
/****************************************************************************/

void ogles2vk_CmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcL, VkImage dst, VkImageLayout dstL,
    uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter)
{
    (void)cb; (void)srcL; (void)dstL; (void)regionCount; (void)pRegions; (void)filter;
    if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
    OGLES2VKImage *s = (OGLES2VKImage *)(uintptr_t)src, *d = (OGLES2VKImage *)(uintptr_t)dst;
    if (!s->boundMemory || !s->boundMemory->data || !d->boundMemory || !d->boundMemory->data) return;
    VkDeviceSize sz = s->memorySize < d->memorySize ? s->memorySize : d->memorySize;
    memcpy((uint8_t*)d->boundMemory->data+d->boundOffset, (uint8_t*)s->boundMemory->data+s->boundOffset, (size_t)sz);
}

void ogles2vk_CmdResolveImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcL, VkImage dst, VkImageLayout dstL,
    uint32_t regionCount, const VkImageResolve *pRegions)
{
    (void)cb; (void)srcL; (void)dstL; (void)regionCount; (void)pRegions;
    if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
    OGLES2VKImage *s = (OGLES2VKImage *)(uintptr_t)src, *d = (OGLES2VKImage *)(uintptr_t)dst;
    if (!s->boundMemory || !s->boundMemory->data || !d->boundMemory || !d->boundMemory->data) return;
    VkDeviceSize sz = s->memorySize < d->memorySize ? s->memorySize : d->memorySize;
    memcpy((uint8_t*)d->boundMemory->data+d->boundOffset, (uint8_t*)s->boundMemory->data+s->boundOffset, (size_t)sz);
}

/****************************************************************************/
/* 100% API coverage: Vulkan 1.3 "2" command variants                        */
/****************************************************************************/

void ogles2vk_CmdCopyBuffer2(VkCommandBuffer cb, const VkCopyBufferInfo2 *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->regionCount; i++) {
        VkBufferCopy r; r.srcOffset=p->pRegions[i].srcOffset; r.dstOffset=p->pRegions[i].dstOffset; r.size=p->pRegions[i].size;
        ogles2vk_CmdCopyBuffer(cb, p->srcBuffer, p->dstBuffer, 1, &r);
    }
}

void ogles2vk_CmdCopyImage2(VkCommandBuffer cb, const VkCopyImageInfo2 *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->regionCount; i++) {
        VkImageCopy r; r.srcSubresource=p->pRegions[i].srcSubresource; r.srcOffset=p->pRegions[i].srcOffset;
        r.dstSubresource=p->pRegions[i].dstSubresource; r.dstOffset=p->pRegions[i].dstOffset; r.extent=p->pRegions[i].extent;
        ogles2vk_CmdCopyImage(cb, p->srcImage, p->srcImageLayout, p->dstImage, p->dstImageLayout, 1, &r);
    }
}

void ogles2vk_CmdCopyBufferToImage2(VkCommandBuffer cb, const VkCopyBufferToImageInfo2 *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->regionCount; i++) {
        VkBufferImageCopy r; r.bufferOffset=p->pRegions[i].bufferOffset; r.bufferRowLength=p->pRegions[i].bufferRowLength;
        r.bufferImageHeight=p->pRegions[i].bufferImageHeight; r.imageSubresource=p->pRegions[i].imageSubresource;
        r.imageOffset=p->pRegions[i].imageOffset; r.imageExtent=p->pRegions[i].imageExtent;
        ogles2vk_CmdCopyBufferToImage(cb, p->srcBuffer, p->dstImage, p->dstImageLayout, 1, &r);
    }
}

void ogles2vk_CmdCopyImageToBuffer2(VkCommandBuffer cb, const VkCopyImageToBufferInfo2 *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->regionCount; i++) {
        VkBufferImageCopy r; r.bufferOffset=p->pRegions[i].bufferOffset; r.bufferRowLength=p->pRegions[i].bufferRowLength;
        r.bufferImageHeight=p->pRegions[i].bufferImageHeight; r.imageSubresource=p->pRegions[i].imageSubresource;
        r.imageOffset=p->pRegions[i].imageOffset; r.imageExtent=p->pRegions[i].imageExtent;
        ogles2vk_CmdCopyImageToBuffer(cb, p->srcImage, p->srcImageLayout, p->dstBuffer, 1, &r);
    }
}

void ogles2vk_CmdBlitImage2(VkCommandBuffer cb, const VkBlitImageInfo2 *p)
{
    if (!p) return;
    ogles2vk_CmdBlitImage(cb, p->srcImage, p->srcImageLayout, p->dstImage, p->dstImageLayout, 0, NULL, p->filter);
}

void ogles2vk_CmdResolveImage2(VkCommandBuffer cb, const VkResolveImageInfo2 *p)
{
    if (!p) return;
    ogles2vk_CmdResolveImage(cb, p->srcImage, p->srcImageLayout, p->dstImage, p->dstImageLayout, 0, NULL);
}

/****************************************************************************/
/* 100% API coverage: Indirect draw commands                                 */
/****************************************************************************/

void ogles2vk_CmdDrawIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_DRAW_INDIRECT);
    if (c) { c->drawIndirect.buffer=(OGLES2VKBuffer*)(uintptr_t)buffer; c->drawIndirect.offset=offset;
             c->drawIndirect.drawCount=drawCount; c->drawIndirect.stride=stride; }
}

void ogles2vk_CmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    OGLES2VKCommandBuffer *cmd = (OGLES2VKCommandBuffer *)cb;
    OGLES2VKCommand *c = ogles2vk_CmdAppend(cmd, OGLES2VK_CMD_DRAW_INDEXED_INDIRECT);
    if (c) { c->drawIndirect.buffer=(OGLES2VKBuffer*)(uintptr_t)buffer; c->drawIndirect.offset=offset;
             c->drawIndirect.drawCount=drawCount; c->drawIndirect.stride=stride; }
}

void ogles2vk_CmdDrawIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{ (void)countBuffer; (void)countBufferOffset; ogles2vk_CmdDrawIndirect(cb, buffer, offset, maxDrawCount, stride); }

void ogles2vk_CmdDrawIndexedIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{ (void)countBuffer; (void)countBufferOffset; ogles2vk_CmdDrawIndexedIndirect(cb, buffer, offset, maxDrawCount, stride); }
