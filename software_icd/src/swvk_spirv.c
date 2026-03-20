/*
** software_vk.library -- Software Vulkan ICD for AmigaOS 4
**
** swvk_spirv.c -- SPIR-V module parser and interpreter
**
** Two-pass approach:
**   1. spv_ParseModule: walk all instructions, build type/constant/
**      decoration/variable tables
**   2. spv_Execute: walk the entry point function body, execute each
**      instruction using a register-based VM
*/

#include <string.h>
#include <math.h>
#include <interfaces/exec.h>

#include "swvk_internal.h"
#include "swvk_spirv.h"

/****************************************************************************/
/* Sin/cos lookup table for fast approximate trig                           */
/* 1024 entries per quadrant + linear interpolation.  On PowerPC, sinf()    */
/* is a software library call (~100+ cycles); table lookup + lerp ~5 cycles.*/
/****************************************************************************/

#define SIN_TABLE_SIZE 1024
#define SIN_TABLE_MASK (SIN_TABLE_SIZE - 1)
static float sinTable[SIN_TABLE_SIZE + 1];
static int sinTableInit = 0;

static void spv_InitSinTable(void)
{
    if (sinTableInit) return;
    for (int i = 0; i <= SIN_TABLE_SIZE; i++)
        sinTable[i] = sinf((float)i * (3.14159265358979f * 0.5f / (float)SIN_TABLE_SIZE));
    sinTableInit = 1;
}

/* Fast approximate sin using lookup table + linear interpolation */
static float spv_FastSin(float x)
{
    /* Reduce to [0, 2*PI] */
    const float TWO_PI = 6.28318530718f;
    const float INV_TWO_PI = 1.0f / 6.28318530718f;
    x = x - floorf(x * INV_TWO_PI) * TWO_PI;
    if (x < 0.0f) x += TWO_PI;

    /* Map to table index (0..4*TABLE_SIZE covers 0..2*PI) */
    float idx = x * (float)(SIN_TABLE_SIZE * 2) / 3.14159265358979f;
    int quadrant = (int)(idx / SIN_TABLE_SIZE) & 3;
    float frac;
    int i;

    switch (quadrant) {
        case 0: /* 0..PI/2: direct lookup */
            i = (int)idx & SIN_TABLE_MASK;
            frac = idx - floorf(idx);
            return sinTable[i] + (sinTable[i+1] - sinTable[i]) * frac;
        case 1: /* PI/2..PI: reverse lookup */
            i = SIN_TABLE_SIZE - ((int)idx & SIN_TABLE_MASK) - 1;
            frac = idx - floorf(idx);
            return sinTable[i] + (sinTable[i+1] - sinTable[i]) * (1.0f - frac);
        case 2: /* PI..3*PI/2: negative direct */
            i = (int)idx & SIN_TABLE_MASK;
            frac = idx - floorf(idx);
            return -(sinTable[i] + (sinTable[i+1] - sinTable[i]) * frac);
        case 3: /* 3*PI/2..2*PI: negative reverse */
            i = SIN_TABLE_SIZE - ((int)idx & SIN_TABLE_MASK) - 1;
            frac = idx - floorf(idx);
            return -(sinTable[i] + (sinTable[i+1] - sinTable[i]) * (1.0f - frac));
    }
    return 0.0f;
}

static float spv_FastCos(float x)
{
    return spv_FastSin(x + 1.57079632679f); /* cos(x) = sin(x + PI/2) */
}

/****************************************************************************/
/* Helpers                                                                  */
/****************************************************************************/

/* Extract opcode and word count from an instruction word */
#define SPV_OP(word)        ((word) & 0xFFFFu)
#define SPV_WORDCOUNT(word) ((word) >> 16u)

/* Get component count for a resolved type */
uint32_t spv_TypeComponentCount(const SpvModule *module, uint32_t typeId)
{
    if (typeId >= SPV_MAX_IDS) return 1;

    const SpvTypeInfo *t = &module->types[typeId];
    switch (t->opcode)
    {
        case SpvOpTypeFloat:
        case SpvOpTypeInt:
        case SpvOpTypeBool:
            return 1;

        case SpvOpTypeVector:
            return t->componentCount;

        case SpvOpTypeMatrix:
        {
            /* Matrix: componentCount = column count, componentTypeId = column vector type */
            uint32_t colSize = spv_TypeComponentCount(module, t->componentTypeId);
            return t->componentCount * colSize;
        }

        case SpvOpTypeArray:
        {
            uint32_t elemSize = spv_TypeComponentCount(module, t->componentTypeId);
            return t->componentCount * elemSize;
        }

        case SpvOpTypeStruct:
        {
            uint32_t total = 0;
            for (uint32_t i = 0; i < t->memberCount; i++)
                total += spv_TypeComponentCount(module, t->memberTypeIds[i]);
            return total;
        }

        case SpvOpTypePointer:
            return spv_TypeComponentCount(module, t->pointeeTypeId);

        default:
            return 1;
    }
}

/* Get the component count of a struct member at a given index */
static uint32_t spv_StructMemberOffset(const SpvModule *module,
                                        uint32_t structTypeId,
                                        uint32_t memberIndex)
{
    const SpvTypeInfo *st = &module->types[structTypeId];
    uint32_t offset = 0;
    for (uint32_t i = 0; i < memberIndex && i < st->memberCount; i++)
        offset += spv_TypeComponentCount(module, st->memberTypeIds[i]);
    return offset;
}

/****************************************************************************/
/* Module parser                                                            */
/****************************************************************************/

int spv_ParseModule(SpvModule *module, const uint32_t *code, uint32_t wordCount)
{
    /* Initialise fast sin/cos table (once, first parse) */
    spv_InitSinTable();

    if (wordCount < 5)
        return -1;

    /* Validate magic number (already byte-swapped) */
    if (code[0] != SPV_MAGIC_NUMBER)
    {
        IExec->DebugPrintF("[software_vk] SPIR-V magic mismatch: 0x%08lx\n",
                           (unsigned long)code[0]);
        return -1;
    }

    /* Initialize tables */
    memset(module->types, 0, sizeof(module->types));
    memset(module->constants, 0, sizeof(module->constants));
    memset(module->pointers, 0, sizeof(module->pointers));

    for (uint32_t i = 0; i < SPV_MAX_IDS; i++)
    {
        module->decorations[i].location      = -1;
        module->decorations[i].builtIn       = -1;
        module->decorations[i].binding       = -1;
        module->decorations[i].descriptorSet = -1;
        for (uint32_t j = 0; j < SPV_MAX_MEMBERS; j++)
        {
            module->memberDecorations[i].builtIn[j]  = -1;
            module->memberDecorations[i].location[j]  = -1;
            module->memberDecorations[i].offset[j]    = -1;
        }
    }

    module->bound = code[3];
    module->entryPointId = 0;
    module->entryFunctionOffset = 0;
    module->templateValues = NULL;
    module->templatePtrs   = NULL;
    module->templateBound  = 0;
    module->templateBuilt  = 0;

    /* Walk all instructions */
    uint32_t pc = 5;  /* Skip header */
    while (pc < wordCount)
    {
        uint32_t instrWord = code[pc];
        uint32_t wc = SPV_WORDCOUNT(instrWord);
        uint32_t op = SPV_OP(instrWord);

        if (wc == 0 || pc + wc > wordCount)
            break;

        switch (op)
        {
        case SpvOpEntryPoint:
            /* Word 2 = execution model, Word 3 = function ID */
            if (wc >= 3)
                module->entryPointId = code[pc + 2];
            break;

        case SpvOpDecorate:
            /* Word 1 = target ID, Word 2 = decoration, Word 3 = value */
            if (wc >= 3)
            {
                uint32_t id  = code[pc + 1];
                uint32_t dec = code[pc + 2];
                if (id < SPV_MAX_IDS)
                {
                    if (dec == SPV_DECORATION_LOCATION && wc >= 4)
                        module->decorations[id].location = (int32_t)code[pc + 3];
                    else if (dec == SPV_DECORATION_BUILTIN && wc >= 4)
                        module->decorations[id].builtIn = (int32_t)code[pc + 3];
                    else if (dec == SPV_DECORATION_BINDING && wc >= 4)
                        module->decorations[id].binding = (int32_t)code[pc + 3];
                    else if (dec == SPV_DECORATION_DESCRIPTOR_SET && wc >= 4)
                        module->decorations[id].descriptorSet = (int32_t)code[pc + 3];
                }
            }
            break;

        case SpvOpMemberDecorate:
            /* Word 1 = struct type ID, Word 2 = member, Word 3 = decoration */
            if (wc >= 4)
            {
                uint32_t id     = code[pc + 1];
                uint32_t member = code[pc + 2];
                uint32_t dec    = code[pc + 3];
                if (id < SPV_MAX_IDS && member < SPV_MAX_MEMBERS)
                {
                    if (dec == SPV_DECORATION_BUILTIN && wc >= 5)
                        module->memberDecorations[id].builtIn[member] = (int32_t)code[pc + 4];
                    else if (dec == SPV_DECORATION_LOCATION && wc >= 5)
                        module->memberDecorations[id].location[member] = (int32_t)code[pc + 4];
                    else if (dec == SPV_DECORATION_OFFSET && wc >= 5)
                        module->memberDecorations[id].offset[member] = (int32_t)code[pc + 4];
                }
            }
            break;

        case SpvOpTypeVoid:
        case SpvOpTypeBool:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
                module->types[code[pc + 1]].opcode = (uint16_t)op;
            break;

        case SpvOpTypeFloat:
            if (wc >= 3 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode = (uint16_t)op;
                module->types[id].width  = (uint16_t)code[pc + 2];
            }
            break;

        case SpvOpTypeInt:
            if (wc >= 4 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode     = (uint16_t)op;
                module->types[id].width      = (uint16_t)code[pc + 2];
                module->types[id].signedness = (uint16_t)code[pc + 3];
            }
            break;

        case SpvOpTypeVector:
            if (wc >= 4 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode          = (uint16_t)op;
                module->types[id].componentTypeId = code[pc + 2];
                module->types[id].componentCount  = (uint16_t)code[pc + 3];
            }
            break;

        case SpvOpTypeMatrix:
            /* componentCount = column count, componentTypeId = column vector type */
            if (wc >= 4 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode          = (uint16_t)op;
                module->types[id].componentTypeId = code[pc + 2];
                module->types[id].componentCount  = (uint16_t)code[pc + 3];
            }
            break;

        case SpvOpTypeArray:
            if (wc >= 4 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode          = (uint16_t)op;
                module->types[id].componentTypeId = code[pc + 2];
                /* Length is a constant ID -- resolve it */
                uint32_t lenId = code[pc + 3];
                if (lenId < SPV_MAX_IDS && module->constants[lenId].isSet)
                    module->types[id].componentCount = (uint16_t)module->constants[lenId].i[0];
                else
                    module->types[id].componentCount = 0;
            }
            break;

        case SpvOpTypeStruct:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode      = (uint16_t)op;
                uint32_t mc = wc - 2;
                if (mc > SPV_MAX_MEMBERS) mc = SPV_MAX_MEMBERS;
                module->types[id].memberCount = (uint16_t)mc;
                for (uint32_t m = 0; m < mc; m++)
                    module->types[id].memberTypeIds[m] = code[pc + 2 + m];
            }
            break;

        case SpvOpTypeImage:
            /* Word 1 = result, Word 2 = sampled type, rest = image params */
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode = (uint16_t)op;
                if (wc >= 3)
                    module->types[id].componentTypeId = code[pc + 2]; /* sampled type */
            }
            break;

        case SpvOpTypeSampler:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
                module->types[code[pc + 1]].opcode = (uint16_t)op;
            break;

        case SpvOpTypeSampledImage:
            /* Word 1 = result, Word 2 = image type */
            if (wc >= 3 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode = (uint16_t)op;
                module->types[id].componentTypeId = code[pc + 2]; /* underlying image type */
            }
            break;

        case SpvOpTypePointer:
            if (wc >= 4 && code[pc + 1] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 1];
                module->types[id].opcode       = (uint16_t)op;
                module->types[id].storageClass = code[pc + 2];
                module->types[id].pointeeTypeId = code[pc + 3];
            }
            break;

        case SpvOpTypeFunction:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
                module->types[code[pc + 1]].opcode = (uint16_t)op;
            break;

        case SpvOpConstant:
            /* Word 1 = type, Word 2 = result ID, Word 3+ = value */
            if (wc >= 4 && code[pc + 2] < SPV_MAX_IDS)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                module->constants[id].isSet = 1;
                module->constants[id].count = 1;
                /* Copy raw bits -- works for both float and int */
                uint32_t rawBits = code[pc + 3];
                memcpy(&module->constants[id].f[0], &rawBits, 4);
                memcpy(&module->constants[id].i[0], &rawBits, 4);
                (void)typeId;
            }
            break;

        case SpvOpConstantTrue:
            if (wc >= 3 && code[pc + 2] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 2];
                module->constants[id].isSet = 1;
                module->constants[id].count = 1;
                module->constants[id].i[0]  = 1;
                module->constants[id].f[0]  = 1.0f;
            }
            break;

        case SpvOpConstantFalse:
            if (wc >= 3 && code[pc + 2] < SPV_MAX_IDS)
            {
                uint32_t id = code[pc + 2];
                module->constants[id].isSet = 1;
                module->constants[id].count = 1;
                module->constants[id].i[0]  = 0;
                module->constants[id].f[0]  = 0.0f;
            }
            break;

        case SpvOpConstantComposite:
            /* Word 1 = type, Word 2 = result ID, Word 3+ = constituent IDs */
            if (wc >= 3 && code[pc + 2] < SPV_MAX_IDS)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t nConstituents = wc - 3;
                uint32_t offset = 0;

                module->constants[id].isSet = 1;

                for (uint32_t c = 0; c < nConstituents; c++)
                {
                    uint32_t cid = code[pc + 3 + c];
                    if (cid < SPV_MAX_IDS && module->constants[cid].isSet)
                    {
                        uint32_t cc = module->constants[cid].count;
                        if (cc == 0) cc = 1;
                        for (uint32_t j = 0; j < cc && (offset + j) < SPV_MAX_COMPONENTS; j++)
                        {
                            module->constants[id].f[offset + j] = module->constants[cid].f[j];
                            module->constants[id].i[offset + j] = module->constants[cid].i[j];
                        }
                        offset += cc;
                    }
                }
                module->constants[id].count = offset;
                (void)typeId;
            }
            break;

        case SpvOpVariable:
            /* Word 1 = pointer type, Word 2 = result ID, Word 3 = storage class */
            if (wc >= 4 && code[pc + 2] < SPV_MAX_IDS)
            {
                uint32_t ptrTypeId    = code[pc + 1];
                uint32_t id           = code[pc + 2];
                uint32_t storageClass = code[pc + 3];

                module->pointers[id].targetId     = id;
                module->pointers[id].offset       = 0;
                module->pointers[id].storageClass = storageClass;
                module->pointers[id].isPointer    = 1;

                if (ptrTypeId < SPV_MAX_IDS)
                    module->pointers[id].typeId = module->types[ptrTypeId].pointeeTypeId;
            }
            break;

        case SpvOpFunction:
            /* Word 2 = result ID -- check if this is the entry point */
            if (wc >= 3 && code[pc + 2] == module->entryPointId)
                module->entryFunctionOffset = pc;
            break;

        case SpvOpExtInst:
        case SpvOpFunctionParameter:
        case SpvOpFunctionCall:
        case SpvOpPhi:
        case SpvOpLoopMerge:
        case SpvOpSelectionMerge:
        case SpvOpBranchConditional:
            /* Runtime-only opcodes -- skip during parsing */
            break;

        default:
            break;
        }

        pc += wc;
    }

    /* Re-resolve array lengths that depend on constants parsed later */
    pc = 5;
    while (pc < wordCount)
    {
        uint32_t instrWord = code[pc];
        uint32_t wc = SPV_WORDCOUNT(instrWord);
        uint32_t op = SPV_OP(instrWord);

        if (wc == 0 || pc + wc > wordCount) break;

        if (op == SpvOpTypeArray && wc >= 4)
        {
            uint32_t id    = code[pc + 1];
            uint32_t lenId = code[pc + 3];
            if (id < SPV_MAX_IDS && lenId < SPV_MAX_IDS &&
                module->types[id].componentCount == 0 &&
                module->constants[lenId].isSet)
            {
                module->types[id].componentCount = (uint16_t)module->constants[lenId].i[0];
            }
        }

        pc += wc;
    }

    IExec->DebugPrintF("[software_vk] SPIR-V parsed: %lu words, bound=%lu, entry=%lu\n",
                       (unsigned long)wordCount,
                       (unsigned long)module->bound,
                       (unsigned long)module->entryPointId);

    return (module->entryPointId != 0) ? 0 : -1;
}

/****************************************************************************/
/* Interpreter                                                              */
/****************************************************************************/

/* Set up input/output variables in the execution state from decorations */
static void spv_SetupIO(SpvExecState *state)
{
    SpvModule *mod = state->module;

    for (uint32_t id = 0; id < mod->bound && id < SPV_MAX_IDS; id++)
    {
        if (!mod->pointers[id].isPointer)
            continue;

        uint32_t sc = mod->pointers[id].storageClass;
        uint32_t typeId = mod->pointers[id].typeId;
        uint32_t cc = spv_TypeComponentCount(mod, typeId);

        if (sc == SPV_STORAGE_INPUT)
        {
            /* Check for built-in input */
            if (mod->decorations[id].builtIn == SPV_BUILTIN_VERTEX_INDEX)
            {
                state->values[id].i[0] = (int32_t)state->vertexIndex;
                /* f[] and i[] are a union -- no sync needed */
                state->values[id].count = 1;
                state->values[id].isSet = 1;
            }
            else if (mod->decorations[id].builtIn == SPV_BUILTIN_INSTANCE_INDEX)
            {
                state->values[id].i[0] = (int32_t)state->instanceIndex;
                /* f[] and i[] are a union -- no sync needed */
                state->values[id].count = 1;
                state->values[id].isSet = 1;
            }
            else if (mod->decorations[id].location >= 0)
            {
                int32_t loc = mod->decorations[id].location;
                if (loc < SPV_MAX_IO_LOCATIONS)
                {
                    uint32_t n = cc < 4 ? cc : 4;
                    for (uint32_t j = 0; j < n; j++)
                        state->values[id].f[j] = state->inputs[loc][j];
                    state->values[id].count = n;
                    state->values[id].isSet = 1;
                }
            }
            /* Also check struct members for built-ins (gl_PerVertex) */
            if (mod->types[typeId].opcode == SpvOpTypeStruct)
            {
                for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                {
                    if (mod->memberDecorations[typeId].builtIn[m] == SPV_BUILTIN_VERTEX_INDEX)
                    {
                        uint32_t off = spv_StructMemberOffset(mod, typeId, m);
                        state->values[id].i[off] = (int32_t)state->vertexIndex;
                        /* f[] and i[] are a union -- no sync needed */
                        state->values[id].count = off + 1;
                        state->values[id].isSet = 1;
                    }
                }
            }
        }
        else if (sc == SPV_STORAGE_PUSH_CONSTANT)
        {
            /* Push constant variable: read member data from pushConstants[] byte array.
            ** The variable's type (through the pointer) is a struct. Each member has
            ** an Offset decoration specifying the byte offset into the push constant block. */
            if (state->pushConstantSize > 0 && typeId < SPV_MAX_IDS &&
                mod->types[typeId].opcode == SpvOpTypeStruct)
            {
                uint32_t compOff = 0;
                for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                {
                    int32_t byteOff = mod->memberDecorations[typeId].offset[m];
                    uint32_t memberTypeId = mod->types[typeId].memberTypeIds[m];
                    uint32_t memberCC = spv_TypeComponentCount(mod, memberTypeId);

                    if (byteOff >= 0 && (uint32_t)byteOff + memberCC * 4 <= state->pushConstantSize)
                    {
                        for (uint32_t j = 0; j < memberCC && (compOff + j) < SPV_MAX_COMPONENTS; j++)
                        {
                            uint32_t rawBits;
                            memcpy(&rawBits, &state->pushConstants[(uint32_t)byteOff + j * 4], 4);
                            memcpy(&state->values[id].f[compOff + j], &rawBits, 4);
                            memcpy(&state->values[id].i[compOff + j], &rawBits, 4);
                        }
                    }
                    compOff += memberCC;
                }
                state->values[id].count = compOff < SPV_MAX_COMPONENTS ? compOff : SPV_MAX_COMPONENTS;
                state->values[id].isSet = 1;
            }
        }
        else if (sc == SPV_STORAGE_UNIFORM)
        {
            /* Uniform buffer variable: read member data from
            ** the bound descriptor set's buffer. The variable has Binding and
            ** DescriptorSet decorations that index into state->uniformData[].
            ** The variable's type is a struct with Offset-decorated members. */
            int32_t setIdx     = mod->decorations[id].descriptorSet;
            int32_t bindingIdx = mod->decorations[id].binding;

            if (setIdx >= 0 && bindingIdx >= 0)
            {
                /* Index into the flat uniformData array */
                uint32_t uIdx = (uint32_t)setIdx * SPV_MAX_BINDINGS_PER_SET + (uint32_t)bindingIdx;

                if (uIdx < SPV_MAX_UNIFORM_BINDINGS &&
                    state->uniformData[uIdx] != NULL &&
                    typeId < SPV_MAX_IDS &&
                    mod->types[typeId].opcode == SpvOpTypeStruct)
                {
                    const uint8_t *uData = state->uniformData[uIdx];
                    uint32_t uSize = state->uniformSizes[uIdx];

                    uint32_t compOff = 0;
                    for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                    {
                        int32_t byteOff = mod->memberDecorations[typeId].offset[m];
                        uint32_t memberTypeId = mod->types[typeId].memberTypeIds[m];
                        uint32_t memberCC = spv_TypeComponentCount(mod, memberTypeId);

                        if (byteOff >= 0 && (uint32_t)byteOff + memberCC * 4 <= uSize)
                        {
                            for (uint32_t j = 0; j < memberCC && (compOff + j) < SPV_MAX_COMPONENTS; j++)
                            {
                                uint32_t rawBits;
                                memcpy(&rawBits, &uData[(uint32_t)byteOff + j * 4], 4);
                                memcpy(&state->values[id].f[compOff + j], &rawBits, 4);
                                memcpy(&state->values[id].i[compOff + j], &rawBits, 4);
                            }
                        }
                        compOff += memberCC;
                    }
                    state->values[id].count = compOff < SPV_MAX_COMPONENTS ? compOff : SPV_MAX_COMPONENTS;
                    state->values[id].isSet = 1;
                }
            }
        }
        else if (sc == SPV_STORAGE_UNIFORM_CONSTANT)
        {
            /* UniformConstant: sampler/image variables.
            ** Store the texture slot index (set*MAX_BINDINGS+binding) so
            ** OpLoad -> OpSampledImage -> OpImageSampleImplicitLod can
            ** find the correct texture data in state->textures[]. */
            int32_t setIdx     = mod->decorations[id].descriptorSet;
            int32_t bindingIdx = mod->decorations[id].binding;

            if (setIdx >= 0 && bindingIdx >= 0)
            {
                int32_t slot = setIdx * SPV_MAX_BINDINGS_PER_SET + bindingIdx;
                if (slot < SPV_MAX_TEXTURES)
                {
                    state->values[id].i[0] = slot;
                    state->values[id].f[0] = 0.0f;
                    state->values[id].count = 1;
                    state->values[id].isSet = 1;
                }
            }
        }
        else if (sc == SPV_STORAGE_OUTPUT)
        {
            /* Initialize output storage to zero */
            memset(&state->values[id], 0, sizeof(SpvValue));
            state->values[id].count = cc < SPV_MAX_COMPONENTS ? cc : SPV_MAX_COMPONENTS;
            state->values[id].isSet = 1;
        }
    }

    /* Copy constants into runtime values */
    for (uint32_t id = 0; id < mod->bound && id < SPV_MAX_IDS; id++)
    {
        if (mod->constants[id].isSet)
        {
            state->values[id] = mod->constants[id];
            state->ptrs[id]   = mod->pointers[id];
        }
        if (mod->pointers[id].isPointer)
            state->ptrs[id] = mod->pointers[id];
    }
}

/* Write outputs back from execution state */
static void spv_CollectOutputs(SpvExecState *state)
{
    SpvModule *mod = state->module;

    for (uint32_t id = 0; id < mod->bound && id < SPV_MAX_IDS; id++)
    {
        if (!mod->pointers[id].isPointer)
            continue;
        if (mod->pointers[id].storageClass != SPV_STORAGE_OUTPUT)
            continue;

        uint32_t typeId = mod->pointers[id].typeId;
        const SpvValue *val = &state->values[id];

        /* Check for built-in outputs */
        if (mod->decorations[id].builtIn == SPV_BUILTIN_POSITION)
        {
            for (uint32_t j = 0; j < 4 && j < val->count; j++)
                state->position[j] = val->f[j];
        }
        else if (mod->types[typeId].opcode == SpvOpTypeStruct)
        {
            /* gl_PerVertex struct -- check member decorations */
            for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
            {
                if (mod->memberDecorations[typeId].builtIn[m] == SPV_BUILTIN_POSITION)
                {
                    uint32_t off = spv_StructMemberOffset(mod, typeId, m);
                    for (uint32_t j = 0; j < 4 && (off + j) < val->count; j++)
                        state->position[j] = val->f[off + j];
                }
            }
        }

        /* User-defined outputs by location */
        if (mod->decorations[id].location >= 0)
        {
            int32_t loc = mod->decorations[id].location;
            if (loc < SPV_MAX_IO_LOCATIONS)
            {
                uint32_t cc = spv_TypeComponentCount(mod, typeId);
                uint32_t n = cc < 4 ? cc : 4;
                for (uint32_t j = 0; j < n; j++)
                    state->outputs[loc][j] = val->f[j];
                state->outputCounts[loc] = n;
            }
        }
    }
}

/****************************************************************************/
/* Texture sampling helper                                                  */
/****************************************************************************/

static void swvk_SampleTexture(SpvExecState *state, int texIdx,
                                float u, float v, SpvValue *result)
{
    const uint8_t *data = state->textures[texIdx].data;
    uint32_t w = state->textures[texIdx].width;
    uint32_t h = state->textures[texIdx].height;
    uint32_t filter = state->textures[texIdx].magFilter;
    uint32_t addrU = state->textures[texIdx].addressModeU;
    uint32_t addrV = state->textures[texIdx].addressModeV;

    if (!data || w == 0 || h == 0)
    {
        /* No data -- return magenta for debugging */
        result->f[0] = 1.0f;
        result->f[1] = 0.0f;
        result->f[2] = 1.0f;
        result->f[3] = 1.0f;
        result->count = 4;
        result->isSet = 1;
        return;
    }

    /* Apply address mode */
    if (addrU == 0) /* REPEAT */
    {
        u = u - floorf(u);
        if (u < 0.0f) u += 1.0f;
    }
    else /* CLAMP_TO_EDGE */
    {
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
    }
    if (addrV == 0)
    {
        v = v - floorf(v);
        if (v < 0.0f) v += 1.0f;
    }
    else
    {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
    }

    if (filter == 0)
    {
        /* NEAREST */
        int px = (int)(u * (float)w) % (int)w;
        int py = (int)(v * (float)h) % (int)h;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if ((uint32_t)px >= w) px = (int)(w - 1);
        if ((uint32_t)py >= h) py = (int)(h - 1);
        const uint8_t *pixel = data + ((uint32_t)py * w + (uint32_t)px) * 4;
        result->f[0] = pixel[0] / 255.0f;
        result->f[1] = pixel[1] / 255.0f;
        result->f[2] = pixel[2] / 255.0f;
        result->f[3] = pixel[3] / 255.0f;
    }
    else
    {
        /* LINEAR (bilinear filtering) */
        float fx = u * (float)w - 0.5f;
        float fy = v * (float)h - 0.5f;
        int x0 = (int)floorf(fx);
        int y0 = (int)floorf(fy);
        float fracX = fx - floorf(fx);
        float fracY = fy - floorf(fy);

        int x1 = x0 + 1;
        int y1 = y0 + 1;

        /* Wrap/clamp coordinates */
        if (addrU == 0)
        {
            x0 = ((x0 % (int)w) + (int)w) % (int)w;
            x1 = ((x1 % (int)w) + (int)w) % (int)w;
        }
        else
        {
            if (x0 < 0) x0 = 0;
            if (x0 >= (int)w) x0 = (int)w - 1;
            if (x1 < 0) x1 = 0;
            if (x1 >= (int)w) x1 = (int)w - 1;
        }
        if (addrV == 0)
        {
            y0 = ((y0 % (int)h) + (int)h) % (int)h;
            y1 = ((y1 % (int)h) + (int)h) % (int)h;
        }
        else
        {
            if (y0 < 0) y0 = 0;
            if (y0 >= (int)h) y0 = (int)h - 1;
            if (y1 < 0) y1 = 0;
            if (y1 >= (int)h) y1 = (int)h - 1;
        }

        const uint8_t *p00 = data + ((uint32_t)y0 * w + (uint32_t)x0) * 4;
        const uint8_t *p10 = data + ((uint32_t)y0 * w + (uint32_t)x1) * 4;
        const uint8_t *p01 = data + ((uint32_t)y1 * w + (uint32_t)x0) * 4;
        const uint8_t *p11 = data + ((uint32_t)y1 * w + (uint32_t)x1) * 4;

        for (int c = 0; c < 4; c++)
        {
            float top = p00[c] * (1.0f - fracX) + p10[c] * fracX;
            float bot = p01[c] * (1.0f - fracX) + p11[c] * fracX;
            result->f[c] = (top * (1.0f - fracY) + bot * fracY) / 255.0f;
        }
    }

    result->count = 4;
    result->isSet = 1;
}

int spv_Execute(SpvExecState *state)
{
    SpvModule *mod = state->module;

    if (!mod || mod->entryFunctionOffset == 0)
        return -1;

    /* Initialize I/O and constants.
    ** If a template is available, use fast dynamic-only setup;
    ** otherwise fall back to full spv_SetupIO. */
    if (mod->templateBuilt && mod->templateValues && mod->templatePtrs)
        spv_SetupIODynamic(state);
    else
        spv_SetupIO(state);

    const uint32_t *code = mod->code;
    uint32_t pc = mod->entryFunctionOffset;
    uint32_t endPc = mod->wordCount;
    int inFunction = 0;

    /* Control flow: label map, predecessor tracking */
    uint32_t prevLabel = 0, currentLabel = 0;

    /* Build label-to-PC map for branching */
    uint32_t labelPCs[SPV_MAX_IDS];
    memset(labelPCs, 0, sizeof(labelPCs));
    {
        uint32_t scanPc = mod->entryFunctionOffset;
        while (scanPc < endPc)
        {
            uint32_t sw = SPV_WORDCOUNT(code[scanPc]);
            uint32_t sop = SPV_OP(code[scanPc]);
            if (sw == 0) break;
            if (sop == SpvOpLabel && sw >= 2)
            {
                uint32_t lid = code[scanPc + 1];
                if (lid < SPV_MAX_IDS) labelPCs[lid] = scanPc;
            }
            /* Stop at FunctionEnd so we don't scan into other functions */
            if (sop == SpvOpFunctionEnd) break;
            scanPc += sw;
        }
    }

    /* Function call stack (single-level, non-recursive) */
    uint32_t callReturnPC = 0;
    int inCall = 0;

    /* Max iteration count to prevent infinite loops */
    uint32_t maxIter = 100000;

    while (pc < endPc && maxIter-- > 0)
    {
        uint32_t instrWord = code[pc];
        uint32_t wc = SPV_WORDCOUNT(instrWord);
        uint32_t op = SPV_OP(instrWord);

        if (wc == 0 || pc + wc > endPc)
            break;

        switch (op)
        {
        case SpvOpFunction:
            inFunction = 1;
            break;

        case SpvOpFunctionEnd:
            if (inCall)
            {
                /* Return from called function to caller */
                inCall = 0;
                pc = callReturnPC;

                /* Clear stale labels from called function, then rebuild */
                memset(labelPCs, 0, sizeof(labelPCs));
                {
                    uint32_t scanPc4 = mod->entryFunctionOffset;
                    while (scanPc4 < endPc)
                    {
                        uint32_t sw4 = SPV_WORDCOUNT(code[scanPc4]);
                        uint32_t sop4 = SPV_OP(code[scanPc4]);
                        if (sw4 == 0) break;
                        if (sop4 == SpvOpLabel && sw4 >= 2)
                        {
                            uint32_t lid = code[scanPc4 + 1];
                            if (lid < SPV_MAX_IDS) labelPCs[lid] = scanPc4;
                        }
                        if (sop4 == SpvOpFunctionEnd) break;
                        scanPc4 += sw4;
                    }
                }
                continue;
            }
            if (inFunction)
                goto done;
            break;

        case SpvOpLabel:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
            {
                prevLabel = currentLabel;
                currentLabel = code[pc + 1];
            }
            break;

        case SpvOpReturn:
            if (inCall)
            {
                inCall = 0;
                pc = callReturnPC;

                /* Clear stale labels from called function, then rebuild */
                memset(labelPCs, 0, sizeof(labelPCs));
                {
                    uint32_t scanPc5 = mod->entryFunctionOffset;
                    while (scanPc5 < endPc)
                    {
                        uint32_t sw5 = SPV_WORDCOUNT(code[scanPc5]);
                        uint32_t sop5 = SPV_OP(code[scanPc5]);
                        if (sw5 == 0) break;
                        if (sop5 == SpvOpLabel && sw5 >= 2)
                        {
                            uint32_t lid = code[scanPc5 + 1];
                            if (lid < SPV_MAX_IDS) labelPCs[lid] = scanPc5;
                        }
                        if (sop5 == SpvOpFunctionEnd) break;
                        scanPc5 += sw5;
                    }
                }
                continue;
            }
            goto done;

        case SpvOpReturnValue:
            if (inCall && wc >= 2)
            {
                /* Store return value: find the OpFunctionCall that invoked us
                ** and write the returned value to the call's result ID.
                ** callReturnPC points to the instruction *after* OpFunctionCall,
                ** so the call instruction is right before it. We find it by
                ** scanning back. Instead, we stored callReturnPC = pc+wc of
                ** the call, so code[callReturnPC - wc_of_call] is the call.
                ** Simpler: just look at the call instruction at callReturnPC
                ** minus its word count. We can scan backwards. */
                {
                    /* The return value ID */
                    uint32_t retValId = code[pc + 1];

                    /* Find the OpFunctionCall instruction just before callReturnPC */
                    /* Scan from entry to find the call whose (pc+wc)==callReturnPC */
                    uint32_t scanPc6 = 5;
                    while (scanPc6 < callReturnPC)
                    {
                        uint32_t sw6 = SPV_WORDCOUNT(code[scanPc6]);
                        if (sw6 == 0) break;
                        if (scanPc6 + sw6 == callReturnPC &&
                            SPV_OP(code[scanPc6]) == SpvOpFunctionCall &&
                            sw6 >= 4)
                        {
                            uint32_t callResultId = code[scanPc6 + 2];
                            if (callResultId < SPV_MAX_IDS && retValId < SPV_MAX_IDS)
                                state->values[callResultId] = state->values[retValId];
                            break;
                        }
                        scanPc6 += sw6;
                    }
                }

                inCall = 0;
                pc = callReturnPC;

                /* Clear stale labels from called function, then rebuild */
                memset(labelPCs, 0, sizeof(labelPCs));
                {
                    uint32_t scanPc7 = mod->entryFunctionOffset;
                    while (scanPc7 < endPc)
                    {
                        uint32_t sw7 = SPV_WORDCOUNT(code[scanPc7]);
                        uint32_t sop7 = SPV_OP(code[scanPc7]);
                        if (sw7 == 0) break;
                        if (sop7 == SpvOpLabel && sw7 >= 2)
                        {
                            uint32_t lid = code[scanPc7 + 1];
                            if (lid < SPV_MAX_IDS) labelPCs[lid] = scanPc7;
                        }
                        if (sop7 == SpvOpFunctionEnd) break;
                        scanPc7 += sw7;
                    }
                }
                continue;
            }
            goto done;

        case SpvOpBranch:
        {
            if (wc >= 2)
            {
                uint32_t targetLbl = code[pc + 1];
                if (targetLbl < SPV_MAX_IDS && labelPCs[targetLbl] > 0)
                {
                    prevLabel = currentLabel;
                    currentLabel = targetLbl;
                    pc = labelPCs[targetLbl];
                    continue;
                }
            }
            break;
        }

        case SpvOpVariable:
        {
            /* Function-local variable */
            if (wc >= 4)
            {
                uint32_t ptrTypeId = code[pc + 1];
                uint32_t id        = code[pc + 2];
                uint32_t sc        = code[pc + 3];

                if (id < SPV_MAX_IDS)
                {
                    state->ptrs[id].targetId     = id;
                    state->ptrs[id].offset       = 0;
                    state->ptrs[id].storageClass = sc;
                    state->ptrs[id].isPointer    = 1;
                    if (ptrTypeId < SPV_MAX_IDS)
                        state->ptrs[id].typeId = mod->types[ptrTypeId].pointeeTypeId;

                    uint32_t cc = spv_TypeComponentCount(mod, state->ptrs[id].typeId);
                    memset(&state->values[id], 0, sizeof(SpvValue));
                    state->values[id].count = cc < SPV_MAX_COMPONENTS ? cc : SPV_MAX_COMPONENTS;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpStore:
        {
            /* Word 1 = pointer ID, Word 2 = value ID */
            if (wc >= 3)
            {
                uint32_t ptrId = code[pc + 1];
                uint32_t valId = code[pc + 2];

                if (ptrId < SPV_MAX_IDS && valId < SPV_MAX_IDS &&
                    state->ptrs[ptrId].isPointer)
                {
                    uint32_t tgt    = state->ptrs[ptrId].targetId;
                    uint32_t off    = state->ptrs[ptrId].offset;
                    const SpvValue *src = &state->values[valId];
                    uint32_t cc = src->count;
                    if (cc == 0) cc = 1;

                    if (tgt < SPV_MAX_IDS)
                    {
                        for (uint32_t j = 0; j < cc && (off + j) < SPV_MAX_COMPONENTS; j++)
                        {
                            state->values[tgt].f[off + j] = src->f[j];
                        }
                        state->values[tgt].isSet = 1;
                    }
                }
            }
            break;
        }

        case SpvOpLoad:
        {
            /* Word 1 = type, Word 2 = result ID, Word 3 = pointer ID */
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t ptrId  = code[pc + 3];

                if (id < SPV_MAX_IDS && ptrId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    uint32_t tgt = state->ptrs[ptrId].isPointer
                                 ? state->ptrs[ptrId].targetId : ptrId;
                    uint32_t off = state->ptrs[ptrId].isPointer
                                 ? state->ptrs[ptrId].offset : 0;

                    if (tgt < SPV_MAX_IDS)
                    {
                        state->values[id].count = cc;
                        state->values[id].isSet = 1;
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            uint32_t si = off + j;
                            if (si < SPV_MAX_COMPONENTS)
                            {
                                state->values[id].f[j] = state->values[tgt].f[si];
                            }
                        }
                    }
                }
            }
            break;
        }

        case SpvOpAccessChain:
        {
            /* Word 1 = type, Word 2 = result, Word 3 = base ptr, Word 4+ = indices */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t baseId = code[pc + 3];

                if (id < SPV_MAX_IDS && baseId < SPV_MAX_IDS)
                {
                    uint32_t tgt = state->ptrs[baseId].isPointer
                                 ? state->ptrs[baseId].targetId : baseId;
                    uint32_t baseOff = state->ptrs[baseId].isPointer
                                     ? state->ptrs[baseId].offset : 0;

                    /* Walk through index chain to compute offset */
                    uint32_t curTypeId = state->ptrs[baseId].isPointer
                                       ? state->ptrs[baseId].typeId
                                       : typeId;
                    uint32_t offset = baseOff;

                    for (uint32_t idx = 4; idx < wc; idx++)
                    {
                        uint32_t indexId = code[pc + idx];
                        int32_t indexVal = 0;
                        if (indexId < SPV_MAX_IDS)
                            indexVal = state->values[indexId].i[0];
                        if (indexVal < 0) indexVal = 0;

                        const SpvTypeInfo *ct = &mod->types[curTypeId];

                        if (ct->opcode == SpvOpTypeStruct)
                        {
                            offset += spv_StructMemberOffset(mod, curTypeId, (uint32_t)indexVal);
                            if ((uint32_t)indexVal < ct->memberCount)
                                curTypeId = ct->memberTypeIds[indexVal];
                        }
                        else if (ct->opcode == SpvOpTypeArray || ct->opcode == SpvOpTypeVector ||
                                 ct->opcode == SpvOpTypeMatrix)
                        {
                            uint32_t elemSize = spv_TypeComponentCount(mod, ct->componentTypeId);
                            offset += (uint32_t)indexVal * elemSize;
                            curTypeId = ct->componentTypeId;
                        }
                    }

                    state->ptrs[id].targetId     = tgt;
                    state->ptrs[id].offset       = offset;
                    state->ptrs[id].storageClass = state->ptrs[baseId].storageClass;
                    state->ptrs[id].isPointer    = 1;

                    if (typeId < SPV_MAX_IDS)
                        state->ptrs[id].typeId = mod->types[typeId].pointeeTypeId;
                }
            }
            break;
        }

        case SpvOpCompositeConstruct:
        {
            /* Word 1 = type, Word 2 = result, Word 3+ = constituents */
            if (wc >= 3)
            {
                uint32_t id = code[pc + 2];
                if (id < SPV_MAX_IDS)
                {
                    uint32_t offset = 0;
                    state->values[id].isSet = 1;

                    for (uint32_t c = 3; c < wc; c++)
                    {
                        uint32_t cid = code[pc + c];
                        if (cid < SPV_MAX_IDS)
                        {
                            uint32_t cc = state->values[cid].count;
                            if (cc == 0) cc = 1;
                            for (uint32_t j = 0; j < cc && (offset + j) < SPV_MAX_COMPONENTS; j++)
                            {
                                state->values[id].f[offset + j] = state->values[cid].f[j];
                                state->values[id].i[offset + j] = state->values[cid].i[j];
                            }
                            offset += cc;
                        }
                    }
                    state->values[id].count = offset;
                }
            }
            break;
        }

        case SpvOpCompositeExtract:
        {
            /* Word 1 = type, Word 2 = result, Word 3 = composite, Word 4+ = indices */
            if (wc >= 5)
            {
                uint32_t typeId  = code[pc + 1];
                uint32_t id      = code[pc + 2];
                uint32_t compId  = code[pc + 3];
                uint32_t index   = code[pc + 4];

                if (id < SPV_MAX_IDS && compId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    /* For vector extract, index is the component */
                    if (cc == 1 && index < SPV_MAX_COMPONENTS)
                    {
                        state->values[id].f[0] = state->values[compId].f[index];
                        state->values[id].i[0] = state->values[compId].i[index];
                    }
                    else
                    {
                        /* Extracting a sub-composite */
                        for (uint32_t j = 0; j < cc && (index + j) < SPV_MAX_COMPONENTS; j++)
                        {
                            state->values[id].f[j] = state->values[compId].f[index + j];
                            state->values[id].i[j] = state->values[compId].i[index + j];
                        }
                    }
                }
            }
            break;
        }

        case SpvOpCompositeInsert:
        {
            if (wc >= 6)
            {
                uint32_t typeId  = code[pc + 1];
                uint32_t id      = code[pc + 2];
                uint32_t objId   = code[pc + 3];
                uint32_t compId  = code[pc + 4];
                uint32_t idx     = code[pc + 5];
                if (id < SPV_MAX_IDS && objId < SPV_MAX_IDS && compId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id] = state->values[compId];
                    if (idx < cc && idx < SPV_MAX_COMPONENTS)
                    {
                        state->values[id].f[idx] = state->values[objId].f[0];
                        state->values[id].i[idx] = state->values[objId].i[0];
                    }
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpVectorExtractDynamic:
        {
            if (wc >= 5)
            {
                uint32_t id     = code[pc + 2];
                uint32_t vecId  = code[pc + 3];
                uint32_t idxId  = code[pc + 4];
                if (id < SPV_MAX_IDS && vecId < SPV_MAX_IDS && idxId < SPV_MAX_IDS)
                {
                    uint32_t idx = (uint32_t)state->values[idxId].i[0];
                    if (idx >= SPV_MAX_COMPONENTS) idx = 0;
                    state->values[id].f[0] = state->values[vecId].f[idx];
                    state->values[id].i[0] = state->values[vecId].i[idx];
                    state->values[id].count = 1;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpVectorInsertDynamic:
        {
            if (wc >= 6)
            {
                uint32_t typeId  = code[pc + 1];
                uint32_t id      = code[pc + 2];
                uint32_t vecId   = code[pc + 3];
                uint32_t compId  = code[pc + 4];
                uint32_t idxId   = code[pc + 5];
                if (id < SPV_MAX_IDS && vecId < SPV_MAX_IDS && compId < SPV_MAX_IDS && idxId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id] = state->values[vecId];
                    uint32_t idx = (uint32_t)state->values[idxId].i[0];
                    if (idx < cc && idx < SPV_MAX_COMPONENTS)
                    {
                        state->values[id].f[idx] = state->values[compId].f[0];
                        state->values[id].i[idx] = state->values[compId].i[0];
                    }
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpVectorShuffle:
        {
            /* Word 1 = type, Word 2 = result, Word 3 = vec1, Word 4 = vec2, Word 5+ = components */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t v1     = code[pc + 3];
                uint32_t v2     = code[pc + 4];

                if (id < SPV_MAX_IDS && v1 < SPV_MAX_IDS && v2 < SPV_MAX_IDS)
                {
                    uint32_t outCount = wc - 5;
                    state->values[id].count = outCount;
                    state->values[id].isSet = 1;

                    /* v1c = component count of first source vector.
                    ** Must use v1's actual count, not the result type. */
                    uint32_t v1c = state->values[v1].count;
                    if (v1c == 0) v1c = 1;

                    for (uint32_t c = 0; c < outCount && c < SPV_MAX_COMPONENTS; c++)
                    {
                        uint32_t sel = code[pc + 5 + c];
                        if (sel == 0xFFFFFFFF)
                        {
                            state->values[id].f[c] = 0.0f;
                        }
                        else if (sel < v1c)
                        {
                            state->values[id].f[c] = state->values[v1].f[sel];
                        }
                        else
                        {
                            state->values[id].f[c] = state->values[v2].f[sel - v1c];
                        }
                    }
                    (void)typeId;
                }
            }
            break;
        }

        case SpvOpFNegate:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].f[j] = -state->values[srcId].f[j];
                }
            }
            break;
        }

        case SpvOpFAdd:
        case SpvOpFSub:
        case SpvOpFMul:
        case SpvOpFDiv:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        float a = state->values[aId].f[j];
                        float b = state->values[bId].f[j];
                        switch (op)
                        {
                            case SpvOpFAdd: state->values[id].f[j] = a + b; break;
                            case SpvOpFSub: state->values[id].f[j] = a - b; break;
                            case SpvOpFMul: state->values[id].f[j] = a * b; break;
                            case SpvOpFDiv: state->values[id].f[j] = (b != 0.0f) ? a / b : 0.0f; break;
                        }
                    }
                }
            }
            break;
        }

        case SpvOpFRem:
        case SpvOpFMod:
        {
            /* Float remainder / modulo: result = a - b * trunc(a/b) (FRem)
            ** or result = a - b * floor(a/b) (FMod) */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        float a = state->values[aId].f[j];
                        float b = state->values[bId].f[j];
                        if (b != 0.0f)
                        {
                            if (op == SpvOpFRem)
                                state->values[id].f[j] = a - b * truncf(a / b);
                            else /* SpvOpFMod */
                                state->values[id].f[j] = a - b * floorf(a / b);
                        }
                        else
                            state->values[id].f[j] = 0.0f;
                    }
                }
            }
            break;
        }

        case SpvOpVectorTimesScalar:
        {
            /* Multiply each component of a vector by a scalar */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t vecId  = code[pc + 3];
                uint32_t sclId  = code[pc + 4];

                if (id < SPV_MAX_IDS && vecId < SPV_MAX_IDS && sclId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    float scalar = state->values[sclId].f[0];
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].f[j] = state->values[vecId].f[j] * scalar;
                }
            }
            break;
        }

        case SpvOpDot:
        {
            if (wc >= 5)
            {
                uint32_t id  = code[pc + 2];
                uint32_t aId = code[pc + 3];
                uint32_t bId = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = state->values[aId].count;
                    if (cc == 0) cc = 1;
                    float dot = 0.0f;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        dot += state->values[aId].f[j] * state->values[bId].f[j];
                    state->values[id].f[0] = dot;
                    state->values[id].count = 1;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpOuterProduct:
        {
            if (wc >= 5)
            {
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3]; /* column vector */
                uint32_t bId    = code[pc + 4]; /* row vector */
                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t rows = state->values[aId].count;
                    uint32_t cols = state->values[bId].count;
                    uint32_t total = rows * cols;
                    if (total > SPV_MAX_COMPONENTS) total = SPV_MAX_COMPONENTS;
                    state->values[id].count = total;
                    state->values[id].isSet = 1;
                    for (uint32_t c = 0; c < cols && c * rows < SPV_MAX_COMPONENTS; c++)
                        for (uint32_t r = 0; r < rows && c * rows + r < SPV_MAX_COMPONENTS; r++)
                            state->values[id].f[c * rows + r] =
                                state->values[aId].f[r] * state->values[bId].f[c];
                }
            }
            break;
        }

        case SpvOpConvertSToF:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].f[j] = (float)state->values[srcId].i[j];
                }
            }
            break;
        }

        case SpvOpConvertFToS:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].i[j] = (int32_t)state->values[srcId].f[j];
                }
            }
            break;
        }

        case SpvOpConvertFToU:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t r = (uint32_t)state->values[srcId].f[j];
                        state->values[id].u[j] = r;
                        memcpy(&state->values[id].f[j], &r, 4);
                    }
                }
            }
            break;
        }

        case SpvOpConvertUToF:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].f[j] = (float)state->values[srcId].u[j];
                }
            }
            break;
        }

        case SpvOpFConvert:
        case SpvOpSConvert:
        case SpvOpUConvert:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        state->values[id].f[j] = state->values[srcId].f[j];
                        state->values[id].i[j] = state->values[srcId].i[j];
                    }
                }
            }
            break;
        }

        case SpvOpBitcast:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    int toFloat = (typeId < SPV_MAX_IDS && mod->types[typeId].opcode == SpvOpTypeFloat);
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        if (toFloat)
                            memcpy(&state->values[id].f[j], &state->values[srcId].i[j], 4);
                        else
                            memcpy(&state->values[id].i[j], &state->values[srcId].f[j], 4);
                    }
                }
            }
            break;
        }

        case SpvOpSelect:
        {
            if (wc >= 6)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t cond   = code[pc + 3];
                uint32_t trueId = code[pc + 4];
                uint32_t falseId = code[pc + 5];

                if (id < SPV_MAX_IDS && cond < SPV_MAX_IDS &&
                    trueId < SPV_MAX_IDS && falseId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    uint32_t sel = state->values[cond].i[0];
                    uint32_t srcId = sel ? trueId : falseId;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        state->values[id].f[j] = state->values[srcId].f[j];
                        state->values[id].i[j] = state->values[srcId].i[j];
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.1: GLSL.std.450 extended instructions
        **------------------------------------------------------------*/
        case SpvOpExtInst:
        {
            if (wc >= 6)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                /* code[pc + 3] = ext set ID -- assume GLSL.std.450 */
                uint32_t inst   = code[pc + 4];

                if (id < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    /* Argument IDs */
                    uint32_t aId = (wc > 5 && code[pc + 5] < SPV_MAX_IDS) ? code[pc + 5] : 0;
                    uint32_t bId = (wc > 6 && code[pc + 6] < SPV_MAX_IDS) ? code[pc + 6] : 0;
                    uint32_t cArgId = (wc > 7 && code[pc + 7] < SPV_MAX_IDS) ? code[pc + 7] : 0;

                    switch (inst)
                    {
                    /* Single-arg per-component math */
                    case GLSL_STD_450_Round:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = roundf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Trunc:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = truncf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_FAbs:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = fabsf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Floor:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = floorf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Ceil:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = ceilf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Fract:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = state->values[aId].f[j] - floorf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Sin:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = spv_FastSin(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Cos:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = spv_FastCos(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Tan:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float sv = spv_FastSin(state->values[aId].f[j]);
                            float cv = spv_FastCos(state->values[aId].f[j]);
                            state->values[id].f[j] = (cv != 0.0f) ? (sv / cv) : 0.0f;
                        }
                        break;
                    case GLSL_STD_450_Exp:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = expf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Log:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = logf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Sqrt:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = sqrtf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_InverseSqrt:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float v = state->values[aId].f[j];
                            state->values[id].f[j] = (v > 0.0f) ? (1.0f / sqrtf(v)) : 0.0f;
                        }
                        break;

                    /* Two-arg per-component math */
                    case GLSL_STD_450_Pow:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = powf(state->values[aId].f[j], state->values[bId].f[j]);
                        break;
                    case GLSL_STD_450_FMin:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float a = state->values[aId].f[j];
                            float b = state->values[bId].f[j];
                            state->values[id].f[j] = (a < b) ? a : b;
                        }
                        break;
                    case GLSL_STD_450_FMax:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float a = state->values[aId].f[j];
                            float b = state->values[bId].f[j];
                            state->values[id].f[j] = (a > b) ? a : b;
                        }
                        break;
                    case GLSL_STD_450_Step:
                        /* step(edge, x): 0.0 if x < edge, else 1.0 */
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = (state->values[bId].f[j] < state->values[aId].f[j]) ? 0.0f : 1.0f;
                        break;

                    /* Three-arg per-component math */
                    case GLSL_STD_450_FClamp:
                        /* clamp(x, minVal, maxVal) */
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float x = state->values[aId].f[j];
                            float lo = state->values[bId].f[j];
                            float hi = state->values[cArgId].f[j];
                            if (x < lo) x = lo;
                            if (x > hi) x = hi;
                            state->values[id].f[j] = x;
                        }
                        break;
                    case GLSL_STD_450_FMix:
                        /* mix(x, y, a) = x * (1-a) + y * a */
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float x = state->values[aId].f[j];
                            float y = state->values[bId].f[j];
                            float t = state->values[cArgId].f[j];
                            state->values[id].f[j] = x * (1.0f - t) + y * t;
                        }
                        break;
                    case GLSL_STD_450_SmoothStep:
                        /* smoothstep(edge0, edge1, x) */
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float edge0 = state->values[aId].f[j];
                            float edge1 = state->values[bId].f[j];
                            float x = state->values[cArgId].f[j];
                            float t = (edge1 != edge0) ? (x - edge0) / (edge1 - edge0) : 0.0f;
                            if (t < 0.0f) t = 0.0f;
                            if (t > 1.0f) t = 1.0f;
                            state->values[id].f[j] = t * t * (3.0f - 2.0f * t);
                        }
                        break;

                    /* Whole-vector operations */
                    case GLSL_STD_450_Length:
                    {
                        uint32_t ac = state->values[aId].count;
                        if (ac == 0) ac = 1;
                        float sum = 0.0f;
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                            sum += state->values[aId].f[j] * state->values[aId].f[j];
                        state->values[id].f[0] = sqrtf(sum);
                        state->values[id].count = 1;
                        break;
                    }
                    case GLSL_STD_450_Distance:
                    {
                        uint32_t ac = state->values[aId].count;
                        if (ac == 0) ac = 1;
                        float sum = 0.0f;
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float d = state->values[aId].f[j] - state->values[bId].f[j];
                            sum += d * d;
                        }
                        state->values[id].f[0] = sqrtf(sum);
                        state->values[id].count = 1;
                        break;
                    }
                    case GLSL_STD_450_Cross:
                    {
                        /* Cross product: only defined for vec3 */
                        float ax = state->values[aId].f[0];
                        float ay = state->values[aId].f[1];
                        float az = state->values[aId].f[2];
                        float bx = state->values[bId].f[0];
                        float by = state->values[bId].f[1];
                        float bz = state->values[bId].f[2];
                        state->values[id].f[0] = ay * bz - az * by;
                        state->values[id].f[1] = az * bx - ax * bz;
                        state->values[id].f[2] = ax * by - ay * bx;
                        state->values[id].count = 3;
                        break;
                    }
                    case GLSL_STD_450_Normalize:
                    {
                        uint32_t ac = state->values[aId].count;
                        if (ac == 0) ac = 1;
                        float sum = 0.0f;
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                            sum += state->values[aId].f[j] * state->values[aId].f[j];
                        float invLen = (sum > 0.0f) ? (1.0f / sqrtf(sum)) : 0.0f;
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = state->values[aId].f[j] * invLen;
                        state->values[id].count = ac;
                        break;
                    }
                    case GLSL_STD_450_Reflect:
                    {
                        /* reflect(I, N) = I - 2 * dot(N, I) * N */
                        uint32_t ac = state->values[aId].count;
                        if (ac == 0) ac = 1;
                        float dot = 0.0f;
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                            dot += state->values[bId].f[j] * state->values[aId].f[j];
                        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = state->values[aId].f[j] - 2.0f * dot * state->values[bId].f[j];
                        state->values[id].count = ac;
                        break;
                    }
                    case GLSL_STD_450_FSign:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            float v = state->values[aId].f[j];
                            state->values[id].f[j] = (v < 0.0f) ? -1.0f : (v > 0.0f) ? 1.0f : 0.0f;
                        }
                        break;
                    case GLSL_STD_450_SSign:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        {
                            int32_t v = state->values[aId].i[j];
                            int32_t r = (v < 0) ? -1 : (v > 0) ? 1 : 0;
                            state->values[id].i[j] = r;
                            state->values[id].f[j] = (float)r;
                        }
                        break;
                    case GLSL_STD_450_Radians:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = state->values[aId].f[j] * (3.14159265358979f / 180.0f);
                        break;
                    case GLSL_STD_450_Degrees:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = state->values[aId].f[j] * (180.0f / 3.14159265358979f);
                        break;
                    case GLSL_STD_450_Asin:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = asinf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Acos:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = acosf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Atan:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = atanf(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Atan2:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = atan2f(state->values[aId].f[j], state->values[bId].f[j]);
                        break;
                    case GLSL_STD_450_Exp2:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = exp2f(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_Log2:
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = log2f(state->values[aId].f[j]);
                        break;
                    case GLSL_STD_450_FaceForward:
                    {
                        /* FaceForward(N, I, Nref) = dot(Nref, I) < 0 ? N : -N */
                        float dot = 0.0f;
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            dot += state->values[cArgId].f[j] * state->values[bId].f[j];
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = (dot < 0.0f)
                                ? state->values[aId].f[j]
                                : -state->values[aId].f[j];
                        break;
                    }
                    case GLSL_STD_450_Refract:
                    {
                        /* Refract(I, N, eta): k = 1 - eta*eta*(1 - dot(N,I)^2)
                        ** if k < 0: result = 0; else: result = eta*I - (eta*d + sqrt(k))*N */
                        float eta = state->values[cArgId].f[0];
                        float d = 0.0f;
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            d += state->values[bId].f[j] * state->values[aId].f[j];
                        float k = 1.0f - eta * eta * (1.0f - d * d);
                        if (k < 0.0f)
                        {
                            for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                                state->values[id].f[j] = 0.0f;
                        }
                        else
                        {
                            float sq = sqrtf(k);
                            for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                                state->values[id].f[j] = eta * state->values[aId].f[j]
                                    - (eta * d + sq) * state->values[bId].f[j];
                        }
                        break;
                    }

                    default:
                        /* Unhandled extended instruction -- zero fill */
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = 0.0f;
                        break;
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.2: Comparison opcodes
        **------------------------------------------------------------*/

        /* Float ordered and unordered comparisons */
        case SpvOpFOrdEqual:
        case SpvOpFOrdNotEqual:
        case SpvOpFOrdLessThan:
        case SpvOpFOrdGreaterThan:
        case SpvOpFOrdLessThanEqual:
        case SpvOpFOrdGreaterThanEqual:
        case SpvOpFUnordEqual:
        case SpvOpFUnordNotEqual:
        case SpvOpFUnordLessThan:
        case SpvOpFUnordGreaterThan:
        case SpvOpFUnordLessThanEqual:
        case SpvOpFUnordGreaterThanEqual:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        float a = state->values[aId].f[j];
                        float b = state->values[bId].f[j];
                        int32_t result = 0;
                        switch (op)
                        {
                            case SpvOpFOrdEqual:            result = (a == b) ? 1 : 0; break;
                            case SpvOpFOrdNotEqual:         result = (a == a && b == b && a != b) ? 1 : 0; break;
                            case SpvOpFOrdLessThan:         result = (a <  b) ? 1 : 0; break;
                            case SpvOpFOrdGreaterThan:      result = (a >  b) ? 1 : 0; break;
                            case SpvOpFOrdLessThanEqual:    result = (a <= b) ? 1 : 0; break;
                            case SpvOpFOrdGreaterThanEqual: result = (a >= b) ? 1 : 0; break;
                            case SpvOpFUnordEqual:            result = (!(a==a) || !(b==b) || a == b) ? 1 : 0; break;
                            case SpvOpFUnordNotEqual:         result = (!(a==a) || !(b==b) || a != b) ? 1 : 0; break;
                            case SpvOpFUnordLessThan:         result = (!(a==a) || !(b==b) || a <  b) ? 1 : 0; break;
                            case SpvOpFUnordGreaterThan:      result = (!(a==a) || !(b==b) || a >  b) ? 1 : 0; break;
                            case SpvOpFUnordLessThanEqual:    result = (!(a==a) || !(b==b) || a <= b) ? 1 : 0; break;
                            case SpvOpFUnordGreaterThanEqual: result = (!(a==a) || !(b==b) || a >= b) ? 1 : 0; break;
                        }
                        state->values[id].i[j] = result;
                        state->values[id].f[j] = (float)result;
                    }
                }
            }
            break;
        }

        /* Integer comparisons (signed and unsigned) */
        case SpvOpIEqual:
        case SpvOpINotEqual:
        case SpvOpUGreaterThan:
        case SpvOpSGreaterThan:
        case SpvOpUGreaterThanEqual:
        case SpvOpSGreaterThanEqual:
        case SpvOpULessThan:
        case SpvOpSLessThan:
        case SpvOpULessThanEqual:
        case SpvOpSLessThanEqual:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        int32_t a = state->values[aId].i[j];
                        int32_t b = state->values[bId].i[j];
                        uint32_t ua = state->values[aId].u[j];
                        uint32_t ub = state->values[bId].u[j];
                        int32_t result = 0;
                        switch (op)
                        {
                            case SpvOpIEqual:            result = (a == b) ? 1 : 0; break;
                            case SpvOpINotEqual:         result = (a != b) ? 1 : 0; break;
                            case SpvOpSGreaterThan:      result = (a >  b) ? 1 : 0; break;
                            case SpvOpSLessThan:         result = (a <  b) ? 1 : 0; break;
                            case SpvOpSGreaterThanEqual: result = (a >= b) ? 1 : 0; break;
                            case SpvOpSLessThanEqual:    result = (a <= b) ? 1 : 0; break;
                            case SpvOpUGreaterThan:      result = (ua >  ub) ? 1 : 0; break;
                            case SpvOpULessThan:         result = (ua <  ub) ? 1 : 0; break;
                            case SpvOpUGreaterThanEqual: result = (ua >= ub) ? 1 : 0; break;
                            case SpvOpULessThanEqual:    result = (ua <= ub) ? 1 : 0; break;
                        }
                        state->values[id].i[j] = result;
                        state->values[id].f[j] = (float)result;
                    }
                }
            }
            break;
        }

        /* Logical operations */
        case SpvOpLogicalEqual:
        case SpvOpLogicalNotEqual:
        case SpvOpLogicalOr:
        case SpvOpLogicalAnd:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        int32_t a = state->values[aId].i[j] ? 1 : 0;
                        int32_t b = state->values[bId].i[j] ? 1 : 0;
                        int32_t result = 0;
                        switch (op)
                        {
                            case SpvOpLogicalEqual:    result = (a == b) ? 1 : 0; break;
                            case SpvOpLogicalNotEqual: result = (a != b) ? 1 : 0; break;
                            case SpvOpLogicalOr:       result = (a || b) ? 1 : 0; break;
                            case SpvOpLogicalAnd:      result = (a && b) ? 1 : 0; break;
                        }
                        state->values[id].i[j] = result;
                        state->values[id].f[j] = (float)result;
                    }
                }
            }
            break;
        }

        case SpvOpLogicalNot:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        int32_t result = state->values[srcId].i[j] ? 0 : 1;
                        state->values[id].i[j] = result;
                        state->values[id].f[j] = (float)result;
                    }
                }
            }
            break;
        }

        case SpvOpIsNan:
        case SpvOpIsInf:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];
                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        float v = state->values[srcId].f[j];
                        int32_t r = (op == SpvOpIsNan) ? (v != v ? 1 : 0) : (v == v && (v - v) != 0.0f ? 1 : 0);
                        state->values[id].i[j] = r;
                        state->values[id].f[j] = (float)r;
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.3: Control flow
        **------------------------------------------------------------*/

        case SpvOpSelectionMerge:
        case SpvOpLoopMerge:
            /* Merge metadata -- no runtime effect */
            break;

        case SpvOpBranchConditional:
        {
            if (wc >= 4)
            {
                uint32_t condId   = code[pc + 1];
                uint32_t trueLbl  = code[pc + 2];
                uint32_t falseLbl = code[pc + 3];

                uint32_t cond = 0;
                if (condId < SPV_MAX_IDS)
                    cond = (uint32_t)state->values[condId].i[0];

                uint32_t targetLbl = cond ? trueLbl : falseLbl;
                if (targetLbl < SPV_MAX_IDS && labelPCs[targetLbl] > 0)
                {
                    prevLabel = currentLabel;
                    currentLabel = targetLbl;
                    pc = labelPCs[targetLbl];
                    continue;
                }
            }
            break;
        }

        case SpvOpPhi:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];

                if (id < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    /* Walk (value, parent_label) pairs */
                    for (uint32_t p = 3; p + 1 < wc; p += 2)
                    {
                        uint32_t valId = code[pc + p];
                        uint32_t lblId = code[pc + p + 1];
                        if (lblId == prevLabel && valId < SPV_MAX_IDS)
                        {
                            state->values[id] = state->values[valId];
                            state->values[id].count = cc;
                            state->values[id].isSet = 1;
                            break;
                        }
                    }
                }
            }
            break;
        }

        case SpvOpSwitch:
        {
            if (wc >= 3)
            {
                uint32_t selId   = code[pc + 1];
                uint32_t defLbl  = code[pc + 2];
                int32_t selVal   = (selId < SPV_MAX_IDS) ? state->values[selId].i[0] : 0;
                uint32_t target  = defLbl;
                for (uint32_t p = 3; p + 1 < wc; p += 2)
                {
                    if ((int32_t)code[pc + p] == selVal)
                    {
                        target = code[pc + p + 1];
                        break;
                    }
                }
                if (target < SPV_MAX_IDS && labelPCs[target] > 0)
                {
                    prevLabel    = currentLabel;
                    currentLabel = target;
                    pc = labelPCs[target];
                    continue;
                }
            }
            break;
        }

        case SpvOpKill:
            state->killed = 1;
            goto done;

        case SpvOpUnreachable:
            goto done;

        case SpvOpDPdx: case SpvOpDPdy: case SpvOpFwidth:
        case SpvOpDPdxFine: case SpvOpDPdyFine: case SpvOpFwidthFine:
        case SpvOpDPdxCoarse: case SpvOpDPdyCoarse: case SpvOpFwidthCoarse:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                if (id < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    memset(&state->values[id], 0, sizeof(SpvValue));
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.4: Matrix operations
        **------------------------------------------------------------*/

        case SpvOpMatrixTimesScalar:
        {
            /* result = matrix * scalar */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t matId  = code[pc + 3];
                uint32_t sclId  = code[pc + 4];

                if (id < SPV_MAX_IDS && matId < SPV_MAX_IDS && sclId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    float s = state->values[sclId].f[0];
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                        state->values[id].f[j] = state->values[matId].f[j] * s;
                }
            }
            break;
        }

        case SpvOpMatrixTimesVector:
        {
            /* result = matrix * vector (matrix columns * vector components, summed) */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t matId  = code[pc + 3];
                uint32_t vecId  = code[pc + 4];

                if (id < SPV_MAX_IDS && matId < SPV_MAX_IDS && vecId < SPV_MAX_IDS)
                {
                    /* Resolve matrix type to get column count and row count */
                    uint32_t matTypeId = typeId; /* result type = column vector type */
                    uint32_t rows = spv_TypeComponentCount(mod, matTypeId);
                    uint32_t cols = state->values[vecId].count;
                    if (cols == 0) cols = 1;
                    if (rows > SPV_MAX_COMPONENTS) rows = SPV_MAX_COMPONENTS;
                    if (cols > SPV_MAX_COMPONENTS) cols = SPV_MAX_COMPONENTS;

                    state->values[id].count = rows;
                    state->values[id].isSet = 1;

                    for (uint32_t r = 0; r < rows; r++)
                    {
                        float sum = 0.0f;
                        for (uint32_t c = 0; c < cols; c++)
                        {
                            uint32_t idx = c * rows + r;
                            if (idx < SPV_MAX_COMPONENTS)
                                sum += state->values[matId].f[idx] * state->values[vecId].f[c];
                        }
                        state->values[id].f[r] = sum;
                    }
                }
            }
            break;
        }

        case SpvOpVectorTimesMatrix:
        {
            /* result = vector * matrix (row vector times matrix) */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t vecId  = code[pc + 3];
                uint32_t matId  = code[pc + 4];

                if (id < SPV_MAX_IDS && vecId < SPV_MAX_IDS && matId < SPV_MAX_IDS)
                {
                    uint32_t outCols = spv_TypeComponentCount(mod, typeId);
                    uint32_t rows = state->values[vecId].count;
                    if (rows == 0) rows = 1;
                    if (outCols > SPV_MAX_COMPONENTS) outCols = SPV_MAX_COMPONENTS;
                    if (rows > SPV_MAX_COMPONENTS) rows = SPV_MAX_COMPONENTS;

                    state->values[id].count = outCols;
                    state->values[id].isSet = 1;

                    for (uint32_t c = 0; c < outCols; c++)
                    {
                        float sum = 0.0f;
                        for (uint32_t r = 0; r < rows; r++)
                        {
                            uint32_t idx = c * rows + r;
                            if (idx < SPV_MAX_COMPONENTS)
                                sum += state->values[vecId].f[r] * state->values[matId].f[idx];
                        }
                        state->values[id].f[c] = sum;
                    }
                }
            }
            break;
        }

        case SpvOpMatrixTimesMatrix:
        {
            /* result = matA * matB */
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aMatId = code[pc + 3];
                uint32_t bMatId = code[pc + 4];

                if (id < SPV_MAX_IDS && aMatId < SPV_MAX_IDS && bMatId < SPV_MAX_IDS)
                {
                    /* Result type is a matrix -- get column count and row count */
                    const SpvTypeInfo *rt = &mod->types[typeId];
                    uint32_t resCols = rt->componentCount;
                    uint32_t resRows = spv_TypeComponentCount(mod, rt->componentTypeId);
                    /* Inner dimension = columns of A = rows of B */
                    /* For matA (rows x K) * matB (K x resCols): K = columns of A */
                    /* We'll derive K from A's total count / resRows */
                    uint32_t aTotalCC = state->values[aMatId].count;
                    uint32_t innerK = (resRows > 0) ? aTotalCC / resRows : 0;
                    if (innerK == 0) innerK = 1;

                    uint32_t totalCC = resCols * resRows;
                    if (totalCC > SPV_MAX_COMPONENTS) totalCC = SPV_MAX_COMPONENTS;

                    state->values[id].count = totalCC;
                    state->values[id].isSet = 1;

                    for (uint32_t c = 0; c < resCols && c * resRows < SPV_MAX_COMPONENTS; c++)
                    {
                        for (uint32_t r = 0; r < resRows; r++)
                        {
                            float sum = 0.0f;
                            for (uint32_t k = 0; k < innerK; k++)
                            {
                                uint32_t aIdx = k * resRows + r;
                                uint32_t bIdx = c * innerK + k;
                                if (aIdx < SPV_MAX_COMPONENTS && bIdx < SPV_MAX_COMPONENTS)
                                    sum += state->values[aMatId].f[aIdx] * state->values[bMatId].f[bIdx];
                            }
                            uint32_t outIdx = c * resRows + r;
                            if (outIdx < SPV_MAX_COMPONENTS)
                                state->values[id].f[outIdx] = sum;
                        }
                    }
                }
            }
            break;
        }

        case SpvOpTranspose:
        {
            /* result = transpose(matrix) */
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t matId  = code[pc + 3];

                if (id < SPV_MAX_IDS && matId < SPV_MAX_IDS)
                {
                    const SpvTypeInfo *rt = &mod->types[typeId];
                    uint32_t resCols = rt->componentCount;
                    uint32_t resRows = spv_TypeComponentCount(mod, rt->componentTypeId);

                    /* Source: srcRows = resCols (transposed dimensions) */
                    uint32_t srcRows = resCols;

                    uint32_t totalCC = resCols * resRows;
                    if (totalCC > SPV_MAX_COMPONENTS) totalCC = SPV_MAX_COMPONENTS;

                    state->values[id].count = totalCC;
                    state->values[id].isSet = 1;

                    for (uint32_t c = 0; c < resCols; c++)
                    {
                        for (uint32_t r = 0; r < resRows; r++)
                        {
                            /* result[c][r] = src[r][c] */
                            uint32_t srcIdx = r * srcRows + c;
                            uint32_t dstIdx = c * resRows + r;
                            if (srcIdx < SPV_MAX_COMPONENTS && dstIdx < SPV_MAX_COMPONENTS)
                                state->values[id].f[dstIdx] = state->values[matId].f[srcIdx];
                        }
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.5: Integer arithmetic
        **------------------------------------------------------------*/

        case SpvOpSNegate:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        state->values[id].i[j] = -state->values[srcId].i[j];
                        /* f[] and i[] are a union -- no sync needed */
                    }
                }
            }
            break;
        }

        case SpvOpIAdd:
        case SpvOpISub:
        case SpvOpIMul:
        case SpvOpSDiv:
        case SpvOpSMod:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        int32_t a = state->values[aId].i[j];
                        int32_t b = state->values[bId].i[j];
                        int32_t result = 0;
                        switch (op)
                        {
                            case SpvOpIAdd: result = a + b; break;
                            case SpvOpISub: result = a - b; break;
                            case SpvOpIMul: result = a * b; break;
                            case SpvOpSDiv: result = (b != 0) ? a / b : 0; break;
                            case SpvOpSMod: result = (b != 0) ? a % b : 0; break;
                        }
                        state->values[id].i[j] = result;
                        memcpy(&state->values[id].f[j], &result, 4);
                    }
                }
            }
            break;
        }

        case SpvOpUDiv:
        case SpvOpUMod:
        case SpvOpSRem:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        if (op == SpvOpSRem)
                        {
                            int32_t a = state->values[aId].i[j];
                            int32_t b = state->values[bId].i[j];
                            int32_t r = (b != 0) ? a % b : 0;
                            state->values[id].i[j] = r;
                            memcpy(&state->values[id].f[j], &r, 4);
                        }
                        else
                        {
                            uint32_t a = state->values[aId].u[j];
                            uint32_t b = state->values[bId].u[j];
                            uint32_t r = 0;
                            if (op == SpvOpUDiv)
                                r = (b != 0) ? a / b : 0;
                            else
                                r = (b != 0) ? a % b : 0;
                            state->values[id].u[j] = r;
                            memcpy(&state->values[id].f[j], &r, 4);
                        }
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.5b: Bitwise operations
        **------------------------------------------------------------*/

        case SpvOpShiftRightLogical:
        case SpvOpShiftRightArithmetic:
        case SpvOpShiftLeftLogical:
        case SpvOpBitwiseOr:
        case SpvOpBitwiseXor:
        case SpvOpBitwiseAnd:
        {
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t aId    = code[pc + 3];
                uint32_t bId    = code[pc + 4];

                if (id < SPV_MAX_IDS && aId < SPV_MAX_IDS && bId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;

                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t ua;
                        int32_t sa;
                        uint32_t ub;
                        int32_t result_i;
                        memcpy(&ua, &state->values[aId].i[j], 4);
                        sa = state->values[aId].i[j];
                        memcpy(&ub, &state->values[bId].i[j], 4);

                        switch (op)
                        {
                            case SpvOpShiftRightLogical:    result_i = (int32_t)(ua >> (ub & 31u)); break;
                            case SpvOpShiftRightArithmetic: result_i = sa >> (ub & 31u); break;
                            case SpvOpShiftLeftLogical:     result_i = (int32_t)(ua << (ub & 31u)); break;
                            case SpvOpBitwiseOr:            result_i = (int32_t)(ua | ub); break;
                            case SpvOpBitwiseXor:           result_i = (int32_t)(ua ^ ub); break;
                            case SpvOpBitwiseAnd:           result_i = (int32_t)(ua & ub); break;
                            default:                        result_i = 0; break;
                        }
                        state->values[id].i[j] = result_i;
                        memcpy(&state->values[id].f[j], &result_i, 4);
                    }
                }
            }
            break;
        }

        case SpvOpNot:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];

                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t ua;
                        int32_t result_i;
                        memcpy(&ua, &state->values[srcId].i[j], 4);
                        result_i = (int32_t)(~ua);
                        state->values[id].i[j] = result_i;
                        memcpy(&state->values[id].f[j], &result_i, 4);
                    }
                }
            }
            break;
        }

        case SpvOpBitFieldInsert:
        {
            if (wc >= 7)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t baseId = code[pc + 3];
                uint32_t insId  = code[pc + 4];
                uint32_t offId  = code[pc + 5];
                uint32_t bitsId = code[pc + 6];
                if (id < SPV_MAX_IDS && baseId < SPV_MAX_IDS && insId < SPV_MAX_IDS
                    && offId < SPV_MAX_IDS && bitsId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t base = state->values[baseId].u[j];
                        uint32_t ins  = state->values[insId].u[j];
                        uint32_t off  = state->values[offId].u[0] & 31u;
                        uint32_t bits = state->values[bitsId].u[0] & 31u;
                        uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
                        uint32_t r = (base & ~(mask << off)) | ((ins & mask) << off);
                        state->values[id].u[j] = r;
                        memcpy(&state->values[id].f[j], &r, 4);
                    }
                }
            }
            break;
        }

        case SpvOpBitFieldSExtract:
        case SpvOpBitFieldUExtract:
        {
            if (wc >= 6)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t baseId = code[pc + 3];
                uint32_t offId  = code[pc + 4];
                uint32_t bitsId = code[pc + 5];
                if (id < SPV_MAX_IDS && baseId < SPV_MAX_IDS && offId < SPV_MAX_IDS && bitsId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t base = state->values[baseId].u[j];
                        uint32_t off  = state->values[offId].u[0] & 31u;
                        uint32_t bits = state->values[bitsId].u[0] & 31u;
                        uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
                        uint32_t extracted = (base >> off) & mask;
                        if (op == SpvOpBitFieldSExtract && bits > 0 && bits < 32)
                        {
                            /* Sign extend */
                            if (extracted & (1u << (bits - 1)))
                                extracted |= ~mask;
                        }
                        state->values[id].u[j] = extracted;
                        memcpy(&state->values[id].f[j], &extracted, 4);
                    }
                }
            }
            break;
        }

        case SpvOpBitReverse:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];
                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        uint32_t v = state->values[srcId].u[j];
                        v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
                        v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
                        v = ((v >> 4) & 0x0F0F0F0Fu) | ((v & 0x0F0F0F0Fu) << 4);
                        v = ((v >> 8) & 0x00FF00FFu) | ((v & 0x00FF00FFu) << 8);
                        v = (v >> 16) | (v << 16);
                        state->values[id].u[j] = v;
                        memcpy(&state->values[id].f[j], &v, 4);
                    }
                }
            }
            break;
        }

        case SpvOpBitCount:
        {
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t srcId  = code[pc + 3];
                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                {
                    uint32_t cc = spv_TypeComponentCount(mod, typeId);
                    state->values[id].count = cc;
                    state->values[id].isSet = 1;
                    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                    {
                        int32_t r = (int32_t)__builtin_popcount(state->values[srcId].u[j]);
                        state->values[id].i[j] = r;
                        memcpy(&state->values[id].f[j], &r, 4);
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A0.6: Function calls (basic, single-level, non-recursive)
        **------------------------------------------------------------*/

        case SpvOpFunctionParameter:
            /* Parameters are pre-loaded by OpFunctionCall -- skip */
            break;

        case SpvOpFunctionCall:
        {
            /* Word 1 = result type, Word 2 = result ID,
            ** Word 3 = function ID, Word 4+ = argument IDs */
            if (wc >= 4)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t id     = code[pc + 2];
                uint32_t funcId = code[pc + 3];

                if (id < SPV_MAX_IDS && funcId < SPV_MAX_IDS && !inCall)
                {
                    /* Find the function's code by scanning for OpFunction with this ID */
                    uint32_t funcPC = 0;
                    {
                        uint32_t scanPc2 = 5;
                        while (scanPc2 < endPc)
                        {
                            uint32_t sw2 = SPV_WORDCOUNT(code[scanPc2]);
                            uint32_t sop2 = SPV_OP(code[scanPc2]);
                            if (sw2 == 0) break;
                            if (sop2 == SpvOpFunction && sw2 >= 3 && code[scanPc2 + 2] == funcId)
                            {
                                funcPC = scanPc2;
                                break;
                            }
                            scanPc2 += sw2;
                        }
                    }

                    if (funcPC > 0)
                    {
                        /* Match arguments to OpFunctionParameter IDs in the callee */
                        uint32_t paramPC = funcPC;
                        paramPC += SPV_WORDCOUNT(code[paramPC]); /* skip OpFunction */
                        uint32_t argIdx = 4;
                        while (paramPC < endPc && argIdx < wc)
                        {
                            uint32_t pw = SPV_WORDCOUNT(code[paramPC]);
                            uint32_t pop = SPV_OP(code[paramPC]);
                            if (pw == 0) break;
                            if (pop == SpvOpFunctionParameter && pw >= 3)
                            {
                                uint32_t paramId = code[paramPC + 2];
                                uint32_t argId = code[pc + argIdx];
                                if (paramId < SPV_MAX_IDS && argId < SPV_MAX_IDS)
                                {
                                    state->values[paramId] = state->values[argId];
                                    state->ptrs[paramId] = state->ptrs[argId];
                                }
                                argIdx++;
                            }
                            else if (pop == SpvOpLabel)
                            {
                                break; /* reached function body */
                            }
                            paramPC += pw;
                        }

                        /* Save return address and jump */
                        callReturnPC = pc + wc;
                        inCall = 1;

                        /* Build label map for the called function */
                        {
                            uint32_t scanPc3 = funcPC;
                            while (scanPc3 < endPc)
                            {
                                uint32_t sw3 = SPV_WORDCOUNT(code[scanPc3]);
                                uint32_t sop3 = SPV_OP(code[scanPc3]);
                                if (sw3 == 0) break;
                                if (sop3 == SpvOpLabel && sw3 >= 2)
                                {
                                    uint32_t lid = code[scanPc3 + 1];
                                    if (lid < SPV_MAX_IDS) labelPCs[lid] = scanPc3;
                                }
                                if (sop3 == SpvOpFunctionEnd) break;
                                scanPc3 += sw3;
                            }
                        }

                        pc = funcPC;
                        continue;
                    }
                    else
                    {
                        /* Function not found -- zero result */
                        uint32_t cc = spv_TypeComponentCount(mod, typeId);
                        state->values[id].count = cc;
                        state->values[id].isSet = 1;
                        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                            state->values[id].f[j] = 0.0f;
                    }
                }
            }
            break;
        }

        /*--------------------------------------------------------------
        ** A1.3: Texture sampling opcodes
        **------------------------------------------------------------*/

        case SpvOpSampledImage:
        {
            /* Word 1=type, Word 2=result, Word 3=image, Word 4=sampler */
            if (wc >= 5)
            {
                uint32_t id    = code[pc + 2];
                uint32_t imgId = code[pc + 3];
                /* Word 4 = sampler ID (not needed -- sampler info is in texture slot) */

                if (id < SPV_MAX_IDS && imgId < SPV_MAX_IDS)
                {
                    /* Copy the image value (which contains the texture slot index) */
                    state->values[id] = state->values[imgId];
                    state->values[id].isSet = 1;
                }
            }
            break;
        }

        case SpvOpImageSampleImplicitLod:
        {
            /* Word 1=type, Word 2=result, Word 3=sampledImage, Word 4=coordinate */
            if (wc >= 5)
            {
                uint32_t id      = code[pc + 2];
                uint32_t imgId   = code[pc + 3];
                uint32_t coordId = code[pc + 4];

                if (id < SPV_MAX_IDS && imgId < SPV_MAX_IDS && coordId < SPV_MAX_IDS)
                {
                    float u = state->values[coordId].f[0];
                    float v = state->values[coordId].f[1];

                    /* The image value's i[0] holds the texture slot index,
                    ** set during spv_SetupIO for UniformConstant variables. */
                    int32_t slot = state->values[imgId].i[0];

                    if (slot >= 0 && slot < SPV_MAX_TEXTURES &&
                        state->textures[slot].isSet)
                    {
                        swvk_SampleTexture(state, slot, u, v, &state->values[id]);
                    }
                    else
                    {
                        /* Default: magenta (debugging) */
                        state->values[id].f[0] = 1.0f;
                        state->values[id].f[1] = 0.0f;
                        state->values[id].f[2] = 1.0f;
                        state->values[id].f[3] = 1.0f;
                        state->values[id].count = 4;
                        state->values[id].isSet = 1;
                    }
                }
            }
            break;
        }

        case SpvOpImageSampleExplicitLod:
        {
            /* Same as ImplicitLod -- LOD always 0 (single mip) */
            if (wc >= 5)
            {
                uint32_t id       = code[pc + 2];
                uint32_t imgId    = code[pc + 3];
                uint32_t coordId  = code[pc + 4];
                if (id < SPV_MAX_IDS && imgId < SPV_MAX_IDS && coordId < SPV_MAX_IDS)
                {
                    float u = state->values[coordId].f[0];
                    float v = state->values[coordId].f[1];
                    int32_t slot = state->values[imgId].i[0];
                    if (slot >= 0 && slot < SPV_MAX_TEXTURES &&
                        state->textures[slot].isSet)
                    {
                        swvk_SampleTexture(state, slot, u, v, &state->values[id]);
                    }
                    else
                    {
                        state->values[id].f[0] = 1.0f;
                        state->values[id].f[1] = 0.0f;
                        state->values[id].f[2] = 1.0f;
                        state->values[id].f[3] = 1.0f;
                        state->values[id].count = 4;
                        state->values[id].isSet = 1;
                    }
                }
            }
            break;
        }

        case SpvOpImageFetch:
        {
            if (wc >= 5)
            {
                uint32_t id      = code[pc + 2];
                uint32_t imgId   = code[pc + 3];
                uint32_t coordId = code[pc + 4];
                if (id < SPV_MAX_IDS && imgId < SPV_MAX_IDS && coordId < SPV_MAX_IDS)
                {
                    /* Find texture slot from image variable */
                    uint32_t slot = 0;
                    if (mod->decorations[imgId].descriptorSet >= 0 && mod->decorations[imgId].binding >= 0)
                        slot = (uint32_t)mod->decorations[imgId].descriptorSet * SPV_MAX_BINDINGS_PER_SET
                             + (uint32_t)mod->decorations[imgId].binding;
                    state->values[id].count = 4;
                    state->values[id].isSet = 1;
                    if (slot < SPV_MAX_TEXTURES && state->textures[slot].isSet && state->textures[slot].data)
                    {
                        int32_t px = state->values[coordId].i[0];
                        int32_t py = state->values[coordId].i[1];
                        uint32_t w = state->textures[slot].width;
                        uint32_t h = state->textures[slot].height;
                        if (px < 0) px = 0;
                        if ((uint32_t)px >= w) px = (int32_t)(w - 1);
                        if (py < 0) py = 0;
                        if ((uint32_t)py >= h) py = (int32_t)(h - 1);
                        const uint8_t *pixel = state->textures[slot].data + ((uint32_t)py * w + (uint32_t)px) * 4;
                        state->values[id].f[0] = pixel[0] / 255.0f;
                        state->values[id].f[1] = pixel[1] / 255.0f;
                        state->values[id].f[2] = pixel[2] / 255.0f;
                        state->values[id].f[3] = pixel[3] / 255.0f;
                    }
                    else
                    {
                        for (uint32_t j = 0; j < 4; j++) state->values[id].f[j] = 0.0f;
                    }
                }
            }
            break;
        }

        case SpvOpImage:
        {
            /* Extract image from sampled image -- pass through */
            if (wc >= 4)
            {
                uint32_t id    = code[pc + 2];
                uint32_t srcId = code[pc + 3];
                if (id < SPV_MAX_IDS && srcId < SPV_MAX_IDS)
                    state->values[id] = state->values[srcId];
            }
            break;
        }

        case SpvOpImageQuerySizeLod:
        case SpvOpImageQuerySize:
        {
            if (wc >= 4)
            {
                uint32_t id    = code[pc + 2];
                uint32_t imgId = code[pc + 3];
                if (id < SPV_MAX_IDS && imgId < SPV_MAX_IDS)
                {
                    uint32_t slot = 0;
                    if (mod->decorations[imgId].descriptorSet >= 0 && mod->decorations[imgId].binding >= 0)
                        slot = (uint32_t)mod->decorations[imgId].descriptorSet * SPV_MAX_BINDINGS_PER_SET
                             + (uint32_t)mod->decorations[imgId].binding;
                    state->values[id].count = 2;
                    state->values[id].isSet = 1;
                    if (slot < SPV_MAX_TEXTURES && state->textures[slot].isSet)
                    {
                        state->values[id].i[0] = (int32_t)state->textures[slot].width;
                        state->values[id].i[1] = (int32_t)state->textures[slot].height;
                        state->values[id].f[0] = (float)state->textures[slot].width;
                        state->values[id].f[1] = (float)state->textures[slot].height;
                    }
                    else
                    {
                        state->values[id].i[0] = 0; state->values[id].i[1] = 0;
                        state->values[id].f[0] = 0.0f; state->values[id].f[1] = 0.0f;
                    }
                }
            }
            break;
        }

        default:
            /* Unknown opcode -- skip */
            break;
        }

        pc += wc;
    }

done:
    /* Collect outputs */
    spv_CollectOutputs(state);

    return 0;
}

/****************************************************************************/
/* Pre-compiled threaded interpreter                                        */
/*                                                                          */
/* Each SPIR-V instruction is pre-decoded into a SpvCompiledInstr with a    */
/* direct function pointer (handler) and pre-extracted operand IDs.         */
/* Execution becomes: fetch instr, call handler, advance PC. This removes   */
/* the main switch/case dispatch overhead and operand extraction cost.      */
/****************************************************************************/

/*--------------------------------------------------------------------------
** Handler functions -- arithmetic (float)
**------------------------------------------------------------------------*/

static void jit_FAdd(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j] + s->values[i->arg2Id].f[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FSub(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j] - s->values[i->arg2Id].f[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FMul(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j] * s->values[i->arg2Id].f[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FDiv(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float b = s->values[i->arg2Id].f[j];
        s->values[i->resultId].f[j] = (b != 0.0f) ? s->values[i->arg1Id].f[j] / b : 0.0f;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FNegate(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = -s->values[i->arg1Id].f[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FRem(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j];
        float b = s->values[i->arg2Id].f[j];
        s->values[i->resultId].f[j] = (b != 0.0f) ? a - b * truncf(a / b) : 0.0f;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FMod(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j];
        float b = s->values[i->arg2Id].f[j];
        s->values[i->resultId].f[j] = (b != 0.0f) ? a - b * floorf(a / b) : 0.0f;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_VectorTimesScalar(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    float scalar = s->values[i->arg2Id].f[0];
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j] * scalar;
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_MatrixTimesScalar(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    float scalar = s->values[i->arg2Id].f[0];
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j] * scalar;
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_Dot(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = s->values[i->arg1Id].count;
    if (cc == 0) cc = 1;
    float dot = 0.0f;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        dot += s->values[i->arg1Id].f[j] * s->values[i->arg2Id].f[j];
    s->values[i->resultId].f[0] = dot;
    s->values[i->resultId].count = 1;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- arithmetic (integer)
**------------------------------------------------------------------------*/

static void jit_IAdd(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = s->values[i->arg1Id].i[j] + s->values[i->arg2Id].i[j];
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_ISub(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = s->values[i->arg1Id].i[j] - s->values[i->arg2Id].i[j];
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_IMul(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = s->values[i->arg1Id].i[j] * s->values[i->arg2Id].i[j];
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SDiv(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t a = s->values[i->arg1Id].i[j];
        int32_t b = s->values[i->arg2Id].i[j];
        int32_t r = (b != 0) ? a / b : 0;
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SMod(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t a = s->values[i->arg1Id].i[j];
        int32_t b = s->values[i->arg2Id].i[j];
        int32_t r = (b != 0) ? a % b : 0;
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_UDiv(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        uint32_t a = s->values[i->arg1Id].u[j];
        uint32_t b = s->values[i->arg2Id].u[j];
        uint32_t r = (b != 0) ? a / b : 0;
        s->values[i->resultId].u[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_UMod(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        uint32_t a = s->values[i->arg1Id].u[j];
        uint32_t b = s->values[i->arg2Id].u[j];
        uint32_t r = (b != 0) ? a % b : 0;
        s->values[i->resultId].u[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SRem(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t a = s->values[i->arg1Id].i[j];
        int32_t b = s->values[i->arg2Id].i[j];
        int32_t r = (b != 0) ? a % b : 0;
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SNegate(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = -s->values[i->arg1Id].i[j];
        s->values[i->resultId].i[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- conversion
**------------------------------------------------------------------------*/

static void jit_ConvertSToF(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = (float)s->values[i->arg1Id].i[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_ConvertFToS(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].i[j] = (int32_t)s->values[i->arg1Id].f[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_ConvertFToU(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        uint32_t r = (uint32_t)s->values[i->arg1Id].f[j];
        s->values[i->resultId].u[j] = r;
        memcpy(&s->values[i->resultId].f[j], &r, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_ConvertUToF(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = (float)s->values[i->arg1Id].u[j];
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_IdentityCopy(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        s->values[i->resultId].f[j] = s->values[i->arg1Id].f[j];
        s->values[i->resultId].i[j] = s->values[i->arg1Id].i[j];
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_BitcastToFloat(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        memcpy(&s->values[i->resultId].f[j], &s->values[i->arg1Id].i[j], 4);
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_BitcastToInt(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        memcpy(&s->values[i->resultId].i[j], &s->values[i->arg1Id].f[j], 4);
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- comparison (float)
**------------------------------------------------------------------------*/

static void jit_FOrdEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].f[j] == s->values[i->arg2Id].f[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FOrdNotEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j];
        float b = s->values[i->arg2Id].f[j];
        int32_t r = (a == a && b == b && a != b) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FOrdLessThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].f[j] < s->values[i->arg2Id].f[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FOrdGreaterThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].f[j] > s->values[i->arg2Id].f[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FOrdLessThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].f[j] <= s->values[i->arg2Id].f[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FOrdGreaterThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].f[j] >= s->values[i->arg2Id].f[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_FUnordEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a == b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_FUnordNotEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a != b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_FUnordLessThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a < b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_FUnordGreaterThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a > b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_FUnordLessThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a <= b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_FUnordGreaterThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        float a = s->values[i->arg1Id].f[j], b = s->values[i->arg2Id].f[j];
        int32_t r = (!(a==a) || !(b==b) || a >= b) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- comparison (integer)
**------------------------------------------------------------------------*/

static void jit_IEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] == s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_INotEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] != s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SGreaterThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] > s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_SLessThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] < s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_UGreaterThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].u[j] > s->values[i->arg2Id].u[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_UGreaterThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].u[j] >= s->values[i->arg2Id].u[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_SGreaterThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] >= s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_ULessThan(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].u[j] < s->values[i->arg2Id].u[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_ULessThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].u[j] <= s->values[i->arg2Id].u[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

static void jit_SLessThanEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] <= s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r; s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc; s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- logical
**------------------------------------------------------------------------*/

static void jit_LogicalAnd(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] && s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_LogicalOr(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = (s->values[i->arg1Id].i[j] || s->values[i->arg2Id].i[j]) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_LogicalNot(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t r = s->values[i->arg1Id].i[j] ? 0 : 1;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_LogicalEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t a = s->values[i->arg1Id].i[j] ? 1 : 0;
        int32_t b = s->values[i->arg2Id].i[j] ? 1 : 0;
        int32_t r = (a == b) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_LogicalNotEqual(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        int32_t a = s->values[i->arg1Id].i[j] ? 1 : 0;
        int32_t b = s->values[i->arg2Id].i[j] ? 1 : 0;
        int32_t r = (a != b) ? 1 : 0;
        s->values[i->resultId].i[j] = r;
        s->values[i->resultId].f[j] = (float)r;
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- composite
**------------------------------------------------------------------------*/

static void jit_CompositeExtract(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = composite, extra = extraction index, componentCount = result cc */
    uint32_t cc = i->componentCount;
    uint32_t index = i->extra;

    if (cc == 1 && index < SPV_MAX_COMPONENTS)
    {
        s->values[i->resultId].f[0] = s->values[i->arg1Id].f[index];
        s->values[i->resultId].i[0] = s->values[i->arg1Id].i[index];
    }
    else
    {
        for (uint32_t j = 0; j < cc && (index + j) < SPV_MAX_COMPONENTS; j++)
        {
            s->values[i->resultId].f[j] = s->values[i->arg1Id].f[index + j];
            s->values[i->resultId].i[j] = s->values[i->arg1Id].i[index + j];
        }
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_Select(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = condition, arg2Id = true val, arg3Id = false val */
    uint32_t cc = i->componentCount;
    uint32_t sel = s->values[i->arg1Id].i[0];
    uint32_t srcId = sel ? i->arg2Id : i->arg3Id;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        s->values[i->resultId].f[j] = s->values[srcId].f[j];
        s->values[i->resultId].i[j] = s->values[srcId].i[j];
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- memory (Store, Load, Variable)
**------------------------------------------------------------------------*/

static void jit_Store(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = pointer ID, arg2Id = value ID */
    uint32_t ptrId = i->arg1Id;
    uint32_t valId = i->arg2Id;

    if (s->ptrs[ptrId].isPointer)
    {
        uint32_t tgt = s->ptrs[ptrId].targetId;
        uint32_t off = s->ptrs[ptrId].offset;
        const SpvValue *src = &s->values[valId];
        uint32_t cc = src->count;
        if (cc == 0) cc = 1;

        if (tgt < SPV_MAX_IDS)
        {
            for (uint32_t j = 0; j < cc && (off + j) < SPV_MAX_COMPONENTS; j++)
            {
                s->values[tgt].f[off + j] = src->f[j];
            }
            s->values[tgt].isSet = 1;
        }
    }
}

static void jit_Load(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* resultId = result, arg1Id = pointer ID, componentCount = type cc */
    uint32_t ptrId = i->arg1Id;
    uint32_t cc = i->componentCount;

    uint32_t tgt = s->ptrs[ptrId].isPointer ? s->ptrs[ptrId].targetId : ptrId;
    uint32_t off = s->ptrs[ptrId].isPointer ? s->ptrs[ptrId].offset : 0;

    if (tgt < SPV_MAX_IDS)
    {
        s->values[i->resultId].count = cc;
        s->values[i->resultId].isSet = 1;
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            uint32_t si = off + j;
            if (si < SPV_MAX_COMPONENTS)
            {
                s->values[i->resultId].f[j] = s->values[tgt].f[si];
                s->values[i->resultId].i[j] = s->values[tgt].i[si];
            }
        }
    }
}

static void jit_Variable(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* resultId = var ID, arg1Id = pointer type ID, extra = storage class
    ** componentCount = pre-computed type cc */
    uint32_t id = i->resultId;

    s->ptrs[id].targetId     = id;
    s->ptrs[id].offset       = 0;
    s->ptrs[id].storageClass = i->extra;
    s->ptrs[id].isPointer    = 1;
    s->ptrs[id].typeId       = i->arg2Id; /* pre-stored pointee type ID */

    uint32_t cc = i->componentCount;
    memset(&s->values[id], 0, sizeof(SpvValue));
    s->values[id].count = cc < SPV_MAX_COMPONENTS ? cc : SPV_MAX_COMPONENTS;
    s->values[id].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- AccessChain (uses module data for type walking)
**------------------------------------------------------------------------*/

static void jit_AccessChain(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* This handler is used for AccessChain with a SINGLE constant index,
    ** which covers the vast majority of cases.
    ** arg1Id = base pointer, arg2Id = index value ID,
    ** extra = pre-stored pointee type ID from result type,
    ** extra2 = not used here
    ** The single-index fast path computes offset directly. */
    uint32_t baseId = i->arg1Id;
    uint32_t id = i->resultId;

    uint32_t tgt = s->ptrs[baseId].isPointer ? s->ptrs[baseId].targetId : baseId;
    uint32_t baseOff = s->ptrs[baseId].isPointer ? s->ptrs[baseId].offset : 0;
    uint32_t curTypeId = s->ptrs[baseId].isPointer ? s->ptrs[baseId].typeId : 0;

    SpvModule *mod = s->module;
    int32_t indexVal = s->values[i->arg2Id].i[0];
    if (indexVal < 0) indexVal = 0;
    uint32_t offset = baseOff;

    const SpvTypeInfo *ct = &mod->types[curTypeId];
    if (ct->opcode == SpvOpTypeStruct)
    {
        offset += spv_StructMemberOffset(mod, curTypeId, (uint32_t)indexVal);
        if ((uint32_t)indexVal < ct->memberCount)
            curTypeId = ct->memberTypeIds[indexVal];
    }
    else if (ct->opcode == SpvOpTypeArray || ct->opcode == SpvOpTypeVector ||
             ct->opcode == SpvOpTypeMatrix)
    {
        uint32_t elemSize = spv_TypeComponentCount(mod, ct->componentTypeId);
        offset += (uint32_t)indexVal * elemSize;
        curTypeId = ct->componentTypeId;
    }

    /* Handle second index if present (arg3Id != 0xFFFF) */
    if (i->arg3Id != 0xFFFF && i->arg3Id < SPV_MAX_IDS)
    {
        indexVal = s->values[i->arg3Id].i[0];
        if (indexVal < 0) indexVal = 0;

        ct = &mod->types[curTypeId];
        if (ct->opcode == SpvOpTypeStruct)
        {
            offset += spv_StructMemberOffset(mod, curTypeId, (uint32_t)indexVal);
            if ((uint32_t)indexVal < ct->memberCount)
                curTypeId = ct->memberTypeIds[indexVal];
        }
        else if (ct->opcode == SpvOpTypeArray || ct->opcode == SpvOpTypeVector ||
                 ct->opcode == SpvOpTypeMatrix)
        {
            uint32_t elemSize = spv_TypeComponentCount(mod, ct->componentTypeId);
            offset += (uint32_t)indexVal * elemSize;
            curTypeId = ct->componentTypeId;
        }
    }

    s->ptrs[id].targetId     = tgt;
    s->ptrs[id].offset       = offset;
    s->ptrs[id].storageClass = s->ptrs[baseId].storageClass;
    s->ptrs[id].isPointer    = 1;
    s->ptrs[id].typeId       = i->extra; /* pre-stored pointee type from result type */
}

/* Full AccessChain fallback for 3+ indices -- walks the raw SPIR-V code.
** The instruction word offset is packed in extra2 (low 16) and arg3Id (not used
** for its normal purpose here, since we use componentCount to store wc). */
static void jit_AccessChainFull(SpvExecState *s, const SpvCompiledInstr *i)
{
    SpvModule *mod = s->module;
    /* Reconstruct PC from packed values: extra2 = low16, pad = high16 */
    uint32_t pc = (uint32_t)i->extra2 | ((uint32_t)i->pad << 16);
    const uint32_t *code = mod->code;
    uint32_t wc = i->componentCount; /* word count stored here for fallback */
    uint32_t id = i->resultId;
    uint32_t baseId = code[pc + 3];

    if (id >= SPV_MAX_IDS || baseId >= SPV_MAX_IDS) return;

    uint32_t tgt = s->ptrs[baseId].isPointer ? s->ptrs[baseId].targetId : baseId;
    uint32_t baseOff = s->ptrs[baseId].isPointer ? s->ptrs[baseId].offset : 0;
    uint32_t curTypeId = s->ptrs[baseId].isPointer ? s->ptrs[baseId].typeId : 0;
    uint32_t offset = baseOff;

    for (uint32_t idx = 4; idx < wc; idx++)
    {
        uint32_t indexId = code[pc + idx];
        int32_t indexVal = 0;
        if (indexId < SPV_MAX_IDS)
            indexVal = s->values[indexId].i[0];
        if (indexVal < 0) indexVal = 0;

        const SpvTypeInfo *ct = &mod->types[curTypeId];
        if (ct->opcode == SpvOpTypeStruct)
        {
            offset += spv_StructMemberOffset(mod, curTypeId, (uint32_t)indexVal);
            if ((uint32_t)indexVal < ct->memberCount)
                curTypeId = ct->memberTypeIds[indexVal];
        }
        else if (ct->opcode == SpvOpTypeArray || ct->opcode == SpvOpTypeVector ||
                 ct->opcode == SpvOpTypeMatrix)
        {
            uint32_t elemSize = spv_TypeComponentCount(mod, ct->componentTypeId);
            offset += (uint32_t)indexVal * elemSize;
            curTypeId = ct->componentTypeId;
        }
    }

    s->ptrs[id].targetId     = tgt;
    s->ptrs[id].offset       = offset;
    s->ptrs[id].storageClass = s->ptrs[baseId].storageClass;
    s->ptrs[id].isPointer    = 1;

    uint32_t typeId = code[pc + 1];
    if (typeId < SPV_MAX_IDS)
        s->ptrs[id].typeId = mod->types[typeId].pointeeTypeId;
}

/*--------------------------------------------------------------------------
** Handler functions -- CompositeConstruct, VectorShuffle (need raw code)
**------------------------------------------------------------------------*/

/* CompositeConstruct fallback: reads constituent list from raw SPIR-V */
static void jit_CompositeConstructFull(SpvExecState *s, const SpvCompiledInstr *i)
{
    SpvModule *mod = s->module;
    uint32_t pc = (uint32_t)i->extra2 | ((uint32_t)i->pad << 16);
    const uint32_t *code = mod->code;
    uint32_t wc = i->componentCount; /* word count */
    uint32_t id = code[pc + 2];

    if (id >= SPV_MAX_IDS) return;

    uint32_t offset = 0;
    s->values[id].isSet = 1;

    for (uint32_t c = 3; c < wc; c++)
    {
        uint32_t cid = code[pc + c];
        if (cid < SPV_MAX_IDS)
        {
            uint32_t cc = s->values[cid].count;
            if (cc == 0) cc = 1;
            for (uint32_t j = 0; j < cc && (offset + j) < SPV_MAX_COMPONENTS; j++)
            {
                s->values[id].f[offset + j] = s->values[cid].f[j];
                s->values[id].i[offset + j] = s->values[cid].i[j];
            }
            offset += cc;
        }
    }
    s->values[id].count = offset;
}

/* VectorShuffle fallback: reads component indices from raw SPIR-V */
static void jit_VectorShuffleFull(SpvExecState *s, const SpvCompiledInstr *i)
{
    SpvModule *mod = s->module;
    uint32_t pc = (uint32_t)i->extra2 | ((uint32_t)i->pad << 16);
    const uint32_t *code = mod->code;
    uint32_t wc = (uint32_t)i->extra; /* word count stored in extra */
    uint32_t id = code[pc + 2];
    uint32_t v1 = code[pc + 3];
    uint32_t v2 = code[pc + 4];

    if (id >= SPV_MAX_IDS || v1 >= SPV_MAX_IDS || v2 >= SPV_MAX_IDS) return;

    uint32_t outCount = wc - 5;
    s->values[id].count = outCount;
    s->values[id].isSet = 1;

    uint32_t v1c = s->values[v1].count;
    if (v1c == 0) v1c = 1;

    for (uint32_t c = 0; c < outCount && c < SPV_MAX_COMPONENTS; c++)
    {
        uint32_t sel = code[pc + 5 + c];
        if (sel == 0xFFFFFFFF)
            s->values[id].f[c] = 0.0f;
        else if (sel < v1c)
            s->values[id].f[c] = s->values[v1].f[sel];
        else
            s->values[id].f[c] = s->values[v2].f[sel - v1c];
    }
}

/*--------------------------------------------------------------------------
** Handler functions -- control flow
**------------------------------------------------------------------------*/

static void jit_Return(SpvExecState *s, const SpvCompiledInstr *i)
{
    (void)i;
    s->jumpTarget = 0xFFFFFFFF; /* signal return */
}

static void jit_Branch(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* extra = target label ID (low 16 bits, enough for our ID space) */
    s->jumpTarget = i->extra;
}

static void jit_BranchConditional(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = condition ID, extra = true label, extra2 = false label */
    uint32_t cond = 0;
    if (i->arg1Id < SPV_MAX_IDS)
        cond = (uint32_t)s->values[i->arg1Id].i[0];
    s->jumpTarget = cond ? i->extra : i->extra2;
}

static void jit_Nop(SpvExecState *s, const SpvCompiledInstr *i)
{
    (void)s;
    (void)i;
}

/*--------------------------------------------------------------------------
** Handler functions -- Phi
** extra stores the index of the Phi's (value,label) pairs in raw SPIR-V.
**------------------------------------------------------------------------*/

static void jit_PhiFull(SpvExecState *s, const SpvCompiledInstr *i)
{
    SpvModule *mod = s->module;
    uint32_t pc = (uint32_t)i->extra2 | ((uint32_t)i->pad << 16);
    const uint32_t *code = mod->code;
    uint32_t wc = (uint32_t)i->extra; /* word count */
    uint32_t id = code[pc + 2];

    if (id >= SPV_MAX_IDS) return;

    uint32_t cc = i->componentCount;

    /* Walk (value, parent_label) pairs. We use arg1Id to carry prevLabel
    ** but since the compiled execution loop does NOT track prevLabel,
    ** we need the caller to have stored it. We do this by piggybacking
    ** on the compiled exec loop's label tracking via arg1Id. */
    /* Actually, the compiled exec loop tracks prevLabel in the state
    ** structure -- but we didn't add it. Let's use arg1Id as a marker
    ** that prevLabel is stored in arg1Id.
    ** No -- we need prevLabel at runtime. Let's store it in the state. */
    /* For Phi to work, spv_ExecuteCompiled tracks prevLabel in a local.
    ** Phi handler needs access to it. Store it in state->jumpTarget
    ** temporarily? No, that would conflict.
    ** The simplest solution: store prevLabel in a field we already have.
    ** Let's reuse the pad field at runtime. Actually, we will NOT use
    ** Phi in the compiled path for the initial implementation. Instead
    ** we fall back. See jit_PhiFallback below. */

    /* Use raw code to check (value, label) pairs against prevLabel.
    ** prevLabel is not available in compiled state, so Phi always falls
    ** back to the interpreter's Phi logic. We approximate by taking
    ** the first value -- this works for simple if/else but not loops.
    ** This is a known limitation; full Phi support requires prevLabel
    ** tracking in the compiled execution loop. */

    /* Actually let's just do it: we'll track prevLabel in the execution
    ** loop. But we can't pass it to the handler. So we store it in
    ** the SpvExecState.jumpTarget field temporarily before calling Phi.
    ** The execution loop sets jumpTarget = prevLabel before calling
    ** any Phi handler, and Phi reads it. */

    /* Read prevLabel from jumpTarget (set by exec loop before calling Phi) */
    uint32_t prevLabel = s->jumpTarget;
    s->jumpTarget = 0; /* reset */

    for (uint32_t p = 3; p + 1 < wc; p += 2)
    {
        uint32_t valId = code[pc + p];
        uint32_t lblId = code[pc + p + 1];
        if (lblId == prevLabel && valId < SPV_MAX_IDS)
        {
            s->values[id] = s->values[valId];
            s->values[id].count = cc;
            s->values[id].isSet = 1;
            return;
        }
    }

    /* If no matching label found, take first value as fallback */
    if (wc >= 5)
    {
        uint32_t valId = code[pc + 3];
        if (valId < SPV_MAX_IDS)
        {
            s->values[id] = s->values[valId];
            s->values[id].count = cc;
            s->values[id].isSet = 1;
        }
    }
}

/*--------------------------------------------------------------------------
** Handler functions -- matrix operations
**------------------------------------------------------------------------*/

static void jit_MatrixTimesVector(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = matrix, arg2Id = vector, componentCount = result rows */
    uint32_t rows = i->componentCount;
    uint32_t cols = s->values[i->arg2Id].count;
    if (cols == 0) cols = 1;
    if (rows > SPV_MAX_COMPONENTS) rows = SPV_MAX_COMPONENTS;
    if (cols > SPV_MAX_COMPONENTS) cols = SPV_MAX_COMPONENTS;

    s->values[i->resultId].count = rows;
    s->values[i->resultId].isSet = 1;

    for (uint32_t r = 0; r < rows; r++)
    {
        float sum = 0.0f;
        for (uint32_t c = 0; c < cols; c++)
        {
            uint32_t idx = c * rows + r;
            if (idx < SPV_MAX_COMPONENTS)
                sum += s->values[i->arg1Id].f[idx] * s->values[i->arg2Id].f[c];
        }
        s->values[i->resultId].f[r] = sum;
    }
}

static void jit_VectorTimesMatrix(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* arg1Id = vector, arg2Id = matrix, componentCount = result cols */
    uint32_t outCols = i->componentCount;
    uint32_t rows = s->values[i->arg1Id].count;
    if (rows == 0) rows = 1;
    if (outCols > SPV_MAX_COMPONENTS) outCols = SPV_MAX_COMPONENTS;
    if (rows > SPV_MAX_COMPONENTS) rows = SPV_MAX_COMPONENTS;

    s->values[i->resultId].count = outCols;
    s->values[i->resultId].isSet = 1;

    for (uint32_t c = 0; c < outCols; c++)
    {
        float sum = 0.0f;
        for (uint32_t r = 0; r < rows; r++)
        {
            uint32_t idx = c * rows + r;
            if (idx < SPV_MAX_COMPONENTS)
                sum += s->values[i->arg1Id].f[r] * s->values[i->arg2Id].f[idx];
        }
        s->values[i->resultId].f[c] = sum;
    }
}

static void jit_MatrixTimesMatrix(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* For M*M we need the matrix type info. Use a raw-code fallback approach.
    ** extra2/pad encode the PC for the raw instruction. extra = result column count.
    ** arg1Id = matA, arg2Id = matB, componentCount = total result components,
    ** extra = resCols, arg3Id = resRows (pre-computed) */
    uint32_t resCols = i->extra;
    uint32_t resRows = i->arg3Id;
    if (resCols == 0) resCols = 1;
    if (resRows == 0) resRows = 1;

    uint32_t aTotalCC = s->values[i->arg1Id].count;
    uint32_t innerK = (resRows > 0) ? aTotalCC / resRows : 1;
    if (innerK == 0) innerK = 1;

    uint32_t totalCC = resCols * resRows;
    if (totalCC > SPV_MAX_COMPONENTS) totalCC = SPV_MAX_COMPONENTS;

    s->values[i->resultId].count = totalCC;
    s->values[i->resultId].isSet = 1;

    for (uint32_t c = 0; c < resCols && c * resRows < SPV_MAX_COMPONENTS; c++)
    {
        for (uint32_t r = 0; r < resRows; r++)
        {
            float sum = 0.0f;
            for (uint32_t k = 0; k < innerK; k++)
            {
                uint32_t aIdx = k * resRows + r;
                uint32_t bIdx = c * innerK + k;
                if (aIdx < SPV_MAX_COMPONENTS && bIdx < SPV_MAX_COMPONENTS)
                    sum += s->values[i->arg1Id].f[aIdx] * s->values[i->arg2Id].f[bIdx];
            }
            uint32_t outIdx = c * resRows + r;
            if (outIdx < SPV_MAX_COMPONENTS)
                s->values[i->resultId].f[outIdx] = sum;
        }
    }
}

static void jit_Transpose(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* extra = resCols, arg3Id = resRows (pre-computed by compiler) */
    uint32_t resCols = i->extra;
    uint32_t resRows = i->arg3Id;
    uint32_t srcRows = resCols; /* source rows = result columns (transposed) */
    uint32_t totalCC = resCols * resRows;
    if (totalCC > SPV_MAX_COMPONENTS) totalCC = SPV_MAX_COMPONENTS;

    s->values[i->resultId].count = totalCC;
    s->values[i->resultId].isSet = 1;

    for (uint32_t c = 0; c < resCols; c++)
    {
        for (uint32_t r = 0; r < resRows; r++)
        {
            uint32_t srcIdx = r * srcRows + c;
            uint32_t dstIdx = c * resRows + r;
            if (srcIdx < SPV_MAX_COMPONENTS && dstIdx < SPV_MAX_COMPONENTS)
                s->values[i->resultId].f[dstIdx] = s->values[i->arg1Id].f[srcIdx];
        }
    }
}

/*--------------------------------------------------------------------------
** Handler functions -- bitwise operations
**------------------------------------------------------------------------*/

static void jit_BitwiseOp(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* extra = opcode (SpvOpShiftRightLogical..SpvOpBitwiseAnd) */
    uint32_t cc = i->componentCount;
    uint32_t op = i->extra;

    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        uint32_t ua;
        int32_t sa;
        uint32_t ub;
        int32_t result_i;
        memcpy(&ua, &s->values[i->arg1Id].i[j], 4);
        sa = s->values[i->arg1Id].i[j];
        memcpy(&ub, &s->values[i->arg2Id].i[j], 4);

        switch (op)
        {
            case SpvOpShiftRightLogical:    result_i = (int32_t)(ua >> (ub & 31u)); break;
            case SpvOpShiftRightArithmetic: result_i = sa >> (ub & 31u); break;
            case SpvOpShiftLeftLogical:     result_i = (int32_t)(ua << (ub & 31u)); break;
            case SpvOpBitwiseOr:            result_i = (int32_t)(ua | ub); break;
            case SpvOpBitwiseXor:           result_i = (int32_t)(ua ^ ub); break;
            case SpvOpBitwiseAnd:           result_i = (int32_t)(ua & ub); break;
            default:                        result_i = 0; break;
        }
        s->values[i->resultId].i[j] = result_i;
        memcpy(&s->values[i->resultId].f[j], &result_i, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_BitwiseNot(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
    {
        uint32_t ua;
        int32_t result_i;
        memcpy(&ua, &s->values[i->arg1Id].i[j], 4);
        result_i = (int32_t)(~ua);
        s->values[i->resultId].i[j] = result_i;
        memcpy(&s->values[i->resultId].f[j], &result_i, 4);
    }
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

/*--------------------------------------------------------------------------
** Handler functions -- ExtInst (GLSL.std.450)
** Dispatches by the pre-stored function number in `extra`.
**------------------------------------------------------------------------*/

static void jit_ExtInst(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    uint32_t inst = i->extra;
    uint32_t aId = i->arg1Id;
    uint32_t bId = i->arg2Id;
    uint32_t cArgId = i->arg3Id;

    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;

    switch (inst)
    {
    case GLSL_STD_450_Round:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = roundf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Trunc:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = truncf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_FAbs:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = fabsf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Floor:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = floorf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Ceil:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = ceilf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Fract:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = s->values[aId].f[j] - floorf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Sin:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = spv_FastSin(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Cos:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = spv_FastCos(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Tan:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float sv = spv_FastSin(s->values[aId].f[j]);
            float cv = spv_FastCos(s->values[aId].f[j]);
            s->values[i->resultId].f[j] = (cv != 0.0f) ? (sv / cv) : 0.0f;
        }
        break;
    case GLSL_STD_450_Exp:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = expf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Log:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = logf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Sqrt:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = sqrtf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_InverseSqrt:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float v = s->values[aId].f[j];
            s->values[i->resultId].f[j] = (v > 0.0f) ? (1.0f / sqrtf(v)) : 0.0f;
        }
        break;

    /* Two-arg */
    case GLSL_STD_450_Pow:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = powf(s->values[aId].f[j], s->values[bId].f[j]);
        break;
    case GLSL_STD_450_FMin:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float a = s->values[aId].f[j], b = s->values[bId].f[j];
            s->values[i->resultId].f[j] = (a < b) ? a : b;
        }
        break;
    case GLSL_STD_450_FMax:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float a = s->values[aId].f[j], b = s->values[bId].f[j];
            s->values[i->resultId].f[j] = (a > b) ? a : b;
        }
        break;
    case GLSL_STD_450_Step:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = (s->values[bId].f[j] < s->values[aId].f[j]) ? 0.0f : 1.0f;
        break;

    /* Three-arg */
    case GLSL_STD_450_FClamp:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float x = s->values[aId].f[j];
            float lo = s->values[bId].f[j];
            float hi = s->values[cArgId].f[j];
            if (x < lo) x = lo;
            if (x > hi) x = hi;
            s->values[i->resultId].f[j] = x;
        }
        break;
    case GLSL_STD_450_FMix:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float x = s->values[aId].f[j];
            float y = s->values[bId].f[j];
            float t = s->values[cArgId].f[j];
            s->values[i->resultId].f[j] = x * (1.0f - t) + y * t;
        }
        break;
    case GLSL_STD_450_SmoothStep:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float edge0 = s->values[aId].f[j];
            float edge1 = s->values[bId].f[j];
            float x = s->values[cArgId].f[j];
            float t = (edge1 != edge0) ? (x - edge0) / (edge1 - edge0) : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            s->values[i->resultId].f[j] = t * t * (3.0f - 2.0f * t);
        }
        break;

    /* Whole-vector */
    case GLSL_STD_450_Length:
    {
        uint32_t ac = s->values[aId].count;
        if (ac == 0) ac = 1;
        float sum = 0.0f;
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
            sum += s->values[aId].f[j] * s->values[aId].f[j];
        s->values[i->resultId].f[0] = sqrtf(sum);
        s->values[i->resultId].count = 1;
        break;
    }
    case GLSL_STD_450_Distance:
    {
        uint32_t ac = s->values[aId].count;
        if (ac == 0) ac = 1;
        float sum = 0.0f;
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
        {
            float d = s->values[aId].f[j] - s->values[bId].f[j];
            sum += d * d;
        }
        s->values[i->resultId].f[0] = sqrtf(sum);
        s->values[i->resultId].count = 1;
        break;
    }
    case GLSL_STD_450_Cross:
    {
        float ax = s->values[aId].f[0], ay = s->values[aId].f[1], az = s->values[aId].f[2];
        float bx = s->values[bId].f[0], by = s->values[bId].f[1], bz = s->values[bId].f[2];
        s->values[i->resultId].f[0] = ay * bz - az * by;
        s->values[i->resultId].f[1] = az * bx - ax * bz;
        s->values[i->resultId].f[2] = ax * by - ay * bx;
        s->values[i->resultId].count = 3;
        break;
    }
    case GLSL_STD_450_Normalize:
    {
        uint32_t ac = s->values[aId].count;
        if (ac == 0) ac = 1;
        float sum = 0.0f;
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
            sum += s->values[aId].f[j] * s->values[aId].f[j];
        float invLen = (sum > 0.0f) ? (1.0f / sqrtf(sum)) : 0.0f;
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = s->values[aId].f[j] * invLen;
        s->values[i->resultId].count = ac;
        break;
    }
    case GLSL_STD_450_Reflect:
    {
        uint32_t ac = s->values[aId].count;
        if (ac == 0) ac = 1;
        float dot = 0.0f;
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
            dot += s->values[bId].f[j] * s->values[aId].f[j];
        for (uint32_t j = 0; j < ac && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = s->values[aId].f[j] - 2.0f * dot * s->values[bId].f[j];
        s->values[i->resultId].count = ac;
        break;
    }
    case GLSL_STD_450_FSign:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            float v = s->values[aId].f[j];
            s->values[i->resultId].f[j] = (v < 0.0f) ? -1.0f : (v > 0.0f) ? 1.0f : 0.0f;
        }
        break;
    case GLSL_STD_450_SSign:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        {
            int32_t v = s->values[aId].i[j];
            int32_t r = (v < 0) ? -1 : (v > 0) ? 1 : 0;
            s->values[i->resultId].i[j] = r;
            s->values[i->resultId].f[j] = (float)r;
        }
        break;
    case GLSL_STD_450_Radians:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = s->values[aId].f[j] * (3.14159265358979f / 180.0f);
        break;
    case GLSL_STD_450_Degrees:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = s->values[aId].f[j] * (180.0f / 3.14159265358979f);
        break;
    case GLSL_STD_450_Asin:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = asinf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Acos:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = acosf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Atan:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = atanf(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Atan2:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = atan2f(s->values[aId].f[j], s->values[bId].f[j]);
        break;
    case GLSL_STD_450_Exp2:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = exp2f(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_Log2:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = log2f(s->values[aId].f[j]);
        break;
    case GLSL_STD_450_FaceForward:
    {
        float dot = 0.0f;
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            dot += s->values[cArgId].f[j] * s->values[bId].f[j];
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = (dot < 0.0f) ? s->values[aId].f[j] : -s->values[aId].f[j];
        break;
    }
    case GLSL_STD_450_Refract:
    {
        float eta = s->values[cArgId].f[0];
        float d = 0.0f;
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            d += s->values[bId].f[j] * s->values[aId].f[j];
        float k = 1.0f - eta * eta * (1.0f - d * d);
        if (k < 0.0f)
            for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                s->values[i->resultId].f[j] = 0.0f;
        else
        {
            float sq = sqrtf(k);
            for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
                s->values[i->resultId].f[j] = eta * s->values[aId].f[j]
                    - (eta * d + sq) * s->values[bId].f[j];
        }
        break;
    }

    default:
        for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
            s->values[i->resultId].f[j] = 0.0f;
        break;
    }
}

/*--------------------------------------------------------------------------
** Handler functions -- texture sampling
**------------------------------------------------------------------------*/

static void jit_SampledImage(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* resultId = result, arg1Id = image ID */
    s->values[i->resultId] = s->values[i->arg1Id];
    s->values[i->resultId].isSet = 1;
}

static void jit_ImageSampleImplicitLod(SpvExecState *s, const SpvCompiledInstr *i)
{
    /* resultId = result, arg1Id = sampledImage, arg2Id = coordinate */
    float u = s->values[i->arg2Id].f[0];
    float v = s->values[i->arg2Id].f[1];
    int32_t slot = s->values[i->arg1Id].i[0];

    if (slot >= 0 && slot < SPV_MAX_TEXTURES && s->textures[slot].isSet)
    {
        swvk_SampleTexture(s, slot, u, v, &s->values[i->resultId]);
    }
    else
    {
        s->values[i->resultId].f[0] = 1.0f;
        s->values[i->resultId].f[1] = 0.0f;
        s->values[i->resultId].f[2] = 1.0f;
        s->values[i->resultId].f[3] = 1.0f;
        s->values[i->resultId].count = 4;
        s->values[i->resultId].isSet = 1;
    }
}

static void jit_DerivativeStub(SpvExecState *s, const SpvCompiledInstr *i)
{
    uint32_t cc = i->componentCount;
    for (uint32_t j = 0; j < cc && j < SPV_MAX_COMPONENTS; j++)
        s->values[i->resultId].f[j] = 0.0f;
    s->values[i->resultId].count = cc;
    s->values[i->resultId].isSet = 1;
}

static void jit_Kill(SpvExecState *s, const SpvCompiledInstr *i)
{
    (void)i;
    s->killed = 1;
    s->jumpTarget = 0xFFFFFFFF;
}

/****************************************************************************/
/* Template state: pre-computed constants/pointers for fast invocation      */
/* (Replaces memset+constant copy with memcpy for faster invocation)        */
/****************************************************************************/

void spv_BuildTemplate(SpvModule *mod)
{
    if (!mod || mod->templateBuilt) return;

    uint32_t bound = mod->bound < SPV_MAX_IDS ? mod->bound : SPV_MAX_IDS;

    mod->templateValues = (SpvValue *)IExec->AllocVecTags(
        bound * sizeof(SpvValue),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    mod->templatePtrs = (SpvPointer *)IExec->AllocVecTags(
        bound * sizeof(SpvPointer),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!mod->templateValues || !mod->templatePtrs)
    {
        /* Allocation failed -- fall back to non-template path */
        if (mod->templateValues) { IExec->FreeVec(mod->templateValues); mod->templateValues = NULL; }
        if (mod->templatePtrs)   { IExec->FreeVec(mod->templatePtrs);   mod->templatePtrs = NULL; }
        return;
    }

    mod->templateBound = bound;

    /* Fill constants into template values (same as spv_SetupIO's second loop) */
    for (uint32_t id = 0; id < bound; id++)
    {
        if (mod->constants[id].isSet)
        {
            mod->templateValues[id] = mod->constants[id];
            mod->templatePtrs[id]   = mod->pointers[id];
        }
        if (mod->pointers[id].isPointer)
            mod->templatePtrs[id] = mod->pointers[id];

        /* Pre-init output variables to zero with correct component count */
        if (mod->pointers[id].isPointer &&
            mod->pointers[id].storageClass == SPV_STORAGE_OUTPUT)
        {
            uint32_t cc = spv_TypeComponentCount(mod, mod->pointers[id].typeId);
            mod->templateValues[id].count = cc < SPV_MAX_COMPONENTS ? cc : SPV_MAX_COMPONENTS;
            mod->templateValues[id].isSet = 1;
        }
    }

    mod->templateBuilt = 1;

    IExec->DebugPrintF("[software_vk] Template built: bound=%lu (%lu bytes values + %lu bytes ptrs)\n",
                       (unsigned long)bound,
                       (unsigned long)(bound * sizeof(SpvValue)),
                       (unsigned long)(bound * sizeof(SpvPointer)));
}

void spv_FreeTemplate(SpvModule *mod)
{
    if (!mod) return;
    if (mod->templateValues)
    {
        IExec->FreeVec(mod->templateValues);
        mod->templateValues = NULL;
    }
    if (mod->templatePtrs)
    {
        IExec->FreeVec(mod->templatePtrs);
        mod->templatePtrs = NULL;
    }
    mod->templateBound = 0;
    mod->templateBuilt = 0;
}

/*--------------------------------------------------------------------------
** spv_SetupIODynamic -- lightweight per-invocation I/O setup
**
** This handles ONLY the per-invocation varying data:
**   - BuiltIn inputs (VertexIndex, InstanceIndex, FragCoord)
**   - Location-based inputs (vertex attributes / interpolated varyings)
**   - Push constant struct members
**   - Uniform buffer struct members
**   - UniformConstant sampler/image slot indices
**
** It does NOT copy constants or pointer state (handled by template memcpy).
** It does NOT initialise output variables (handled by template).
**------------------------------------------------------------------------*/
void spv_SetupIODynamic(SpvExecState *state)
{
    SpvModule *mod = state->module;
    state->killed = 0;

    for (uint32_t id = 0; id < mod->bound && id < SPV_MAX_IDS; id++)
    {
        if (!mod->pointers[id].isPointer)
            continue;

        uint32_t sc = mod->pointers[id].storageClass;
        uint32_t typeId = mod->pointers[id].typeId;

        if (sc == SPV_STORAGE_INPUT)
        {
            uint32_t cc = spv_TypeComponentCount(mod, typeId);

            /* Check for built-in input */
            if (mod->decorations[id].builtIn == SPV_BUILTIN_VERTEX_INDEX)
            {
                state->values[id].i[0] = (int32_t)state->vertexIndex;
                /* f[] and i[] are a union -- no sync needed */
                state->values[id].count = 1;
                state->values[id].isSet = 1;
            }
            else if (mod->decorations[id].builtIn == SPV_BUILTIN_INSTANCE_INDEX)
            {
                state->values[id].i[0] = (int32_t)state->instanceIndex;
                /* f[] and i[] are a union -- no sync needed */
                state->values[id].count = 1;
                state->values[id].isSet = 1;
            }
            else if (mod->decorations[id].builtIn == SPV_BUILTIN_FRAG_COORD)
            {
                /* FragCoord is set by the caller via state->inputs or directly */
                state->values[id].isSet = 1;
                state->values[id].count = cc < SPV_MAX_COMPONENTS ? cc : SPV_MAX_COMPONENTS;
            }
            else if (mod->decorations[id].location >= 0)
            {
                int32_t loc = mod->decorations[id].location;
                if (loc < SPV_MAX_IO_LOCATIONS)
                {
                    uint32_t n = cc < 4 ? cc : 4;
                    for (uint32_t j = 0; j < n; j++)
                        state->values[id].f[j] = state->inputs[loc][j];
                    state->values[id].count = n;
                    state->values[id].isSet = 1;
                }
            }

            /* Also check struct members for built-ins (gl_PerVertex) */
            if (typeId < SPV_MAX_IDS && mod->types[typeId].opcode == SpvOpTypeStruct)
            {
                for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                {
                    if (mod->memberDecorations[typeId].builtIn[m] == SPV_BUILTIN_VERTEX_INDEX)
                    {
                        uint32_t off = spv_StructMemberOffset(mod, typeId, m);
                        state->values[id].i[off] = (int32_t)state->vertexIndex;
                        /* f[] and i[] are a union -- no sync needed */
                        state->values[id].count = off + 1;
                        state->values[id].isSet = 1;
                    }
                    if (mod->memberDecorations[typeId].builtIn[m] == SPV_BUILTIN_INSTANCE_INDEX)
                    {
                        uint32_t off = spv_StructMemberOffset(mod, typeId, m);
                        state->values[id].i[off] = (int32_t)state->instanceIndex;
                        /* f[] and i[] are a union -- no sync needed */
                        state->values[id].count = off + 1;
                        state->values[id].isSet = 1;
                    }
                }
            }
        }
        else if (sc == SPV_STORAGE_PUSH_CONSTANT)
        {
            /* Push constant variable: read member data from pushConstants[] */
            if (state->pushConstantSize > 0 && typeId < SPV_MAX_IDS &&
                mod->types[typeId].opcode == SpvOpTypeStruct)
            {
                uint32_t compOff = 0;
                for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                {
                    int32_t byteOff = mod->memberDecorations[typeId].offset[m];
                    uint32_t memberTypeId = mod->types[typeId].memberTypeIds[m];
                    uint32_t memberCC = spv_TypeComponentCount(mod, memberTypeId);

                    if (byteOff >= 0 && (uint32_t)byteOff + memberCC * 4 <= state->pushConstantSize)
                    {
                        for (uint32_t j = 0; j < memberCC && (compOff + j) < SPV_MAX_COMPONENTS; j++)
                        {
                            uint32_t rawBits;
                            memcpy(&rawBits, &state->pushConstants[(uint32_t)byteOff + j * 4], 4);
                            memcpy(&state->values[id].f[compOff + j], &rawBits, 4);
                            memcpy(&state->values[id].i[compOff + j], &rawBits, 4);
                        }
                    }
                    compOff += memberCC;
                }
                state->values[id].count = compOff < SPV_MAX_COMPONENTS ? compOff : SPV_MAX_COMPONENTS;
                state->values[id].isSet = 1;
            }
        }
        else if (sc == SPV_STORAGE_UNIFORM)
        {
            /* Uniform buffer variable */
            int32_t setIdx     = mod->decorations[id].descriptorSet;
            int32_t bindingIdx = mod->decorations[id].binding;

            if (setIdx >= 0 && bindingIdx >= 0)
            {
                uint32_t uIdx = (uint32_t)setIdx * SPV_MAX_BINDINGS_PER_SET + (uint32_t)bindingIdx;

                if (uIdx < SPV_MAX_UNIFORM_BINDINGS &&
                    state->uniformData[uIdx] != NULL &&
                    typeId < SPV_MAX_IDS &&
                    mod->types[typeId].opcode == SpvOpTypeStruct)
                {
                    const uint8_t *uData = state->uniformData[uIdx];
                    uint32_t uSize = state->uniformSizes[uIdx];

                    uint32_t compOff = 0;
                    for (uint32_t m = 0; m < mod->types[typeId].memberCount; m++)
                    {
                        int32_t byteOff = mod->memberDecorations[typeId].offset[m];
                        uint32_t memberTypeId = mod->types[typeId].memberTypeIds[m];
                        uint32_t memberCC = spv_TypeComponentCount(mod, memberTypeId);

                        if (byteOff >= 0 && (uint32_t)byteOff + memberCC * 4 <= uSize)
                        {
                            for (uint32_t j = 0; j < memberCC && (compOff + j) < SPV_MAX_COMPONENTS; j++)
                            {
                                uint32_t rawBits;
                                memcpy(&rawBits, &uData[(uint32_t)byteOff + j * 4], 4);
                                memcpy(&state->values[id].f[compOff + j], &rawBits, 4);
                                memcpy(&state->values[id].i[compOff + j], &rawBits, 4);
                            }
                        }
                        compOff += memberCC;
                    }
                    state->values[id].count = compOff < SPV_MAX_COMPONENTS ? compOff : SPV_MAX_COMPONENTS;
                    state->values[id].isSet = 1;
                }
            }
        }
        else if (sc == SPV_STORAGE_UNIFORM_CONSTANT)
        {
            /* UniformConstant: sampler/image slot index */
            int32_t setIdx     = mod->decorations[id].descriptorSet;
            int32_t bindingIdx = mod->decorations[id].binding;

            if (setIdx >= 0 && bindingIdx >= 0)
            {
                int32_t slot = setIdx * SPV_MAX_BINDINGS_PER_SET + bindingIdx;
                if (slot < SPV_MAX_TEXTURES)
                {
                    state->values[id].i[0] = slot;
                    state->values[id].f[0] = 0.0f;
                    state->values[id].count = 1;
                    state->values[id].isSet = 1;
                }
            }
        }
        /* NOTE: SPV_STORAGE_OUTPUT is handled by the template -- skip here */
    }
}

/****************************************************************************/
/* Compiler: walk SPIR-V and create compiled instruction array              */
/****************************************************************************/

int spv_CompileProgram(SpvModule *module)
{
    if (!module || module->entryFunctionOffset == 0)
        return -1;

    /* Allocate compiled program */
    SpvCompiledProgram *prog = (SpvCompiledProgram *)IExec->AllocVecTags(
        sizeof(SpvCompiledProgram),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!prog)
        return -1;

    prog->instrs = (SpvCompiledInstr *)IExec->AllocVecTags(
        SPV_MAX_COMPILED_INSTRS * sizeof(SpvCompiledInstr),
        AVT_Type, MEMF_SHARED,
        AVT_ClearWithValue, 0,
        TAG_DONE);

    if (!prog->instrs)
    {
        IExec->FreeVec(prog);
        return -1;
    }

    /* Initialize label map with sentinel (0xFFFFFFFF = unmapped) */
    memset(prog->labelMap, 0xFF, sizeof(prog->labelMap));

    const uint32_t *code = module->code;
    uint32_t pc = module->entryFunctionOffset;
    uint32_t endPc = module->wordCount;
    int inFunction = 0;

    while (pc < endPc && prog->instrCount < SPV_MAX_COMPILED_INSTRS)
    {
        uint32_t instrWord = code[pc];
        uint32_t wc = SPV_WORDCOUNT(instrWord);
        uint32_t op = SPV_OP(instrWord);

        if (wc == 0 || pc + wc > endPc)
            break;

        SpvCompiledInstr *ci = &prog->instrs[prog->instrCount];
        /* Default: mark arg3Id as unused */
        ci->arg3Id = 0xFFFF;

        switch (op)
        {
        case SpvOpFunction:
            inFunction = 1;
            break;

        case SpvOpFunctionEnd:
            if (inFunction)
                goto compile_done;
            break;

        case SpvOpLabel:
            if (wc >= 2 && code[pc + 1] < SPV_MAX_IDS)
                prog->labelMap[code[pc + 1]] = prog->instrCount;
            break;

        case SpvOpReturn:
        case SpvOpReturnValue:
            ci->handler = jit_Return;
            prog->instrCount++;
            break;

        /* Float arithmetic */
        case SpvOpFAdd:
            if (wc >= 5)
            {
                ci->handler = jit_FAdd;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFSub:
            if (wc >= 5)
            {
                ci->handler = jit_FSub;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFMul:
            if (wc >= 5)
            {
                ci->handler = jit_FMul;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFDiv:
            if (wc >= 5)
            {
                ci->handler = jit_FDiv;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFNegate:
            if (wc >= 4)
            {
                ci->handler = jit_FNegate;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFRem:
            if (wc >= 5)
            {
                ci->handler = jit_FRem;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFMod:
            if (wc >= 5)
            {
                ci->handler = jit_FMod;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpVectorTimesScalar:
            if (wc >= 5)
            {
                ci->handler = jit_VectorTimesScalar;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* vector */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* scalar */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpMatrixTimesScalar:
            if (wc >= 5)
            {
                ci->handler = jit_MatrixTimesScalar;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* matrix */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* scalar */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpDot:
            if (wc >= 5)
            {
                ci->handler = jit_Dot;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = 1;
                prog->instrCount++;
            }
            break;

        /* Integer arithmetic */
        case SpvOpIAdd:
            if (wc >= 5)
            {
                ci->handler = jit_IAdd;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpISub:
            if (wc >= 5)
            {
                ci->handler = jit_ISub;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpIMul:
            if (wc >= 5)
            {
                ci->handler = jit_IMul;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpSDiv:
            if (wc >= 5)
            {
                ci->handler = jit_SDiv;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpSMod:
            if (wc >= 5)
            {
                ci->handler = jit_SMod;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;
        case SpvOpUDiv:
            if (wc >= 5) { ci->handler = jit_UDiv; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpUMod:
            if (wc >= 5) { ci->handler = jit_UMod; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpSRem:
            if (wc >= 5) { ci->handler = jit_SRem; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;

        case SpvOpSNegate:
            if (wc >= 4)
            {
                ci->handler = jit_SNegate;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        /* Conversion */
        case SpvOpConvertSToF:
            if (wc >= 4)
            {
                ci->handler = jit_ConvertSToF;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpConvertFToS:
            if (wc >= 4)
            {
                ci->handler = jit_ConvertFToS;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;
        case SpvOpConvertFToU:
            if (wc >= 4) { ci->handler = jit_ConvertFToU; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpConvertUToF:
            if (wc >= 4) { ci->handler = jit_ConvertUToF; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFConvert:
        case SpvOpSConvert:
        case SpvOpUConvert:
            if (wc >= 4) { ci->handler = jit_IdentityCopy; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpBitcast:
            if (wc >= 4)
            {
                uint32_t tid = code[pc+1];
                int toFloat = (tid < SPV_MAX_IDS && module->types[tid].opcode == SpvOpTypeFloat);
                ci->handler = toFloat ? jit_BitcastToFloat : jit_BitcastToInt;
                ci->resultId = (uint16_t)code[pc+2];
                ci->arg1Id = (uint16_t)code[pc+3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]);
                prog->instrCount++;
            }
            break;

        /* Float comparisons */
        case SpvOpFOrdEqual:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdEqual;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFOrdNotEqual:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdNotEqual;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFOrdLessThan:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdLessThan;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFOrdGreaterThan:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdGreaterThan;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFOrdLessThanEqual:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdLessThanEqual;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpFOrdGreaterThanEqual:
            if (wc >= 5)
            {
                ci->handler = jit_FOrdGreaterThanEqual;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;
        case SpvOpFUnordEqual:
            if (wc >= 5) { ci->handler = jit_FUnordEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFUnordNotEqual:
            if (wc >= 5) { ci->handler = jit_FUnordNotEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFUnordLessThan:
            if (wc >= 5) { ci->handler = jit_FUnordLessThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFUnordGreaterThan:
            if (wc >= 5) { ci->handler = jit_FUnordGreaterThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFUnordLessThanEqual:
            if (wc >= 5) { ci->handler = jit_FUnordLessThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpFUnordGreaterThanEqual:
            if (wc >= 5) { ci->handler = jit_FUnordGreaterThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;

        /* Integer comparisons */
        case SpvOpIEqual:
            if (wc >= 5) { ci->handler = jit_IEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpINotEqual:
            if (wc >= 5) { ci->handler = jit_INotEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpSGreaterThan:
            if (wc >= 5) { ci->handler = jit_SGreaterThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpSLessThan:
            if (wc >= 5) { ci->handler = jit_SLessThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpUGreaterThan:
            if (wc >= 5) { ci->handler = jit_UGreaterThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpUGreaterThanEqual:
            if (wc >= 5) { ci->handler = jit_UGreaterThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpSGreaterThanEqual:
            if (wc >= 5) { ci->handler = jit_SGreaterThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpULessThan:
            if (wc >= 5) { ci->handler = jit_ULessThan; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpULessThanEqual:
            if (wc >= 5) { ci->handler = jit_ULessThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpSLessThanEqual:
            if (wc >= 5) { ci->handler = jit_SLessThanEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;

        /* Logical operations */
        case SpvOpLogicalAnd:
            if (wc >= 5) { ci->handler = jit_LogicalAnd; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpLogicalOr:
            if (wc >= 5) { ci->handler = jit_LogicalOr; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpLogicalNot:
            if (wc >= 4) { ci->handler = jit_LogicalNot; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpLogicalEqual:
            if (wc >= 5) { ci->handler = jit_LogicalEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;
        case SpvOpLogicalNotEqual:
            if (wc >= 5) { ci->handler = jit_LogicalNotEqual; ci->resultId = (uint16_t)code[pc+2]; ci->arg1Id = (uint16_t)code[pc+3]; ci->arg2Id = (uint16_t)code[pc+4]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;

        /* Select */
        case SpvOpSelect:
            if (wc >= 6)
            {
                ci->handler = jit_Select;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* condition */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* true */
                ci->arg3Id   = (uint16_t)code[pc + 5]; /* false */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        /* Composite */
        case SpvOpCompositeExtract:
            if (wc >= 5)
            {
                ci->handler = jit_CompositeExtract;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* composite */
                ci->extra    = (uint16_t)code[pc + 4]; /* index */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpCompositeConstruct:
            if (wc >= 3)
            {
                /* Use fallback that reads raw code */
                ci->handler = jit_CompositeConstructFull;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->componentCount = (uint16_t)wc; /* store word count */
                ci->extra2 = (uint16_t)(pc & 0xFFFF);
                ci->pad    = (uint16_t)((pc >> 16) & 0xFFFF);
                prog->instrCount++;
            }
            break;

        case SpvOpVectorShuffle:
            if (wc >= 5)
            {
                ci->handler = jit_VectorShuffleFull;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->extra  = (uint16_t)wc; /* word count */
                ci->extra2 = (uint16_t)(pc & 0xFFFF);
                ci->pad    = (uint16_t)((pc >> 16) & 0xFFFF);
                prog->instrCount++;
            }
            break;

        /* Memory operations */
        case SpvOpStore:
            if (wc >= 3)
            {
                ci->handler = jit_Store;
                ci->arg1Id  = (uint16_t)code[pc + 1]; /* pointer */
                ci->arg2Id  = (uint16_t)code[pc + 2]; /* value */
                prog->instrCount++;
            }
            break;

        case SpvOpLoad:
            if (wc >= 4)
            {
                ci->handler = jit_Load;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* pointer */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpVariable:
            if (wc >= 4)
            {
                uint32_t ptrTypeId = code[pc + 1];
                ci->handler = jit_Variable;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)ptrTypeId;
                ci->extra    = (uint16_t)code[pc + 3]; /* storage class */
                /* Pre-compute pointee type ID and component count */
                uint32_t pointeeType = 0;
                if (ptrTypeId < SPV_MAX_IDS)
                    pointeeType = module->types[ptrTypeId].pointeeTypeId;
                ci->arg2Id = (uint16_t)pointeeType; /* store pointee type */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, pointeeType);
                prog->instrCount++;
            }
            break;

        case SpvOpAccessChain:
            if (wc >= 5)
            {
                uint32_t typeId = code[pc + 1];
                uint32_t numIndices = wc - 4;

                if (numIndices <= 2)
                {
                    /* Fast path: 1-2 indices */
                    ci->handler = jit_AccessChain;
                    ci->resultId = (uint16_t)code[pc + 2];
                    ci->arg1Id   = (uint16_t)code[pc + 3]; /* base pointer */
                    ci->arg2Id   = (uint16_t)code[pc + 4]; /* first index */
                    if (numIndices >= 2)
                        ci->arg3Id = (uint16_t)code[pc + 5]; /* second index */
                    /* Pre-store pointee type ID from result type */
                    if (typeId < SPV_MAX_IDS)
                        ci->extra = (uint16_t)module->types[typeId].pointeeTypeId;
                }
                else
                {
                    /* Full fallback for 3+ indices */
                    ci->handler = jit_AccessChainFull;
                    ci->resultId = (uint16_t)code[pc + 2];
                    ci->componentCount = (uint16_t)wc; /* store word count */
                    ci->extra2 = (uint16_t)(pc & 0xFFFF);
                    ci->pad    = (uint16_t)((pc >> 16) & 0xFFFF);
                }
                prog->instrCount++;
            }
            break;

        /* Control flow */
        case SpvOpBranch:
            if (wc >= 2)
            {
                ci->handler = jit_Branch;
                ci->extra = (uint16_t)code[pc + 1]; /* target label */
                prog->instrCount++;
            }
            break;

        case SpvOpBranchConditional:
            if (wc >= 4)
            {
                ci->handler = jit_BranchConditional;
                ci->arg1Id = (uint16_t)code[pc + 1]; /* condition */
                ci->extra  = (uint16_t)code[pc + 2]; /* true label */
                ci->extra2 = (uint16_t)code[pc + 3]; /* false label */
                prog->instrCount++;
            }
            break;

        case SpvOpPhi:
            if (wc >= 5)
            {
                ci->handler = jit_PhiFull;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->extra  = (uint16_t)wc; /* word count */
                ci->extra2 = (uint16_t)(pc & 0xFFFF);
                ci->pad    = (uint16_t)((pc >> 16) & 0xFFFF);
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        /* Matrix operations */
        case SpvOpMatrixTimesVector:
            if (wc >= 5)
            {
                ci->handler = jit_MatrixTimesVector;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* matrix */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* vector */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpVectorTimesMatrix:
            if (wc >= 5)
            {
                ci->handler = jit_VectorTimesMatrix;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* vector */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* matrix */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpMatrixTimesMatrix:
            if (wc >= 5)
            {
                uint32_t typeId_mtm = code[pc + 1];
                ci->handler = jit_MatrixTimesMatrix;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* matA */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* matB */
                /* Pre-compute result dimensions */
                if (typeId_mtm < SPV_MAX_IDS)
                {
                    const SpvTypeInfo *rt = &module->types[typeId_mtm];
                    ci->extra = (uint16_t)rt->componentCount; /* resCols */
                    ci->arg3Id = (uint16_t)spv_TypeComponentCount(module, rt->componentTypeId); /* resRows */
                }
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, typeId_mtm);
                prog->instrCount++;
            }
            break;

        case SpvOpTranspose:
            if (wc >= 4)
            {
                uint32_t typeId_tr = code[pc + 1];
                ci->handler = jit_Transpose;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                if (typeId_tr < SPV_MAX_IDS)
                {
                    const SpvTypeInfo *rt_tr = &module->types[typeId_tr];
                    ci->extra = (uint16_t)rt_tr->componentCount; /* resCols */
                    ci->arg3Id = (uint16_t)spv_TypeComponentCount(module, rt_tr->componentTypeId); /* resRows */
                }
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, typeId_tr);
                prog->instrCount++;
            }
            break;

        /* Bitwise operations */
        case SpvOpShiftRightLogical:
        case SpvOpShiftRightArithmetic:
        case SpvOpShiftLeftLogical:
        case SpvOpBitwiseOr:
        case SpvOpBitwiseXor:
        case SpvOpBitwiseAnd:
            if (wc >= 5)
            {
                ci->handler = jit_BitwiseOp;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->arg2Id   = (uint16_t)code[pc + 4];
                ci->extra    = (uint16_t)op; /* store opcode for dispatch */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        case SpvOpNot:
            if (wc >= 4)
            {
                ci->handler = jit_BitwiseNot;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3];
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                prog->instrCount++;
            }
            break;

        /* ExtInst (GLSL.std.450) */
        case SpvOpExtInst:
            if (wc >= 6)
            {
                ci->handler = jit_ExtInst;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->extra    = (uint16_t)code[pc + 4]; /* instruction number */
                ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc + 1]);
                ci->arg1Id = (wc > 5 && code[pc + 5] < SPV_MAX_IDS) ? (uint16_t)code[pc + 5] : 0;
                ci->arg2Id = (wc > 6 && code[pc + 6] < SPV_MAX_IDS) ? (uint16_t)code[pc + 6] : 0;
                ci->arg3Id = (wc > 7 && code[pc + 7] < SPV_MAX_IDS) ? (uint16_t)code[pc + 7] : 0;
                prog->instrCount++;
            }
            break;

        /* Texture sampling */
        case SpvOpSampledImage:
            if (wc >= 5)
            {
                ci->handler = jit_SampledImage;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* image */
                prog->instrCount++;
            }
            break;

        case SpvOpImageSampleImplicitLod:
            if (wc >= 5)
            {
                ci->handler = jit_ImageSampleImplicitLod;
                ci->resultId = (uint16_t)code[pc + 2];
                ci->arg1Id   = (uint16_t)code[pc + 3]; /* sampledImage */
                ci->arg2Id   = (uint16_t)code[pc + 4]; /* coordinate */
                prog->instrCount++;
            }
            break;

        /* Derivatives -- stub (return 0.0) */
        case SpvOpDPdx: case SpvOpDPdy: case SpvOpFwidth:
        case SpvOpDPdxFine: case SpvOpDPdyFine: case SpvOpFwidthFine:
        case SpvOpDPdxCoarse: case SpvOpDPdyCoarse: case SpvOpFwidthCoarse:
            if (wc >= 4) { ci->handler = jit_DerivativeStub; ci->resultId = (uint16_t)code[pc+2]; ci->componentCount = (uint16_t)spv_TypeComponentCount(module, code[pc+1]); prog->instrCount++; }
            break;

        /* Control flow */
        case SpvOpKill:
            ci->handler = jit_Kill;
            prog->instrCount++;
            break;
        case SpvOpUnreachable:
            ci->handler = jit_Nop;
            prog->instrCount++;
            break;
        case SpvOpSwitch:
            /* Variable-length -- falls through to default jit_Nop for now */
            ci->handler = jit_Nop;
            prog->instrCount++;
            break;

        /* Metadata/no-op opcodes */
        case SpvOpSelectionMerge:
        case SpvOpLoopMerge:
        case SpvOpNop:
        case SpvOpSource:
        case SpvOpName:
        case SpvOpMemberName:
        case SpvOpFunctionParameter:
            /* Skip -- no compiled instruction needed */
            break;

        default:
            /* Unknown opcode -- emit a no-op so instruction index stays valid */
            ci->handler = jit_Nop;
            prog->instrCount++;
            break;
        }

        pc += wc;
    }

compile_done:
    module->compiled = prog;

    /* Build template state for fast invocation setup */
    spv_BuildTemplate(module);

    IExec->DebugPrintF("[software_vk] SPIR-V compiled: %lu instructions\n",
                       (unsigned long)prog->instrCount);

    return 0;
}

/****************************************************************************/
/* Compiled program execution                                               */
/****************************************************************************/

int spv_ExecuteCompiled(SpvExecState *state)
{
    SpvModule *mod = state->module;
    if (!mod || !mod->compiled)
        return -1;

    SpvCompiledProgram *prog = mod->compiled;
    if (prog->instrCount == 0)
        return -1;

    /* Initialize I/O and constants (same as interpreter).
    ** If a template is available, use fast dynamic-only setup;
    ** otherwise fall back to full spv_SetupIO. */
    if (mod->templateBuilt && mod->templateValues && mod->templatePtrs)
        spv_SetupIODynamic(state);
    else
        spv_SetupIO(state);

    state->jumpTarget = 0;
    uint32_t ipc = 0;
    uint32_t maxIter = 100000;
    uint32_t prevLabel = 0;
    uint32_t currentLabel = 0;

    while (ipc < prog->instrCount && maxIter-- > 0)
    {
        const SpvCompiledInstr *instr = &prog->instrs[ipc];

        /* For Phi instructions, temporarily store prevLabel in jumpTarget
        ** so the handler can read it */
        if (instr->handler == jit_PhiFull)
        {
            state->jumpTarget = prevLabel;
            instr->handler(state, instr);
            /* jumpTarget was reset by the Phi handler */
        }
        else
        {
            instr->handler(state, instr);
        }

        /* Check for jump (branch handlers set state->jumpTarget) */
        if (state->jumpTarget != 0)
        {
            uint32_t target = state->jumpTarget;
            state->jumpTarget = 0;

            if (target == 0xFFFFFFFF)
                goto exec_done; /* return */

            if (target < SPV_MAX_IDS && prog->labelMap[target] != 0xFFFFFFFF)
            {
                prevLabel = currentLabel;
                currentLabel = target;
                ipc = prog->labelMap[target];
            }
            else
            {
                ipc++;
            }
        }
        else
        {
            ipc++;
        }
    }

exec_done:
    spv_CollectOutputs(state);
    return 0;
}

/****************************************************************************/
/* Free compiled program                                                    */
/****************************************************************************/

void spv_FreeCompiled(SpvCompiledProgram *prog)
{
    if (!prog)
        return;

    if (prog->instrs)
        IExec->FreeVec(prog->instrs);

    IExec->FreeVec(prog);
}
