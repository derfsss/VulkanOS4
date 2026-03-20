/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_draw.c -- vkQueueSubmit, command execution, rasterisation
**
** This is the core rendering file. vkQueueSubmit walks recorded command
** buffers and executes each command. Draw commands run the SPIR-V vertex
** shader, rasterise triangles using edge functions, interpolate varyings
** with barycentric coordinates, run the fragment shader, and write ARGB32
** pixels into the target image.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>

#include "swvk_internal.h"
#include "swvk_spirv.h"

/****************************************************************************/
/* Rendering state (threaded through command execution)                     */
/****************************************************************************/

typedef struct SWVKRenderState
{
    SWVKPipeline   *pipeline;
    VkViewport      viewport;
    VkRect2D        scissor;
    SWVKImageView  *colorTarget;
    VkClearValue    clearValue;
    VkRect2D        renderArea;
    VkAttachmentLoadOp loadOp;
    uint32_t        inRenderPass;

    /* Depth buffer */
    SWVKImageView  *depthTarget;

    /* Push constants */
    uint8_t         pushConstants[SWVK_MAX_PUSH_CONSTANT_SIZE];
    uint32_t        pushConstantSize;

    /* Vertex buffer bindings */
    SWVKBuffer     *vertexBuffers[SWVK_MAX_VERTEX_BINDINGS];
    VkDeviceSize    vertexOffsets[SWVK_MAX_VERTEX_BINDINGS];

    /* Index buffer */
    SWVKBuffer     *indexBuffer;
    VkDeviceSize    indexOffset;
    VkIndexType     indexType;

    /* Descriptor sets */
    SWVKDescriptorSet *descriptorSets[SWVK_MAX_DESCRIPTOR_SETS];

    /* Dynamic state overrides */
    VkBool32        hasDynCullMode;
    VkCullModeFlags dynCullMode;
    VkBool32        hasDynFrontFace;
    VkFrontFace     dynFrontFace;
    VkBool32        hasDynTopology;
    VkPrimitiveTopology dynTopology;
    VkBool32        hasDynDepthTestEnable;
    VkBool32        dynDepthTestEnable;
    VkBool32        hasDynDepthWriteEnable;
    VkBool32        dynDepthWriteEnable;
    VkBool32        hasDynDepthCompareOp;
    VkCompareOp     dynDepthCompareOp;

    /* Cached shader state allocations (reused across draw calls) */
    SpvExecState   *cachedVertState;
    SpvExecState   *cachedFragState;
} SWVKRenderState;

/****************************************************************************/
/* Dynamic state helpers                                                    */
/* Return the effective value: dynamic override if set, else pipeline.      */
/****************************************************************************/

static VkBool32 swvk_GetDepthTestEnable(SWVKRenderState *rs)
{
    if (rs->hasDynDepthTestEnable)  return rs->dynDepthTestEnable;
    if (rs->pipeline)               return rs->pipeline->depthTestEnable;
    return VK_FALSE;
}

static VkBool32 swvk_GetDepthWriteEnable(SWVKRenderState *rs)
{
    if (rs->hasDynDepthWriteEnable) return rs->dynDepthWriteEnable;
    if (rs->pipeline)               return rs->pipeline->depthWriteEnable;
    return VK_FALSE;
}

static VkCompareOp swvk_GetDepthCompareOp(SWVKRenderState *rs)
{
    if (rs->hasDynDepthCompareOp) return rs->dynDepthCompareOp;
    if (rs->pipeline)             return rs->pipeline->depthCompareOp;
    return VK_COMPARE_OP_LESS;
}

static VkPrimitiveTopology swvk_GetTopology(SWVKRenderState *rs)
{
    if (rs->hasDynTopology) return rs->dynTopology;
    if (rs->pipeline)       return rs->pipeline->topology;
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

/****************************************************************************/
/* Per-vertex output from vertex shader                                     */
/****************************************************************************/

typedef struct SWVKVertexOutput
{
    float position[4];                          /* gl_Position (clip space) */
    float screenX, screenY, screenZ;            /* After viewport transform */
    float varyings[SPV_MAX_IO_LOCATIONS][4];    /* Outputs by location */
    uint32_t varyingCounts[SPV_MAX_IO_LOCATIONS];
} SWVKVertexOutput;

/****************************************************************************/
/* Framebuffer clear                                                        */
/****************************************************************************/

static void swvk_ClearFramebuffer(SWVKImageView *target, const VkClearValue *clearValue,
                                  const VkRect2D *area)
{
    if (!target || !target->image || !target->image->boundMemory)
        return;

    SWVKImage *img = target->image;
    VkDeviceSize requiredSize = (VkDeviceSize)img->width * img->height * sizeof(uint32_t);
    if (img->boundOffset + requiredSize > img->boundMemory->size)
        return;

    uint32_t *pixels = (uint32_t *)((uint8_t *)img->boundMemory->data + img->boundOffset);
    uint32_t w = img->width;

    /* Convert clear color to ARGB32 */
    float r = clearValue->color.float32[0];
    float g = clearValue->color.float32[1];
    float b = clearValue->color.float32[2];
    float a = clearValue->color.float32[3];

    /* Clamp to [0,1] */
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    uint32_t clearPixel = ((uint32_t)(a * 255.0f) << 24) |
                          ((uint32_t)(r * 255.0f) << 16) |
                          ((uint32_t)(g * 255.0f) << 8) |
                          ((uint32_t)(b * 255.0f));

    int32_t sx0 = area->offset.x;
    int32_t sy0 = area->offset.y;
    int32_t sx1 = sx0 + (int32_t)area->extent.width;
    int32_t sy1 = sy0 + (int32_t)area->extent.height;

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > (int32_t)img->width)  sx1 = (int32_t)img->width;
    if (sy1 > (int32_t)img->height) sy1 = (int32_t)img->height;

    for (int32_t y = sy0; y < sy1; y++)
        for (int32_t x = sx0; x < sx1; x++)
            pixels[y * w + x] = clearPixel;
}

/****************************************************************************/
/* Depth buffer clear                                                       */
/****************************************************************************/

static void swvk_ClearDepthBuffer(SWVKImageView *target, float clearDepth,
                                  const VkRect2D *area)
{
    if (!target || !target->image || !target->image->boundMemory)
        return;

    SWVKImage *img = target->image;
    VkDeviceSize requiredSize = (VkDeviceSize)img->width * img->height * sizeof(float);
    if (img->boundOffset + requiredSize > img->boundMemory->size)
        return;

    float *depthBuf = (float *)((uint8_t *)img->boundMemory->data + img->boundOffset);
    uint32_t w = img->width;

    int32_t sx0 = area->offset.x;
    int32_t sy0 = area->offset.y;
    int32_t sx1 = sx0 + (int32_t)area->extent.width;
    int32_t sy1 = sy0 + (int32_t)area->extent.height;

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > (int32_t)img->width)  sx1 = (int32_t)img->width;
    if (sy1 > (int32_t)img->height) sy1 = (int32_t)img->height;

    for (int32_t y = sy0; y < sy1; y++)
        for (int32_t x = sx0; x < sx1; x++)
            depthBuf[y * w + x] = clearDepth;
}

/****************************************************************************/
/* Descriptor set -> shader uniform setup                                   */
/****************************************************************************/

static void swvk_SetupUniformData(SpvExecState *state, SWVKRenderState *rs)
{
    if (!rs || !state)
        return;

    /* Walk bound descriptor sets and populate uniformData/uniformSizes
    ** for each set*bindings combination. The SPIR-V interpreter uses
    ** set and binding decorations to index into this array. */
    for (uint32_t s = 0; s < SWVK_MAX_DESCRIPTOR_SETS; s++)
    {
        SWVKDescriptorSet *set = rs->descriptorSets[s];
        if (!set)
            continue;

        for (uint32_t b = 0; b < set->bindingCount && b < SWVK_MAX_DESCRIPTOR_BINDINGS; b++)
        {
            SWVKDescriptorBinding *binding = &set->bindings[b];

            /* Uniform buffer descriptors */
            if (binding->buffer && binding->buffer->boundMemory &&
                binding->buffer->boundMemory->data)
            {
                uint32_t idx = s * SWVK_MAX_DESCRIPTOR_BINDINGS + b;
                if (idx >= SPV_MAX_UNIFORM_BINDINGS)
                    continue;

                /* Validate offset fits within allocation */
                VkDeviceSize totalOff = binding->buffer->boundOffset + binding->offset;
                if (totalOff >= binding->buffer->boundMemory->size)
                    continue;

                uint8_t *base = (uint8_t *)binding->buffer->boundMemory->data + totalOff;

                /* Compute actual range (VK_WHOLE_SIZE = ~0ULL) */
                VkDeviceSize bufSize = binding->buffer->boundMemory->size - totalOff;
                VkDeviceSize range = binding->range;
                if (range == (VkDeviceSize)~0ULL || range > bufSize)
                    range = bufSize;

                state->uniformData[idx]  = base;
                state->uniformSizes[idx] = (uint32_t)range;
            }

            /* Combined image sampler descriptors */
            if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                binding->imageView && binding->imageView->image &&
                binding->imageView->image->boundMemory &&
                binding->sampler)
            {
                SWVKImage *img = binding->imageView->image;

                /* Validate texture data fits within bound memory */
                VkDeviceSize texSize = (VkDeviceSize)img->width * img->height * 4;
                if (img->boundOffset + texSize > img->boundMemory->size)
                    continue;

                uint32_t texSlot = s * SWVK_MAX_DESCRIPTOR_BINDINGS + b;
                if (texSlot < SPV_MAX_TEXTURES)
                {
                    state->textures[texSlot].data =
                        (const uint8_t *)img->boundMemory->data + img->boundOffset;
                    state->textures[texSlot].width = img->width;
                    state->textures[texSlot].height = img->height;
                    state->textures[texSlot].bytesPerPixel = 4;
                    state->textures[texSlot].format = (uint32_t)img->format;
                    state->textures[texSlot].magFilter =
                        (uint32_t)binding->sampler->magFilter;
                    state->textures[texSlot].addressModeU =
                        (uint32_t)binding->sampler->addressModeU;
                    state->textures[texSlot].addressModeV =
                        (uint32_t)binding->sampler->addressModeV;
                    state->textures[texSlot].isSet = 1;
                }
            }
        }
    }
}

/****************************************************************************/
/* Vertex shader execution                                                  */
/****************************************************************************/

/* Read a float from a vertex buffer at a given byte offset.
** Handles bounds checking against the buffer's bound memory. */
static float swvk_ReadVertexFloat(SWVKBuffer *buf, VkDeviceSize baseOffset, uint32_t byteOff)
{
    if (!buf || !buf->boundMemory || !buf->boundMemory->data)
        return 0.0f;

    VkDeviceSize addr = buf->boundOffset + baseOffset + byteOff;
    if (addr + 4 > buf->boundMemory->size)
        return 0.0f;

    float val;
    memcpy(&val, (uint8_t *)buf->boundMemory->data + addr, 4);
    return val;
}

/* Run vertex shader using a pre-allocated SpvExecState (reused per draw).
** The caller provides a zeroed state; this function re-initializes
** the module and built-in fields for each invocation. */
static int swvk_RunVertexShader(SWVKPipeline *pipeline, uint32_t vertexIndex,
                                SWVKVertexOutput *out, SpvExecState *state,
                                SWVKRenderState *rs)
{
    if (!pipeline->vertShader || !pipeline->vertShader->parsed || !state)
        return -1;

    /* Reset state for this invocation.
    ** If a pre-built template is available, memcpy it instead of memset.
    ** The template includes zero-init plus pre-filled constants, pointer
    ** state, and output variable initialisation -- replacing memset +
    ** the constant/pointer copy loop in spv_SetupIO. */
    SpvModule *mod = pipeline->vertShader->parsed;
    uint32_t bound = mod->bound < SPV_MAX_IDS ? mod->bound : SPV_MAX_IDS;
    if (mod->templateBuilt && mod->templateValues && mod->templatePtrs)
    {
        memcpy(state->values, mod->templateValues, mod->templateBound * sizeof(SpvValue));
        memcpy(state->ptrs,   mod->templatePtrs,   mod->templateBound * sizeof(SpvPointer));
    }
    else
    {
        memset(state->values, 0, bound * sizeof(SpvValue));
        memset(state->ptrs, 0, bound * sizeof(SpvPointer));
    }
    memset(state->outputs, 0, sizeof(state->outputs));
    memset(state->outputCounts, 0, sizeof(state->outputCounts));
    memset(state->position, 0, sizeof(state->position));
    state->jumpTarget = 0;

    state->module       = mod;
    state->vertexIndex  = vertexIndex;
    state->instanceIndex = 0;

    /* Copy push constants into execution state */
    if (rs && rs->pushConstantSize > 0)
    {
        uint32_t pcCopy = rs->pushConstantSize;
        if (pcCopy > sizeof(state->pushConstants)) pcCopy = sizeof(state->pushConstants);
        memcpy(state->pushConstants, rs->pushConstants, pcCopy);
        state->pushConstantSize = rs->pushConstantSize;
    }
    else
    {
        state->pushConstantSize = 0;
    }

    /* Set up uniform/texture data only if descriptors are actually bound.
    ** Skip the memset+setup entirely for simple shaders without descriptors. */
    if (rs && (rs->descriptorSets[0] || rs->descriptorSets[1] ||
               rs->descriptorSets[2] || rs->descriptorSets[3]))
    {
        memset(state->uniformData, 0, sizeof(state->uniformData));
        memset(state->uniformSizes, 0, sizeof(state->uniformSizes));
        memset(state->textures, 0, sizeof(state->textures));
        swvk_SetupUniformData(state, rs);
    }

    /* Set up vertex attribute inputs from bound vertex buffers */
    if (rs && pipeline->vertexAttributeCount > 0)
    {
        for (uint32_t a = 0; a < pipeline->vertexAttributeCount; a++)
        {
            const VkVertexInputAttributeDescription *attr = &pipeline->vertexAttributes[a];
            uint32_t binding = attr->binding;
            uint32_t loc     = attr->location;

            if (binding >= SWVK_MAX_VERTEX_BINDINGS || loc >= SPV_MAX_IO_LOCATIONS)
                continue;

            SWVKBuffer *buf = rs->vertexBuffers[binding];
            if (!buf)
                continue;

            /* Find stride for this binding */
            uint32_t stride = 0;
            for (uint32_t b = 0; b < pipeline->vertexBindingCount; b++)
            {
                if (pipeline->vertexBindings[b].binding == binding)
                {
                    stride = pipeline->vertexBindings[b].stride;
                    break;
                }
            }

            VkDeviceSize vertBase = rs->vertexOffsets[binding] + (VkDeviceSize)stride * vertexIndex + attr->offset;

            /* Determine component count from format (common formats) */
            uint32_t nComponents = 0;
            switch (attr->format)
            {
                case VK_FORMAT_R32_SFLOAT:           nComponents = 1; break;
                case VK_FORMAT_R32G32_SFLOAT:        nComponents = 2; break;
                case VK_FORMAT_R32G32B32_SFLOAT:     nComponents = 3; break;
                case VK_FORMAT_R32G32B32A32_SFLOAT:  nComponents = 4; break;
                default:                              nComponents = 4; break;
            }

            for (uint32_t j = 0; j < nComponents; j++)
                state->inputs[loc][j] = swvk_ReadVertexFloat(buf, vertBase, j * 4);
            state->inputCounts[loc] = nComponents;
        }
    }

    /* Use pre-compiled program if available, else interpreter */
    int result = (state->module->compiled)
               ? spv_ExecuteCompiled(state)
               : spv_Execute(state);

    if (result == 0)
    {
        /* Copy outputs */
        out->position[0] = state->position[0];
        out->position[1] = state->position[1];
        out->position[2] = state->position[2];
        out->position[3] = state->position[3];

        for (uint32_t loc = 0; loc < SPV_MAX_IO_LOCATIONS; loc++)
        {
            out->varyingCounts[loc] = state->outputCounts[loc];
            for (uint32_t j = 0; j < 4; j++)
                out->varyings[loc][j] = state->outputs[loc][j];
        }
    }

    return result;
}

/****************************************************************************/
/* Perspective divide and viewport transform                                */
/****************************************************************************/

static void swvk_ViewportTransform(SWVKVertexOutput *v, const VkViewport *vp)
{
    /* Perspective divide: clip -> NDC */
    float w = v->position[3];
    if (w == 0.0f) w = 1.0f;

    float ndcX = v->position[0] / w;
    float ndcY = v->position[1] / w;
    float ndcZ = v->position[2] / w;

    /* Viewport transform: NDC [-1,1] -> screen pixels
    ** OpenGL convention (matches GPU ICD / OGLES2): Y=-1 at bottom,
    ** Y=+1 at top. This ensures software and GPU ICDs produce
    ** identical output for the same projection matrices. */
    v->screenX = vp->x + (ndcX + 1.0f) * 0.5f * vp->width;
    v->screenY = vp->y + (1.0f - ndcY) * 0.5f * vp->height;
    v->screenZ = vp->minDepth + ndcZ * (vp->maxDepth - vp->minDepth);
}

/****************************************************************************/
/* Fragment shader execution                                                */
/****************************************************************************/

/* Run fragment shader using a pre-allocated SpvExecState (reused per draw).
** The caller must provide a zeroed state; this function re-initializes
** the module and I/O fields for each invocation. */
static void swvk_RunFragmentShader(SWVKPipeline *pipeline,
                                   SpvExecState *state,
                                   const float inputs[SPV_MAX_IO_LOCATIONS][4],
                                   const uint32_t inputCounts[SPV_MAX_IO_LOCATIONS],
                                   float outColor[4],
                                   SWVKRenderState *rs)
{
    /* Default output if no fragment shader */
    outColor[0] = 1.0f;
    outColor[1] = 1.0f;
    outColor[2] = 1.0f;
    outColor[3] = 1.0f;

    if (!pipeline->fragShader || !pipeline->fragShader->parsed || !state)
        return;

    /* Reset state for this invocation.
    ** If a pre-built template is available, memcpy it instead of memset.
    ** The template includes zero-init plus pre-filled constants, pointer
    ** state, and output variable initialisation. */
    SpvModule *fmod = pipeline->fragShader->parsed;
    uint32_t fbound = fmod->bound < SPV_MAX_IDS ? fmod->bound : SPV_MAX_IDS;
    if (fmod->templateBuilt && fmod->templateValues && fmod->templatePtrs)
    {
        memcpy(state->values, fmod->templateValues, fmod->templateBound * sizeof(SpvValue));
        memcpy(state->ptrs,   fmod->templatePtrs,   fmod->templateBound * sizeof(SpvPointer));
    }
    else
    {
        memset(state->values, 0, fbound * sizeof(SpvValue));
        memset(state->ptrs, 0, fbound * sizeof(SpvPointer));
    }
    memset(state->outputs, 0, sizeof(state->outputs));
    memset(state->outputCounts, 0, sizeof(state->outputCounts));
    state->module = fmod;
    state->jumpTarget = 0;

    /* Copy push constants into execution state */
    if (rs && rs->pushConstantSize > 0)
    {
        uint32_t pcCopy = rs->pushConstantSize;
        if (pcCopy > sizeof(state->pushConstants)) pcCopy = sizeof(state->pushConstants);
        memcpy(state->pushConstants, rs->pushConstants, pcCopy);
        state->pushConstantSize = rs->pushConstantSize;
    }
    else
    {
        state->pushConstantSize = 0;
    }

    /* Set up uniform/texture data only if descriptors are actually bound.
    ** Skip the memset+setup entirely for simple shaders without descriptors. */
    if (rs && (rs->descriptorSets[0] || rs->descriptorSets[1] ||
               rs->descriptorSets[2] || rs->descriptorSets[3]))
    {
        memset(state->uniformData, 0, sizeof(state->uniformData));
        memset(state->uniformSizes, 0, sizeof(state->uniformSizes));
        memset(state->textures, 0, sizeof(state->textures));
        swvk_SetupUniformData(state, rs);
    }

    /* Set fragment shader inputs (from interpolated vertex outputs) */
    for (uint32_t loc = 0; loc < SPV_MAX_IO_LOCATIONS; loc++)
    {
        state->inputCounts[loc] = inputCounts[loc];
        for (uint32_t j = 0; j < 4; j++)
            state->inputs[loc][j] = inputs[loc][j];
    }

    /* Use pre-compiled program if available, else interpreter */
    int fragResult = (state->module->compiled)
                   ? spv_ExecuteCompiled(state)
                   : spv_Execute(state);
    if (fragResult == 0)
    {
        /* Read output location 0 as the fragment color */
        for (uint32_t j = 0; j < 4; j++)
            outColor[j] = state->outputs[0][j];
    }
}

/****************************************************************************/
/* Triangle rasterisation (edge function method)                            */
/* Optimised with incremental edge functions, hoisted depth state,          */
/* reduced memset, fast ARGB conversion.                                    */
/****************************************************************************/

static float swvk_EdgeFunction(float ax, float ay, float bx, float by,
                               float cx, float cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

/* Fast float-to-byte clamp (0.0-1.0 -> 0-255) */
static inline uint32_t swvk_FloatToByte(float f)
{
    if (f <= 0.0f) return 0;
    if (f >= 1.0f) return 255;
    return (uint32_t)(f * 255.0f);
}

static void swvk_RasteriseTriangle(SWVKRenderState *rs,
                                   SWVKVertexOutput *v0,
                                   SWVKVertexOutput *v1,
                                   SWVKVertexOutput *v2,
                                   SpvExecState *fragState)
{
    if (!rs->colorTarget || !rs->colorTarget->image || !rs->colorTarget->image->boundMemory)
        return;

    SWVKImage *img = rs->colorTarget->image;
    uint32_t fbW = img->width;
    uint32_t fbH = img->height;

    /* Validate that pixel data fits within the bound memory */
    VkDeviceSize requiredSize = (VkDeviceSize)fbW * fbH * sizeof(uint32_t);
    if (img->boundOffset + requiredSize > img->boundMemory->size)
        return;

    uint32_t *pixels = (uint32_t *)((uint8_t *)img->boundMemory->data + img->boundOffset);

    /* Compute bounding box */
    float minX = v0->screenX;
    float minY = v0->screenY;
    float maxX = v0->screenX;
    float maxY = v0->screenY;

    if (v1->screenX < minX) minX = v1->screenX;
    if (v2->screenX < minX) minX = v2->screenX;
    if (v1->screenX > maxX) maxX = v1->screenX;
    if (v2->screenX > maxX) maxX = v2->screenX;

    if (v1->screenY < minY) minY = v1->screenY;
    if (v2->screenY < minY) minY = v2->screenY;
    if (v1->screenY > maxY) maxY = v1->screenY;
    if (v2->screenY > maxY) maxY = v2->screenY;

    /* Clamp to framebuffer and scissor */
    int ix0 = (int)minX;
    int iy0 = (int)minY;
    int ix1 = (int)(maxX + 1.0f);
    int iy1 = (int)(maxY + 1.0f);

    if (ix0 < rs->scissor.offset.x) ix0 = rs->scissor.offset.x;
    if (iy0 < rs->scissor.offset.y) iy0 = rs->scissor.offset.y;
    if (ix1 > rs->scissor.offset.x + (int)rs->scissor.extent.width)
        ix1 = rs->scissor.offset.x + (int)rs->scissor.extent.width;
    if (iy1 > rs->scissor.offset.y + (int)rs->scissor.extent.height)
        iy1 = rs->scissor.offset.y + (int)rs->scissor.extent.height;

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > (int)fbW) ix1 = (int)fbW;
    if (iy1 > (int)fbH) iy1 = (int)fbH;

    if (ix0 >= ix1 || iy0 >= iy1)
        return;  /* Bounding box fully clipped */

    /* Total area (2x signed area of triangle) */
    float area = swvk_EdgeFunction(v0->screenX, v0->screenY,
                                   v1->screenX, v1->screenY,
                                   v2->screenX, v2->screenY);
    if (area == 0.0f)
        return;  /* Degenerate triangle */

    float invArea = 1.0f / area;

    /* Pre-compute edge function increments for scanline traversal.
    ** Edge function E(P) = (P.x - A.x)(B.y - A.y) - (P.y - A.y)(B.x - A.x)
    ** dE/dx = (B.y - A.y), dE/dy = -(B.x - A.x)
    ** This avoids recomputing the full edge function per pixel. */
    float e0_dx = v2->screenY - v1->screenY;
    float e0_dy = -(v2->screenX - v1->screenX);
    float e1_dx = v0->screenY - v2->screenY;
    float e1_dy = -(v0->screenX - v2->screenX);
    float e2_dx = v1->screenY - v0->screenY;
    float e2_dy = -(v1->screenX - v0->screenX);

    /* Hoist depth test state outside pixel loop */
    VkBool32 doDepthTest = swvk_GetDepthTestEnable(rs);
    VkBool32 doDepthWrite = swvk_GetDepthWriteEnable(rs);
    VkCompareOp depthOp = swvk_GetDepthCompareOp(rs);
    float *depthBuf = NULL;
    uint32_t dW = 0, dH = 0;

    if (doDepthTest && rs->depthTarget &&
        rs->depthTarget->image && rs->depthTarget->image->boundMemory)
    {
        SWVKImage *dImg = rs->depthTarget->image;
        VkDeviceSize depthRequired = (VkDeviceSize)dImg->width * dImg->height * sizeof(float);
        if (dImg->boundOffset + depthRequired <= dImg->boundMemory->size)
        {
            depthBuf = (float *)((uint8_t *)dImg->boundMemory->data + dImg->boundOffset);
            dW = dImg->width;
            dH = dImg->height;
        }
        else
        {
            doDepthTest = VK_FALSE;
        }
    }

    /* Determine active varying mask (avoid memset for unused locs) */
    uint32_t activeVaryingMask = 0;
    for (uint32_t loc = 0; loc < SPV_MAX_IO_LOCATIONS && loc < 32; loc++)
    {
        if (v0->varyingCounts[loc] > 0)
            activeVaryingMask |= (1u << loc);
    }

    /* Compute initial edge values at top-left corner of bounding box */
    float startPx = (float)ix0 + 0.5f;
    float startPy = (float)iy0 + 0.5f;

    float row_w0 = swvk_EdgeFunction(v1->screenX, v1->screenY,
                                      v2->screenX, v2->screenY,
                                      startPx, startPy);
    float row_w1 = swvk_EdgeFunction(v2->screenX, v2->screenY,
                                      v0->screenX, v0->screenY,
                                      startPx, startPy);
    float row_w2 = swvk_EdgeFunction(v0->screenX, v0->screenY,
                                      v1->screenX, v1->screenY,
                                      startPx, startPy);

    /* Rasterise with incremental edge functions */
    for (int y = iy0; y < iy1; y++)
    {
        float w0 = row_w0;
        float w1 = row_w1;
        float w2 = row_w2;

        for (int x = ix0; x < ix1; x++)
        {
            /* Inside test (handle both CW and CCW winding) */
            if (area > 0.0f)
            {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                    goto next_pixel;
            }
            else
            {
                if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f)
                    goto next_pixel;
            }

            {
                /* Barycentric coordinates */
                float b0 = w0 * invArea;
                float b1 = w1 * invArea;
                float b2 = w2 * invArea;

                /* Depth test (hoisted state) */
                if (doDepthTest && depthBuf)
                {
                    if ((uint32_t)x < dW && (uint32_t)y < dH)
                    {
                        float fragZ = b0 * v0->screenZ + b1 * v1->screenZ + b2 * v2->screenZ;
                        float curDepth = depthBuf[y * dW + x];

                        VkBool32 pass = VK_FALSE;
                        switch (depthOp)
                        {
                            case VK_COMPARE_OP_NEVER:            pass = VK_FALSE; break;
                            case VK_COMPARE_OP_LESS:             pass = (fragZ < curDepth);  break;
                            case VK_COMPARE_OP_EQUAL:            pass = (fragZ == curDepth); break;
                            case VK_COMPARE_OP_LESS_OR_EQUAL:    pass = (fragZ <= curDepth); break;
                            case VK_COMPARE_OP_GREATER:          pass = (fragZ > curDepth);  break;
                            case VK_COMPARE_OP_NOT_EQUAL:        pass = (fragZ != curDepth); break;
                            case VK_COMPARE_OP_GREATER_OR_EQUAL: pass = (fragZ >= curDepth); break;
                            case VK_COMPARE_OP_ALWAYS:           pass = VK_TRUE; break;
                            default:                             pass = VK_TRUE; break;
                        }

                        if (!pass) goto next_pixel;

                        if (doDepthWrite)
                            depthBuf[y * dW + x] = fragZ;
                    }
                }

                /* Interpolate varyings (only active locations) */
                float fragInputs[SPV_MAX_IO_LOCATIONS][4];
                uint32_t fragInputCounts[SPV_MAX_IO_LOCATIONS];
                memset(fragInputCounts, 0, sizeof(fragInputCounts));

                uint32_t mask = activeVaryingMask;
                while (mask)
                {
                    /* Find lowest set bit */
                    uint32_t loc = 0;
                    uint32_t tmp = mask;
                    while ((tmp & 1) == 0) { loc++; tmp >>= 1; }
                    mask &= mask - 1;  /* Clear lowest set bit */

                    uint32_t cc = v0->varyingCounts[loc];
                    if (cc > 4) cc = 4;
                    fragInputCounts[loc] = cc;

                    for (uint32_t j = 0; j < cc; j++)
                    {
                        fragInputs[loc][j] =
                            b0 * v0->varyings[loc][j] +
                            b1 * v1->varyings[loc][j] +
                            b2 * v2->varyings[loc][j];
                    }
                }

                /* Run fragment shader (reuses pre-allocated state) */
                float outColor[4];
                swvk_RunFragmentShader(rs->pipeline, fragState, fragInputs, fragInputCounts, outColor, rs);

                /* OpKill: skip pixel if fragment was discarded */
                if (fragState->killed) { fragState->killed = 0; goto next_pixel; }

                /* Fast ARGB32 conversion with inline clamp */
                uint32_t pixel = (swvk_FloatToByte(outColor[3]) << 24) |
                                 (swvk_FloatToByte(outColor[0]) << 16) |
                                 (swvk_FloatToByte(outColor[1]) << 8) |
                                  swvk_FloatToByte(outColor[2]);

                pixels[y * fbW + x] = pixel;
            }

        next_pixel:
            w0 += e0_dx;
            w1 += e1_dx;
            w2 += e2_dx;
        }

        /* Advance to next row */
        row_w0 += e0_dy;
        row_w1 += e1_dy;
        row_w2 += e2_dy;
    }
}

/****************************************************************************/
/* Line rasterisation (Bresenham's algorithm)                               */
/****************************************************************************/

static void swvk_RasteriseLine(SWVKRenderState *rs,
                                SWVKVertexOutput *v0,
                                SWVKVertexOutput *v1,
                                SpvExecState *fragState)
{
    if (!rs->colorTarget || !rs->colorTarget->image || !rs->colorTarget->image->boundMemory)
        return;

    SWVKImage *img = rs->colorTarget->image;
    uint32_t fbW = img->width;
    uint32_t fbH = img->height;

    VkDeviceSize requiredSize = (VkDeviceSize)fbW * fbH * sizeof(uint32_t);
    if (img->boundOffset + requiredSize > img->boundMemory->size)
        return;

    uint32_t *pixels = (uint32_t *)((uint8_t *)img->boundMemory->data + img->boundOffset);

    /* Bresenham's line algorithm */
    int x0 = (int)(v0->screenX + 0.5f);
    int y0 = (int)(v0->screenY + 0.5f);
    int x1 = (int)(v1->screenX + 0.5f);
    int y1 = (int)(v1->screenY + 0.5f);

    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    /* Total step count for interpolation */
    int totalSteps = (dx > dy) ? dx : dy;
    if (totalSteps < 1) totalSteps = 1;
    float invTotal = 1.0f / (float)totalSteps;
    int stepNum = 0;

    /* Scissor bounds */
    int scX0 = rs->scissor.offset.x;
    int scY0 = rs->scissor.offset.y;
    if (scX0 < 0) scX0 = 0;
    if (scY0 < 0) scY0 = 0;
    int scX1 = rs->scissor.offset.x + (int)rs->scissor.extent.width;
    int scY1 = rs->scissor.offset.y + (int)rs->scissor.extent.height;
    if (scX1 > (int)fbW) scX1 = (int)fbW;
    if (scY1 > (int)fbH) scY1 = (int)fbH;

    int cx = x0, cy = y0;
    for (;;)
    {
        if (cx >= scX0 && cx < scX1 && cy >= scY0 && cy < scY1)
        {
            float t = (float)stepNum * invTotal;

            /* Interpolate varyings between endpoints */
            float fragInputs[SPV_MAX_IO_LOCATIONS][4];
            uint32_t fragInputCounts[SPV_MAX_IO_LOCATIONS];
            memset(fragInputs, 0, sizeof(fragInputs));
            memset(fragInputCounts, 0, sizeof(fragInputCounts));

            for (uint32_t loc = 0; loc < SPV_MAX_IO_LOCATIONS; loc++)
            {
                uint32_t cc = v0->varyingCounts[loc];
                if (cc == 0) continue;
                if (cc > 4) cc = 4;
                fragInputCounts[loc] = cc;
                for (uint32_t j = 0; j < cc; j++)
                    fragInputs[loc][j] = v0->varyings[loc][j] * (1.0f - t)
                                       + v1->varyings[loc][j] * t;
            }

            /* Run fragment shader */
            float outColor[4];
            swvk_RunFragmentShader(rs->pipeline, fragState,
                                   fragInputs, fragInputCounts, outColor, rs);

            /* OpKill: skip pixel if fragment was discarded */
            if (fragState->killed) { fragState->killed = 0; continue; }

            /* Clamp and write ARGB32 pixel */
            if (outColor[0] < 0.0f) outColor[0] = 0.0f;
            if (outColor[0] > 1.0f) outColor[0] = 1.0f;
            if (outColor[1] < 0.0f) outColor[1] = 0.0f;
            if (outColor[1] > 1.0f) outColor[1] = 1.0f;
            if (outColor[2] < 0.0f) outColor[2] = 0.0f;
            if (outColor[2] > 1.0f) outColor[2] = 1.0f;
            if (outColor[3] < 0.0f) outColor[3] = 0.0f;
            if (outColor[3] > 1.0f) outColor[3] = 1.0f;

            uint32_t pixel = ((uint32_t)(outColor[3] * 255.0f) << 24) |
                             ((uint32_t)(outColor[0] * 255.0f) << 16) |
                             ((uint32_t)(outColor[1] * 255.0f) << 8) |
                             ((uint32_t)(outColor[2] * 255.0f));
            pixels[cy * fbW + cx] = pixel;
        }

        if (cx == x1 && cy == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
        stepNum++;
    }
}

/****************************************************************************/
/* Draw command execution                                                   */
/****************************************************************************/

static void swvk_ExecuteDraw(SWVKRenderState *rs, const SWVKCommand *cmd)
{
    if (!rs->pipeline)
        return;

    uint32_t vertexCount   = cmd->draw.vertexCount;
    uint32_t firstVertex   = cmd->draw.firstVertex;
    uint32_t instanceCount = cmd->draw.instanceCount;
    (void)instanceCount;  /* single instance only */

    if (vertexCount == 0)
        return;

    /* Guard against integer overflow in allocation size */
    if (vertexCount > 0xFFFFFFFFU / sizeof(SWVKVertexOutput))
    {
        IExec->DebugPrintF("[software_vk] vertexCount overflow: %lu\n",
                           (unsigned long)vertexCount);
        return;
    }

    /* Run vertex shader for each vertex */
    SWVKVertexOutput *verts = (SWVKVertexOutput *)IExec->AllocVecTags(
        vertexCount * sizeof(SWVKVertexOutput),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!verts)
        return;

    /* Reuse cached SpvExecState across draw calls.
    ** Allocate once per command buffer execution, not per draw call.
    ** This eliminates ~160KB of heap alloc/free per draw call. */
    SpvExecState *vertState = NULL;
    if (rs->pipeline->vertShader && rs->pipeline->vertShader->parsed)
    {
        if (!rs->cachedVertState)
        {
            rs->cachedVertState = (SpvExecState *)IExec->AllocVecTags(
                sizeof(SpvExecState),
                AVT_Type, MEMF_SHARED,
                AVT_ClearWithValue, 0,
                TAG_DONE);
        }
        vertState = rs->cachedVertState;
        if (!vertState)
        {
            IExec->DebugPrintF("[software_vk] WARNING: vertState alloc failed\n");
            IExec->FreeVec(verts);
            return;
        }
    }

    for (uint32_t i = 0; i < vertexCount; i++)
    {
        if (swvk_RunVertexShader(rs->pipeline, firstVertex + i, &verts[i], vertState, rs) != 0)
        {
            IExec->DebugPrintF("[software_vk] Vertex shader failed at vertex %lu\n",
                               (unsigned long)(firstVertex + i));
            IExec->FreeVec(verts);
            return;
        }

        /* Perspective divide + viewport transform */
        swvk_ViewportTransform(&verts[i], &rs->viewport);
    }

    /* Reuse cached fragment state */
    SpvExecState *fragState = NULL;
    if (rs->pipeline->fragShader && rs->pipeline->fragShader->parsed)
    {
        if (!rs->cachedFragState)
        {
            rs->cachedFragState = (SpvExecState *)IExec->AllocVecTags(
                sizeof(SpvExecState),
                AVT_Type, MEMF_SHARED,
                AVT_ClearWithValue, 0,
                TAG_DONE);
        }
        fragState = rs->cachedFragState;
        if (!fragState)
            IExec->DebugPrintF("[software_vk] WARNING: fragState alloc failed\n");
    }

    /* Rasterise primitives using effective topology (pipeline or dynamic) */
    VkPrimitiveTopology topo = swvk_GetTopology(rs);
    if (topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    {
        for (uint32_t i = 0; i + 2 < vertexCount; i += 3)
            swvk_RasteriseTriangle(rs, &verts[i], &verts[i + 1], &verts[i + 2], fragState);
    }
    else if (topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
    {
        for (uint32_t i = 0; i + 2 < vertexCount; i++)
        {
            if (i % 2 == 0)
                swvk_RasteriseTriangle(rs, &verts[i], &verts[i + 1], &verts[i + 2], fragState);
            else
                swvk_RasteriseTriangle(rs, &verts[i + 1], &verts[i], &verts[i + 2], fragState);
        }
    }
    else if (topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
    {
        for (uint32_t i = 1; i + 1 < vertexCount; i++)
            swvk_RasteriseTriangle(rs, &verts[0], &verts[i], &verts[i + 1], fragState);
    }
    else if (topo == VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
    {
        for (uint32_t i = 0; i + 1 < vertexCount; i += 2)
            swvk_RasteriseLine(rs, &verts[i], &verts[i + 1], fragState);
    }
    else if (topo == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
    {
        for (uint32_t i = 0; i + 1 < vertexCount; i++)
            swvk_RasteriseLine(rs, &verts[i], &verts[i + 1], fragState);
    }

    /* Don't free cached states -- reused by next draw call.
    ** Freed at end of command buffer execution. */
    IExec->FreeVec(verts);
}

/****************************************************************************/
/* Indexed draw command execution                                           */
/****************************************************************************/

static void swvk_ExecuteDrawIndexed(SWVKRenderState *rs, const SWVKCommand *cmd)
{
    if (!rs->pipeline)
        return;

    uint32_t indexCount    = cmd->drawIndexed.indexCount;
    uint32_t instanceCount = cmd->drawIndexed.instanceCount;
    uint32_t firstIndex    = cmd->drawIndexed.firstIndex;
    int32_t  vertexOffset  = cmd->drawIndexed.vertexOffset;
    (void)instanceCount;  /* single instance only */

    if (indexCount == 0)
        return;

    /* Validate index buffer */
    if (!rs->indexBuffer || !rs->indexBuffer->boundMemory || !rs->indexBuffer->boundMemory->data)
    {
        IExec->DebugPrintF("[software_vk] DrawIndexed: no index buffer bound\n");
        return;
    }

    SWVKBuffer *ibuf = rs->indexBuffer;
    uint8_t *ibufData = (uint8_t *)ibuf->boundMemory->data + ibuf->boundOffset + rs->indexOffset;

    /* Determine index size */
    uint32_t indexSize = (rs->indexType == VK_INDEX_TYPE_UINT16) ? 2 : 4;

    /* Bounds check: ensure all indices fit within the index buffer */
    VkDeviceSize requiredIBSize = (VkDeviceSize)(firstIndex + indexCount) * indexSize;
    if (ibuf->boundOffset + rs->indexOffset + requiredIBSize > ibuf->boundMemory->size)
    {
        IExec->DebugPrintF("[software_vk] DrawIndexed: index buffer overflow\n");
        return;
    }

    /* Guard against integer overflow in allocation size */
    if (indexCount > 0xFFFFFFFFU / sizeof(SWVKVertexOutput))
    {
        IExec->DebugPrintF("[software_vk] DrawIndexed: indexCount overflow: %lu\n",
                           (unsigned long)indexCount);
        return;
    }

    /* Allocate vertex output array (one per index) */
    SWVKVertexOutput *verts = (SWVKVertexOutput *)IExec->AllocVecTags(
        indexCount * sizeof(SWVKVertexOutput),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!verts)
        return;

    /* Reuse cached vertex shader state */
    SpvExecState *vertState = NULL;
    if (rs->pipeline->vertShader && rs->pipeline->vertShader->parsed)
    {
        if (!rs->cachedVertState)
        {
            rs->cachedVertState = (SpvExecState *)IExec->AllocVecTags(
                sizeof(SpvExecState),
                AVT_Type, MEMF_SHARED,
                AVT_ClearWithValue, 0,
                TAG_DONE);
        }
        vertState = rs->cachedVertState;
        if (!vertState)
        {
            IExec->DebugPrintF("[software_vk] WARNING: vertState alloc failed (indexed)\n");
            IExec->FreeVec(verts);
            return;
        }
    }

    /* Run vertex shader for each indexed vertex */
    for (uint32_t i = 0; i < indexCount; i++)
    {
        /* Read vertex index from index buffer */
        uint32_t vertIdx;
        if (rs->indexType == VK_INDEX_TYPE_UINT16)
        {
            uint16_t idx16;
            memcpy(&idx16, ibufData + (firstIndex + i) * 2, 2);
            vertIdx = (uint32_t)idx16;
        }
        else
        {
            memcpy(&vertIdx, ibufData + (firstIndex + i) * 4, 4);
        }

        /* Apply vertex offset (guard against underflow from negative offset) */
        int32_t signedIdx = (int32_t)vertIdx + vertexOffset;
        if (signedIdx < 0) signedIdx = 0;
        vertIdx = (uint32_t)signedIdx;

        if (swvk_RunVertexShader(rs->pipeline, vertIdx, &verts[i], vertState, rs) != 0)
        {
            IExec->DebugPrintF("[software_vk] Vertex shader failed at indexed vertex %lu (idx=%lu)\n",
                               (unsigned long)i, (unsigned long)vertIdx);
            IExec->FreeVec(verts);
            return;
        }

        /* Perspective divide + viewport transform */
        swvk_ViewportTransform(&verts[i], &rs->viewport);
    }

    /* Reuse cached fragment state */
    SpvExecState *fragState = NULL;
    if (rs->pipeline->fragShader && rs->pipeline->fragShader->parsed)
    {
        if (!rs->cachedFragState)
        {
            rs->cachedFragState = (SpvExecState *)IExec->AllocVecTags(
                sizeof(SpvExecState),
                AVT_Type, MEMF_SHARED,
                AVT_ClearWithValue, 0,
                TAG_DONE);
        }
        fragState = rs->cachedFragState;
        if (!fragState)
            IExec->DebugPrintF("[software_vk] WARNING: fragState alloc failed (indexed)\n");
    }

    /* Rasterise primitives using effective topology (pipeline or dynamic) */
    VkPrimitiveTopology topoIdx = swvk_GetTopology(rs);
    if (topoIdx == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    {
        for (uint32_t i = 0; i + 2 < indexCount; i += 3)
            swvk_RasteriseTriangle(rs, &verts[i], &verts[i + 1], &verts[i + 2], fragState);
    }
    else if (topoIdx == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
    {
        for (uint32_t i = 0; i + 2 < indexCount; i++)
        {
            if (i % 2 == 0)
                swvk_RasteriseTriangle(rs, &verts[i], &verts[i + 1], &verts[i + 2], fragState);
            else
                swvk_RasteriseTriangle(rs, &verts[i + 1], &verts[i], &verts[i + 2], fragState);
        }
    }
    else if (topoIdx == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
    {
        for (uint32_t i = 1; i + 1 < indexCount; i++)
            swvk_RasteriseTriangle(rs, &verts[0], &verts[i], &verts[i + 1], fragState);
    }
    else if (topoIdx == VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
    {
        for (uint32_t i = 0; i + 1 < indexCount; i += 2)
            swvk_RasteriseLine(rs, &verts[i], &verts[i + 1], fragState);
    }
    else if (topoIdx == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
    {
        for (uint32_t i = 0; i + 1 < indexCount; i++)
            swvk_RasteriseLine(rs, &verts[i], &verts[i + 1], fragState);
    }

    /* Don't free cached states -- reused by next draw call */
    IExec->FreeVec(verts);
}

/****************************************************************************/
/* Command buffer execution                                                 */
/****************************************************************************/

static void swvk_ExecuteCommandBuffer(SWVKCommandBuffer *cmdbuf)
{
    SWVKRenderState rs;
    memset(&rs, 0, sizeof(rs));

    /* Default viewport/scissor is 0x0 -- application MUST set via
    ** vkCmdSetViewport/vkCmdSetScissor before drawing. */
    rs.viewport.maxDepth = 1.0f;

    for (uint32_t i = 0; i < cmdbuf->commandCount; i++)
    {
        const SWVKCommand *cmd = &cmdbuf->commands[i];

        switch (cmd->type)
        {
        case SWVK_CMD_BIND_PIPELINE:
            rs.pipeline = cmd->bindPipeline.pipeline;
            break;

        case SWVK_CMD_SET_VIEWPORT:
            rs.viewport = cmd->setViewport.viewport;
            break;

        case SWVK_CMD_SET_SCISSOR:
            rs.scissor = cmd->setScissor.scissor;
            break;

        case SWVK_CMD_BEGIN_RENDERING:
            rs.colorTarget  = cmd->beginRendering.colorAttachment;
            rs.clearValue   = cmd->beginRendering.clearValue;
            rs.renderArea   = cmd->beginRendering.renderArea;
            rs.loadOp       = cmd->beginRendering.loadOp;
            rs.inRenderPass = 1;

            if (rs.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                swvk_ClearFramebuffer(rs.colorTarget, &rs.clearValue, &rs.renderArea);

            /* Depth attachment */
            rs.depthTarget = cmd->beginRendering.depthAttachment;
            if (rs.depthTarget && cmd->beginRendering.depthLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            {
                float clearDepth = cmd->beginRendering.depthClearValue.depthStencil.depth;
                swvk_ClearDepthBuffer(rs.depthTarget, clearDepth, &rs.renderArea);
            }
            break;

        case SWVK_CMD_END_RENDERING:
            rs.inRenderPass = 0;
            break;

        case SWVK_CMD_DRAW:
            if (rs.inRenderPass)
                swvk_ExecuteDraw(&rs, cmd);
            break;

        case SWVK_CMD_PUSH_CONSTANTS:
        {
            uint32_t off  = cmd->pushConstants.offset;
            uint32_t size = cmd->pushConstants.size;
            if (off + size <= SWVK_MAX_PUSH_CONSTANT_SIZE)
            {
                memcpy(rs.pushConstants + off, cmd->pushConstants.data + off, size);
                if (off + size > rs.pushConstantSize)
                    rs.pushConstantSize = off + size;
            }
            break;
        }

        case SWVK_CMD_BIND_VERTEX_BUFFERS:
        {
            uint32_t first = cmd->bindVertexBuffers.firstBinding;
            uint32_t count = cmd->bindVertexBuffers.bindingCount;
            for (uint32_t b = 0; b < count && (first + b) < SWVK_MAX_VERTEX_BINDINGS; b++)
            {
                rs.vertexBuffers[first + b] = cmd->bindVertexBuffers.buffers[b];
                rs.vertexOffsets[first + b]  = cmd->bindVertexBuffers.offsets[b];
            }
            break;
        }

        case SWVK_CMD_BIND_INDEX_BUFFER:
            rs.indexBuffer = cmd->bindIndexBuffer.buffer;
            rs.indexOffset = cmd->bindIndexBuffer.offset;
            rs.indexType   = cmd->bindIndexBuffer.indexType;
            break;

        case SWVK_CMD_DRAW_INDEXED:
            if (rs.inRenderPass)
                swvk_ExecuteDrawIndexed(&rs, cmd);
            break;

        case SWVK_CMD_BIND_DESCRIPTOR_SETS:
        {
            uint32_t first = cmd->bindDescriptorSets.firstSet;
            uint32_t count = cmd->bindDescriptorSets.setCount;
            for (uint32_t s = 0; s < count && (first + s) < SWVK_MAX_DESCRIPTOR_SETS; s++)
                rs.descriptorSets[first + s] = cmd->bindDescriptorSets.sets[s];
            break;
        }

        /* Legacy render pass */
        case SWVK_CMD_BEGIN_RENDER_PASS:
        {
            SWVKRenderPass  *rp = cmd->beginRenderPass.renderPass;
            SWVKFramebuffer *fb = cmd->beginRenderPass.framebuffer;
            rs.renderArea   = cmd->beginRenderPass.renderArea;
            rs.inRenderPass = 1;

            /* Set color target from first color attachment */
            if (rp->colorAttachmentCount > 0 &&
                rp->colorAttachments[0].attachment != VK_ATTACHMENT_UNUSED)
            {
                uint32_t attIdx = rp->colorAttachments[0].attachment;
                if (attIdx < fb->attachmentCount)
                    rs.colorTarget = fb->attachments[attIdx];
                if (attIdx < rp->attachmentCount &&
                    rp->attachments[attIdx].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
                    attIdx < cmd->beginRenderPass.clearValueCount)
                {
                    swvk_ClearFramebuffer(rs.colorTarget,
                                          &cmd->beginRenderPass.clearValues[attIdx],
                                          &rs.renderArea);
                }
            }

            /* Set depth target */
            if (rp->hasDepth &&
                rp->depthAttachment.attachment != VK_ATTACHMENT_UNUSED)
            {
                uint32_t dIdx = rp->depthAttachment.attachment;
                if (dIdx < fb->attachmentCount)
                    rs.depthTarget = fb->attachments[dIdx];
                if (dIdx < rp->attachmentCount &&
                    rp->attachments[dIdx].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
                    dIdx < cmd->beginRenderPass.clearValueCount)
                {
                    swvk_ClearDepthBuffer(rs.depthTarget,
                                          cmd->beginRenderPass.clearValues[dIdx].depthStencil.depth,
                                          &rs.renderArea);
                }
            }
            break;
        }

        case SWVK_CMD_END_RENDER_PASS:
            rs.inRenderPass = 0;
            break;

        case SWVK_CMD_NEXT_SUBPASS:
            break; /* Single subpass only */

        case SWVK_CMD_PIPELINE_BARRIER:
            break; /* No-op for software renderer */

        /* Dynamic state */
        case SWVK_CMD_SET_CULL_MODE:
            rs.hasDynCullMode = VK_TRUE;
            rs.dynCullMode    = cmd->setCullMode.cullMode;
            break;

        case SWVK_CMD_SET_FRONT_FACE:
            rs.hasDynFrontFace = VK_TRUE;
            rs.dynFrontFace    = cmd->setFrontFace.frontFace;
            break;

        case SWVK_CMD_SET_PRIMITIVE_TOPOLOGY:
            rs.hasDynTopology = VK_TRUE;
            rs.dynTopology    = cmd->setPrimitiveTopology.topology;
            break;

        case SWVK_CMD_SET_DEPTH_TEST_ENABLE:
            rs.hasDynDepthTestEnable = VK_TRUE;
            rs.dynDepthTestEnable    = cmd->setDepthTestEnable.enable;
            break;

        case SWVK_CMD_SET_DEPTH_WRITE_ENABLE:
            rs.hasDynDepthWriteEnable = VK_TRUE;
            rs.dynDepthWriteEnable    = cmd->setDepthWriteEnable.enable;
            break;

        case SWVK_CMD_SET_DEPTH_COMPARE_OP:
            rs.hasDynDepthCompareOp = VK_TRUE;
            rs.dynDepthCompareOp    = cmd->setDepthCompareOp.compareOp;
            break;

        /* No-op dynamic state commands */
        case SWVK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE:
        case SWVK_CMD_SET_STENCIL_TEST_ENABLE:
        case SWVK_CMD_SET_STENCIL_OP:
        case SWVK_CMD_SET_RASTERIZER_DISCARD_ENABLE:
        case SWVK_CMD_SET_DEPTH_BIAS_ENABLE:
        case SWVK_CMD_SET_PRIMITIVE_RESTART_ENABLE:
            break;

        /* Transfer commands */
        case SWVK_CMD_COPY_BUFFER:
        {
            SWVKBuffer *src = cmd->copyBuffer.src;
            SWVKBuffer *dst = cmd->copyBuffer.dst;
            if (src && src->boundMemory && src->boundMemory->data &&
                dst && dst->boundMemory && dst->boundMemory->data)
            {
                VkDeviceSize srcOff = src->boundOffset + cmd->copyBuffer.srcOffset;
                VkDeviceSize dstOff = dst->boundOffset + cmd->copyBuffer.dstOffset;
                VkDeviceSize cpSize = cmd->copyBuffer.size;

                /* Bounds check */
                if (srcOff + cpSize <= src->boundMemory->size &&
                    dstOff + cpSize <= dst->boundMemory->size)
                {
                    uint8_t *srcData = (uint8_t *)src->boundMemory->data + srcOff;
                    uint8_t *dstData = (uint8_t *)dst->boundMemory->data + dstOff;
                    memcpy(dstData, srcData, (size_t)cpSize);
                }
                else
                {
                    IExec->DebugPrintF("[software_vk] WARNING: CmdCopyBuffer out of bounds\n");
                }
            }
            break;
        }

        case SWVK_CMD_COPY_BUFFER_TO_IMAGE:
        {
            SWVKBuffer *srcBuf = cmd->copyBufferToImage.srcBuffer;
            SWVKImage  *dstImg = cmd->copyBufferToImage.dstImage;
            if (srcBuf && srcBuf->boundMemory && srcBuf->boundMemory->data &&
                dstImg && dstImg->boundMemory && dstImg->boundMemory->data)
            {
                uint8_t *bufBase = (uint8_t *)srcBuf->boundMemory->data +
                                   srcBuf->boundOffset +
                                   cmd->copyBufferToImage.bufferOffset;
                uint8_t *imgBase = (uint8_t *)dstImg->boundMemory->data +
                                   dstImg->boundOffset;

                /* Compute bytes per pixel for the image format */
                uint32_t bpp = 4; /* Default RGBA */
                switch (dstImg->format)
                {
                    case VK_FORMAT_R32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT:
                        bpp = 4; break;
                    case VK_FORMAT_R32G32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT_S8_UINT:
                        bpp = 8; break;
                    case VK_FORMAT_R32G32B32_SFLOAT:
                        bpp = 12; break;
                    case VK_FORMAT_R32G32B32A32_SFLOAT:
                        bpp = 16; break;
                    case VK_FORMAT_D16_UNORM:
                        bpp = 2; break;
                    default:
                        bpp = 4; break;
                }

                uint32_t imgW = dstImg->width;
                uint32_t extW = cmd->copyBufferToImage.imageExtent.width;
                uint32_t extH = cmd->copyBufferToImage.imageExtent.height;
                int32_t  offX = cmd->copyBufferToImage.imageOffset.x;
                int32_t  offY = cmd->copyBufferToImage.imageOffset.y;
                uint32_t bufRowLen = cmd->copyBufferToImage.bufferRowLength;
                if (bufRowLen == 0) bufRowLen = extW;

                /* Copy row by row */
                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t imgRow = (uint32_t)offY + row;
                    if (imgRow >= dstImg->height) break;

                    uint8_t *bufRow = bufBase + (size_t)row * bufRowLen * bpp;
                    uint8_t *imgRow_ = imgBase + ((size_t)imgRow * imgW + (uint32_t)offX) * bpp;
                    uint32_t rowBytes = extW * bpp;

                    /* Bounds check image side */
                    VkDeviceSize imgOff = (VkDeviceSize)((imgRow_ + rowBytes) - (uint8_t *)dstImg->boundMemory->data);
                    if (imgOff <= dstImg->boundMemory->size)
                        memcpy(imgRow_, bufRow, rowBytes);
                }
            }
            break;
        }

        case SWVK_CMD_COPY_IMAGE_TO_BUFFER:
        {
            SWVKImage  *srcImg = cmd->copyImageToBuffer.srcImage;
            SWVKBuffer *dstBuf = cmd->copyImageToBuffer.dstBuffer;
            if (srcImg && srcImg->boundMemory && srcImg->boundMemory->data &&
                dstBuf && dstBuf->boundMemory && dstBuf->boundMemory->data)
            {
                uint8_t *imgBase = (uint8_t *)srcImg->boundMemory->data +
                                   srcImg->boundOffset;
                uint8_t *bufBase = (uint8_t *)dstBuf->boundMemory->data +
                                   dstBuf->boundOffset +
                                   cmd->copyImageToBuffer.bufferOffset;

                uint32_t bpp = 4;
                switch (srcImg->format)
                {
                    case VK_FORMAT_R32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT:
                        bpp = 4; break;
                    case VK_FORMAT_R32G32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT_S8_UINT:
                        bpp = 8; break;
                    case VK_FORMAT_R32G32B32_SFLOAT:
                        bpp = 12; break;
                    case VK_FORMAT_R32G32B32A32_SFLOAT:
                        bpp = 16; break;
                    case VK_FORMAT_D16_UNORM:
                        bpp = 2; break;
                    default:
                        bpp = 4; break;
                }

                uint32_t imgW = srcImg->width;
                uint32_t extW = cmd->copyImageToBuffer.imageExtent.width;
                uint32_t extH = cmd->copyImageToBuffer.imageExtent.height;
                int32_t  offX = cmd->copyImageToBuffer.imageOffset.x;
                int32_t  offY = cmd->copyImageToBuffer.imageOffset.y;
                uint32_t bufRowLen = cmd->copyImageToBuffer.bufferRowLength;
                if (bufRowLen == 0) bufRowLen = extW;

                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t imgRow = (uint32_t)offY + row;
                    if (imgRow >= srcImg->height) break;

                    uint8_t *imgSrc = imgBase + ((size_t)imgRow * imgW + (uint32_t)offX) * bpp;
                    uint8_t *bufRow = bufBase + (size_t)row * bufRowLen * bpp;
                    uint32_t rowBytes = extW * bpp;

                    VkDeviceSize imgEnd = (VkDeviceSize)((imgSrc + rowBytes) -
                        (uint8_t *)srcImg->boundMemory->data);
                    if (imgEnd <= srcImg->boundMemory->size)
                        memcpy(bufRow, imgSrc, rowBytes);
                }
            }
            break;
        }

        case SWVK_CMD_COPY_IMAGE:
        {
            SWVKImage *srcImg = cmd->copyImage.src;
            SWVKImage *dstImg = cmd->copyImage.dst;
            if (srcImg && srcImg->boundMemory && srcImg->boundMemory->data &&
                dstImg && dstImg->boundMemory && dstImg->boundMemory->data)
            {
                uint32_t bpp = 4;
                switch (srcImg->format)
                {
                    case VK_FORMAT_R32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT:
                        bpp = 4; break;
                    case VK_FORMAT_R32G32_SFLOAT:
                    case VK_FORMAT_D32_SFLOAT_S8_UINT:
                        bpp = 8; break;
                    case VK_FORMAT_R32G32B32_SFLOAT:
                        bpp = 12; break;
                    case VK_FORMAT_R32G32B32A32_SFLOAT:
                        bpp = 16; break;
                    case VK_FORMAT_D16_UNORM:
                        bpp = 2; break;
                    default:
                        bpp = 4; break;
                }

                uint32_t extW = cmd->copyImage.extent.width;
                uint32_t extH = cmd->copyImage.extent.height;

                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t srcRow = (uint32_t)cmd->copyImage.srcOffset.y + row;
                    uint32_t dstRow = (uint32_t)cmd->copyImage.dstOffset.y + row;
                    if (srcRow >= srcImg->height || dstRow >= dstImg->height) break;

                    uint8_t *srcPtr = (uint8_t *)srcImg->boundMemory->data
                        + srcImg->boundOffset
                        + ((size_t)srcRow * srcImg->width + (uint32_t)cmd->copyImage.srcOffset.x) * bpp;
                    uint8_t *dstPtr = (uint8_t *)dstImg->boundMemory->data
                        + dstImg->boundOffset
                        + ((size_t)dstRow * dstImg->width + (uint32_t)cmd->copyImage.dstOffset.x) * bpp;

                    uint32_t rowBytes = extW * bpp;
                    VkDeviceSize srcEnd = (VkDeviceSize)((srcPtr + rowBytes) -
                        (uint8_t *)srcImg->boundMemory->data);
                    VkDeviceSize dstEnd = (VkDeviceSize)((dstPtr + rowBytes) -
                        (uint8_t *)dstImg->boundMemory->data);
                    if (srcEnd <= srcImg->boundMemory->size &&
                        dstEnd <= dstImg->boundMemory->size)
                        memcpy(dstPtr, srcPtr, rowBytes);
                }
            }
            break;
        }

        case SWVK_CMD_FILL_BUFFER:
        {
            SWVKBuffer *buf = cmd->fillBuffer.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                VkDeviceSize off = buf->boundOffset + cmd->fillBuffer.offset;
                VkDeviceSize fillSize = cmd->fillBuffer.size;

                /* VK_WHOLE_SIZE means fill to end of buffer */
                if (fillSize == VK_WHOLE_SIZE)
                    fillSize = buf->size - cmd->fillBuffer.offset;

                /* Align to 4 bytes */
                fillSize &= ~3ULL;

                if (off + fillSize <= buf->boundMemory->size)
                {
                    uint32_t *dst = (uint32_t *)((uint8_t *)buf->boundMemory->data + off);
                    uint32_t val = cmd->fillBuffer.data;
                    size_t count = (size_t)(fillSize / 4);
                    for (size_t j = 0; j < count; j++)
                        dst[j] = val;
                }
                else
                {
                    IExec->DebugPrintF("[software_vk] WARNING: CmdFillBuffer out of bounds\n");
                }
            }
            break;
        }

        case SWVK_CMD_UPDATE_BUFFER:
        {
            SWVKBuffer *buf = cmd->updateBuffer.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                VkDeviceSize off = buf->boundOffset + cmd->updateBuffer.offset;
                VkDeviceSize upSize = cmd->updateBuffer.size;

                if (off + upSize <= buf->boundMemory->size)
                {
                    uint8_t *dst = (uint8_t *)buf->boundMemory->data + off;
                    memcpy(dst, cmd->updateBuffer.data, (size_t)upSize);
                }
                else
                {
                    IExec->DebugPrintF("[software_vk] WARNING: CmdUpdateBuffer out of bounds\n");
                }
            }
            break;
        }

        /* Dynamic state (stored in render state but unused by SW rasteriser) */
        case SWVK_CMD_SET_LINE_WIDTH:
        case SWVK_CMD_SET_DEPTH_BIAS:
        case SWVK_CMD_SET_BLEND_CONSTANTS:
        case SWVK_CMD_SET_DEPTH_BOUNDS_VAL:
        case SWVK_CMD_SET_STENCIL_COMPARE_MASK:
        case SWVK_CMD_SET_STENCIL_WRITE_MASK:
        case SWVK_CMD_SET_STENCIL_REFERENCE:
            /* No-op: these dynamic states are not used by the software rasteriser */
            break;

        /* Indirect draw */
        case SWVK_CMD_DRAW_INDIRECT:
        {
            SWVKBuffer *buf = cmd->drawIndirect.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                uint8_t *base = (uint8_t *)buf->boundMemory->data +
                                buf->boundOffset + cmd->drawIndirect.offset;
                uint32_t stride = cmd->drawIndirect.stride;
                if (stride == 0)
                    stride = sizeof(VkDrawIndirectCommand);

                for (uint32_t d = 0; d < cmd->drawIndirect.drawCount; d++)
                {
                    const VkDrawIndirectCommand *ic =
                        (const VkDrawIndirectCommand *)(base + d * stride);
                    SWVKCommand tmpCmd;
                    memset(&tmpCmd, 0, sizeof(tmpCmd));
                    tmpCmd.type               = SWVK_CMD_DRAW;
                    tmpCmd.draw.vertexCount   = ic->vertexCount;
                    tmpCmd.draw.instanceCount = ic->instanceCount;
                    tmpCmd.draw.firstVertex   = ic->firstVertex;
                    tmpCmd.draw.firstInstance  = ic->firstInstance;
                    swvk_ExecuteDraw(&rs, &tmpCmd);
                }
            }
            break;
        }

        case SWVK_CMD_DRAW_INDEXED_INDIRECT:
        {
            SWVKBuffer *buf = cmd->drawIndirect.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                uint8_t *base = (uint8_t *)buf->boundMemory->data +
                                buf->boundOffset + cmd->drawIndirect.offset;
                uint32_t stride = cmd->drawIndirect.stride;
                if (stride == 0)
                    stride = sizeof(VkDrawIndexedIndirectCommand);

                for (uint32_t d = 0; d < cmd->drawIndirect.drawCount; d++)
                {
                    const VkDrawIndexedIndirectCommand *ic =
                        (const VkDrawIndexedIndirectCommand *)(base + d * stride);
                    SWVKCommand tmpCmd;
                    memset(&tmpCmd, 0, sizeof(tmpCmd));
                    tmpCmd.type                    = SWVK_CMD_DRAW_INDEXED;
                    tmpCmd.drawIndexed.indexCount   = ic->indexCount;
                    tmpCmd.drawIndexed.instanceCount = ic->instanceCount;
                    tmpCmd.drawIndexed.firstIndex   = ic->firstIndex;
                    tmpCmd.drawIndexed.vertexOffset = ic->vertexOffset;
                    tmpCmd.drawIndexed.firstInstance = ic->firstInstance;
                    swvk_ExecuteDrawIndexed(&rs, &tmpCmd);
                }
            }
            break;
        }
        }
    }

    /* Free cached shader states at end of command buffer execution */
    if (rs.cachedVertState)
        IExec->FreeVec(rs.cachedVertState);
    if (rs.cachedFragState)
        IExec->FreeVec(rs.cachedFragState);
}

/****************************************************************************/
/* vkQueueSubmit                                                            */
/****************************************************************************/

VkResult swvk_QueueSubmit(VkQueue queue,
                          uint32_t submitCount,
                          const VkSubmitInfo *pSubmits,
                          VkFence fence)
{
    (void)queue;

    for (uint32_t s = 0; s < submitCount; s++)
    {
        const VkSubmitInfo *submit = &pSubmits[s];

        for (uint32_t c = 0; c < submit->commandBufferCount; c++)
        {
            SWVKCommandBuffer *cmdbuf =
                (SWVKCommandBuffer *)submit->pCommandBuffers[c];

            if (cmdbuf)
            {
                IExec->DebugPrintF("[software_vk] Executing command buffer (%lu commands)\n",
                                   (unsigned long)cmdbuf->commandCount);
                swvk_ExecuteCommandBuffer(cmdbuf);
            }
        }
    }

    /* Signal fence if provided (synchronous -- always immediately signalled) */
    if (fence != VK_NULL_HANDLE)
    {
        SWVKFence *f = (SWVKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }

    return VK_SUCCESS;
}

/****************************************************************************/
/* vkQueueSubmit2 (Vulkan 1.3)                                              */
/* Translates VkSubmitInfo2 to v1-style execution.                          */
/****************************************************************************/

VkResult swvk_QueueSubmit2(VkQueue queue,
                            uint32_t submitCount,
                            const VkSubmitInfo2 *pSubmits,
                            VkFence fence)
{
    (void)queue;

    for (uint32_t s = 0; s < submitCount; s++)
    {
        const VkSubmitInfo2 *submit = &pSubmits[s];

        for (uint32_t c = 0; c < submit->commandBufferInfoCount; c++)
        {
            SWVKCommandBuffer *cmdbuf =
                (SWVKCommandBuffer *)submit->pCommandBufferInfos[c].commandBuffer;

            if (cmdbuf)
            {
                IExec->DebugPrintF("[software_vk] QueueSubmit2: executing command buffer (%lu commands)\n",
                                   (unsigned long)cmdbuf->commandCount);
                swvk_ExecuteCommandBuffer(cmdbuf);
            }
        }
    }

    /* Signal fence if provided */
    if (fence != VK_NULL_HANDLE)
    {
        SWVKFence *f = (SWVKFence *)(uintptr_t)fence;
        f->signalled = VK_TRUE;
    }

    return VK_SUCCESS;
}
