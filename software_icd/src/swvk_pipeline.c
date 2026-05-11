/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_pipeline.c -- Shader modules, pipeline layout/cache, graphics pipelines
**
** Shader module loading (byte-swap, validate, parse SPIR-V),
** pipeline layout, pipeline cache, graphics pipeline creation.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "swvk_internal.h"
#include "swvk_spirv.h"

/****************************************************************************/
/* Shader module creation                                                   */
/****************************************************************************/

VkResult swvk_CreateShaderModule(VkDevice device,
                                 const VkShaderModuleCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkShaderModule *pShaderModule)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo->pCode || pCreateInfo->codeSize < 20)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* codeSize is in bytes, must be a multiple of 4 */
    if (pCreateInfo->codeSize % 4 != 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t wordCount = (uint32_t)(pCreateInfo->codeSize / 4);

    /* Allocate shader module */
    SWVKShaderModule *mod = (SWVKShaderModule *)IExec->AllocVecTags(
        sizeof(SWVKShaderModule),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mod)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Allocate code buffer */
    mod->code = (uint32_t *)IExec->AllocVecTags(
        wordCount * 4,
        AVT_Type, MEMF_SHARED,
        TAG_DONE);

    if (!mod->code)
    {
        IExec->FreeVec(mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* Detect SPIR-V endianness from the magic number and swap only when
    ** needed. Per the Vulkan spec, the magic word may be supplied in either
    ** endianness — the driver must accept both. The previous unconditional
    ** byte-swap was wrong for callers that already produce host-byte-order
    ** SPIR-V (e.g. uint32_t array literals compiled on a big-endian host
    ** like ImGui's __glsl_shader_*_spv on PowerPC). Reading the first word
    ** as raw bytes gives us a byte-order-independent way to decide. */
    {
        const unsigned char *firstBytes = (const unsigned char *)pCreateInfo->pCode;
        /* Big-endian magic: 07 23 02 03   Little-endian magic: 03 02 23 07 */
        int needSwap;
        if (firstBytes[0] == 0x03 && firstBytes[1] == 0x02 &&
            firstBytes[2] == 0x23 && firstBytes[3] == 0x07) {
            needSwap = 1;   /* SPIR-V stream is LE, host is BE — swap */
        } else if (firstBytes[0] == 0x07 && firstBytes[1] == 0x23 &&
                   firstBytes[2] == 0x02 && firstBytes[3] == 0x03) {
            needSwap = 0;   /* SPIR-V stream is already BE — copy verbatim */
        } else {
            D(("[software_vk] Invalid SPIR-V magic bytes: "
                               "%02x %02x %02x %02x\n",
                               firstBytes[0], firstBytes[1],
                               firstBytes[2], firstBytes[3]));
            IExec->FreeVec(mod->code);
            IExec->FreeVec(mod);
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        if (needSwap) {
            for (uint32_t i = 0; i < wordCount; i++)
                mod->code[i] = __builtin_bswap32(pCreateInfo->pCode[i]);
        } else {
            for (uint32_t i = 0; i < wordCount; i++)
                mod->code[i] = pCreateInfo->pCode[i];
        }
    }

    mod->wordCount = wordCount;

    /* Allocate and parse the SPIR-V module structure */
    mod->parsed = (SpvModule *)IExec->AllocVecTags(
        sizeof(SpvModule),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mod->parsed)
    {
        IExec->FreeVec(mod->code);
        IExec->FreeVec(mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    mod->parsed->code      = mod->code;
    mod->parsed->wordCount = mod->wordCount;

    int parseResult = spv_ParseModule(mod->parsed, mod->code, mod->wordCount);
    if (parseResult != 0)
    {
        D(("[software_vk] SPIR-V parse failed\n"));
        IExec->FreeVec(mod->parsed);
        IExec->FreeVec(mod->code);
        IExec->FreeVec(mod);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Pre-compile the parsed module for faster execution.
    ** Non-fatal if compilation fails -- falls back to interpreter. */
    if (spv_CompileProgram(mod->parsed) != 0)
        D(("[software_vk] SPIR-V pre-compile skipped (non-fatal)\n"));

    *pShaderModule = (VkShaderModule)(uintptr_t)mod;

    D(("[software_vk] Shader module created: %lu words\n",
                       (unsigned long)wordCount));

    return VK_SUCCESS;
}

/****************************************************************************/
/* Shader module destruction                                                */
/****************************************************************************/

void swvk_DestroyShaderModule(VkDevice device,
                              VkShaderModule shaderModule,
                              const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (shaderModule == VK_NULL_HANDLE)
        return;

    SWVKShaderModule *mod = (SWVKShaderModule *)(uintptr_t)shaderModule;

    if (mod->parsed)
    {
        /* Free compiled program and template before the module */
        if (mod->parsed->compiled)
            spv_FreeCompiled(mod->parsed->compiled);
        spv_FreeTemplate(mod->parsed);
        IExec->FreeVec(mod->parsed);
    }
    if (mod->code)
        IExec->FreeVec(mod->code);

    IExec->FreeVec(mod);
}

/****************************************************************************/
/* Pipeline layout                                                          */
/****************************************************************************/

VkResult swvk_CreatePipelineLayout(VkDevice device,
                                   const VkPipelineLayoutCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkPipelineLayout *pPipelineLayout)
{
    (void)device;
    (void)pAllocator;

    SWVKPipelineLayout *layout = (SWVKPipelineLayout *)IExec->AllocVecTags(
        sizeof(SWVKPipelineLayout),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Copy push constant ranges */
    if (pCreateInfo->pPushConstantRanges && pCreateInfo->pushConstantRangeCount > 0)
    {
        layout->pushConstantRangeCount = pCreateInfo->pushConstantRangeCount;
        if (layout->pushConstantRangeCount > SWVK_MAX_PUSH_CONSTANT_RANGES)
            layout->pushConstantRangeCount = SWVK_MAX_PUSH_CONSTANT_RANGES;
        for (uint32_t i = 0; i < layout->pushConstantRangeCount; i++)
            layout->pushConstantRanges[i] = pCreateInfo->pPushConstantRanges[i];
    }

    /* Copy descriptor set layout references */
    if (pCreateInfo->pSetLayouts && pCreateInfo->setLayoutCount > 0)
    {
        layout->setLayoutCount = pCreateInfo->setLayoutCount;
        if (layout->setLayoutCount > SWVK_MAX_DESCRIPTOR_SETS)
            layout->setLayoutCount = SWVK_MAX_DESCRIPTOR_SETS;
        for (uint32_t i = 0; i < layout->setLayoutCount; i++)
            layout->setLayouts[i] = (SWVKDescriptorSetLayout *)(uintptr_t)pCreateInfo->pSetLayouts[i];
    }

    *pPipelineLayout = (VkPipelineLayout)(uintptr_t)layout;
    return VK_SUCCESS;
}

void swvk_DestroyPipelineLayout(VkDevice device,
                                VkPipelineLayout pipelineLayout,
                                const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (pipelineLayout == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)pipelineLayout);
}

/****************************************************************************/
/* Pipeline cache                                                           */
/* No-op -- no caching.                                                     */
/****************************************************************************/

VkResult swvk_CreatePipelineCache(VkDevice device,
                                  const VkPipelineCacheCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkPipelineCache *pPipelineCache)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    SWVKPipelineCache *cache = (SWVKPipelineCache *)IExec->AllocVecTags(
        sizeof(SWVKPipelineCache),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!cache)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pPipelineCache = (VkPipelineCache)(uintptr_t)cache;
    return VK_SUCCESS;
}

void swvk_DestroyPipelineCache(VkDevice device,
                               VkPipelineCache pipelineCache,
                               const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (pipelineCache == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)pipelineCache);
}

/****************************************************************************/
/* Pipeline cache data retrieval / merge                                    */
/* No-op -- cache is always empty.                                          */
/****************************************************************************/

VkResult swvk_GetPipelineCacheData(VkDevice device,
                                    VkPipelineCache pipelineCache,
                                    size_t *pDataSize,
                                    void *pData)
{
    (void)device; (void)pipelineCache; (void)pData;
    if (pDataSize) *pDataSize = 0;
    return VK_SUCCESS;
}

VkResult swvk_MergePipelineCaches(VkDevice device,
                                   VkPipelineCache dstCache,
                                   uint32_t srcCacheCount,
                                   const VkPipelineCache *pSrcCaches)
{
    (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;
    return VK_SUCCESS;
}

/****************************************************************************/
/* Render pass creation / destruction                                       */
/****************************************************************************/

VkResult swvk_CreateRenderPass(VkDevice device,
                                const VkRenderPassCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkRenderPass *pRenderPass)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pRenderPass)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKRenderPass *rp = (SWVKRenderPass *)IExec->AllocVecTags(
        sizeof(SWVKRenderPass),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!rp)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Copy attachment descriptions */
    rp->attachmentCount = pCreateInfo->attachmentCount;
    if (rp->attachmentCount > SWVK_MAX_ATTACHMENTS)
        rp->attachmentCount = SWVK_MAX_ATTACHMENTS;
    if (pCreateInfo->pAttachments)
    {
        for (uint32_t i = 0; i < rp->attachmentCount; i++)
            rp->attachments[i] = pCreateInfo->pAttachments[i];
    }

    /* Extract first subpass (single subpass only) */
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses)
    {
        const VkSubpassDescription *sp = &pCreateInfo->pSubpasses[0];

        /* Color attachments */
        rp->colorAttachmentCount = sp->colorAttachmentCount;
        if (rp->colorAttachmentCount > SWVK_MAX_ATTACHMENTS)
            rp->colorAttachmentCount = SWVK_MAX_ATTACHMENTS;
        if (sp->pColorAttachments)
        {
            for (uint32_t i = 0; i < rp->colorAttachmentCount; i++)
                rp->colorAttachments[i] = sp->pColorAttachments[i];
        }

        /* Depth attachment */
        if (sp->pDepthStencilAttachment &&
            sp->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
        {
            rp->depthAttachment = *sp->pDepthStencilAttachment;
            rp->hasDepth = VK_TRUE;
        }
    }

    *pRenderPass = (VkRenderPass)(uintptr_t)rp;

    D(("[software_vk] Render pass created: %lu attachments\n",
                       (unsigned long)rp->attachmentCount));

    return VK_SUCCESS;
}

void swvk_DestroyRenderPass(VkDevice device,
                             VkRenderPass renderPass,
                             const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (renderPass == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)renderPass);
}

/****************************************************************************/
/* Framebuffer creation / destruction                                       */
/****************************************************************************/

VkResult swvk_CreateFramebuffer(VkDevice device,
                                 const VkFramebufferCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkFramebuffer *pFramebuffer)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pFramebuffer)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKFramebuffer *fb = (SWVKFramebuffer *)IExec->AllocVecTags(
        sizeof(SWVKFramebuffer),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!fb)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    fb->width  = pCreateInfo->width;
    fb->height = pCreateInfo->height;

    fb->attachmentCount = pCreateInfo->attachmentCount;
    if (fb->attachmentCount > SWVK_MAX_ATTACHMENTS)
        fb->attachmentCount = SWVK_MAX_ATTACHMENTS;

    if (pCreateInfo->pAttachments)
    {
        for (uint32_t i = 0; i < fb->attachmentCount; i++)
            fb->attachments[i] = (SWVKImageView *)(uintptr_t)pCreateInfo->pAttachments[i];
    }

    *pFramebuffer = (VkFramebuffer)(uintptr_t)fb;

    D(("[software_vk] Framebuffer created: %lux%lu, %lu attachments\n",
                       (unsigned long)fb->width, (unsigned long)fb->height,
                       (unsigned long)fb->attachmentCount));

    return VK_SUCCESS;
}

void swvk_DestroyFramebuffer(VkDevice device,
                              VkFramebuffer framebuffer,
                              const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (framebuffer == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)framebuffer);
}

/****************************************************************************/
/* Graphics pipeline creation                                               */
/* Extracts shader modules and pipeline state from the create info.         */
/****************************************************************************/

VkResult swvk_CreateGraphicsPipelines(VkDevice device,
                                      VkPipelineCache pipelineCache,
                                      uint32_t createInfoCount,
                                      const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                      const VkAllocationCallbacks *pAllocator,
                                      VkPipeline *pPipelines)
{
    (void)device;
    (void)pipelineCache;
    (void)pAllocator;

    for (uint32_t i = 0; i < createInfoCount; i++)
    {
        const VkGraphicsPipelineCreateInfo *ci = &pCreateInfos[i];

        SWVKPipeline *pipe = (SWVKPipeline *)IExec->AllocVecTags(
            sizeof(SWVKPipeline),
            AVT_Type, MEMF_SHARED,
            AVT_ClearWithValue, 0,
            TAG_DONE);

        if (!pipe)
        {
            /* Clean up already-created pipelines in this batch */
            for (uint32_t j = 0; j < i; j++)
            {
                if (pPipelines[j] != VK_NULL_HANDLE)
                    IExec->FreeVec((APTR)(uintptr_t)pPipelines[j]);
                pPipelines[j] = VK_NULL_HANDLE;
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        /* Extract vertex and fragment shader modules from stages */
        for (uint32_t s = 0; s < ci->stageCount; s++)
        {
            const VkPipelineShaderStageCreateInfo *stage = &ci->pStages[s];
            SWVKShaderModule *mod = (SWVKShaderModule *)(uintptr_t)stage->module;

            if (stage->stage == VK_SHADER_STAGE_VERTEX_BIT)
                pipe->vertShader = mod;
            else if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT)
                pipe->fragShader = mod;
        }

        /* Vertex input state */
        if (ci->pVertexInputState)
        {
            const VkPipelineVertexInputStateCreateInfo *vis = ci->pVertexInputState;

            pipe->vertexBindingCount = vis->vertexBindingDescriptionCount;
            if (pipe->vertexBindingCount > SWVK_MAX_VERTEX_BINDINGS)
                pipe->vertexBindingCount = SWVK_MAX_VERTEX_BINDINGS;
            for (uint32_t b = 0; b < pipe->vertexBindingCount; b++)
                pipe->vertexBindings[b] = vis->pVertexBindingDescriptions[b];

            pipe->vertexAttributeCount = vis->vertexAttributeDescriptionCount;
            if (pipe->vertexAttributeCount > SWVK_MAX_VERTEX_ATTRIBUTES)
                pipe->vertexAttributeCount = SWVK_MAX_VERTEX_ATTRIBUTES;
            for (uint32_t a = 0; a < pipe->vertexAttributeCount; a++)
                pipe->vertexAttributes[a] = vis->pVertexAttributeDescriptions[a];
        }

        /* Input assembly */
        if (ci->pInputAssemblyState)
            pipe->topology = ci->pInputAssemblyState->topology;
        else
            pipe->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        /* Rasterisation */
        if (ci->pRasterizationState)
        {
            pipe->cullMode    = ci->pRasterizationState->cullMode;
            pipe->frontFace   = ci->pRasterizationState->frontFace;
            pipe->polygonMode = ci->pRasterizationState->polygonMode;
            pipe->lineWidth   = ci->pRasterizationState->lineWidth;
        }
        else
        {
            pipe->cullMode    = VK_CULL_MODE_NONE;
            pipe->frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pipe->polygonMode = VK_POLYGON_MODE_FILL;
            pipe->lineWidth   = 1.0f;
        }

        /* Color blend */
        if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount > 0)
            pipe->blendEnable = ci->pColorBlendState->pAttachments[0].blendEnable;
        else
            pipe->blendEnable = VK_FALSE;

        /* Depth/stencil state */
        if (ci->pDepthStencilState)
        {
            pipe->depthTestEnable  = ci->pDepthStencilState->depthTestEnable;
            pipe->depthWriteEnable = ci->pDepthStencilState->depthWriteEnable;
            pipe->depthCompareOp   = ci->pDepthStencilState->depthCompareOp;
        }
        else
        {
            pipe->depthTestEnable  = VK_FALSE;
            pipe->depthWriteEnable = VK_FALSE;
            pipe->depthCompareOp   = VK_COMPARE_OP_LESS;
        }

        /* Pipeline layout */
        pipe->layout = (SWVKPipelineLayout *)(uintptr_t)ci->layout;

        /* Vulkan 1.3 dynamic rendering format (walk pNext chain) */
        pipe->colorAttachmentFormat = VK_FORMAT_B8G8R8A8_UNORM;
        pipe->depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        {
            const void *pNext = ci->pNext;
            while (pNext)
            {
                const VkPipelineRenderingCreateInfo *pri =
                    (const VkPipelineRenderingCreateInfo *)pNext;
                if (pri->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO)
                {
                    if (pri->colorAttachmentCount > 0 && pri->pColorAttachmentFormats)
                        pipe->colorAttachmentFormat = pri->pColorAttachmentFormats[0];
                    pipe->depthAttachmentFormat = pri->depthAttachmentFormat;
                    break;
                }
                /* All Vulkan structures have sType at offset 0 and pNext at offset 4 */
                pNext = pri->pNext;
            }
        }

        pPipelines[i] = (VkPipeline)(uintptr_t)pipe;

        D(("[software_vk] Graphics pipeline created (vert=%s frag=%s)\n",
                           pipe->vertShader ? "yes" : "no",
                           pipe->fragShader ? "yes" : "no"));
    }

    return VK_SUCCESS;
}

void swvk_DestroyPipeline(VkDevice device,
                          VkPipeline pipeline,
                          const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;

    if (pipeline == VK_NULL_HANDLE)
        return;

    IExec->FreeVec((APTR)(uintptr_t)pipeline);
}

/****************************************************************************/
/* Compute pipeline stub                                                    */
/****************************************************************************/

VkResult swvk_CreateComputePipelines(VkDevice device,
                                       VkPipelineCache pipelineCache,
                                       uint32_t createInfoCount,
                                       const VkComputePipelineCreateInfo *pCreateInfos,
                                       const VkAllocationCallbacks *pAllocator,
                                       VkPipeline *pPipelines)
{
    (void)device; (void)pipelineCache; (void)createInfoCount;
    (void)pCreateInfos; (void)pAllocator; (void)pPipelines;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/****************************************************************************/
/* Render pass 2                                                            */
/* Extract data from VkRenderPassCreateInfo2 and reuse v1 logic.            */
/****************************************************************************/

VkResult swvk_CreateRenderPass2(VkDevice device,
                                  const VkRenderPassCreateInfo2 *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkRenderPass *pRenderPass)
{
    (void)pAllocator;

    if (!pCreateInfo || !pRenderPass)
        return VK_ERROR_INITIALIZATION_FAILED;

    SWVKRenderPass *rp = (SWVKRenderPass *)IExec->AllocVecTags(
        sizeof(SWVKRenderPass),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!rp)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Copy attachment descriptions from "2" structs */
    rp->attachmentCount = pCreateInfo->attachmentCount;
    if (rp->attachmentCount > SWVK_MAX_ATTACHMENTS)
        rp->attachmentCount = SWVK_MAX_ATTACHMENTS;
    if (pCreateInfo->pAttachments)
    for (uint32_t i = 0; i < rp->attachmentCount; i++)
    {
        const VkAttachmentDescription2 *a2 = &pCreateInfo->pAttachments[i];
        rp->attachments[i].flags          = a2->flags;
        rp->attachments[i].format         = a2->format;
        rp->attachments[i].samples        = a2->samples;
        rp->attachments[i].loadOp         = a2->loadOp;
        rp->attachments[i].storeOp        = a2->storeOp;
        rp->attachments[i].stencilLoadOp  = a2->stencilLoadOp;
        rp->attachments[i].stencilStoreOp = a2->stencilStoreOp;
        rp->attachments[i].initialLayout  = a2->initialLayout;
        rp->attachments[i].finalLayout    = a2->finalLayout;
    }

    /* Extract first subpass (single subpass only) */
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses)
    {
        const VkSubpassDescription2 *sp = &pCreateInfo->pSubpasses[0];

        rp->colorAttachmentCount = sp->colorAttachmentCount;
        if (rp->colorAttachmentCount > SWVK_MAX_ATTACHMENTS)
            rp->colorAttachmentCount = SWVK_MAX_ATTACHMENTS;
        if (sp->pColorAttachments)
        {
            for (uint32_t i = 0; i < rp->colorAttachmentCount; i++)
            {
                rp->colorAttachments[i].attachment = sp->pColorAttachments[i].attachment;
                rp->colorAttachments[i].layout     = sp->pColorAttachments[i].layout;
            }
        }

        if (sp->pDepthStencilAttachment &&
            sp->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
        {
            rp->depthAttachment.attachment = sp->pDepthStencilAttachment->attachment;
            rp->depthAttachment.layout     = sp->pDepthStencilAttachment->layout;
            rp->hasDepth = VK_TRUE;
        }
    }

    *pRenderPass = (VkRenderPass)(uintptr_t)rp;

    D(("[software_vk] Render pass 2 created: %lu attachments\n",
                       (unsigned long)rp->attachmentCount));

    (void)device;
    return VK_SUCCESS;
}
