#pragma GCC diagnostic ignored "-Wformat-truncation"

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
/*------------------------------------------------------------------------
** Post-process: replace integer modulo (%) with equivalent expression.
**
** Warp3D Nova compiler: "Modulo for element type Int32 isn't implemented yet."
** Replace "A % B" with "(A - (A / B) * B)" which uses only division and
** multiplication (both supported).
**----------------------------------------------------------------------*/
static char *ogles2vk_PostProcessIntModulo(const char *glsl)
{
    if (!strchr(glsl, '%'))
        return NULL;

    size_t origLen = strlen(glsl);
    size_t bufSize = origLen * 2 + 256;
    char *out = (char *)IExec->AllocVecTags(bufSize,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!out) return NULL;

    size_t outPos = 0;
    const char *p = glsl;
    int changed = 0;

    while (*p)
    {
        /* Look for " % " pattern (integer modulo) */
        if (p[0] == ' ' && p[1] == '%' && p[2] == ' ')
        {
            /* Find the left operand: scan back to find start of identifier/expression */
            /* We need the token before " % ". Scan back over alphanumeric/underscore/parens */
            const char *lEnd = p;  /* points to space before % */
            const char *lStart = lEnd - 1;

            /* Handle simple identifier: scan back over [a-zA-Z0-9_] */
            while (lStart > glsl && ((lStart[-1] >= 'a' && lStart[-1] <= 'z') ||
                   (lStart[-1] >= 'A' && lStart[-1] <= 'Z') ||
                   (lStart[-1] >= '0' && lStart[-1] <= '9') || lStart[-1] == '_'))
                lStart--;

            /* Find the right operand: scan forward over digits/identifiers */
            const char *rStart = p + 3;
            const char *rEnd = rStart;
            while ((*rEnd >= 'a' && *rEnd <= 'z') || (*rEnd >= 'A' && *rEnd <= 'Z') ||
                   (*rEnd >= '0' && *rEnd <= '9') || *rEnd == '_')
                rEnd++;

            size_t lLen = (size_t)(lEnd - lStart);
            size_t rLen = (size_t)(rEnd - rStart);

            if (lLen > 0 && rLen > 0 && lLen < 64 && rLen < 64)
            {
                /* Rewind output to remove already-copied left operand */
                if (outPos >= lLen)
                    outPos -= lLen;

                /* Emit: (A - (A / B) * B) */
                outPos += (size_t)snprintf(out + outPos, bufSize - outPos,
                    "(%.*s - (%.*s / %.*s) * %.*s)",
                    (int)lLen, lStart, (int)lLen, lStart,
                    (int)rLen, rStart, (int)rLen, rStart);

                p = rEnd;
                changed = 1;
                continue;
            }
        }

        out[outPos++] = *p++;
        if (outPos >= bufSize - 2) break;
    }
    out[outPos] = '\0';

    if (!changed) { IExec->FreeVec(out); return NULL; }
    return out;
}

static char *ogles2vk_PostProcessInstancing(const char *glsl)
{
    /* Check if gl_InstanceID is used at all */
    if (!strstr(glsl, "gl_InstanceID"))
        return NULL;  /* No instancing, no changes needed */

    /* We need to:
     * 1. Add "uniform int u_InstanceIndex;" after #version line
     *    (can't use gl_ prefix -- OGLES2 reserves all gl_ identifiers)
     * 2. Replace "gl_InstanceID" with "u_InstanceIndex"
     * 3. Remove " + SPIRV_Cross_BaseInstance" where it appears
     * 4. Remove the SPIRV_Cross_BaseInstance uniform declaration
     * 5. Remove #ifdef GL_ARB_shader_draw_parameters blocks
     */
    size_t origLen = strlen(glsl);
    size_t bufSize = origLen + 256;
    char *out = (char *)IExec->AllocVecTags(bufSize,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!out) return NULL;

    size_t outPos = 0;
    const char *p = glsl;

    /* Find end of #version line and inject uniform */
    const char *versionEnd = strstr(p, "\n");
    if (versionEnd)
    {
        versionEnd++;
        size_t headerLen = (size_t)(versionEnd - p);
        memcpy(out + outPos, p, headerLen);
        outPos += headerLen;
        p = versionEnd;

        const char *inject = "uniform highp int u_InstanceIndex;\n";
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
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Skip #ifdef/#else/#endif GL_ARB_shader_draw_parameters blocks */
        if (strncmp(p, "#ifdef GL_ARB_shader_draw_parameters", 35) == 0 ||
            strncmp(p, "#extension GL_ARB_shader_draw_parameters", 40) == 0 ||
            strncmp(p, "#define SPIRV_Cross_BaseInstance", 31) == 0 ||
            strncmp(p, "#else", 5) == 0 ||
            strncmp(p, "#endif", 6) == 0)
        {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* Replace "gl_InstanceID" with "u_InstanceIndex" (same length: 15 vs 13+2) */
        if (strncmp(p, "gl_InstanceID", 13) == 0 &&
            !(p[13] >= 'A' && p[13] <= 'z'))  /* Not part of longer identifier */
        {
            const char *repl = "u_InstanceIndex";
            size_t replLen = strlen(repl);
            memcpy(out + outPos, repl, replLen);
            outPos += replLen;
            p += 13;
            continue;
        }

        /* Remove " + SPIRV_Cross_BaseInstance" */
        if (strncmp(p, " + SPIRV_Cross_BaseInstance", 27) == 0)
        {
            p += 27;
            continue;
        }
        if (strncmp(p, "SPIRV_Cross_BaseInstance + ", 26) == 0)
        {
            p += 26;
            continue;
        }

        out[outPos++] = *p++;

        if (outPos >= bufSize - 2)
            break;
    }
    out[outPos] = '\0';

    return out;
}

/*------------------------------------------------------------------------
** Post-process: replace ARRAY[gl_VertexID] with if-else via temp variable.
**
** The Warp3D Nova GCN code generator can't emit scalar (SOP2) instructions
** for array addresses derived from a VGPR (gl_VertexID).  We replace each
** occurrence of ARRAY[gl_VertexID] inline with a temp variable, and insert
** the if-else chain before the line.  This handles all contexts including
** vec4(ARRAY[gl_VertexID], 0.0, 1.0).
**
** Example:
**   gl_Position = vec4(_19[gl_VertexID], 0.0, 1.0);
** becomes:
**   vec2 _19_vtx;
**   if (gl_VertexID == 0) _19_vtx = _19[0];
**   else if (gl_VertexID == 1) _19_vtx = _19[1];
**   else _19_vtx = _19[2];
**   gl_Position = vec4(_19_vtx, 0.0, 1.0);
**----------------------------------------------------------------------*/

/* Small table to track array declarations */
#define MAX_TRACKED_ARRAYS 8
#define MAX_ARRAYNAME 32
struct ArrayInfo { char name[MAX_ARRAYNAME]; char type[16]; int size; };

static char *ogles2vk_PostProcessVertexIDArrays(const char *glsl)
{
    if (!strstr(glsl, "[gl_VertexID]"))
        return NULL;

    /* Scan for array declarations: "TYPE NAME[N];" to get type + size */
    struct ArrayInfo arrays[MAX_TRACKED_ARRAYS];
    int arrayCount = 0;

    const char *scan = glsl;
    while (*scan && arrayCount < MAX_TRACKED_ARRAYS)
    {
        const char *bracket = strchr(scan, '[');
        if (!bracket) break;

        const char *closeBracket = strchr(bracket + 1, ']');
        if (!closeBracket || closeBracket[1] != ';') { scan = bracket + 1; continue; }

        /* Find start of line */
        const char *lineStart = bracket;
        while (lineStart > glsl && lineStart[-1] != '\n') lineStart--;

        /* Must start with a type keyword */
        if (strncmp(lineStart, "vec", 3) != 0 &&
            strncmp(lineStart, "mat", 3) != 0 &&
            strncmp(lineStart, "ivec", 4) != 0 &&
            strncmp(lineStart, "float", 5) != 0 &&
            strncmp(lineStart, "int ", 4) != 0)
        { scan = bracket + 1; continue; }

        /* Extract type name (first word) */
        const char *typeEnd = lineStart;
        while (*typeEnd && *typeEnd != ' ') typeEnd++;
        size_t typeLen = (size_t)(typeEnd - lineStart);
        if (typeLen == 0 || typeLen >= 16) { scan = bracket + 1; continue; }

        /* Extract array name: word before '[' */
        const char *nameEnd = bracket;
        const char *nameStart = nameEnd - 1;
        while (nameStart > lineStart && nameStart[-1] != ' ') nameStart--;
        size_t nameLen = (size_t)(nameEnd - nameStart);
        if (nameLen == 0 || nameLen >= MAX_ARRAYNAME) { scan = bracket + 1; continue; }

        /* Extract size */
        int arrSize = 0;
        const char *d = bracket + 1;
        while (d < closeBracket && *d >= '0' && *d <= '9')
            arrSize = arrSize * 10 + (*d++ - '0');
        if (arrSize <= 0 || arrSize > 64) { scan = bracket + 1; continue; }

        memcpy(arrays[arrayCount].name, nameStart, nameLen);
        arrays[arrayCount].name[nameLen] = '\0';
        memcpy(arrays[arrayCount].type, lineStart, typeLen);
        arrays[arrayCount].type[typeLen] = '\0';
        arrays[arrayCount].size = arrSize;
        arrayCount++;
        scan = closeBracket + 1;
    }

    if (arrayCount == 0)
        return NULL;

    /* Process line by line.  For each line containing NAME[gl_VertexID]:
       1. Insert if-else chain assigning to temp "NAME_vtx" BEFORE the line
       2. Replace "NAME[gl_VertexID]" with "NAME_vtx" inline */
    size_t origLen = strlen(glsl);
    size_t bufSize = origLen * 4 + 8192;
    char *out = (char *)IExec->AllocVecTags(bufSize,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!out) return NULL;

    size_t outPos = 0;
    const char *p = glsl;
    int changed = 0;
    int tempDeclared[MAX_TRACKED_ARRAYS];
    memset(tempDeclared, 0, sizeof(tempDeclared));

    while (*p)
    {
        /* Find next newline to process one line at a time */
        const char *lineEnd = strchr(p, '\n');
        if (!lineEnd) lineEnd = p + strlen(p);
        size_t lineLen = (size_t)(lineEnd - p) + (*lineEnd == '\n' ? 1 : 0);

        /* Check if this line contains any ARRAY[gl_VertexID] */
        int foundArr = -1;
        for (int i = 0; i < arrayCount; i++)
        {
            size_t nlen = strlen(arrays[i].name);
            const char *s = p;
            while (s < lineEnd)
            {
                s = strstr(s, arrays[i].name);
                if (!s || s >= lineEnd) break;
                if (strncmp(s + nlen, "[gl_VertexID]", 13) == 0)
                {
                    foundArr = i;
                    break;
                }
                s++;
            }
            if (foundArr >= 0) break;
        }

        if (foundArr >= 0)
        {
            /* Find indentation of this line */
            const char *indEnd = p;
            while (indEnd < lineEnd && (*indEnd == ' ' || *indEnd == '\t')) indEnd++;
            size_t indent = (size_t)(indEnd - p);
            char indBuf[32];
            if (indent >= sizeof(indBuf)) indent = sizeof(indBuf) - 1;
            memcpy(indBuf, p, indent);
            indBuf[indent] = '\0';

            char tmpName[64];
            snprintf(tmpName, sizeof(tmpName), "%s_vtx", arrays[foundArr].name);

            /* Declare temp variable (once per array) */
            if (!tempDeclared[foundArr])
            {
                outPos += (size_t)snprintf(out + outPos, bufSize - outPos,
                    "%s%s %s;\n", indBuf, arrays[foundArr].type, tmpName);
                tempDeclared[foundArr] = 1;
            }

            /* Emit if-else chain to assign temp */
            for (int idx = 0; idx < arrays[foundArr].size; idx++)
            {
                if (idx == 0)
                    outPos += (size_t)snprintf(out + outPos, bufSize - outPos,
                        "%sif (gl_VertexID == %d) %s = %s[%d];\n",
                        indBuf, idx, tmpName, arrays[foundArr].name, idx);
                else if (idx < arrays[foundArr].size - 1)
                    outPos += (size_t)snprintf(out + outPos, bufSize - outPos,
                        "%selse if (gl_VertexID == %d) %s = %s[%d];\n",
                        indBuf, idx, tmpName, arrays[foundArr].name, idx);
                else
                    outPos += (size_t)snprintf(out + outPos, bufSize - outPos,
                        "%selse %s = %s[%d];\n",
                        indBuf, tmpName, arrays[foundArr].name, idx);
            }

            /* Now copy the line, replacing all NAME[gl_VertexID] with NAME_vtx */
            size_t nlen = strlen(arrays[foundArr].name);
            size_t tmpLen = strlen(tmpName);
            const char *lp = p;
            while (lp < p + lineLen)
            {
                if (lp + nlen + 13 <= p + lineLen &&
                    strncmp(lp, arrays[foundArr].name, nlen) == 0 &&
                    strncmp(lp + nlen, "[gl_VertexID]", 13) == 0)
                {
                    memcpy(out + outPos, tmpName, tmpLen);
                    outPos += tmpLen;
                    lp += nlen + 13;
                }
                else
                {
                    out[outPos++] = *lp++;
                }
            }
            changed = 1;
        }
        else
        {
            /* No match, copy line as-is */
            memcpy(out + outPos, p, lineLen);
            outPos += lineLen;
        }

        p += lineLen;
        if (outPos >= bufSize - 512) break;
    }
    out[outPos] = '\0';

    if (!changed) { IExec->FreeVec(out); return NULL; }
    return out;
}

/*------------------------------------------------------------------------
** Post-process: convert const array initializers to assignments in main().
**
** SPIRV-Cross generates:
**   const vec2 _19[3] = vec2[](vec2(0.0, -0.5), vec2(0.5), vec2(-0.5, 0.5));
**
** The Warp3D Nova OGLES2 shader compiler doesn't support const array
** initializer syntax (OpConstantComposite with OpTypeArray).  Convert to:
**   vec2 _19[3];
** with initialization moved into main():
**   _19[0] = vec2(0.0, -0.5);
**   _19[1] = vec2(0.5);
**   _19[2] = vec2(-0.5, 0.5);
**----------------------------------------------------------------------*/
static char *ogles2vk_PostProcessConstArrays(const char *glsl)
{
    /* Quick check: look for the pattern "= vec" or "= mat" or "= float["
       preceded by "] " which indicates a const array initializer */
    if (!strstr(glsl, "] = vec") && !strstr(glsl, "] = mat") &&
        !strstr(glsl, "] = float[") && !strstr(glsl, "] = int["))
        return NULL;  /* No const arrays */

    size_t origLen = strlen(glsl);
    /* Generous buffer: each const array line expands to N assignment lines */
    size_t bufSize = origLen * 3 + 4096;
    char *out = (char *)IExec->AllocVecTags(bufSize,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!out) return NULL;

    /* Collect init statements to inject into main() */
    char *initBuf = (char *)IExec->AllocVecTags(bufSize / 2,
        AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!initBuf) { IExec->FreeVec(out); return NULL; }
    size_t initPos = 0;

    size_t outPos = 0;
    const char *p = glsl;

    while (*p)
    {
        /* Detect: "const TYPE NAME[N] = TYPE[](" at line start */
        if (strncmp(p, "const ", 6) == 0)
        {
            /* Check if this is an array declaration with initializer */
            const char *lineEnd = strchr(p, '\n');
            if (!lineEnd) lineEnd = p + strlen(p);

            /* Find the bracket pair [N] and = TYPE[]( */
            const char *bracket = strchr(p, '[');
            const char *eq = strstr(p, "] = ");
            const char *arrCtor = strstr(p, "](");

            if (bracket && eq && arrCtor &&
                bracket < eq && eq < arrCtor &&
                bracket < lineEnd && arrCtor < lineEnd)
            {
                /* Extract: "const TYPE NAME" before '[' */
                /* Skip "const " to get "TYPE NAME" */
                const char *typeStart = p + 6;  /* after "const " */
                size_t prefixLen = (size_t)(bracket - typeStart);

                /* Extract array size string between [ and ] */
                const char *sizeStart = bracket + 1;
                const char *sizeEnd = strchr(sizeStart, ']');
                if (!sizeEnd || sizeEnd > eq) goto copyline;

                /* Extract the type name (before the space+varname) */
                /* typeStart points to "vec2 _19" -- we need "vec2" */
                const char *varNameStart = NULL;
                const char *sp;
                for (sp = typeStart; sp < bracket; sp++)
                {
                    if (*sp == ' ') varNameStart = sp + 1;
                }
                if (!varNameStart) goto copyline;

                size_t varNameLen = (size_t)(bracket - varNameStart);

                /* Write declaration without const or initializer:
                   "TYPE NAME[N];\n" */
                memcpy(out + outPos, typeStart, prefixLen);
                outPos += prefixLen;
                /* Copy [N] */
                size_t bracketPartLen = (size_t)(sizeEnd + 1 - bracket);
                memcpy(out + outPos, bracket, bracketPartLen);
                outPos += bracketPartLen;
                out[outPos++] = ';';
                out[outPos++] = '\n';

                /* Now parse the constructor arguments and build
                   init statements: "NAME[0] = value; NAME[1] = value; ..." */
                const char *argsStart = arrCtor + 2; /* skip "](" */
                int idx = 0;
                const char *valStart = argsStart;
                int depth = 1; /* We're inside the outer parens */
                const char *cp = argsStart;

                while (*cp && cp < lineEnd && depth > 0)
                {
                    if (*cp == '(') depth++;
                    else if (*cp == ')') {
                        depth--;
                        if (depth == 0)
                        {
                            /* End of constructor: emit last element */
                            size_t valLen = (size_t)(cp - valStart);
                            /* Trim leading whitespace */
                            while (valLen > 0 && (*valStart == ' ' || *valStart == '\t'))
                            { valStart++; valLen--; }

                            if (valLen > 0)
                            {
                                initPos += (size_t)snprintf(initBuf + initPos,
                                    bufSize / 2 - initPos,
                                    "    %.*s[%d] = %.*s;\n",
                                    (int)varNameLen, varNameStart,
                                    idx,
                                    (int)valLen, valStart);
                            }
                        }
                    }
                    else if (*cp == ',' && depth == 1)
                    {
                        /* Element separator at top level */
                        size_t valLen = (size_t)(cp - valStart);
                        while (valLen > 0 && (*valStart == ' ' || *valStart == '\t'))
                        { valStart++; valLen--; }

                        if (valLen > 0)
                        {
                            initPos += (size_t)snprintf(initBuf + initPos,
                                bufSize / 2 - initPos,
                                "    %.*s[%d] = %.*s;\n",
                                (int)varNameLen, varNameStart,
                                idx,
                                (int)valLen, valStart);
                        }
                        idx++;
                        valStart = cp + 1;
                    }
                    cp++;
                }

                /* Skip past the original line */
                p = lineEnd;
                if (*p == '\n') p++;
                continue;
            }
        }

copyline:
        /* Copy character as-is, but intercept "void main()" to inject inits */
        if (initPos > 0 && strncmp(p, "void main()", 11) == 0)
        {
            /* Copy "void main()\n{" then inject init code */
            const char *braceNl = strstr(p, "{\n");
            if (braceNl)
            {
                size_t headLen = (size_t)(braceNl + 2 - p);
                memcpy(out + outPos, p, headLen);
                outPos += headLen;
                /* Inject array init statements */
                memcpy(out + outPos, initBuf, initPos);
                outPos += initPos;
                p += headLen;
                initPos = 0;  /* Done injecting */
                continue;
            }
        }

        out[outPos++] = *p++;
        if (outPos >= bufSize - 2) break;
    }
    out[outPos] = '\0';

    IExec->FreeVec(initBuf);
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

            /* After flatten_buffer_block + emit_push_constant_as_uniform_buffer,
               SPIRV-Cross always emits the uniform as "PushConstants" regardless
               of the original SPIR-V block name. */
            const char *pcName = "PushConstants";

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

    /* Step 8: Post-process GLSL for OGLES2 compatibility */
    {
        const char *currentGlsl = glslSource;

        /* 8a: Convert const array initializers to assignments in main() */
        char *ppArrays = ogles2vk_PostProcessConstArrays(currentGlsl);
        if (ppArrays) currentGlsl = ppArrays;

        /* 8b: Replace ARRAY[gl_VertexID] with if-else constant indexing */
        char *ppVtxID = ogles2vk_PostProcessVertexIDArrays(currentGlsl);
        if (ppVtxID) currentGlsl = ppVtxID;

        /* 8c: gl_InstanceID -> u_InstanceIndex uniform */
        char *ppInst = ogles2vk_PostProcessInstancing(currentGlsl);
        if (ppInst) currentGlsl = ppInst;

        /* 8d: Replace integer modulo (%) with (A - (A/B)*B) */
        char *ppMod = ogles2vk_PostProcessIntModulo(currentGlsl);
        if (ppMod) currentGlsl = ppMod;

        size_t glslLen = strlen(currentGlsl);

        /* Step 9: Copy to AmigaOS-managed buffer */
        result->glsl = (char *)IExec->AllocVecTags(glslLen + 1,
            AVT_Type, MEMF_PRIVATE, TAG_DONE);
        if (result->glsl)
        {
            memcpy(result->glsl, currentGlsl, glslLen + 1);
            ret = 1;
        }

        if (ppMod) IExec->FreeVec(ppMod);
        if (ppInst) IExec->FreeVec(ppInst);
        if (ppVtxID) IExec->FreeVec(ppVtxID);
        if (ppArrays) IExec->FreeVec(ppArrays);
    }

cleanup:
    if (context)
        spvc_context_destroy(context);

    return ret;
}
