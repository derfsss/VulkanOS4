/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_spirv2glsl.c -- SPIR-V to GLSL ES 3.1 transpiler via SPIRV-Cross
**
** Uses the SPIRV-Cross C API (spvc) to convert Vulkan SPIR-V bytecode
** to GLSL ES source strings for compilation via ogles2.library.
** Replaces the hand-rolled transpiler with full SPIR-V spec coverage.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "ogles2vk_internal.h"
#include "spirv_cross_c.h"

/*------------------------------------------------------------------------
** Post-process: replace gl_InstanceID + SPIRV_Cross_BaseInstance
** with our custom gl_InstanceIndex uniform.
**
** SPIRV-Cross emits:
**   int idx = (gl_InstanceID + SPIRV_Cross_BaseInstance);
**
** AmigaOS OGLES2 may not support gl_InstanceID. We replace all
** occurrences of "gl_InstanceID" with "gl_InstanceIndex" and remove
** the SPIRV_Cross_BaseInstance addition. We also inject a uniform
** declaration for gl_InstanceIndex.
**----------------------------------------------------------------------*/
static char *ogles2vk_PostProcessInstancing(const char *glsl)
{
    /* Check if gl_InstanceID is used at all */
    if (!strstr(glsl, "gl_InstanceID"))
        return NULL;  /* No instancing, no changes needed */

    /* We need to:
     * 1. Add "uniform int gl_InstanceIndex;" after #version line
     * 2. Replace "gl_InstanceID" with "gl_InstanceIndex"
     * 3. Remove " + SPIRV_Cross_BaseInstance" where it appears
     * 4. Remove the SPIRV_Cross_BaseInstance uniform declaration
     */
    size_t origLen = strlen(glsl);
    size_t bufSize = origLen + 256;  /* Extra space for injected uniform */
    char *out = (char *)IExec->AllocVecTags(bufSize,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!out) return NULL;

    size_t outPos = 0;
    const char *p = glsl;

    /* Find end of #version line and inject uniform */
    const char *versionEnd = strstr(p, "\n");
    if (versionEnd)
    {
        versionEnd++;  /* Include newline */
        size_t headerLen = (size_t)(versionEnd - p);
        memcpy(out + outPos, p, headerLen);
        outPos += headerLen;
        p = versionEnd;

        /* Inject gl_InstanceIndex uniform */
        const char *inject = "uniform highp int gl_InstanceIndex;\n";
        size_t injectLen = strlen(inject);
        memcpy(out + outPos, inject, injectLen);
        outPos += injectLen;
    }

    /* Process remaining text */
    while (*p)
    {
        /* Skip SPIRV_Cross_BaseInstance uniform declaration line */
        if (strncmp(p, "uniform int SPIRV_Cross_BaseInstance;", 36) == 0 ||
            strncmp(p, "uniform highp int SPIRV_Cross_BaseInstance;", 43) == 0)
        {
            /* Skip to end of line */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Replace "gl_InstanceID" with "gl_InstanceIndex" */
        if (strncmp(p, "gl_InstanceID", 13) == 0)
        {
            const char *repl = "gl_InstanceIndex";
            size_t replLen = strlen(repl);
            memcpy(out + outPos, repl, replLen);
            outPos += replLen;
            p += 13;
            continue;
        }

        /* Remove " + SPIRV_Cross_BaseInstance" */
        if (strncmp(p, " + SPIRV_Cross_BaseInstance", 26) == 0)
        {
            p += 26;
            continue;
        }
        /* Also handle "(SPIRV_Cross_BaseInstance + " prefix form */
        if (strncmp(p, "SPIRV_Cross_BaseInstance + ", 26) == 0)
        {
            p += 26;
            continue;
        }

        out[outPos++] = *p++;

        /* Safety: prevent buffer overflow */
        if (outPos >= bufSize - 2)
            break;
    }
    out[outPos] = '\0';

    return out;
}

/*------------------------------------------------------------------------
** ogles2vk_SPIRV2GLSL -- Transpile SPIR-V to GLSL ES 3.1 via SPIRV-Cross
**
** Parameters:
**   leCode    -- SPIR-V words in little-endian byte order (original)
**   wordCount -- Number of 32-bit words
**   codeSize  -- Size in bytes (= wordCount * 4)
**   isVertex  -- 1 for vertex shader, 0 for fragment shader
**   result    -- Output: GLSL source + push constant/sampler metadata
**
** Returns 1 on success, 0 on failure.
** Caller must free result->glsl with IExec->FreeVec().
**----------------------------------------------------------------------*/
int ogles2vk_SPIRV2GLSL(const uint32_t *leCode, uint32_t wordCount,
                         uint32_t codeSize, int isVertex,
                         OGLES2VKTranspileResult *result)
{
    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_compiler_options options = NULL;
    spvc_resources resources = NULL;
    const spvc_reflected_resource *resourceList = NULL;
    size_t resourceCount = 0;
    const char *glslSource = NULL;
    int ret = 0;

    (void)codeSize;

    if (!leCode || wordCount < 5 || !result)
        return 0;

    memset(result, 0, sizeof(*result));
    result->pcArrayName[0] = '\0';

    /* Step 1: Create SPVC context */
    if (spvc_context_create(&context) != SPVC_SUCCESS)
    {
        IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: context_create failed\n");
        return 0;
    }

    /* Step 2: Parse SPIR-V (little-endian -- SPVC handles byte order) */
    if (spvc_context_parse_spirv(context, leCode, (size_t)wordCount, &ir) != SPVC_SUCCESS)
    {
        IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: parse_spirv failed: %s\n",
                           spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    /* Step 3: Create GLSL compiler */
    if (spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir,
            SPVC_CAPTURE_MODE_COPY, &compiler) != SPVC_SUCCESS)
    {
        IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: create_compiler failed: %s\n",
                           spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    /* Step 4: Set GLSL options */
    if (spvc_compiler_create_compiler_options(compiler, &options) != SPVC_SUCCESS)
        goto cleanup;

    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION, 310);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_TRUE);
    spvc_compiler_options_set_bool(options,
        SPVC_COMPILER_OPTION_GLSL_EMIT_PUSH_CONSTANT_AS_UNIFORM_BUFFER, SPVC_TRUE);

    if (spvc_compiler_install_compiler_options(compiler, options) != SPVC_SUCCESS)
        goto cleanup;

    /* Step 5: Reflection -- get push constant resources and flatten them */
    if (spvc_compiler_create_shader_resources(compiler, &resources) != SPVC_SUCCESS)
        goto cleanup;

    if (spvc_resources_get_resource_list_for_type(resources,
            SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &resourceList, &resourceCount) == SPVC_SUCCESS)
    {
        for (size_t i = 0; i < resourceCount; i++)
        {
            spvc_variable_id id = resourceList[i].id;

            /* Flatten the push constant block to vec4 array */
            spvc_compiler_flatten_buffer_block(compiler, id);

            /* Get the name SPIRV-Cross will use */
            const char *pcName = spvc_compiler_get_name(compiler, id);
            if (!pcName || pcName[0] == '\0')
                pcName = "PushConstants";

            /* Store name */
            snprintf(result->pcArrayName, sizeof(result->pcArrayName), "%s", pcName);

            /* Get the declared struct size to compute vec4 count */
            spvc_type_id typeId = resourceList[i].type_id;
            spvc_type type = spvc_compiler_get_type_handle(compiler, typeId);
            size_t pcSize = 0;
            spvc_compiler_get_declared_struct_size(compiler, type, &pcSize);

            /* vec4 count = ceil(byteSize / 16) */
            result->pcArraySize = (uint32_t)((pcSize + 15) / 16);

            IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: push constants '%s' "
                               "size=%lu vec4s=%lu\n",
                               result->pcArrayName, (unsigned long)pcSize,
                               (unsigned long)result->pcArraySize);
        }
    }

    /* Step 6: Reflection -- get sampled image resources */
    resourceList = NULL;
    resourceCount = 0;
    if (spvc_resources_get_resource_list_for_type(resources,
            SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &resourceList, &resourceCount) == SPVC_SUCCESS)
    {
        for (size_t i = 0; i < resourceCount && result->samplerCount < OGLES2VK_MAX_SAMPLERS; i++)
        {
            uint32_t idx = result->samplerCount++;
            const char *name = spvc_compiler_get_name(compiler, resourceList[i].id);
            if (name)
                snprintf(result->samplers[idx].name, sizeof(result->samplers[idx].name),
                         "%s", name);
            result->samplers[idx].set = spvc_compiler_get_decoration(compiler,
                resourceList[i].id, SpvDecorationDescriptorSet);
            result->samplers[idx].binding = spvc_compiler_get_decoration(compiler,
                resourceList[i].id, SpvDecorationBinding);

            IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: sampler '%s' set=%u binding=%u\n",
                               result->samplers[idx].name,
                               (unsigned)result->samplers[idx].set,
                               (unsigned)result->samplers[idx].binding);
        }
    }

    /* Step 7: Compile to GLSL */
    if (spvc_compiler_compile(compiler, &glslSource) != SPVC_SUCCESS)
    {
        IExec->DebugPrintF("[ogles2_vk] SPIRV-Cross: compile failed: %s\n",
                           spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    /* Step 8: Post-process for instancing (gl_InstanceID -> gl_InstanceIndex) */
    {
        char *postProcessed = ogles2vk_PostProcessInstancing(glslSource);
        const char *finalGlsl = postProcessed ? postProcessed : glslSource;
        size_t glslLen = strlen(finalGlsl);

        /* Step 9: Copy to AmigaOS-managed buffer */
        result->glsl = (char *)IExec->AllocVecTags(glslLen + 1,
            AVT_Type, MEMF_PRIVATE, TAG_DONE);
        if (result->glsl)
        {
            memcpy(result->glsl, finalGlsl, glslLen + 1);
            ret = 1;
        }

        if (postProcessed)
            IExec->FreeVec(postProcessed);
    }

cleanup:
    if (context)
        spvc_context_destroy(context);

    return ret;
}
