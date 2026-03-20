#ifndef SWVK_SPIRV_H
#define SWVK_SPIRV_H

/*
** swvk_spirv.h -- SPIR-V parser and interpreter for software_vk.library
**
** Handles loading, parsing, and executing SPIR-V shader modules.
** The interpreter is a simple register-based VM where each SPIR-V
** result ID maps to a value slot.
**
** The interpreter supports 144 core opcodes + 37 GLSL.std.450 = 210+
** distinct operations. Includes comparisons, control flow, matrix ops,
** integer/unsigned arithmetic, type conversions, derivatives, image ops,
** bit manipulation, OpKill.
*/

#include <stdint.h>

/*------------------------------------------------------------------------
** SPIR-V constants
**----------------------------------------------------------------------*/

#define SPV_MAGIC_NUMBER  0x07230203

/* Maximum tracked SPIR-V IDs and value components */
#define SPV_MAX_IDS        512
#define SPV_MAX_COMPONENTS  16   /* Enough for vec4[4] or vec3[5] */
#define SPV_MAX_MEMBERS      8   /* Max struct members */

/*------------------------------------------------------------------------
** SPIR-V opcodes (supported subset)
**----------------------------------------------------------------------*/
enum {
    SpvOpNop                    = 0,
    SpvOpSource                 = 3,
    SpvOpName                   = 5,
    SpvOpMemberName             = 6,
    SpvOpExtInstImport          = 11,
    SpvOpExtInst                = 12,
    SpvOpMemoryModel            = 14,
    SpvOpEntryPoint             = 15,
    SpvOpExecutionMode          = 16,
    SpvOpCapability             = 17,
    SpvOpTypeVoid               = 19,
    SpvOpTypeBool               = 20,
    SpvOpTypeInt                = 21,
    SpvOpTypeFloat              = 22,
    SpvOpTypeVector             = 23,
    SpvOpTypeMatrix             = 24,
    SpvOpTypeImage              = 25,
    SpvOpTypeSampler            = 26,
    SpvOpTypeSampledImage       = 27,
    SpvOpTypeArray              = 28,
    SpvOpTypeStruct             = 30,
    SpvOpTypePointer            = 32,
    SpvOpTypeFunction           = 33,
    SpvOpConstantTrue           = 41,
    SpvOpConstantFalse          = 42,
    SpvOpConstant               = 43,
    SpvOpConstantComposite      = 44,
    SpvOpFunction               = 54,
    SpvOpFunctionParameter      = 55,
    SpvOpFunctionEnd            = 56,
    SpvOpFunctionCall           = 57,
    SpvOpVariable               = 59,
    SpvOpLoad                   = 61,
    SpvOpStore                  = 62,
    SpvOpAccessChain            = 65,
    SpvOpDecorate               = 71,
    SpvOpMemberDecorate         = 72,
    SpvOpVectorExtractDynamic   = 77,
    SpvOpVectorInsertDynamic    = 78,
    SpvOpVectorShuffle          = 79,
    SpvOpCompositeConstruct     = 80,
    SpvOpCompositeExtract       = 81,
    SpvOpCompositeInsert        = 82,
    SpvOpTranspose              = 84,
    SpvOpSampledImage           = 86,
    SpvOpImageSampleImplicitLod = 87,
    SpvOpImageSampleExplicitLod = 88,
    SpvOpImageFetch             = 95,
    SpvOpImage                  = 100,
    SpvOpImageQuerySizeLod      = 103,
    SpvOpImageQuerySize         = 104,
    SpvOpConvertFToU            = 109,
    SpvOpConvertFToS            = 110,
    SpvOpConvertSToF            = 111,
    SpvOpConvertUToF            = 112,
    SpvOpUConvert               = 113,
    SpvOpSConvert               = 114,
    SpvOpFConvert               = 115,
    SpvOpBitcast                = 124,
    SpvOpSNegate                = 126,
    SpvOpFNegate                = 127,
    SpvOpIAdd                   = 128,
    SpvOpFAdd                   = 129,
    SpvOpISub                   = 130,
    SpvOpFSub                   = 131,
    SpvOpIMul                   = 132,
    SpvOpFMul                   = 133,
    SpvOpUDiv                   = 134,
    SpvOpSDiv                   = 135,
    SpvOpFDiv                   = 136,
    SpvOpUMod                   = 137,
    SpvOpSRem                   = 138,
    SpvOpSMod                   = 139,
    SpvOpFRem                   = 140,
    SpvOpFMod                   = 141,
    SpvOpVectorTimesScalar      = 142,
    SpvOpMatrixTimesScalar      = 143,
    SpvOpVectorTimesMatrix      = 144,
    SpvOpMatrixTimesVector      = 145,
    SpvOpMatrixTimesMatrix      = 146,
    SpvOpOuterProduct           = 147,
    SpvOpDot                    = 148,
    SpvOpIsNan                  = 156,
    SpvOpIsInf                  = 157,
    SpvOpLogicalEqual           = 164,
    SpvOpLogicalNotEqual        = 165,
    SpvOpLogicalOr              = 166,
    SpvOpLogicalAnd             = 167,
    SpvOpLogicalNot             = 168,
    SpvOpSelect                 = 169,
    SpvOpIEqual                 = 170,
    SpvOpINotEqual              = 171,
    SpvOpUGreaterThan           = 172,
    SpvOpSGreaterThan           = 173,
    SpvOpUGreaterThanEqual      = 174,
    SpvOpSGreaterThanEqual      = 175,
    SpvOpULessThan              = 176,
    SpvOpSLessThan              = 177,
    SpvOpULessThanEqual         = 178,
    SpvOpSLessThanEqual         = 179,
    SpvOpFOrdEqual              = 180,
    SpvOpFUnordEqual            = 181,
    SpvOpFOrdNotEqual           = 182,
    SpvOpFUnordNotEqual         = 183,
    SpvOpFOrdLessThan           = 184,
    SpvOpFUnordLessThan         = 185,
    SpvOpFOrdGreaterThan        = 186,
    SpvOpFUnordGreaterThan      = 187,
    SpvOpFOrdLessThanEqual      = 188,
    SpvOpFUnordLessThanEqual    = 189,
    SpvOpFOrdGreaterThanEqual   = 190,
    SpvOpFUnordGreaterThanEqual = 191,
    SpvOpShiftRightLogical      = 194,
    SpvOpShiftRightArithmetic   = 195,
    SpvOpShiftLeftLogical       = 196,
    SpvOpBitwiseOr              = 197,
    SpvOpBitwiseXor             = 198,
    SpvOpBitwiseAnd             = 199,
    SpvOpNot                    = 200,
    SpvOpBitFieldInsert         = 201,
    SpvOpBitFieldSExtract       = 202,
    SpvOpBitFieldUExtract       = 203,
    SpvOpBitReverse             = 204,
    SpvOpBitCount               = 205,
    SpvOpDPdx                   = 207,
    SpvOpDPdy                   = 208,
    SpvOpFwidth                 = 209,
    SpvOpDPdxFine               = 210,
    SpvOpDPdyFine               = 211,
    SpvOpFwidthFine             = 212,
    SpvOpDPdxCoarse             = 213,
    SpvOpDPdyCoarse             = 214,
    SpvOpFwidthCoarse           = 215,
    SpvOpPhi                    = 245,
    SpvOpLoopMerge              = 246,
    SpvOpSelectionMerge         = 247,
    SpvOpLabel                  = 248,
    SpvOpBranch                 = 249,
    SpvOpBranchConditional      = 250,
    SpvOpSwitch                 = 251,
    SpvOpKill                   = 252,
    SpvOpReturn                 = 253,
    SpvOpReturnValue            = 254,
    SpvOpUnreachable            = 255,
};

/*------------------------------------------------------------------------
** GLSL.std.450 extended instruction constants
**----------------------------------------------------------------------*/
#define GLSL_STD_450_Round          1
#define GLSL_STD_450_Trunc          3
#define GLSL_STD_450_FAbs           4
#define GLSL_STD_450_FSign          6
#define GLSL_STD_450_SSign          7
#define GLSL_STD_450_Floor          8
#define GLSL_STD_450_Ceil           9
#define GLSL_STD_450_Fract         10
#define GLSL_STD_450_Radians       11
#define GLSL_STD_450_Degrees       12
#define GLSL_STD_450_Sin           13
#define GLSL_STD_450_Cos           14
#define GLSL_STD_450_Tan           15
#define GLSL_STD_450_Asin          16
#define GLSL_STD_450_Acos          17
#define GLSL_STD_450_Atan          18
#define GLSL_STD_450_Atan2         25
#define GLSL_STD_450_Pow           26
#define GLSL_STD_450_Exp           27
#define GLSL_STD_450_Log           28
#define GLSL_STD_450_Exp2          29
#define GLSL_STD_450_Log2          30
#define GLSL_STD_450_Sqrt          31
#define GLSL_STD_450_InverseSqrt   32
#define GLSL_STD_450_FMin          37
#define GLSL_STD_450_FMax          40
#define GLSL_STD_450_FClamp        43
#define GLSL_STD_450_FMix          46
#define GLSL_STD_450_Step          48
#define GLSL_STD_450_SmoothStep    49
#define GLSL_STD_450_Length        66
#define GLSL_STD_450_Distance      67
#define GLSL_STD_450_Cross         68
#define GLSL_STD_450_Normalize     69
#define GLSL_STD_450_FaceForward   70
#define GLSL_STD_450_Reflect       71
#define GLSL_STD_450_Refract       72

/* Decoration IDs */
#define SPV_DECORATION_BUILTIN      11
#define SPV_DECORATION_LOCATION     30

/* BuiltIn values */
#define SPV_BUILTIN_POSITION        0
#define SPV_BUILTIN_POINT_SIZE      1
#define SPV_BUILTIN_FRAG_COORD      15
#define SPV_BUILTIN_VERTEX_INDEX    42
#define SPV_BUILTIN_INSTANCE_INDEX  43

/* Storage classes */
#define SPV_STORAGE_UNIFORM_CONSTANT 0
#define SPV_STORAGE_INPUT           1
#define SPV_STORAGE_UNIFORM         2
#define SPV_STORAGE_OUTPUT          3
#define SPV_STORAGE_PRIVATE         6
#define SPV_STORAGE_FUNCTION        7
#define SPV_STORAGE_PUSH_CONSTANT   9

/* Additional decoration IDs */
#define SPV_DECORATION_BINDING          33
#define SPV_DECORATION_DESCRIPTOR_SET   34
#define SPV_DECORATION_OFFSET           35

/*------------------------------------------------------------------------
** Parsed type info
**----------------------------------------------------------------------*/
typedef struct SpvTypeInfo
{
    uint16_t opcode;           /* SpvOpType* that created this */
    uint16_t componentCount;   /* Vector size, array length */
    uint32_t componentTypeId;  /* Element type for vectors/arrays */
    uint16_t memberCount;      /* Struct member count */
    uint32_t memberTypeIds[SPV_MAX_MEMBERS];
    uint32_t storageClass;     /* For pointer types */
    uint32_t pointeeTypeId;    /* What the pointer points to */
    uint16_t width;            /* Bit width for int/float */
    uint16_t signedness;       /* For int types */
} SpvTypeInfo;

/*------------------------------------------------------------------------
** Decoration info
**----------------------------------------------------------------------*/
typedef struct SpvDecorations
{
    int32_t  location;         /* -1 if not decorated */
    int32_t  builtIn;          /* -1 if not decorated */
    int32_t  binding;          /* -1 if not decorated */
    int32_t  descriptorSet;    /* -1 if not decorated */
} SpvDecorations;

/* Member decorations (per-struct) */
typedef struct SpvMemberDecorations
{
    int32_t  builtIn[SPV_MAX_MEMBERS];   /* -1 if not decorated */
    int32_t  location[SPV_MAX_MEMBERS];  /* -1 if not decorated */
    int32_t  offset[SPV_MAX_MEMBERS];    /* -1 if not decorated (byte offset for push constants) */
} SpvMemberDecorations;

/*------------------------------------------------------------------------
** Value storage (register file)
**----------------------------------------------------------------------*/
typedef struct SpvValue
{
    union {
        float    f[SPV_MAX_COMPONENTS];
        int32_t  i[SPV_MAX_COMPONENTS];
        uint32_t u[SPV_MAX_COMPONENTS];
    };
    uint32_t count;            /* Number of active components */
    uint32_t isSet;            /* Has this value been written? */
} SpvValue;

/*------------------------------------------------------------------------
** Pointer representation (for OpVariable, OpAccessChain)
**----------------------------------------------------------------------*/
typedef struct SpvPointer
{
    uint32_t targetId;         /* SPIR-V ID this pointer refers to */
    uint32_t offset;           /* Component offset within target */
    uint32_t storageClass;     /* Storage class */
    uint32_t typeId;           /* Pointee type ID */
    uint32_t isPointer;        /* 1 if this value is a pointer */
} SpvPointer;

/*------------------------------------------------------------------------
** Parsed SPIR-V module
**----------------------------------------------------------------------*/
typedef struct SpvCompiledProgram SpvCompiledProgram;  /* forward decl */

typedef struct SpvModule
{
    uint32_t           *code;       /* Byte-swapped SPIR-V words (owned) */
    uint32_t            wordCount;
    uint32_t            bound;      /* Highest ID + 1 */

    SpvTypeInfo         types[SPV_MAX_IDS];
    SpvDecorations      decorations[SPV_MAX_IDS];
    SpvMemberDecorations memberDecorations[SPV_MAX_IDS];
    SpvValue            constants[SPV_MAX_IDS];
    SpvPointer          pointers[SPV_MAX_IDS];

    uint32_t            entryPointId;
    uint32_t            entryFunctionOffset; /* Word offset of entry function body */

    SpvCompiledProgram *compiled;  /* Pre-compiled program (NULL if not compiled) */

    /* Template state for fast invocation setup.
    ** Instead of memset(zeros) + spv_SetupIO constant/pointer copy each
    ** invocation, we memcpy a pre-built template that already contains
    ** constants, pointer state, and output variable initialisation. */
    SpvValue   *templateValues;  /* Heap-allocated, templateBound entries */
    SpvPointer *templatePtrs;    /* Heap-allocated, templateBound entries */
    uint32_t    templateBound;   /* Number of entries in templates */
    int         templateBuilt;   /* 1 if templates are valid */
} SpvModule;

/*------------------------------------------------------------------------
** Interpreter execution state
**----------------------------------------------------------------------*/
#define SPV_MAX_IO_LOCATIONS  8
#define SPV_MAX_BINDINGS_PER_SET 8   /* must match SWVK_MAX_DESCRIPTOR_BINDINGS */
#define SPV_MAX_UNIFORM_BINDINGS 32  /* max descriptor sets * max bindings */

typedef struct SpvExecState
{
    SpvModule  *module;
    SpvValue    values[SPV_MAX_IDS];   /* Runtime value registers */
    SpvPointer  ptrs[SPV_MAX_IDS];     /* Runtime pointer state */

    /* Built-in inputs */
    uint32_t    vertexIndex;
    uint32_t    instanceIndex;

    /* Built-in outputs */
    float       position[4];           /* gl_Position */
    float       pointSize;             /* gl_PointSize */

    /* Push constants */
    uint8_t     pushConstants[128];
    uint32_t    pushConstantSize;

    /* User-defined I/O (by location) */
    float       inputs[SPV_MAX_IO_LOCATIONS][4];
    uint32_t    inputCounts[SPV_MAX_IO_LOCATIONS];
    float       outputs[SPV_MAX_IO_LOCATIONS][4];
    uint32_t    outputCounts[SPV_MAX_IO_LOCATIONS];

    /* Descriptor set uniform buffer data.
    ** Indexed by: set * MAX_BINDINGS_PER_SET + binding
    ** where MAX_BINDINGS_PER_SET = 8 (SWVK_MAX_DESCRIPTOR_BINDINGS) */
    const uint8_t *uniformData[SPV_MAX_UNIFORM_BINDINGS];
    uint32_t       uniformSizes[SPV_MAX_UNIFORM_BINDINGS];

    /* Texture data for combined image samplers.
    ** Indexed by: set * MAX_BINDINGS_PER_SET + binding */
    #define SPV_MAX_TEXTURES 8
    struct {
        const uint8_t *data;        /* pixel data (RGBA8) */
        uint32_t       width;
        uint32_t       height;
        uint32_t       bytesPerPixel; /* 4 for RGBA8 */
        uint32_t       format;        /* VkFormat value */
        uint32_t       magFilter;     /* 0=nearest, 1=linear */
        uint32_t       addressModeU;  /* 0=repeat, 2=clamp_to_edge */
        uint32_t       addressModeV;
        uint32_t       isSet;
    } textures[SPV_MAX_TEXTURES];

    /* Compiled interpreter jump target (0=none, 0xFFFFFFFF=return) */
    uint32_t    jumpTarget;

    /* OpKill: fragment discard flag (1=killed, skip pixel write) */
    uint32_t    killed;
} SpvExecState;

/*------------------------------------------------------------------------
** Pre-compiled program (threaded interpreter)
**----------------------------------------------------------------------*/

struct SpvCompiledInstr;  /* forward declaration for handler typedef */

typedef void (*SpvCompiledHandler)(SpvExecState *state, const struct SpvCompiledInstr *instr);

typedef struct SpvCompiledInstr {
    SpvCompiledHandler handler;  /* Direct function pointer */
    uint16_t resultId;           /* Result value ID */
    uint16_t arg1Id;             /* First operand ID */
    uint16_t arg2Id;             /* Second operand ID */
    uint16_t arg3Id;             /* Third operand ID */
    uint16_t componentCount;     /* Pre-computed component count */
    uint16_t extra;              /* Opcode-specific: ExtInst func, compare op, etc. */
    uint16_t extra2;             /* Additional data (target label for branches) */
    uint16_t pad;                /* Alignment */
} SpvCompiledInstr;

#define SPV_MAX_COMPILED_INSTRS 1024

struct SpvCompiledProgram {
    SpvCompiledInstr *instrs;    /* Array of pre-decoded instructions */
    uint32_t instrCount;         /* Number of instructions */
    uint32_t labelMap[SPV_MAX_IDS]; /* Label ID -> instruction index */
};

/*------------------------------------------------------------------------
** API
**----------------------------------------------------------------------*/

/* Parse a byte-swapped SPIR-V module. Returns 0 on success, -1 on error. */
int spv_ParseModule(SpvModule *module, const uint32_t *code, uint32_t wordCount);

/* Execute the entry point function. Returns 0 on success, -1 on error. */
int spv_Execute(SpvExecState *state);

/* Get component count for a type ID */
uint32_t spv_TypeComponentCount(const SpvModule *module, uint32_t typeId);

/* Compile a parsed module into a pre-decoded instruction array.
** Returns 0 on success, -1 on error. */
int spv_CompileProgram(SpvModule *module);

/* Execute using the pre-compiled program. Returns 0 on success. */
int spv_ExecuteCompiled(SpvExecState *state);

/* Free a compiled program. */
void spv_FreeCompiled(SpvCompiledProgram *prog);

/* Build pre-computed template state for fast invocation setup.
** Called automatically from spv_CompileProgram; also callable separately. */
void spv_BuildTemplate(SpvModule *mod);

/* Free template state (called from DestroyShaderModule). */
void spv_FreeTemplate(SpvModule *mod);

/* Lightweight I/O setup that only handles per-invocation data
** (vertex index, instance index, input locations, push constants,
** uniform buffers, sampler bindings).  Assumes template state was
** already memcpy'd into state->values / state->ptrs. */
void spv_SetupIODynamic(SpvExecState *state);

#endif /* SWVK_SPIRV_H */
