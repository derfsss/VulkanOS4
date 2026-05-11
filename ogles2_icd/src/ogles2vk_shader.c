/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_shader.c -- Shader module, pipeline layout, pipeline cache,
**                   and graphics pipeline management
**
** CPU-side metadata and SPIR-V storage. SPIR-V bytecode is stored at
** vkCreateShaderModule time. Actual GPU shader compilation happens
** lazily at first draw when an OGLES2 context is available.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "ogles2vk_internal.h"

/* OGLES2 interface (defined in ogles2vk_main.c) */
struct OGLES2IFace;
extern struct OGLES2IFace *IOGLES2;

/****************************************************************************/
/* SPIR-V constants                                                         */
/****************************************************************************/

#define SPV_MAGIC_LE 0x07230203  /* Little-endian SPIR-V magic */
#define SPV_MAGIC_BE 0x03022307  /* Big-endian (byte-swapped) magic */

/****************************************************************************/
/* Shader module creation                                                   */
/****************************************************************************/

VkResult ogles2vk_CreateShaderModule(VkDevice device,
                                   const VkShaderModuleCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkShaderModule *pShaderModule)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pShaderModule)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* SPIR-V header is 5 words (20 bytes) minimum; size must be word-aligned */
    if (!pCreateInfo->pCode || pCreateInfo->codeSize < 20 ||
        (pCreateInfo->codeSize % 4) != 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t wordCount = (uint32_t)(pCreateInfo->codeSize / 4);

    /* Validate SPIR-V magic number (accept both LE and BE byte order) */
    uint32_t magic = pCreateInfo->pCode[0];
    if (magic != SPV_MAGIC_LE && magic != SPV_MAGIC_BE)
    {
        D(("[ogles2_vk] Invalid SPIR-V magic: 0x%08lx\n",
                           (unsigned long)magic));
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Allocate shader module struct */
    OGLES2VKShaderModule *mod = (OGLES2VKShaderModule *)IExec->AllocVecTags(
        sizeof(OGLES2VKShaderModule),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mod)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Allocate original code buffer (LE SPIR-V for SPIRV-Cross + glShaderBinary) */
    mod->codeOrig = (uint32_t *)IExec->AllocVecTags(
        pCreateInfo->codeSize,
        AVT_Type, MEMF_SHARED,
        TAG_DONE);

    if (!mod->codeOrig)
    {
        IExec->FreeVec(mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* Store original bytes (as-is from the application -- LE SPIR-V) */
    memcpy(mod->codeOrig, pCreateInfo->pCode, pCreateInfo->codeSize);

    mod->wordCount = wordCount;
    mod->codeSize  = (uint32_t)pCreateInfo->codeSize;
    mod->glShader = 0;  /* Compiled lazily */

    *pShaderModule = (VkShaderModule)(uintptr_t)mod;

    D(("[ogles2_vk] Shader module created (%lu words, magic=0x%02x%02x%02x%02x)\n",
                       (unsigned long)wordCount,
                       ((uint8_t *)mod->codeOrig)[0], ((uint8_t *)mod->codeOrig)[1],
                       ((uint8_t *)mod->codeOrig)[2], ((uint8_t *)mod->codeOrig)[3]));

    return VK_SUCCESS;
}

void ogles2vk_DestroyShaderModule(VkDevice device,
                                VkShaderModule shaderModule,
                                const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (shaderModule == VK_NULL_HANDLE)
        return;

    OGLES2VKShaderModule *mod = (OGLES2VKShaderModule *)(uintptr_t)shaderModule;

    /* Delete GL shader object if compiled */
    if (mod->glShader && IOGLES2)
    {
        /* Import the full OGLES2IFace definition from ogles2vk_exec.c */
        extern void ogles2vk_DeleteShader(uint32_t shader);
        ogles2vk_DeleteShader(mod->glShader);
        mod->glShader = 0;
    }

    if (mod->codeOrig)
        IExec->FreeVec(mod->codeOrig);
    IExec->FreeVec(mod);
}

/****************************************************************************/
/* Pipeline layout                                                          */
/****************************************************************************/

VkResult ogles2vk_CreatePipelineLayout(VkDevice device,
                                     const VkPipelineLayoutCreateInfo *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkPipelineLayout *pPipelineLayout)
{
    (void)device;
    (void)pAllocator;

    if (!pCreateInfo || !pPipelineLayout)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKPipelineLayout *layout = (OGLES2VKPipelineLayout *)IExec->AllocVecTags(
        sizeof(OGLES2VKPipelineLayout),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    /* Store push constant ranges */
    layout->pushConstantRangeCount = pCreateInfo->pushConstantRangeCount;
    if (layout->pushConstantRangeCount > OGLES2VK_MAX_PUSH_CONSTANT_RANGES)
        layout->pushConstantRangeCount = OGLES2VK_MAX_PUSH_CONSTANT_RANGES;

    if (pCreateInfo->pPushConstantRanges && layout->pushConstantRangeCount > 0)
    {
        for (uint32_t i = 0; i < layout->pushConstantRangeCount; i++)
            layout->pushConstantRanges[i] = pCreateInfo->pPushConstantRanges[i];
    }

    /* Store descriptor set layout references */
    layout->setLayoutCount = pCreateInfo->setLayoutCount;
    if (layout->setLayoutCount > OGLES2VK_MAX_DESCRIPTOR_SETS)
        layout->setLayoutCount = OGLES2VK_MAX_DESCRIPTOR_SETS;

    if (pCreateInfo->pSetLayouts && layout->setLayoutCount > 0)
    {
        for (uint32_t i = 0; i < layout->setLayoutCount; i++)
            layout->setLayouts[i] = (void *)(uintptr_t)pCreateInfo->pSetLayouts[i];
    }

    *pPipelineLayout = (VkPipelineLayout)(uintptr_t)layout;

    return VK_SUCCESS;
}

void ogles2vk_DestroyPipelineLayout(VkDevice device,
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
/* Pipeline cache (no-op)                                                   */
/****************************************************************************/

VkResult ogles2vk_CreatePipelineCache(VkDevice device,
                                    const VkPipelineCacheCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkPipelineCache *pPipelineCache)
{
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (!pPipelineCache)
        return VK_ERROR_INITIALIZATION_FAILED;

    OGLES2VKPipelineCache *cache = (OGLES2VKPipelineCache *)IExec->AllocVecTags(
        sizeof(OGLES2VKPipelineCache),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!cache)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    *pPipelineCache = (VkPipelineCache)(uintptr_t)cache;

    return VK_SUCCESS;
}

void ogles2vk_DestroyPipelineCache(VkDevice device,
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
/* Graphics pipeline creation                                               */
/****************************************************************************/

VkResult ogles2vk_CreateGraphicsPipelines(VkDevice device,
                                        VkPipelineCache pipelineCache,
                                        uint32_t createInfoCount,
                                        const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                        const VkAllocationCallbacks *pAllocator,
                                        VkPipeline *pPipelines)
{
    (void)device;
    (void)pipelineCache;
    (void)pAllocator;

    if (!pCreateInfos || !pPipelines || createInfoCount == 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < createInfoCount; i++)
    {
        const VkGraphicsPipelineCreateInfo *ci = &pCreateInfos[i];

        OGLES2VKPipeline *pipe = (OGLES2VKPipeline *)IExec->AllocVecTags(
            sizeof(OGLES2VKPipeline),
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
        if (ci->pStages)
        for (uint32_t s = 0; s < ci->stageCount; s++)
        {
            const VkPipelineShaderStageCreateInfo *stage = &ci->pStages[s];
            OGLES2VKShaderModule *mod = (OGLES2VKShaderModule *)(uintptr_t)stage->module;

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
            if (pipe->vertexBindingCount > OGLES2VK_MAX_VERTEX_BINDINGS)
                pipe->vertexBindingCount = OGLES2VK_MAX_VERTEX_BINDINGS;
            for (uint32_t b = 0; b < pipe->vertexBindingCount; b++)
                pipe->vertexBindings[b] = vis->pVertexBindingDescriptions[b];

            pipe->vertexAttributeCount = vis->vertexAttributeDescriptionCount;
            if (pipe->vertexAttributeCount > OGLES2VK_MAX_VERTEX_ATTRIBUTES)
                pipe->vertexAttributeCount = OGLES2VK_MAX_VERTEX_ATTRIBUTES;
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
        if (ci->pColorBlendState && ci->pColorBlendState->attachmentCount > 0
            && ci->pColorBlendState->pAttachments)
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
        pipe->layout = (OGLES2VKPipelineLayout *)(uintptr_t)ci->layout;

        /* GL shader program -- compiled lazily at first draw */
        pipe->glProgram = 0;

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
                pNext = pri->pNext;
            }
        }

        pPipelines[i] = (VkPipeline)(uintptr_t)pipe;

        D(("[ogles2_vk] Pipeline created (vert=%s frag=%s)\n",
                           pipe->vertShader ? "yes" : "no",
                           pipe->fragShader ? "yes" : "no"));
    }

    return VK_SUCCESS;
}

void ogles2vk_DestroyPipeline(VkDevice device,
                            VkPipeline pipeline,
                            const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;

    if (pipeline == VK_NULL_HANDLE)
        return;

    (void)device;
    OGLES2VKPipeline *pipe = (OGLES2VKPipeline *)(uintptr_t)pipeline;

    /* Delete GL program if linked */
    if (pipe->glProgram && IOGLES2)
    {
        extern void ogles2vk_DeleteProgram(uint32_t program);
        ogles2vk_DeleteProgram(pipe->glProgram);
        pipe->glProgram = 0;
    }

    IExec->FreeVec(pipe);
}

/****************************************************************************/
/* 100% API coverage: Compute pipeline stub                                 */
/****************************************************************************/

VkResult ogles2vk_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
    uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos,
    const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    (void)device; (void)pipelineCache; (void)createInfoCount;
    (void)pCreateInfos; (void)pAllocator; (void)pPipelines;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/****************************************************************************/
/* 100% API coverage: Pipeline cache data                                   */
/****************************************************************************/

VkResult ogles2vk_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
    size_t *pDataSize, void *pData)
{
    (void)device; (void)pipelineCache; (void)pData;
    if (pDataSize) *pDataSize = 0;
    return VK_SUCCESS;
}

VkResult ogles2vk_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
    uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches)
{
    (void)device; (void)dstCache; (void)srcCacheCount; (void)pSrcCaches;
    return VK_SUCCESS;
}

/****************************************************************************/
/* 100% API coverage: Render pass + framebuffer                             */
/****************************************************************************/

VkResult ogles2vk_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKRenderPass *rp = (OGLES2VKRenderPass *)IExec->AllocVecTags(sizeof(OGLES2VKRenderPass),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->attachmentCount = pCreateInfo->attachmentCount;
    if (rp->attachmentCount > OGLES2VK_MAX_ATTACHMENTS) rp->attachmentCount = OGLES2VK_MAX_ATTACHMENTS;
    if (pCreateInfo->pAttachments)
        for (uint32_t i = 0; i < rp->attachmentCount; i++)
            rp->attachments[i] = pCreateInfo->pAttachments[i];
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses) {
        const VkSubpassDescription *sp = &pCreateInfo->pSubpasses[0];
        rp->colorAttachmentCount = sp->colorAttachmentCount;
        if (rp->colorAttachmentCount > OGLES2VK_MAX_ATTACHMENTS) rp->colorAttachmentCount = OGLES2VK_MAX_ATTACHMENTS;
        if (sp->pColorAttachments)
            for (uint32_t i = 0; i < rp->colorAttachmentCount; i++)
                rp->colorAttachments[i] = sp->pColorAttachments[i];
        if (sp->pDepthStencilAttachment && sp->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED) {
            rp->depthAttachment = *sp->pDepthStencilAttachment;
            rp->hasDepth = VK_TRUE;
        }
    }
    *pRenderPass = (VkRenderPass)(uintptr_t)rp;
    return VK_SUCCESS;
}

void ogles2vk_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (renderPass != VK_NULL_HANDLE) IExec->FreeVec((APTR)(uintptr_t)renderPass);
}

VkResult ogles2vk_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pFramebuffer) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKFramebuffer *fb = (OGLES2VKFramebuffer *)IExec->AllocVecTags(sizeof(OGLES2VKFramebuffer),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!fb) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fb->width = pCreateInfo->width;
    fb->height = pCreateInfo->height;
    fb->attachmentCount = pCreateInfo->attachmentCount;
    if (fb->attachmentCount > OGLES2VK_MAX_ATTACHMENTS) fb->attachmentCount = OGLES2VK_MAX_ATTACHMENTS;
    if (pCreateInfo->pAttachments)
        for (uint32_t i = 0; i < fb->attachmentCount; i++)
            fb->attachments[i] = (OGLES2VKImageView *)(uintptr_t)pCreateInfo->pAttachments[i];
    *pFramebuffer = (VkFramebuffer)(uintptr_t)fb;
    return VK_SUCCESS;
}

void ogles2vk_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator)
{
    (void)device; (void)pAllocator;
    if (framebuffer != VK_NULL_HANDLE) IExec->FreeVec((APTR)(uintptr_t)framebuffer);
}

/****************************************************************************/
/* 100% API coverage: Render pass 2                                         */
/****************************************************************************/

VkResult ogles2vk_CreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    (void)pAllocator; (void)device;
    if (!pCreateInfo || !pRenderPass) return VK_ERROR_INITIALIZATION_FAILED;
    OGLES2VKRenderPass *rp = (OGLES2VKRenderPass *)IExec->AllocVecTags(sizeof(OGLES2VKRenderPass),
        AVT_Type, MEMF_SHARED, AVT_ClearWithValue, 0, TAG_DONE);
    if (!rp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    rp->attachmentCount = pCreateInfo->attachmentCount;
    if (rp->attachmentCount > OGLES2VK_MAX_ATTACHMENTS) rp->attachmentCount = OGLES2VK_MAX_ATTACHMENTS;
    if (pCreateInfo->pAttachments)
    for (uint32_t i = 0; i < rp->attachmentCount; i++) {
        const VkAttachmentDescription2 *a2 = &pCreateInfo->pAttachments[i];
        rp->attachments[i].flags = a2->flags;
        rp->attachments[i].format = a2->format;
        rp->attachments[i].samples = a2->samples;
        rp->attachments[i].loadOp = a2->loadOp;
        rp->attachments[i].storeOp = a2->storeOp;
        rp->attachments[i].stencilLoadOp = a2->stencilLoadOp;
        rp->attachments[i].stencilStoreOp = a2->stencilStoreOp;
        rp->attachments[i].initialLayout = a2->initialLayout;
        rp->attachments[i].finalLayout = a2->finalLayout;
    }
    if (pCreateInfo->subpassCount > 0 && pCreateInfo->pSubpasses) {
        const VkSubpassDescription2 *sp = &pCreateInfo->pSubpasses[0];
        rp->colorAttachmentCount = sp->colorAttachmentCount;
        if (rp->colorAttachmentCount > OGLES2VK_MAX_ATTACHMENTS) rp->colorAttachmentCount = OGLES2VK_MAX_ATTACHMENTS;
        if (sp->pColorAttachments)
            for (uint32_t i = 0; i < rp->colorAttachmentCount; i++) {
                rp->colorAttachments[i].attachment = sp->pColorAttachments[i].attachment;
                rp->colorAttachments[i].layout = sp->pColorAttachments[i].layout;
            }
        if (sp->pDepthStencilAttachment && sp->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED) {
            rp->depthAttachment.attachment = sp->pDepthStencilAttachment->attachment;
            rp->depthAttachment.layout = sp->pDepthStencilAttachment->layout;
            rp->hasDepth = VK_TRUE;
        }
    }
    *pRenderPass = (VkRenderPass)(uintptr_t)rp;
    return VK_SUCCESS;
}
