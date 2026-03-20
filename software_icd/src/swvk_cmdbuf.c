/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_cmdbuf.c -- Command pool, command buffer, and command recording
**
** Command buffers record a list of commands (bind pipeline, set viewport,
** draw, etc.) that are played back during vkQueueSubmit.
** Commands are stored in a fixed-size array (SWVK_MAX_COMMANDS).
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "swvk_internal.h"

/****************************************************************************/
/* Command pool                                                             */
/****************************************************************************/

VkResult swvk_CreateCommandPool(VkDevice device,
                                const VkCommandPoolCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkCommandPool *pCommandPool)
{
    (void)device;
    (void)pAllocator;

    SWVKCommandPool *pool = (SWVKCommandPool *)IExec->AllocVecTags(
        sizeof(SWVKCommandPool),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->queueFamilyIndex = pCreateInfo->queueFamilyIndex;

    *pCommandPool = (VkCommandPool)(uintptr_t)pool;
    return VK_SUCCESS;
}

void swvk_DestroyCommandPool(VkDevice device,
                             VkCommandPool commandPool,
                             const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (commandPool == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)commandPool);
}

/****************************************************************************/
/* Command buffer allocation                                                */
/****************************************************************************/

VkResult swvk_AllocateCommandBuffers(VkDevice device,
                                     const VkCommandBufferAllocateInfo *pAllocateInfo,
                                     VkCommandBuffer *pCommandBuffers)
{
    (void)device;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++)
    {
        SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)IExec->AllocVecTags(
            sizeof(SWVKCommandBuffer),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!cmd)
        {
            /* Free already-allocated buffers in this batch */
            for (uint32_t j = 0; j < i; j++)
            {
                if (pCommandBuffers[j])
                    IExec->FreeVec((APTR)pCommandBuffers[j]);
                pCommandBuffers[j] = NULL;
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        /* VkCommandBuffer is a dispatchable handle (pointer) */
        pCommandBuffers[i] = (VkCommandBuffer)cmd;
    }

    return VK_SUCCESS;
}

void swvk_FreeCommandBuffers(VkDevice device,
                             VkCommandPool commandPool,
                             uint32_t commandBufferCount,
                             const VkCommandBuffer *pCommandBuffers)
{
    (void)device;
    (void)commandPool;

    for (uint32_t i = 0; i < commandBufferCount; i++)
    {
        if (pCommandBuffers[i])
            IExec->FreeVec((APTR)pCommandBuffers[i]);
    }
}

/****************************************************************************/
/* Command buffer recording lifecycle                                       */
/****************************************************************************/

VkResult swvk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                 const VkCommandBufferBeginInfo *pBeginInfo)
{
    (void)pBeginInfo;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    cmd->commandCount = 0;
    cmd->recording    = 1;

    return VK_SUCCESS;
}

VkResult swvk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    cmd->recording = 0;

    IExec->DebugPrintF("[software_vk] Command buffer recorded: %lu commands\n",
                       (unsigned long)cmd->commandCount);

    return VK_SUCCESS;
}

/****************************************************************************/
/* Helper: append a command to the command buffer                           */
/****************************************************************************/

static SWVKCommand *swvk_CmdAppend(SWVKCommandBuffer *cmd, SWVKCommandType type)
{
    if (cmd->commandCount >= SWVK_MAX_COMMANDS)
    {
        IExec->DebugPrintF("[software_vk] WARNING: command buffer full (%u/%u), command dropped\n",
                           (unsigned)cmd->commandCount, (unsigned)SWVK_MAX_COMMANDS);
        return NULL;
    }

    SWVKCommand *c = &cmd->commands[cmd->commandCount++];
    memset(c, 0, sizeof(SWVKCommand));
    c->type = type;
    return c;
}

/****************************************************************************/
/* Command recording functions                                              */
/****************************************************************************/

void swvk_CmdBindPipeline(VkCommandBuffer commandBuffer,
                          VkPipelineBindPoint pipelineBindPoint,
                          VkPipeline pipeline)
{
    (void)pipelineBindPoint;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BIND_PIPELINE);
    if (c)
        c->bindPipeline.pipeline = (SWVKPipeline *)(uintptr_t)pipeline;
}

void swvk_CmdSetViewport(VkCommandBuffer commandBuffer,
                         uint32_t firstViewport,
                         uint32_t viewportCount,
                         const VkViewport *pViewports)
{
    (void)firstViewport;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    /* Only a single viewport is supported */
    if (viewportCount >= 1)
    {
        SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_VIEWPORT);
        if (c)
            c->setViewport.viewport = pViewports[0];
    }
}

void swvk_CmdSetScissor(VkCommandBuffer commandBuffer,
                        uint32_t firstScissor,
                        uint32_t scissorCount,
                        const VkRect2D *pScissors)
{
    (void)firstScissor;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    /* Only a single scissor is supported */
    if (scissorCount >= 1)
    {
        SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_SCISSOR);
        if (c)
            c->setScissor.scissor = pScissors[0];
    }
}

void swvk_CmdDraw(VkCommandBuffer commandBuffer,
                  uint32_t vertexCount,
                  uint32_t instanceCount,
                  uint32_t firstVertex,
                  uint32_t firstInstance)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_DRAW);
    if (c)
    {
        c->draw.vertexCount   = vertexCount;
        c->draw.instanceCount = instanceCount;
        c->draw.firstVertex   = firstVertex;
        c->draw.firstInstance = firstInstance;
    }
}

void swvk_CmdBeginRendering(VkCommandBuffer commandBuffer,
                            const VkRenderingInfo *pRenderingInfo)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BEGIN_RENDERING);
    if (c)
    {
        c->beginRendering.renderArea = pRenderingInfo->renderArea;

        /* Extract first color attachment info */
        if (pRenderingInfo->colorAttachmentCount > 0 &&
            pRenderingInfo->pColorAttachments)
        {
            const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[0];
            c->beginRendering.colorAttachment =
                (SWVKImageView *)(uintptr_t)att->imageView;
            c->beginRendering.loadOp    = att->loadOp;
            c->beginRendering.storeOp   = att->storeOp;
            c->beginRendering.clearValue = att->clearValue;
        }

        /* Extract depth attachment info */
        if (pRenderingInfo->pDepthAttachment &&
            pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE)
        {
            const VkRenderingAttachmentInfo *datt = pRenderingInfo->pDepthAttachment;
            c->beginRendering.depthAttachment =
                (SWVKImageView *)(uintptr_t)datt->imageView;
            c->beginRendering.depthLoadOp    = datt->loadOp;
            c->beginRendering.depthClearValue = datt->clearValue;
        }
    }
}

void swvk_CmdEndRendering(VkCommandBuffer commandBuffer)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_END_RENDERING);
}

void swvk_CmdPushConstants(VkCommandBuffer commandBuffer,
                           VkPipelineLayout layout,
                           VkShaderStageFlags stageFlags,
                           uint32_t offset,
                           uint32_t size,
                           const void *pValues)
{
    (void)layout;
    (void)stageFlags;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pValues || size == 0)
        return;

    /* Bounds check */
    if (offset + size > SWVK_MAX_PUSH_CONSTANT_SIZE)
    {
        IExec->DebugPrintF("[software_vk] WARNING: push constants exceed max size (%u + %u > %u)\n",
                           (unsigned)offset, (unsigned)size, (unsigned)SWVK_MAX_PUSH_CONSTANT_SIZE);
        return;
    }

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_PUSH_CONSTANTS);
    if (c)
    {
        c->pushConstants.offset = offset;
        c->pushConstants.size   = size;
        memcpy(c->pushConstants.data + offset, pValues, size);
    }
}

void swvk_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                               uint32_t firstBinding,
                               uint32_t bindingCount,
                               const VkBuffer *pBuffers,
                               const VkDeviceSize *pOffsets)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pBuffers || bindingCount == 0)
        return;

    /* Bounds check */
    if (firstBinding + bindingCount > SWVK_MAX_VERTEX_BINDINGS)
    {
        IExec->DebugPrintF("[software_vk] WARNING: vertex binding out of range (%u + %u > %u)\n",
                           (unsigned)firstBinding, (unsigned)bindingCount,
                           (unsigned)SWVK_MAX_VERTEX_BINDINGS);
        return;
    }

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BIND_VERTEX_BUFFERS);
    if (c)
    {
        c->bindVertexBuffers.firstBinding = firstBinding;
        c->bindVertexBuffers.bindingCount = bindingCount;
        for (uint32_t i = 0; i < bindingCount; i++)
        {
            c->bindVertexBuffers.buffers[i] = (SWVKBuffer *)(uintptr_t)pBuffers[i];
            c->bindVertexBuffers.offsets[i] = pOffsets ? pOffsets[i] : 0;
        }
    }
}

void swvk_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                             VkBuffer buffer,
                             VkDeviceSize offset,
                             VkIndexType indexType)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BIND_INDEX_BUFFER);
    if (c)
    {
        c->bindIndexBuffer.buffer    = (SWVKBuffer *)(uintptr_t)buffer;
        c->bindIndexBuffer.offset    = offset;
        c->bindIndexBuffer.indexType = indexType;
    }
}

void swvk_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                         uint32_t indexCount,
                         uint32_t instanceCount,
                         uint32_t firstIndex,
                         int32_t vertexOffset,
                         uint32_t firstInstance)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_DRAW_INDEXED);
    if (c)
    {
        c->drawIndexed.indexCount    = indexCount;
        c->drawIndexed.instanceCount = instanceCount;
        c->drawIndexed.firstIndex   = firstIndex;
        c->drawIndexed.vertexOffset = vertexOffset;
        c->drawIndexed.firstInstance = firstInstance;
    }
}

/****************************************************************************/
/* Command buffer reset                                                     */
/****************************************************************************/

VkResult swvk_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                                  VkCommandBufferResetFlags flags)
{
    (void)flags;
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    if (cmd)
    {
        cmd->commandCount = 0;
        cmd->recording    = 0;
    }
    return VK_SUCCESS;
}

/****************************************************************************/
/* Pipeline barrier (no-op for software renderer)                           */
/****************************************************************************/

void swvk_CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                              VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask,
                              VkDependencyFlags dependencyFlags,
                              uint32_t memoryBarrierCount,
                              const void *pMemoryBarriers,
                              uint32_t bufferMemoryBarrierCount,
                              const void *pBufferMemoryBarriers,
                              uint32_t imageMemoryBarrierCount,
                              const void *pImageMemoryBarriers)
{
    /* Record as a no-op command so the command count is accurate */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_PIPELINE_BARRIER);

    (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
}

/****************************************************************************/
/* Legacy render pass commands                                              */
/****************************************************************************/

void swvk_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                              const VkRenderPassBeginInfo *pRenderPassBegin,
                              VkSubpassContents contents)
{
    (void)contents;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BEGIN_RENDER_PASS);
    if (c)
    {
        c->beginRenderPass.renderPass  = (SWVKRenderPass *)(uintptr_t)pRenderPassBegin->renderPass;
        c->beginRenderPass.framebuffer = (SWVKFramebuffer *)(uintptr_t)pRenderPassBegin->framebuffer;
        c->beginRenderPass.renderArea  = pRenderPassBegin->renderArea;

        uint32_t cvCount = pRenderPassBegin->clearValueCount;
        if (cvCount > SWVK_MAX_ATTACHMENTS)
            cvCount = SWVK_MAX_ATTACHMENTS;
        c->beginRenderPass.clearValueCount = cvCount;
        if (pRenderPassBegin->pClearValues && cvCount > 0)
            memcpy(c->beginRenderPass.clearValues, pRenderPassBegin->pClearValues,
                   cvCount * sizeof(VkClearValue));
    }
}

void swvk_CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_END_RENDER_PASS);
}

void swvk_CmdNextSubpass(VkCommandBuffer commandBuffer,
                          VkSubpassContents contents)
{
    (void)contents;
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_NEXT_SUBPASS);
}

/****************************************************************************/
/* Vulkan 1.3 dynamic state commands                                        */
/****************************************************************************/

void swvk_CmdSetCullMode(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_CULL_MODE);
    if (c) c->setCullMode.cullMode = cullMode;
}

void swvk_CmdSetFrontFace(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_FRONT_FACE);
    if (c) c->setFrontFace.frontFace = frontFace;
}

void swvk_CmdSetPrimitiveTopology(VkCommandBuffer commandBuffer, VkPrimitiveTopology topology)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_PRIMITIVE_TOPOLOGY);
    if (c) c->setPrimitiveTopology.topology = topology;
}

void swvk_CmdSetViewportWithCount(VkCommandBuffer commandBuffer,
                                   uint32_t viewportCount,
                                   const VkViewport *pViewports)
{
    /* Delegate to existing SetViewport with firstViewport=0 */
    swvk_CmdSetViewport(commandBuffer, 0, viewportCount, pViewports);
}

void swvk_CmdSetScissorWithCount(VkCommandBuffer commandBuffer,
                                  uint32_t scissorCount,
                                  const VkRect2D *pScissors)
{
    /* Delegate to existing SetScissor with firstScissor=0 */
    swvk_CmdSetScissor(commandBuffer, 0, scissorCount, pScissors);
}

void swvk_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer,
                                 uint32_t firstBinding,
                                 uint32_t bindingCount,
                                 const VkBuffer *pBuffers,
                                 const VkDeviceSize *pOffsets,
                                 const VkDeviceSize *pSizes,
                                 const VkDeviceSize *pStrides)
{
    /* Delegate to existing BindVertexBuffers (pSizes/pStrides ignored) */
    (void)pSizes;
    (void)pStrides;
    swvk_CmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

void swvk_CmdSetDepthTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthTestEnable)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_TEST_ENABLE);
    if (c) c->setDepthTestEnable.enable = depthTestEnable;
}

void swvk_CmdSetDepthWriteEnable(VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_WRITE_ENABLE);
    if (c) c->setDepthWriteEnable.enable = depthWriteEnable;
}

void swvk_CmdSetDepthCompareOp(VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_COMPARE_OP);
    if (c) c->setDepthCompareOp.compareOp = depthCompareOp;
}

void swvk_CmdSetDepthBoundsTestEnable(VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable)
{
    (void)depthBoundsTestEnable;
    /* No-op: depth bounds not supported in software renderer */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE);
}

void swvk_CmdSetStencilTestEnable(VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable)
{
    (void)stencilTestEnable;
    /* No-op: stencil not supported in software renderer */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_STENCIL_TEST_ENABLE);
}

void swvk_CmdSetStencilOp(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            VkStencilOp failOp,
                            VkStencilOp passOp,
                            VkStencilOp depthFailOp,
                            VkCompareOp compareOp)
{
    (void)faceMask; (void)failOp;
    (void)passOp; (void)depthFailOp; (void)compareOp;
    /* No-op: stencil not supported in software renderer */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_STENCIL_OP);
}

void swvk_CmdSetRasterizerDiscardEnable(VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable)
{
    (void)rasterizerDiscardEnable;
    /* No-op */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_RASTERIZER_DISCARD_ENABLE);
}

void swvk_CmdSetDepthBiasEnable(VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable)
{
    (void)depthBiasEnable;
    /* No-op */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_BIAS_ENABLE);
}

void swvk_CmdSetPrimitiveRestartEnable(VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable)
{
    (void)primitiveRestartEnable;
    /* No-op */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_SET_PRIMITIVE_RESTART_ENABLE);
}

/****************************************************************************/
/* Pipeline barrier v2 (no-op)                                              */
/****************************************************************************/

void swvk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                const void *pDependencyInfo)
{
    (void)pDependencyInfo;
    /* Record as a no-op command, same as v1 */
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    swvk_CmdAppend(cmd, SWVK_CMD_PIPELINE_BARRIER);
}

/****************************************************************************/
/* Transfer command recording                                               */
/****************************************************************************/

void swvk_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                          VkBuffer srcBuffer,
                          VkBuffer dstBuffer,
                          uint32_t regionCount,
                          const VkBufferCopy *pRegions)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pRegions || regionCount == 0)
        return;

    /* Record first region only */
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_COPY_BUFFER);
    if (c)
    {
        c->copyBuffer.src       = (SWVKBuffer *)(uintptr_t)srcBuffer;
        c->copyBuffer.dst       = (SWVKBuffer *)(uintptr_t)dstBuffer;
        c->copyBuffer.srcOffset = pRegions[0].srcOffset;
        c->copyBuffer.dstOffset = pRegions[0].dstOffset;
        c->copyBuffer.size      = pRegions[0].size;
    }

    /* Record additional regions as separate commands */
    for (uint32_t i = 1; i < regionCount; i++)
    {
        c = swvk_CmdAppend(cmd, SWVK_CMD_COPY_BUFFER);
        if (c)
        {
            c->copyBuffer.src       = (SWVKBuffer *)(uintptr_t)srcBuffer;
            c->copyBuffer.dst       = (SWVKBuffer *)(uintptr_t)dstBuffer;
            c->copyBuffer.srcOffset = pRegions[i].srcOffset;
            c->copyBuffer.dstOffset = pRegions[i].dstOffset;
            c->copyBuffer.size      = pRegions[i].size;
        }
    }
}

void swvk_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                                 VkBuffer srcBuffer,
                                 VkImage dstImage,
                                 VkImageLayout dstImageLayout,
                                 uint32_t regionCount,
                                 const VkBufferImageCopy *pRegions)
{
    (void)dstImageLayout;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pRegions || regionCount == 0)
        return;

    /* Record first region only */
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_COPY_BUFFER_TO_IMAGE);
    if (c)
    {
        c->copyBufferToImage.srcBuffer      = (SWVKBuffer *)(uintptr_t)srcBuffer;
        c->copyBufferToImage.dstImage       = (SWVKImage *)(uintptr_t)dstImage;
        c->copyBufferToImage.bufferOffset   = pRegions[0].bufferOffset;
        c->copyBufferToImage.bufferRowLength = pRegions[0].bufferRowLength;
        c->copyBufferToImage.imageOffset    = pRegions[0].imageOffset;
        c->copyBufferToImage.imageExtent    = pRegions[0].imageExtent;
    }
}

void swvk_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                                 VkImage srcImage,
                                 VkImageLayout srcImageLayout,
                                 VkBuffer dstBuffer,
                                 uint32_t regionCount,
                                 const VkBufferImageCopy *pRegions)
{
    (void)srcImageLayout;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pRegions || regionCount == 0)
        return;

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_COPY_IMAGE_TO_BUFFER);
    if (c)
    {
        c->copyImageToBuffer.srcImage       = (SWVKImage *)(uintptr_t)srcImage;
        c->copyImageToBuffer.dstBuffer      = (SWVKBuffer *)(uintptr_t)dstBuffer;
        c->copyImageToBuffer.bufferOffset   = pRegions[0].bufferOffset;
        c->copyImageToBuffer.bufferRowLength = pRegions[0].bufferRowLength;
        c->copyImageToBuffer.imageOffset    = pRegions[0].imageOffset;
        c->copyImageToBuffer.imageExtent    = pRegions[0].imageExtent;
    }
}

void swvk_CmdCopyImage(VkCommandBuffer commandBuffer,
                         VkImage srcImage,
                         VkImageLayout srcImageLayout,
                         VkImage dstImage,
                         VkImageLayout dstImageLayout,
                         uint32_t regionCount,
                         const VkImageCopy *pRegions)
{
    (void)srcImageLayout;
    (void)dstImageLayout;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pRegions || regionCount == 0)
        return;

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_COPY_IMAGE);
    if (c)
    {
        c->copyImage.src       = (SWVKImage *)(uintptr_t)srcImage;
        c->copyImage.dst       = (SWVKImage *)(uintptr_t)dstImage;
        c->copyImage.srcOffset = pRegions[0].srcOffset;
        c->copyImage.dstOffset = pRegions[0].dstOffset;
        c->copyImage.extent    = pRegions[0].extent;
    }
}

void swvk_CmdFillBuffer(VkCommandBuffer commandBuffer,
                          VkBuffer dstBuffer,
                          VkDeviceSize dstOffset,
                          VkDeviceSize size,
                          uint32_t data)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_FILL_BUFFER);
    if (c)
    {
        c->fillBuffer.buffer = (SWVKBuffer *)(uintptr_t)dstBuffer;
        c->fillBuffer.offset = dstOffset;
        c->fillBuffer.size   = size;
        c->fillBuffer.data   = data;
    }
}

void swvk_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                            VkBuffer dstBuffer,
                            VkDeviceSize dstOffset,
                            VkDeviceSize dataSize,
                            const void *pData)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pData || dataSize == 0)
        return;

    /* Clamp to inline storage limit */
    if (dataSize > 64)
    {
        IExec->DebugPrintF("[software_vk] WARNING: CmdUpdateBuffer dataSize %lu exceeds 64 byte limit\n",
                           (unsigned long)dataSize);
        dataSize = 64;
    }

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_UPDATE_BUFFER);
    if (c)
    {
        c->updateBuffer.buffer = (SWVKBuffer *)(uintptr_t)dstBuffer;
        c->updateBuffer.offset = dstOffset;
        c->updateBuffer.size   = dataSize;
        memcpy(c->updateBuffer.data, pData, (size_t)dataSize);
    }
}

/****************************************************************************/
/* Descriptor set binding command recording                                 */
/****************************************************************************/

void swvk_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                 VkPipelineBindPoint pipelineBindPoint,
                                 VkPipelineLayout layout,
                                 uint32_t firstSet,
                                 uint32_t descriptorSetCount,
                                 const VkDescriptorSet *pDescriptorSets,
                                 uint32_t dynamicOffsetCount,
                                 const uint32_t *pDynamicOffsets)
{
    (void)pipelineBindPoint;
    (void)layout;
    (void)dynamicOffsetCount;
    (void)pDynamicOffsets;

    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;

    if (!pDescriptorSets || descriptorSetCount == 0)
        return;

    /* Bounds check */
    if (firstSet + descriptorSetCount > SWVK_MAX_DESCRIPTOR_SETS)
    {
        IExec->DebugPrintF("[software_vk] WARNING: descriptor set out of range (%u + %u > %u)\n",
                           (unsigned)firstSet, (unsigned)descriptorSetCount,
                           (unsigned)SWVK_MAX_DESCRIPTOR_SETS);
        return;
    }

    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_BIND_DESCRIPTOR_SETS);
    if (c)
    {
        c->bindDescriptorSets.firstSet = firstSet;
        c->bindDescriptorSets.setCount = descriptorSetCount;
        for (uint32_t i = 0; i < descriptorSetCount; i++)
            c->bindDescriptorSets.sets[i] = (SWVKDescriptorSet *)(uintptr_t)pDescriptorSets[i];
    }
}

/****************************************************************************/
/* No-op command recording stubs                                             */
/****************************************************************************/

void swvk_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
    (void)commandBuffer; (void)deviceMask;
    /* No-op: single device */
}

void swvk_CmdDispatch(VkCommandBuffer commandBuffer,
                        uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    (void)commandBuffer; (void)groupCountX; (void)groupCountY; (void)groupCountZ;
    /* No-op: no compute support */
}

void swvk_CmdDispatchBase(VkCommandBuffer commandBuffer,
                            uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ,
                            uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    (void)commandBuffer;
    (void)baseGroupX; (void)baseGroupY; (void)baseGroupZ;
    (void)groupCountX; (void)groupCountY; (void)groupCountZ;
}

void swvk_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                                VkBuffer buffer, VkDeviceSize offset)
{
    (void)commandBuffer; (void)buffer; (void)offset;
}

void swvk_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                                uint32_t commandBufferCount,
                                const VkCommandBuffer *pCommandBuffers)
{
    (void)commandBuffer; (void)commandBufferCount; (void)pCommandBuffers;
    /* No-op: secondary command buffers not supported */
}

/****************************************************************************/
/* Event command recording (all no-ops for immediate mode)                   */
/****************************************************************************/

void swvk_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                        VkPipelineStageFlags stageMask)
{
    (void)commandBuffer; (void)event; (void)stageMask;
}

void swvk_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                          VkPipelineStageFlags stageMask)
{
    (void)commandBuffer; (void)event; (void)stageMask;
}

void swvk_CmdWaitEvents(VkCommandBuffer commandBuffer,
                          uint32_t eventCount, const VkEvent *pEvents,
                          VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                          uint32_t memoryBarrierCount, const void *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers)
{
    (void)commandBuffer; (void)eventCount; (void)pEvents;
    (void)srcStageMask; (void)dstStageMask;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers;
}

void swvk_CmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                          const VkDependencyInfo *pDependencyInfo)
{
    (void)commandBuffer; (void)event; (void)pDependencyInfo;
}

void swvk_CmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent event,
                            VkPipelineStageFlags2 stageMask)
{
    (void)commandBuffer; (void)event; (void)stageMask;
}

void swvk_CmdWaitEvents2(VkCommandBuffer commandBuffer,
                            uint32_t eventCount, const VkEvent *pEvents,
                            const VkDependencyInfo *pDependencyInfos)
{
    (void)commandBuffer; (void)eventCount; (void)pEvents; (void)pDependencyInfos;
}

/****************************************************************************/
/* Query command recording (no-ops for software renderer)                    */
/****************************************************************************/

void swvk_CmdBeginQuery(VkCommandBuffer commandBuffer,
                          VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
    (void)commandBuffer; (void)queryPool; (void)query; (void)flags;
}

void swvk_CmdEndQuery(VkCommandBuffer commandBuffer,
                        VkQueryPool queryPool, uint32_t query)
{
    (void)commandBuffer; (void)queryPool; (void)query;
}

void swvk_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                               VkQueryPool queryPool,
                               uint32_t firstQuery, uint32_t queryCount)
{
    (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount;
}

/* Monotonic timestamp counter -- clock() doesn't work in library context */
static uint64_t swvk_timestamp_counter = 0;

void swvk_CmdWriteTimestamp(VkCommandBuffer commandBuffer,
                              VkPipelineStageFlagBits pipelineStage,
                              VkQueryPool queryPool, uint32_t query)
{
    (void)commandBuffer; (void)pipelineStage;
    if (queryPool == VK_NULL_HANDLE) return;
    SWVKQueryPool *pool = (SWVKQueryPool *)(uintptr_t)queryPool;
    if (query < pool->count)
        pool->results[query] = ++swvk_timestamp_counter;
}

void swvk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                               VkPipelineStageFlags2 stage,
                               VkQueryPool queryPool, uint32_t query)
{
    (void)commandBuffer; (void)stage;
    if (queryPool == VK_NULL_HANDLE) return;
    SWVKQueryPool *pool = (SWVKQueryPool *)(uintptr_t)queryPool;
    if (query < pool->count)
        pool->results[query] = ++swvk_timestamp_counter;
}

void swvk_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                                     VkQueryPool queryPool,
                                     uint32_t firstQuery, uint32_t queryCount,
                                     VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                     VkDeviceSize stride, VkQueryResultFlags flags)
{
    (void)commandBuffer; (void)queryPool; (void)firstQuery; (void)queryCount;
    (void)dstBuffer; (void)dstOffset; (void)stride; (void)flags;
}

/****************************************************************************/
/* Render pass 2 command recording (forward to v1)                           */
/****************************************************************************/

void swvk_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                                 const VkRenderPassBeginInfo *pRenderPassBegin,
                                 const VkSubpassBeginInfo *pSubpassBeginInfo)
{
    (void)pSubpassBeginInfo;
    swvk_CmdBeginRenderPass(commandBuffer, pRenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
}

void swvk_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                             const VkSubpassBeginInfo *pSubpassBeginInfo,
                             const VkSubpassEndInfo *pSubpassEndInfo)
{
    (void)pSubpassBeginInfo; (void)pSubpassEndInfo;
    swvk_CmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

void swvk_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                               const VkSubpassEndInfo *pSubpassEndInfo)
{
    (void)pSubpassEndInfo;
    swvk_CmdEndRenderPass(commandBuffer);
}

/****************************************************************************/
/* Dynamic state command recording                                           */
/****************************************************************************/

void swvk_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_LINE_WIDTH);
    if (c) c->setLineWidth.lineWidth = lineWidth;
}

void swvk_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                             float depthBiasConstantFactor,
                             float depthBiasClamp,
                             float depthBiasSlopeFactor)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_BIAS);
    if (c)
    {
        c->setDepthBias.constantFactor = depthBiasConstantFactor;
        c->setDepthBias.clamp          = depthBiasClamp;
        c->setDepthBias.slopeFactor    = depthBiasSlopeFactor;
    }
}

void swvk_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                                 const float blendConstants[4])
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_BLEND_CONSTANTS);
    if (c && blendConstants)
    {
        c->setBlendConstants.constants[0] = blendConstants[0];
        c->setBlendConstants.constants[1] = blendConstants[1];
        c->setBlendConstants.constants[2] = blendConstants[2];
        c->setBlendConstants.constants[3] = blendConstants[3];
    }
}

void swvk_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                               float minDepthBounds, float maxDepthBounds)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_DEPTH_BOUNDS_VAL);
    if (c)
    {
        c->setDepthBoundsVal.minBounds = minDepthBounds;
        c->setDepthBoundsVal.maxBounds = maxDepthBounds;
    }
}

void swvk_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                      VkStencilFaceFlags faceMask,
                                      uint32_t compareMask)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_STENCIL_COMPARE_MASK);
    if (c)
    {
        c->setStencilMask.faceMask = faceMask;
        c->setStencilMask.mask     = compareMask;
    }
}

void swvk_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                                    VkStencilFaceFlags faceMask,
                                    uint32_t writeMask)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_STENCIL_WRITE_MASK);
    if (c)
    {
        c->setStencilMask.faceMask = faceMask;
        c->setStencilMask.mask     = writeMask;
    }
}

void swvk_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                                    VkStencilFaceFlags faceMask,
                                    uint32_t reference)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_SET_STENCIL_REFERENCE);
    if (c)
    {
        c->setStencilRef.faceMask  = faceMask;
        c->setStencilRef.reference = reference;
    }
}

/****************************************************************************/
/* Clear commands (inline -- operate on image memory directly)               */
/****************************************************************************/

void swvk_CmdClearColorImage(VkCommandBuffer commandBuffer,
                               VkImage image, VkImageLayout imageLayout,
                               const VkClearColorValue *pColor,
                               uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
    (void)commandBuffer; (void)imageLayout; (void)rangeCount; (void)pRanges;

    if (image == VK_NULL_HANDLE || !pColor)
        return;

    SWVKImage *img = (SWVKImage *)(uintptr_t)image;
    if (!img->boundMemory || !img->boundMemory->data)
        return;

    uint8_t *pixels = (uint8_t *)img->boundMemory->data + img->boundOffset;
    uint32_t pixelCount = img->width * img->height * img->depth;

    /* Convert clear color to ARGB32 */
    uint8_t r = (uint8_t)(pColor->float32[0] * 255.0f);
    uint8_t g = (uint8_t)(pColor->float32[1] * 255.0f);
    uint8_t b = (uint8_t)(pColor->float32[2] * 255.0f);
    uint8_t a = (uint8_t)(pColor->float32[3] * 255.0f);
    uint32_t argb = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                    ((uint32_t)g << 8)  | (uint32_t)b;

    uint32_t *dst = (uint32_t *)pixels;
    for (uint32_t i = 0; i < pixelCount; i++)
        dst[i] = argb;
}

void swvk_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                                       VkImage image, VkImageLayout imageLayout,
                                       const VkClearDepthStencilValue *pDepthStencil,
                                       uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
    (void)commandBuffer; (void)imageLayout; (void)rangeCount; (void)pRanges;

    if (image == VK_NULL_HANDLE || !pDepthStencil)
        return;

    SWVKImage *img = (SWVKImage *)(uintptr_t)image;
    if (!img->boundMemory || !img->boundMemory->data)
        return;

    float *depth = (float *)((uint8_t *)img->boundMemory->data + img->boundOffset);
    uint32_t pixelCount = img->width * img->height;

    for (uint32_t i = 0; i < pixelCount; i++)
        depth[i] = pDepthStencil->depth;
}

void swvk_CmdClearAttachments(VkCommandBuffer commandBuffer,
                                 uint32_t attachmentCount,
                                 const VkClearAttachment *pAttachments,
                                 uint32_t rectCount, const VkClearRect *pRects)
{
    (void)commandBuffer; (void)attachmentCount; (void)pAttachments;
    (void)rectCount; (void)pRects;
    /* No-op: would require access to current render targets which are
    ** tracked in render state during execution, not during recording */
}

/****************************************************************************/
/* Blit/resolve (simplified memcpy -- no scaling/MSAA in SW)                 */
/****************************************************************************/

void swvk_CmdBlitImage(VkCommandBuffer commandBuffer,
                          VkImage srcImage, VkImageLayout srcImageLayout,
                          VkImage dstImage, VkImageLayout dstImageLayout,
                          uint32_t regionCount, const VkImageBlit *pRegions,
                          VkFilter filter)
{
    (void)commandBuffer; (void)srcImageLayout; (void)dstImageLayout;
    (void)regionCount; (void)pRegions; (void)filter;

    if (srcImage == VK_NULL_HANDLE || dstImage == VK_NULL_HANDLE)
        return;

    SWVKImage *src = (SWVKImage *)(uintptr_t)srcImage;
    SWVKImage *dst = (SWVKImage *)(uintptr_t)dstImage;

    if (!src->boundMemory || !src->boundMemory->data ||
        !dst->boundMemory || !dst->boundMemory->data)
        return;

    /* Simplified: copy min of both image sizes */
    VkDeviceSize copySize = src->memorySize < dst->memorySize
                          ? src->memorySize : dst->memorySize;
    memcpy((uint8_t *)dst->boundMemory->data + dst->boundOffset,
           (uint8_t *)src->boundMemory->data + src->boundOffset,
           (size_t)copySize);
}

void swvk_CmdResolveImage(VkCommandBuffer commandBuffer,
                             VkImage srcImage, VkImageLayout srcImageLayout,
                             VkImage dstImage, VkImageLayout dstImageLayout,
                             uint32_t regionCount, const VkImageResolve *pRegions)
{
    (void)commandBuffer; (void)srcImageLayout; (void)dstImageLayout;
    (void)regionCount; (void)pRegions;

    if (srcImage == VK_NULL_HANDLE || dstImage == VK_NULL_HANDLE)
        return;

    SWVKImage *src = (SWVKImage *)(uintptr_t)srcImage;
    SWVKImage *dst = (SWVKImage *)(uintptr_t)dstImage;

    if (!src->boundMemory || !src->boundMemory->data ||
        !dst->boundMemory || !dst->boundMemory->data)
        return;

    /* No MSAA in SW -- just copy */
    VkDeviceSize copySize = src->memorySize < dst->memorySize
                          ? src->memorySize : dst->memorySize;
    memcpy((uint8_t *)dst->boundMemory->data + dst->boundOffset,
           (uint8_t *)src->boundMemory->data + src->boundOffset,
           (size_t)copySize);
}

/****************************************************************************/
/* Vulkan 1.3 "2" command variants (wrap v1 implementations)                 */
/****************************************************************************/

void swvk_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                            const VkCopyBufferInfo2 *pCopyBufferInfo)
{
    if (!pCopyBufferInfo) return;

    for (uint32_t i = 0; i < pCopyBufferInfo->regionCount; i++)
    {
        VkBufferCopy region;
        region.srcOffset = pCopyBufferInfo->pRegions[i].srcOffset;
        region.dstOffset = pCopyBufferInfo->pRegions[i].dstOffset;
        region.size      = pCopyBufferInfo->pRegions[i].size;
        swvk_CmdCopyBuffer(commandBuffer, pCopyBufferInfo->srcBuffer,
                           pCopyBufferInfo->dstBuffer, 1, &region);
    }
}

void swvk_CmdCopyImage2(VkCommandBuffer commandBuffer,
                           const VkCopyImageInfo2 *pCopyImageInfo)
{
    if (!pCopyImageInfo) return;

    for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++)
    {
        VkImageCopy region;
        region.srcSubresource = pCopyImageInfo->pRegions[i].srcSubresource;
        region.srcOffset      = pCopyImageInfo->pRegions[i].srcOffset;
        region.dstSubresource = pCopyImageInfo->pRegions[i].dstSubresource;
        region.dstOffset      = pCopyImageInfo->pRegions[i].dstOffset;
        region.extent         = pCopyImageInfo->pRegions[i].extent;
        swvk_CmdCopyImage(commandBuffer, pCopyImageInfo->srcImage,
                          pCopyImageInfo->srcImageLayout,
                          pCopyImageInfo->dstImage,
                          pCopyImageInfo->dstImageLayout, 1, &region);
    }
}

void swvk_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                                   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
    if (!pCopyBufferToImageInfo) return;

    for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; i++)
    {
        VkBufferImageCopy region;
        region.bufferOffset      = pCopyBufferToImageInfo->pRegions[i].bufferOffset;
        region.bufferRowLength   = pCopyBufferToImageInfo->pRegions[i].bufferRowLength;
        region.bufferImageHeight = pCopyBufferToImageInfo->pRegions[i].bufferImageHeight;
        region.imageSubresource  = pCopyBufferToImageInfo->pRegions[i].imageSubresource;
        region.imageOffset       = pCopyBufferToImageInfo->pRegions[i].imageOffset;
        region.imageExtent       = pCopyBufferToImageInfo->pRegions[i].imageExtent;
        swvk_CmdCopyBufferToImage(commandBuffer,
                                  pCopyBufferToImageInfo->srcBuffer,
                                  pCopyBufferToImageInfo->dstImage,
                                  pCopyBufferToImageInfo->dstImageLayout,
                                  1, &region);
    }
}

void swvk_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                                   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
    if (!pCopyImageToBufferInfo) return;

    for (uint32_t i = 0; i < pCopyImageToBufferInfo->regionCount; i++)
    {
        VkBufferImageCopy region;
        region.bufferOffset      = pCopyImageToBufferInfo->pRegions[i].bufferOffset;
        region.bufferRowLength   = pCopyImageToBufferInfo->pRegions[i].bufferRowLength;
        region.bufferImageHeight = pCopyImageToBufferInfo->pRegions[i].bufferImageHeight;
        region.imageSubresource  = pCopyImageToBufferInfo->pRegions[i].imageSubresource;
        region.imageOffset       = pCopyImageToBufferInfo->pRegions[i].imageOffset;
        region.imageExtent       = pCopyImageToBufferInfo->pRegions[i].imageExtent;
        swvk_CmdCopyImageToBuffer(commandBuffer,
                                  pCopyImageToBufferInfo->srcImage,
                                  pCopyImageToBufferInfo->srcImageLayout,
                                  pCopyImageToBufferInfo->dstBuffer,
                                  1, &region);
    }
}

void swvk_CmdBlitImage2(VkCommandBuffer commandBuffer,
                           const VkBlitImageInfo2 *pBlitImageInfo)
{
    if (!pBlitImageInfo) return;

    /* Simplified: just do a whole-image blit via the v1 path */
    swvk_CmdBlitImage(commandBuffer,
                      pBlitImageInfo->srcImage, pBlitImageInfo->srcImageLayout,
                      pBlitImageInfo->dstImage, pBlitImageInfo->dstImageLayout,
                      0, NULL, pBlitImageInfo->filter);
}

void swvk_CmdResolveImage2(VkCommandBuffer commandBuffer,
                              const VkResolveImageInfo2 *pResolveImageInfo)
{
    if (!pResolveImageInfo) return;

    swvk_CmdResolveImage(commandBuffer,
                         pResolveImageInfo->srcImage, pResolveImageInfo->srcImageLayout,
                         pResolveImageInfo->dstImage, pResolveImageInfo->dstImageLayout,
                         0, NULL);
}

/****************************************************************************/
/* Indirect draw command recording                                           */
/****************************************************************************/

void swvk_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                            VkBuffer buffer, VkDeviceSize offset,
                            uint32_t drawCount, uint32_t stride)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_DRAW_INDIRECT);
    if (c)
    {
        c->drawIndirect.buffer    = (SWVKBuffer *)(uintptr_t)buffer;
        c->drawIndirect.offset    = offset;
        c->drawIndirect.drawCount = drawCount;
        c->drawIndirect.stride    = stride;
    }
}

void swvk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                                    VkBuffer buffer, VkDeviceSize offset,
                                    uint32_t drawCount, uint32_t stride)
{
    SWVKCommandBuffer *cmd = (SWVKCommandBuffer *)commandBuffer;
    SWVKCommand *c = swvk_CmdAppend(cmd, SWVK_CMD_DRAW_INDEXED_INDIRECT);
    if (c)
    {
        c->drawIndirect.buffer    = (SWVKBuffer *)(uintptr_t)buffer;
        c->drawIndirect.offset    = offset;
        c->drawIndirect.drawCount = drawCount;
        c->drawIndirect.stride    = stride;
    }
}

void swvk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                                  VkBuffer buffer, VkDeviceSize offset,
                                  VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                  uint32_t maxDrawCount, uint32_t stride)
{
    (void)countBuffer; (void)countBufferOffset;
    /* Simplified: ignore count buffer, use maxDrawCount */
    swvk_CmdDrawIndirect(commandBuffer, buffer, offset, maxDrawCount, stride);
}

void swvk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                                         VkBuffer buffer, VkDeviceSize offset,
                                         VkBuffer countBuffer, VkDeviceSize countBufferOffset,
                                         uint32_t maxDrawCount, uint32_t stride)
{
    (void)countBuffer; (void)countBufferOffset;
    /* Simplified: ignore count buffer, use maxDrawCount */
    swvk_CmdDrawIndexedIndirect(commandBuffer, buffer, offset, maxDrawCount, stride);
}
