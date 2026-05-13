/*
** ogles2_vk.library -- W3D Nova Hardware Vulkan ICD for AmigaOS 4
**
** ogles2vk_exec.c -- Command buffer execution via OGLES2 backend
**
** Uses ogles2.library (OpenGL ES 2.0) instead of direct W3D Nova API
** calls. OGLES2 wraps W3D Nova with a standard GL API, integrated
** GLSL compiler, and SPIR-V binary support.
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <interfaces/exec.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "ogles2vk_internal.h"

/* OGLES2 interface (defined in ogles2vk_main.c) */
struct OGLES2IFace;
extern struct OGLES2IFace *IOGLES2;

/* GL constants we need (avoid pulling in full GL headers in this file) */
#define GL_FALSE                 0
#define GL_TRUE                  1
#define GL_TRIANGLES             0x0004
#define GL_TRIANGLE_STRIP        0x0005
#define GL_TRIANGLE_FAN          0x0006
#define GL_LINES                 0x0001
#define GL_LINE_STRIP            0x0003
#define GL_POINTS                0x0000
#define GL_DEPTH_TEST            0x0B71
#define GL_CULL_FACE             0x0B44
#define GL_SCISSOR_TEST          0x0C11
#define GL_BLEND                 0x0BE2
#define GL_BACK                  0x0405
#define GL_FRONT                 0x0404
#define GL_FRONT_AND_BACK        0x0408
#define GL_CW                    0x0900
#define GL_CCW                   0x0901
#define GL_LESS                  0x0201
#define GL_COLOR_BUFFER_BIT      0x4000
#define GL_DEPTH_BUFFER_BIT      0x0100
#define GL_VERTEX_SHADER         0x8B31
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_INFO_LOG_LENGTH       0x8B84
#define GL_FLOAT                 0x1406
#define GL_FLOAT_VEC2            0x8B50
#define GL_FLOAT_VEC3            0x8B51
#define GL_FLOAT_VEC4            0x8B52
#define GL_FLOAT_MAT2            0x8B5A
#define GL_FLOAT_MAT3            0x8B5B
#define GL_FLOAT_MAT4            0x8B5C
#define GL_ACTIVE_UNIFORMS       0x8B86
#define GL_ARRAY_BUFFER          0x8892
#define GL_STATIC_DRAW           0x88E4
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_UNSIGNED_SHORT        0x1403
#define GL_UNSIGNED_INT          0x1405
#define GL_SHADER_BINARY_FORMAT_SPIR_V 0x9551

/* Texture constants */
#define GL_TEXTURE_2D            0x0DE1
#define GL_TEXTURE0              0x84C0
#define GL_RGBA                  0x1908
#define GL_UNSIGNED_BYTE         0x1401
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801
#define GL_TEXTURE_WRAP_S        0x2802
#define GL_TEXTURE_WRAP_T        0x2803
#define GL_NEAREST               0x2600
#define GL_LINEAR                0x2601
#define GL_REPEAT                0x2901
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_MIRRORED_REPEAT       0x8370
#define GL_SAMPLER_2D            0x8B5E
#define GL_UNPACK_ALIGNMENT      0x0CF5

/* Stencil/state constants */
#define GL_STENCIL_TEST          0x0B90
#define GL_KEEP                  0x1E00
#define GL_REPLACE               0x1E01
#define GL_INCR                  0x1E02
#define GL_DECR                  0x1E03
#define GL_INVERT                0x150A
#define GL_INCR_WRAP             0x8507
#define GL_DECR_WRAP             0x8508
#define GL_ZERO                  0x0000
#define GL_RASTERIZER_DISCARD    0x8C89
#define GL_POLYGON_OFFSET_FILL   0x8037

/* Max texture units for descriptor binding */
#define OGLES2VK_MAX_BOUND_TEXTURES 8

/* Minimal OGLES2IFace -- just the methods we call.
** Method offsets MUST match the real OGLES2IFace from the SDK header. */
struct OGLES2IFace
{
    struct InterfaceData Data;
    uint32 APICALL (*Obtain)(struct OGLES2IFace *Self);
    uint32 APICALL (*Release)(struct OGLES2IFace *Self);
    void   APICALL (*Expunge)(struct OGLES2IFace *Self);
    struct Interface * APICALL (*Clone)(struct OGLES2IFace *Self);

    /* agl methods (slots 4-9) */
    void * APICALL (*aglCreateContext_AVOID)(struct OGLES2IFace *Self, uint32 *errcode, struct TagItem *tags);
    void * APICALL (*aglCreateContextTags_AVOID)(struct OGLES2IFace *Self, uint32 *errcode, ...);
    void   APICALL (*aglDestroyContext)(struct OGLES2IFace *Self, void *context);
    void   APICALL (*aglMakeCurrent)(struct OGLES2IFace *Self, void *context);
    void   APICALL (*aglSwapBuffers)(struct OGLES2IFace *Self);
    void   APICALL (*aglSetBitmap)(struct OGLES2IFace *Self, void *bitmap);

    /* GL ES 2.0 methods -- offsets must match interfaces/ogles2.h exactly */
    void   APICALL (*glActiveTexture)(struct OGLES2IFace *Self, uint32 texture);
    void   APICALL (*glAttachShader)(struct OGLES2IFace *Self, uint32 program, uint32 shader);
    void   APICALL (*glBindAttribLocation)(struct OGLES2IFace *Self, uint32 program, uint32 index, const char *name);
    void   APICALL (*glBindBuffer)(struct OGLES2IFace *Self, uint32 target, uint32 buffer);
    void   APICALL (*glBindFramebuffer)(struct OGLES2IFace *Self, uint32 target, uint32 framebuffer);
    void   APICALL (*glBindRenderbuffer)(struct OGLES2IFace *Self, uint32 target, uint32 renderbuffer);
    void   APICALL (*glBindTexture)(struct OGLES2IFace *Self, uint32 target, uint32 texture);
    void   APICALL (*glBlendColor)(struct OGLES2IFace *Self, float red, float green, float blue, float alpha);
    void   APICALL (*glBlendEquation)(struct OGLES2IFace *Self, uint32 mode);
    void   APICALL (*glBlendEquationSeparate)(struct OGLES2IFace *Self, uint32 modeRGB, uint32 modeAlpha);
    void   APICALL (*glBlendFunc)(struct OGLES2IFace *Self, uint32 sfactor, uint32 dfactor);
    void   APICALL (*glBlendFuncSeparate)(struct OGLES2IFace *Self, uint32 sR, uint32 dR, uint32 sA, uint32 dA);
    void   APICALL (*glBufferData)(struct OGLES2IFace *Self, uint32 target, int32 size, const void *data, uint32 usage);
    void   APICALL (*glBufferSubData)(struct OGLES2IFace *Self, uint32 target, int32 offset, int32 size, const void *data);
    uint32 APICALL (*glCheckFramebufferStatus)(struct OGLES2IFace *Self, uint32 target);
    void   APICALL (*glClear)(struct OGLES2IFace *Self, uint32 mask);
    void   APICALL (*glClearColor)(struct OGLES2IFace *Self, float r, float g, float b, float a);
    void   APICALL (*glClearDepthf)(struct OGLES2IFace *Self, float d);
    void   APICALL (*glClearStencil)(struct OGLES2IFace *Self, int32 s);
    void   APICALL (*glColorMask)(struct OGLES2IFace *Self, uint8 r, uint8 g, uint8 b, uint8 a);
    void   APICALL (*glCompileShader)(struct OGLES2IFace *Self, uint32 shader);
    void   APICALL (*glCompressedTexImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, uint32 f, int32 w, int32 h, int32 b, int32 s, const void *d);
    void   APICALL (*glCompressedTexSubImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, int32 x, int32 y, int32 w, int32 h, uint32 f, int32 s, const void *d);
    void   APICALL (*glCopyTexImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, uint32 f, int32 x, int32 y, int32 w, int32 h, int32 b);
    void   APICALL (*glCopyTexSubImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, int32 xo, int32 yo, int32 x, int32 y, int32 w, int32 h);
    uint32 APICALL (*glCreateProgram)(struct OGLES2IFace *Self);
    uint32 APICALL (*glCreateShader)(struct OGLES2IFace *Self, uint32 type);
    void   APICALL (*glCullFace)(struct OGLES2IFace *Self, uint32 mode);
    void   APICALL (*glDeleteBuffers)(struct OGLES2IFace *Self, int32 n, const uint32 *bufs);
    void   APICALL (*glDeleteFramebuffers)(struct OGLES2IFace *Self, int32 n, const uint32 *fbs);
    void   APICALL (*glDeleteProgram)(struct OGLES2IFace *Self, uint32 program);
    void   APICALL (*glDeleteRenderbuffers)(struct OGLES2IFace *Self, int32 n, const uint32 *rbs);
    void   APICALL (*glDeleteShader)(struct OGLES2IFace *Self, uint32 shader);
    void   APICALL (*glDeleteTextures)(struct OGLES2IFace *Self, int32 n, const uint32 *texs);
    void   APICALL (*glDepthFunc)(struct OGLES2IFace *Self, uint32 func);
    void   APICALL (*glDepthMask)(struct OGLES2IFace *Self, uint8 flag);
    void   APICALL (*glDepthRangef)(struct OGLES2IFace *Self, float n, float f);
    void   APICALL (*glDetachShader)(struct OGLES2IFace *Self, uint32 program, uint32 shader);
    void   APICALL (*glDisable)(struct OGLES2IFace *Self, uint32 cap);
    void   APICALL (*glDisableVertexAttribArray)(struct OGLES2IFace *Self, uint32 index);
    void   APICALL (*glDrawArrays)(struct OGLES2IFace *Self, uint32 mode, int32 first, int32 count);
    void   APICALL (*glDrawElements)(struct OGLES2IFace *Self, uint32 mode, int32 count, uint32 type, const void *indices);
    void   APICALL (*glEnable)(struct OGLES2IFace *Self, uint32 cap);
    void   APICALL (*glEnableVertexAttribArray)(struct OGLES2IFace *Self, uint32 index);
    void   APICALL (*glFinish)(struct OGLES2IFace *Self);
    void   APICALL (*glFlush)(struct OGLES2IFace *Self);
    void   APICALL (*glFramebufferRenderbuffer)(struct OGLES2IFace *Self, uint32 t, uint32 a, uint32 rt, uint32 rb);
    void   APICALL (*glFramebufferTexture2D)(struct OGLES2IFace *Self, uint32 t, uint32 a, uint32 tt, uint32 tex, int32 l);
    void   APICALL (*glFrontFace)(struct OGLES2IFace *Self, uint32 mode);
    void   APICALL (*glGenBuffers)(struct OGLES2IFace *Self, int32 n, uint32 *bufs);
    void   APICALL (*glGenerateMipmap)(struct OGLES2IFace *Self, uint32 target);
    void   APICALL (*glGenFramebuffers)(struct OGLES2IFace *Self, int32 n, uint32 *fbs);
    void   APICALL (*glGenRenderbuffers)(struct OGLES2IFace *Self, int32 n, uint32 *rbs);
    void   APICALL (*glGenTextures)(struct OGLES2IFace *Self, int32 n, uint32 *texs);
    void   APICALL (*glGetActiveAttrib)(struct OGLES2IFace *Self, uint32 prog, uint32 idx, int32 bufSize, int32 *len, int32 *size, uint32 *type, char *name);
    void   APICALL (*glGetActiveUniform)(struct OGLES2IFace *Self, uint32 prog, uint32 idx, int32 bufSize, int32 *len, int32 *size, uint32 *type, char *name);
    void   APICALL (*glGetAttachedShaders)(struct OGLES2IFace *Self, uint32 prog, int32 maxCount, int32 *count, uint32 *shaders);
    int32  APICALL (*glGetAttribLocation)(struct OGLES2IFace *Self, uint32 prog, const char *name);
    void   APICALL (*glGetBooleanv)(struct OGLES2IFace *Self, uint32 pname, uint8 *data);
    void   APICALL (*glGetBufferParameteriv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, int32 *params);
    uint32 APICALL (*glGetError)(struct OGLES2IFace *Self);
    void   APICALL (*glGetFloatv)(struct OGLES2IFace *Self, uint32 pname, float *data);
    void   APICALL (*glGetFramebufferAttachmentParameteriv)(struct OGLES2IFace *Self, uint32 t, uint32 a, uint32 p, int32 *params);
    void   APICALL (*glGetIntegerv)(struct OGLES2IFace *Self, uint32 pname, int32 *data);
    void   APICALL (*glGetProgramiv)(struct OGLES2IFace *Self, uint32 prog, uint32 pname, int32 *params);
    void   APICALL (*glGetProgramInfoLog)(struct OGLES2IFace *Self, uint32 prog, int32 bufSize, int32 *len, char *log);
    void   APICALL (*glGetRenderbufferParameteriv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, int32 *params);
    void   APICALL (*glGetShaderiv)(struct OGLES2IFace *Self, uint32 shader, uint32 pname, int32 *params);
    void   APICALL (*glGetShaderInfoLog)(struct OGLES2IFace *Self, uint32 shader, int32 bufSize, int32 *len, char *log);
    void   APICALL (*glGetShaderPrecisionFormat)(struct OGLES2IFace *Self, uint32 st, uint32 pt, int32 *range, int32 *prec);
    void   APICALL (*glGetShaderSource)(struct OGLES2IFace *Self, uint32 shader, int32 bufSize, int32 *len, char *src);
    const char * APICALL (*glGetString)(struct OGLES2IFace *Self, uint32 name);
    void   APICALL (*glGetTexParameterfv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, float *params);
    void   APICALL (*glGetTexParameteriv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, int32 *params);
    void   APICALL (*glGetUniformfv)(struct OGLES2IFace *Self, uint32 prog, int32 location, float *params);
    void   APICALL (*glGetUniformiv)(struct OGLES2IFace *Self, uint32 prog, int32 location, int32 *params);
    int32  APICALL (*glGetUniformLocation)(struct OGLES2IFace *Self, uint32 prog, const char *name);
    void   APICALL (*glGetVertexAttribfv)(struct OGLES2IFace *Self, uint32 idx, uint32 pname, float *params);
    void   APICALL (*glGetVertexAttribiv)(struct OGLES2IFace *Self, uint32 idx, uint32 pname, int32 *params);
    void   APICALL (*glGetVertexAttribPointerv)(struct OGLES2IFace *Self, uint32 idx, uint32 pname, void **ptr);
    void   APICALL (*glHint)(struct OGLES2IFace *Self, uint32 target, uint32 mode);
    uint8  APICALL (*glIsBuffer)(struct OGLES2IFace *Self, uint32 buffer);
    uint8  APICALL (*glIsEnabled)(struct OGLES2IFace *Self, uint32 cap);
    uint8  APICALL (*glIsFramebuffer)(struct OGLES2IFace *Self, uint32 fb);
    uint8  APICALL (*glIsProgram)(struct OGLES2IFace *Self, uint32 prog);
    uint8  APICALL (*glIsRenderbuffer)(struct OGLES2IFace *Self, uint32 rb);
    uint8  APICALL (*glIsShader)(struct OGLES2IFace *Self, uint32 shader);
    uint8  APICALL (*glIsTexture)(struct OGLES2IFace *Self, uint32 texture);
    void   APICALL (*glLineWidth)(struct OGLES2IFace *Self, float width);
    void   APICALL (*glLinkProgram)(struct OGLES2IFace *Self, uint32 program);
    void   APICALL (*glPixelStorei)(struct OGLES2IFace *Self, uint32 pname, int32 param);
    void   APICALL (*glPolygonOffset)(struct OGLES2IFace *Self, float factor, float units);
    void   APICALL (*glReadPixels)(struct OGLES2IFace *Self, int32 x, int32 y, int32 w, int32 h, uint32 fmt, uint32 type, void *pixels);
    void   APICALL (*glReleaseShaderCompiler)(struct OGLES2IFace *Self);
    void   APICALL (*glRenderbufferStorage)(struct OGLES2IFace *Self, uint32 target, uint32 fmt, int32 w, int32 h);
    void   APICALL (*glSampleCoverage)(struct OGLES2IFace *Self, float value, uint8 invert);
    void   APICALL (*glScissor)(struct OGLES2IFace *Self, int32 x, int32 y, int32 w, int32 h);
    void   APICALL (*glShaderBinary)(struct OGLES2IFace *Self, int32 count, const uint32 *shaders, uint32 binaryformat, const void *binary, int32 length);
    void   APICALL (*glShaderSource)(struct OGLES2IFace *Self, uint32 shader, int32 count, const char *const *string, const int32 *length);
    void   APICALL (*glStencilFunc)(struct OGLES2IFace *Self, uint32 func, int32 ref, uint32 mask);
    void   APICALL (*glStencilFuncSeparate)(struct OGLES2IFace *Self, uint32 face, uint32 func, int32 ref, uint32 mask);
    void   APICALL (*glStencilMask)(struct OGLES2IFace *Self, uint32 mask);
    void   APICALL (*glStencilMaskSeparate)(struct OGLES2IFace *Self, uint32 face, uint32 mask);
    void   APICALL (*glStencilOp)(struct OGLES2IFace *Self, uint32 sfail, uint32 dpfail, uint32 dppass);
    void   APICALL (*glStencilOpSeparate)(struct OGLES2IFace *Self, uint32 face, uint32 sfail, uint32 dpfail, uint32 dppass);
    void   APICALL (*glTexImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, int32 ifmt, int32 w, int32 h, int32 b, uint32 fmt, uint32 type, const void *pixels);
    void   APICALL (*glTexParameterf)(struct OGLES2IFace *Self, uint32 target, uint32 pname, float param);
    void   APICALL (*glTexParameterfv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, const float *params);
    void   APICALL (*glTexParameteri)(struct OGLES2IFace *Self, uint32 target, uint32 pname, int32 param);
    void   APICALL (*glTexParameteriv)(struct OGLES2IFace *Self, uint32 target, uint32 pname, const int32 *params);
    void   APICALL (*glTexSubImage2D)(struct OGLES2IFace *Self, uint32 t, int32 l, int32 x, int32 y, int32 w, int32 h, uint32 fmt, uint32 type, const void *pixels);
    void   APICALL (*glUniform1f)(struct OGLES2IFace *Self, int32 location, float v0);
    void   APICALL (*glUniform1fv)(struct OGLES2IFace *Self, int32 location, int32 count, const float *value);
    void   APICALL (*glUniform1i)(struct OGLES2IFace *Self, int32 location, int32 v0);
    void   APICALL (*glUniform1iv)(struct OGLES2IFace *Self, int32 location, int32 count, const int32 *value);
    void   APICALL (*glUniform2f)(struct OGLES2IFace *Self, int32 location, float v0, float v1);
    void   APICALL (*glUniform2fv)(struct OGLES2IFace *Self, int32 location, int32 count, const float *value);
    void   APICALL (*glUniform2i)(struct OGLES2IFace *Self, int32 location, int32 v0, int32 v1);
    void   APICALL (*glUniform2iv)(struct OGLES2IFace *Self, int32 location, int32 count, const int32 *value);
    void   APICALL (*glUniform3f)(struct OGLES2IFace *Self, int32 location, float v0, float v1, float v2);
    void   APICALL (*glUniform3fv)(struct OGLES2IFace *Self, int32 location, int32 count, const float *value);
    void   APICALL (*glUniform3i)(struct OGLES2IFace *Self, int32 location, int32 v0, int32 v1, int32 v2);
    void   APICALL (*glUniform3iv)(struct OGLES2IFace *Self, int32 location, int32 count, const int32 *value);
    void   APICALL (*glUniform4f)(struct OGLES2IFace *Self, int32 location, float v0, float v1, float v2, float v3);
    void   APICALL (*glUniform4fv)(struct OGLES2IFace *Self, int32 location, int32 count, const float *value);
    void   APICALL (*glUniform4i)(struct OGLES2IFace *Self, int32 location, int32 v0, int32 v1, int32 v2, int32 v3);
    void   APICALL (*glUniform4iv)(struct OGLES2IFace *Self, int32 location, int32 count, const int32 *value);
    void   APICALL (*glUniformMatrix2fv)(struct OGLES2IFace *Self, int32 location, int32 count, uint8 transpose, const float *value);
    void   APICALL (*glUniformMatrix3fv)(struct OGLES2IFace *Self, int32 location, int32 count, uint8 transpose, const float *value);
    void   APICALL (*glUniformMatrix4fv)(struct OGLES2IFace *Self, int32 location, int32 count, uint8 transpose, const float *value);
    void   APICALL (*glUseProgram)(struct OGLES2IFace *Self, uint32 program);
    void   APICALL (*glValidateProgram)(struct OGLES2IFace *Self, uint32 program);
    void   APICALL (*glVertexAttrib1f)(struct OGLES2IFace *Self, uint32 idx, float x);
    void   APICALL (*glVertexAttrib1fv)(struct OGLES2IFace *Self, uint32 idx, const float *v);
    void   APICALL (*glVertexAttrib2f)(struct OGLES2IFace *Self, uint32 idx, float x, float y);
    void   APICALL (*glVertexAttrib2fv)(struct OGLES2IFace *Self, uint32 idx, const float *v);
    void   APICALL (*glVertexAttrib3f)(struct OGLES2IFace *Self, uint32 idx, float x, float y, float z);
    void   APICALL (*glVertexAttrib3fv)(struct OGLES2IFace *Self, uint32 idx, const float *v);
    void   APICALL (*glVertexAttrib4f)(struct OGLES2IFace *Self, uint32 idx, float x, float y, float z, float w);
    void   APICALL (*glVertexAttrib4fv)(struct OGLES2IFace *Self, uint32 idx, const float *v);
    void   APICALL (*glVertexAttribPointer)(struct OGLES2IFace *Self, uint32 idx, int32 size, uint32 type, uint8 normalized, int32 stride, const void *ptr);
    void   APICALL (*glViewport)(struct OGLES2IFace *Self, int32 x, int32 y, int32 w, int32 h);

    /* Extended methods after standard GL ES 2.0 */
    void   APICALL (*glPolygonMode)(struct OGLES2IFace *Self, uint32 face, uint32 mode);
    void * APICALL (*aglSetParams_AVOID)(struct OGLES2IFace *Self, struct TagItem *tags);
    void * APICALL (*aglSetParamsTags_AVOID)(struct OGLES2IFace *Self, ...);
    void   APICALL (*glProvokingVertex)(struct OGLES2IFace *Self, uint32 mode);
    void   APICALL (*glDrawElementsBaseVertexOES)(struct OGLES2IFace *Self, uint32 mode, int32 count, uint32 type, const void *indices, int32 basevertex);
    void * APICALL (*aglCreateContext2)(struct OGLES2IFace *Self, uint32 *errcode, struct TagItem *tags);
    void * APICALL (*aglCreateContextTags2)(struct OGLES2IFace *Self, uint32 *errcode, ...);
    void   APICALL (*aglSetParams2)(struct OGLES2IFace *Self, struct TagItem *tags);
};

/* OGLES2 context creation tags */
#define OGLES2_CCT_MIN          (1UL<<31)
#define OGLES2_CCT_WINDOW       (OGLES2_CCT_MIN+1)
#define OGLES2_CCT_DEPTH        (OGLES2_CCT_MIN+3)
#define OGLES2_CCT_VSYNC        (OGLES2_CCT_MIN+5)
#define OGLES2_CCT_SHADER_COMPAT_PATCH (OGLES2_CCT_MIN+10)
#define OGLES2_CCT_SPIRV_OPTIMIZE      (OGLES2_CCT_MIN+17)

/****************************************************************************/
/* OGLES2 context management                                                */
/****************************************************************************/

int ogles2vk_InitOGLES2Context(OGLES2VKDevice *dev, void *window)
{
    if (!dev || !IOGLES2 || !window)
    {
        /* Pinpoint which dependency is missing — the previous catch-all
        ** message hid whether IOGLES2 failed to load or whether the surface
        ** never received a window handle. */
        D(("[ogles2_vk] InitOGLES2 missing: dev=0x%08lx IOGLES2=0x%08lx window=0x%08lx\n",
                           (unsigned long)(uintptr_t)dev,
                           (unsigned long)(uintptr_t)IOGLES2,
                           (unsigned long)(uintptr_t)window));
        return 0;
    }

    if (dev->glContext)
        return 1;  /* Already initialised */

    uint32_t errCode = 0;
    struct TagItem ctxTags[] = {
        { OGLES2_CCT_WINDOW,             (Tag)window },
        { OGLES2_CCT_DEPTH,              24 },
        { OGLES2_CCT_VSYNC,              0 },
        { OGLES2_CCT_SHADER_COMPAT_PATCH, 1 },
        { TAG_DONE,                       0 }
    };

    void *ctx = IOGLES2->aglCreateContext2((uint32 *)&errCode, ctxTags);
    if (!ctx)
    {
        D(("[ogles2_vk] aglCreateContext2 failed (err=%lu)\n",
                           (unsigned long)errCode));
        return 0;
    }

    IOGLES2->aglMakeCurrent(ctx);
    dev->glContext = ctx;

    /* Query renderer name */
#ifdef DEBUG    
    const char *renderer = IOGLES2->glGetString(0x1F01); /* GL_RENDERER */
    D(("[ogles2_vk] OGLES2 context created: %s\n",
                       renderer ? renderer : "unknown"));
#endif

    /* Set initial GL state */
    IOGLES2->glDisable(GL_DEPTH_TEST);
    IOGLES2->glDisable(GL_CULL_FACE);
    IOGLES2->glDisable(GL_BLEND);
    IOGLES2->glDepthMask(GL_TRUE);
    IOGLES2->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    IOGLES2->glClearDepthf(1.0f);

    return 1;
}

void ogles2vk_ShutdownOGLES2Context(OGLES2VKDevice *dev)
{
    if (!dev || !dev->glContext || !IOGLES2)
        return;

    IOGLES2->aglDestroyContext(dev->glContext);
    dev->glContext = NULL;

    D(("[ogles2_vk] OGLES2 context destroyed\n"));
}

/* Toggle OGLES2 vsync on the current context. Called by
** ogles2vk_CreateSwapchainKHR with the swapchain's VkPresentModeKHR
** so that VK_PRESENT_MODE_FIFO_KHR (and friends) actually
** honour the display refresh rate -- previously the context was
** created with VSYNC=0 hard-coded, which let the example loop submit
** at thousands of FPS against a 75-144 Hz display, producing visible
** animation skipping as the W3D Nova compositor sampled one of many
** queued frames per refresh non-deterministically. With this enabled,
** aglSwapBuffers blocks for the actual display tick, so each
** vkQueuePresentKHR call pace-matches the monitor regardless of
** refresh rate. */
void ogles2vk_SetVsync(OGLES2VKDevice *dev, int enable)
{
    if (!dev || !dev->glContext || !IOGLES2)
        return;

    struct TagItem tags[] = {
        { OGLES2_CCT_VSYNC, (Tag)(enable ? 1u : 0u) },
        { TAG_DONE,          0 }
    };
    IOGLES2->aglSetParams2(tags);
    D(("[ogles2_vk] OGLES2 vsync %s\n", enable ? "ON" : "OFF"));
}

/****************************************************************************/
/* GL resource cleanup helpers (called from ogles2vk_shader.c)              */
/****************************************************************************/

void ogles2vk_DeleteShader(uint32_t shader)
{
    if (IOGLES2 && shader)
        IOGLES2->glDeleteShader(shader);
}

void ogles2vk_DeleteProgram(uint32_t program)
{
    if (IOGLES2 && program)
        IOGLES2->glDeleteProgram(program);
}

/****************************************************************************/
/* Shader compilation via OGLES2                                            */
/****************************************************************************/

static int ogles2vk_EnsureShadersCompiled(OGLES2VKPipeline *pipe)
{
    if (!IOGLES2)
        return 0;

    if (pipe->glProgram)
        return 1;  /* Already linked */

    if (!pipe->vertShader || !pipe->fragShader)
    {
        D(("[ogles2_vk] Pipeline missing vert or frag shader\n"));
        return 0;
    }

    /* Compile vertex shader via SPIRV-Cross transpiler (if not already done) */
    if (!pipe->vertShader->glShader)
    {
        uint32_t vs = IOGLES2->glCreateShader(GL_VERTEX_SHADER);
        if (!vs) return 0;

        int32 status = 0;
        OGLES2VKTranspileResult *vr = &pipe->vertShader->transpile;

        if (ogles2vk_SPIRV2GLSL(pipe->vertShader->codeOrig,
                pipe->vertShader->wordCount, pipe->vertShader->codeSize,
                1, vr) && vr->glsl)
        {
            D(("[ogles2_vk] SPIRV-Cross vert GLSL:\n%s\n", vr->glsl));
            const char *src = vr->glsl;
            IOGLES2->glShaderSource(vs, 1, &src, NULL);
            IOGLES2->glCompileShader(vs);

            IOGLES2->glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
            if (!status)
            {
                char log[512] = {0};
                IOGLES2->glGetShaderInfoLog(vs, sizeof(log)-1, NULL, log);
                D(("[ogles2_vk] GLSL vert compile error: %s\n", log));
            }
        }

        if (!status)
        {
            char log[512] = {0};
            IOGLES2->glGetShaderInfoLog(vs, sizeof(log)-1, NULL, log);
            D(("[ogles2_vk] Vert shader FAILED: %s\n", log));
            IOGLES2->glDeleteShader(vs);
            return 0;
        }

        pipe->vertShader->glShader = vs;
        D(("[ogles2_vk] Vertex shader compiled (GL %lu)\n",
                           (unsigned long)vs));
    }

    /* Compile fragment shader via SPIRV-Cross transpiler (if not already done) */
    if (!pipe->fragShader->glShader)
    {
        uint32_t fs = IOGLES2->glCreateShader(GL_FRAGMENT_SHADER);
        if (!fs) return 0;

        int32 status = 0;
        OGLES2VKTranspileResult *fr = &pipe->fragShader->transpile;

        if (ogles2vk_SPIRV2GLSL(pipe->fragShader->codeOrig,
                pipe->fragShader->wordCount, pipe->fragShader->codeSize,
                0, fr) && fr->glsl)
        {
            D(("[ogles2_vk] SPIRV-Cross frag GLSL:\n%s\n", fr->glsl));
            const char *src = fr->glsl;
            IOGLES2->glShaderSource(fs, 1, &src, NULL);
            IOGLES2->glCompileShader(fs);

            IOGLES2->glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
            if (!status)
            {
                char log[512] = {0};
                IOGLES2->glGetShaderInfoLog(fs, sizeof(log)-1, NULL, log);
                D(("[ogles2_vk] GLSL frag compile error: %s\n", log));
            }
        }

        if (!status)
        {
            char log[512] = {0};
            IOGLES2->glGetShaderInfoLog(fs, sizeof(log)-1, NULL, log);
            D(("[ogles2_vk] Frag shader FAILED: %s\n", log));
            IOGLES2->glDeleteShader(fs);
            return 0;
        }

        pipe->fragShader->glShader = fs;
        D(("[ogles2_vk] Fragment shader compiled (GL %lu)\n",
                           (unsigned long)fs));
    }

    /* Link program */
    uint32_t prog = IOGLES2->glCreateProgram();
    if (!prog)
        return 0;

    IOGLES2->glAttachShader(prog, pipe->vertShader->glShader);
    IOGLES2->glAttachShader(prog, pipe->fragShader->glShader);
    IOGLES2->glLinkProgram(prog);

    int32 linkStatus = 0;
    IOGLES2->glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        char log[512] = {0};
        IOGLES2->glGetProgramInfoLog(prog, sizeof(log)-1, NULL, log);
        D(("[ogles2_vk] Program link error: %s\n", log));
        IOGLES2->glDeleteProgram(prog);
        return 0;
    }

    pipe->glProgram = prog;
    D(("[ogles2_vk] Shader program linked (GL %lu)\n",
                       (unsigned long)prog));

    /* Use cached transpile results from shader modules (survive across pipelines) */
    OGLES2VKTranspileResult *vertResult = &pipe->vertShader->transpile;
    OGLES2VKTranspileResult *fragResult = &pipe->fragShader->transpile;

    /* Look up push constant vec4 array uniform from SPIRV-Cross result.
    ** Both vert and frag may have push constants; prefer whichever has them. */
    pipe->pcArrayLoc = -1;
    pipe->pcArraySize = 0;
    {
        OGLES2VKTranspileResult *pcResult = NULL;
        if (vertResult->pcArraySize > 0)
            pcResult = vertResult;
        else if (fragResult->pcArraySize > 0)
            pcResult = fragResult;

        if (pcResult)
        {
            /* SPIRV-Cross flatten-ubo emits uniform name as array, e.g. "PushConstants[0]".
            ** glGetUniformLocation wants the base name or "name[0]". */
            char pcUniformName[80];
            snprintf(pcUniformName, sizeof(pcUniformName), "%s[0]", pcResult->pcArrayName);
            pipe->pcArrayLoc = IOGLES2->glGetUniformLocation(prog, pcUniformName);
            if (pipe->pcArrayLoc < 0)
            {
                /* Try without [0] suffix */
                pipe->pcArrayLoc = IOGLES2->glGetUniformLocation(prog, pcResult->pcArrayName);
            }
            pipe->pcArraySize = pcResult->pcArraySize;

            D(("[ogles2_vk] Push constant array '%s': loc=%ld size=%lu vec4s\n",
                               pcResult->pcArrayName,
                               (long)pipe->pcArrayLoc,
                               (unsigned long)pipe->pcArraySize));
        }
    }

    /* Look up sampler uniforms from SPIRV-Cross reflection results.
    ** Merge samplers from both vert and frag results. */
    pipe->samplerCount = 0;
    memset(pipe->samplerLocs, 0xFF, sizeof(pipe->samplerLocs)); /* -1 */
    {
        OGLES2VKTranspileResult *sampResults[2] = { vertResult, fragResult };
        for (int r = 0; r < 2; r++)
        {
            OGLES2VKTranspileResult *sr = sampResults[r];
            for (uint32_t i = 0; i < sr->samplerCount && pipe->samplerCount < OGLES2VK_MAX_SAMPLERS; i++)
            {
                uint32_t idx = pipe->samplerCount++;
                pipe->samplerLocs[idx] = IOGLES2->glGetUniformLocation(prog, sr->samplers[i].name);

                D(("[ogles2_vk] Sampler '%s' (set=%u bind=%u): loc=%ld\n",
                                   sr->samplers[i].name,
                                   (unsigned)sr->samplers[i].set,
                                   (unsigned)sr->samplers[i].binding,
                                   (long)pipe->samplerLocs[idx]));
            }
        }
    }

    return 1;
}

/****************************************************************************/
/* Topology mapping                                                         */
/****************************************************************************/

static uint32_t ogles2vk_MapTopology(VkPrimitiveTopology topology)
{
    switch (topology)
    {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:     return GL_POINTS;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      return GL_LINES;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:     return GL_LINE_STRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  return GL_TRIANGLES;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
    default:
        D(("[ogles2_vk] Unsupported topology %u, defaulting to TRIANGLES\n",
                           (unsigned)topology));
        return GL_TRIANGLES;
    }
}

/****************************************************************************/
/* VkFormat -> GL vertex attribute mapping                                  */
/****************************************************************************/

static int ogles2vk_FormatToAttrib(VkFormat format, int32 *outSize, uint32_t *outType, uint8_t *outNormalized)
{
    *outNormalized = GL_FALSE;
    switch (format)
    {
    /* 32-bit float (only float formats present in the project's reduced
    ** vulkan_core.h alongside R8G8B8A8_UNORM). */
    case VK_FORMAT_R32_SFLOAT:
        *outSize = 1; *outType = GL_FLOAT; return 1;
    case VK_FORMAT_R32G32_SFLOAT:
        *outSize = 2; *outType = GL_FLOAT; return 1;
    case VK_FORMAT_R32G32B32_SFLOAT:
        *outSize = 3; *outType = GL_FLOAT; return 1;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *outSize = 4; *outType = GL_FLOAT; return 1;

    /* 8-bit unsigned normalised RGBA — ImGui packs vertex colour as 4 bytes
    ** in this format (format value 37). Without this case the driver
    ** rejects every ImGui draw call. */
    case VK_FORMAT_R8G8B8A8_UNORM:
        *outSize = 4; *outType = GL_UNSIGNED_BYTE; *outNormalized = GL_TRUE; return 1;

    default:
        D(("[ogles2_vk] Unsupported vertex format %lu\n",
                           (unsigned long)format));
        return 0;
    }
}

/****************************************************************************/
/* Sampler state mapping                                                    */
/****************************************************************************/

static int32_t ogles2vk_MapFilter(VkFilter filter)
{
    switch (filter)
    {
    case VK_FILTER_NEAREST: return GL_NEAREST;
    case VK_FILTER_LINEAR:  return GL_LINEAR;
    default:
        D(("[ogles2_vk] Unsupported filter %u, defaulting to LINEAR\n",
                           (unsigned)filter));
        return GL_LINEAR;
    }
}

static int32_t ogles2vk_MapAddressMode(VkSamplerAddressMode mode)
{
    switch (mode)
    {
    case VK_SAMPLER_ADDRESS_MODE_REPEAT:          return GL_REPEAT;
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:   return GL_CLAMP_TO_EDGE;
    default:
        D(("[ogles2_vk] Unsupported address mode %u, defaulting to REPEAT\n",
                           (unsigned)mode));
        return GL_REPEAT;
    }
}

/* Map VkFormat to bytes per pixel (for transfer commands) */
static uint32_t ogles2vk_FormatBpp(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT:
        return 4;
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    case VK_FORMAT_D16_UNORM:
        return 2;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4;
    default:
        return 4;
    }
}

/* Map Vulkan stencil op to GL stencil op */
static uint32_t ogles2vk_MapStencilOp(uint32_t op)
{
    switch (op)
    {
    case 0 /* VK_STENCIL_OP_KEEP */:               return GL_KEEP;
    case 1 /* VK_STENCIL_OP_ZERO */:               return GL_ZERO;
    case 2 /* VK_STENCIL_OP_REPLACE */:             return GL_REPLACE;
    case 3 /* VK_STENCIL_OP_INCREMENT_AND_CLAMP */: return GL_INCR;
    case 4 /* VK_STENCIL_OP_DECREMENT_AND_CLAMP */: return GL_DECR;
    case 5 /* VK_STENCIL_OP_INVERT */:              return GL_INVERT;
    case 6 /* VK_STENCIL_OP_INCREMENT_AND_WRAP */:  return GL_INCR_WRAP;
    case 7 /* VK_STENCIL_OP_DECREMENT_AND_WRAP */:  return GL_DECR_WRAP;
    default:                                         return GL_KEEP;
    }
}

/* Map Vulkan compare op to GL depth func */
static uint32_t ogles2vk_MapDepthFunc(VkCompareOp op)
{
    switch (op)
    {
    case VK_COMPARE_OP_NEVER:            return 0x0200; /* GL_NEVER */
    case VK_COMPARE_OP_LESS:             return GL_LESS;
    case VK_COMPARE_OP_EQUAL:            return 0x0202; /* GL_EQUAL */
    case VK_COMPARE_OP_LESS_OR_EQUAL:    return 0x0203; /* GL_LEQUAL */
    case VK_COMPARE_OP_GREATER:          return 0x0204; /* GL_GREATER */
    case VK_COMPARE_OP_NOT_EQUAL:        return 0x0205; /* GL_NOTEQUAL */
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return 0x0206; /* GL_GEQUAL */
    case VK_COMPARE_OP_ALWAYS:           return 0x0207; /* GL_ALWAYS */
    default:
        D(("[ogles2_vk] Unsupported compare op %u, defaulting to LESS\n",
                           (unsigned)op));
        return GL_LESS;
    }
}

/****************************************************************************/
/* Command buffer execution                                                 */
/****************************************************************************/

void ogles2vk_ExecuteCommandBuffer(OGLES2VKDevice *dev, OGLES2VKCommandBuffer *cmdbuf)
{
    OGLES2VKPipeline *curPipeline = NULL;
    VkPrimitiveTopology curTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    int pipelineReady = 0;

    /* Push constant tracking */
    uint8_t pushConstData[OGLES2VK_MAX_PUSH_CONSTANT_SIZE];
    uint32_t pushConstSize = 0;
    int pushConstDirty = 0;

    /* Vertex buffer tracking */
    OGLES2VKBuffer *boundVBs[OGLES2VK_MAX_VERTEX_BINDINGS];
    VkDeviceSize boundVBOffsets[OGLES2VK_MAX_VERTEX_BINDINGS];
    int vbDirty = 0;
    uint32_t glVBOs[OGLES2VK_MAX_VERTEX_BINDINGS];
    uint32_t glVBOCount = 0;
    uint32_t enabledAttribs[OGLES2VK_MAX_VERTEX_ATTRIBUTES];
    uint32_t enabledAttribCount = 0;

    /* Index buffer tracking */
    OGLES2VKBuffer *boundIndexBuffer = NULL;
    VkDeviceSize boundIndexOffset = 0;
    VkIndexType  boundIndexType = VK_INDEX_TYPE_UINT32;
    uint32_t glIBO = 0;

    /* Descriptor set tracking */
    OGLES2VKDescriptorSet *boundDescSets[OGLES2VK_MAX_DESCRIPTOR_SETS];
    memset(boundDescSets, 0, sizeof(boundDescSets));

    /* Texture tracking */
    uint32_t glTextures[OGLES2VK_MAX_BOUND_TEXTURES];
    uint32_t glTextureCount = 0;
    memset(glTextures, 0, sizeof(glTextures));

    if (!dev || !cmdbuf || !dev->glContext || !IOGLES2)
        return;

    memset(pushConstData, 0, sizeof(pushConstData));
    memset(boundVBs, 0, sizeof(boundVBs));
    memset(boundVBOffsets, 0, sizeof(boundVBOffsets));
    memset(glVBOs, 0, sizeof(glVBOs));

    for (uint32_t i = 0; i < cmdbuf->commandCount; i++)
    {
        OGLES2VKCommand *c = &cmdbuf->commands[i];

        switch (c->type)
        {
        case OGLES2VK_CMD_BIND_PIPELINE:
        {
            OGLES2VKPipeline *newPipe = c->bindPipeline.pipeline;
            /* Only force VBO reconfiguration if pipeline changed */
            if (newPipe != curPipeline)
                vbDirty = 1;
            curPipeline = newPipe;
            pipelineReady = 0;
            if (curPipeline)
            {
                curTopology = curPipeline->topology;

                if (ogles2vk_EnsureShadersCompiled(curPipeline))
                {
                    IOGLES2->glUseProgram(curPipeline->glProgram);
                    pipelineReady = 1;

                    /* Apply pipeline state */
                    if (curPipeline->depthTestEnable)
                    {
                        IOGLES2->glEnable(GL_DEPTH_TEST);
                        IOGLES2->glDepthFunc(ogles2vk_MapDepthFunc(curPipeline->depthCompareOp));
                    }
                    else
                        IOGLES2->glDisable(GL_DEPTH_TEST);

                    IOGLES2->glDepthMask(curPipeline->depthWriteEnable ? GL_TRUE : GL_FALSE);

                    if (curPipeline->blendEnable)
                        IOGLES2->glEnable(GL_BLEND);
                    else
                        IOGLES2->glDisable(GL_BLEND);

                    if (curPipeline->cullMode == VK_CULL_MODE_NONE)
                        IOGLES2->glDisable(GL_CULL_FACE);
                    else
                    {
                        IOGLES2->glEnable(GL_CULL_FACE);
                        if (curPipeline->cullMode & VK_CULL_MODE_BACK_BIT)
                            IOGLES2->glCullFace(GL_BACK);
                        else
                            IOGLES2->glCullFace(GL_FRONT);
                    }

                    IOGLES2->glFrontFace(
                        curPipeline->frontFace == VK_FRONT_FACE_CLOCKWISE ? GL_CW : GL_CCW);
                }
                else
                {
                    D(("[ogles2_vk] Pipeline not ready, draw skipped\n"));
                }
            }
            break;
        }

        case OGLES2VK_CMD_SET_VIEWPORT:
        {
            VkViewport *vp = &c->setViewport.viewport;
            IOGLES2->glViewport((int32)vp->x, (int32)vp->y,
                                (int32)vp->width, (int32)vp->height);
            IOGLES2->glDepthRangef(vp->minDepth, vp->maxDepth);
            break;
        }

        case OGLES2VK_CMD_SET_SCISSOR:
        {
            int32_t sx = c->setScissor.scissor.offset.x;
            int32_t sy = c->setScissor.scissor.offset.y;
            IOGLES2->glEnable(GL_SCISSOR_TEST);
            IOGLES2->glScissor(sx < 0 ? 0 : sx, sy < 0 ? 0 : sy,
                (int32)c->setScissor.scissor.extent.width,
                (int32)c->setScissor.scissor.extent.height);
            break;
        }

        case OGLES2VK_CMD_BEGIN_RENDERING:
            if (c->beginRendering.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
            {
                IOGLES2->glClearColor(
                    c->beginRendering.clearValue.color.float32[0],
                    c->beginRendering.clearValue.color.float32[1],
                    c->beginRendering.clearValue.color.float32[2],
                    c->beginRendering.clearValue.color.float32[3]);

                uint32_t clearBits = GL_COLOR_BUFFER_BIT;

                if (c->beginRendering.depthAttachment &&
                    c->beginRendering.depthLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                {
                    IOGLES2->glClearDepthf(
                        c->beginRendering.depthClearValue.depthStencil.depth);
                    clearBits |= GL_DEPTH_BUFFER_BIT;
                }

                IOGLES2->glClear(clearBits);
            }
            break;

        case OGLES2VK_CMD_END_RENDERING:
            IOGLES2->glFinish();
            break;

        case OGLES2VK_CMD_PUSH_CONSTANTS:
        {
            uint32_t off = c->pushConstants.offset;
            uint32_t sz  = c->pushConstants.size;
            if (off + sz <= OGLES2VK_MAX_PUSH_CONSTANT_SIZE)
            {
                memcpy(pushConstData + off, c->pushConstants.data, sz);
                if (off + sz > pushConstSize)
                    pushConstSize = off + sz;
                pushConstDirty = 1;
            }
            break;
        }

        case OGLES2VK_CMD_DRAW:
        case OGLES2VK_CMD_DRAW_INDEXED:
        {
            if (!pipelineReady)
                break;

            int isIndexed = (c->type == OGLES2VK_CMD_DRAW_INDEXED);

            /* Index buffer validation (indexed draws only) */
            uint32_t idxCount = 0, firstIndex = 0, idxSize = 0, glIdxType = 0;
            int32_t  vertexOffset = 0;
            uint8_t *ibBase = NULL;

            if (isIndexed)
            {
                if (!boundIndexBuffer || !boundIndexBuffer->boundMemory ||
                    !boundIndexBuffer->boundMemory->data)
                {
                    D(("[ogles2_vk] DrawIndexed: no index buffer bound\n"));
                    break;
                }

                idxCount     = c->drawIndexed.indexCount;
                firstIndex   = c->drawIndexed.firstIndex;
                vertexOffset = c->drawIndexed.vertexOffset;

                if (idxCount == 0)
                    break;

                idxSize   = (boundIndexType == VK_INDEX_TYPE_UINT16) ? 2 : 4;
                glIdxType = (boundIndexType == VK_INDEX_TYPE_UINT16)
                            ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

                /* Guard against integer overflow */
                if (idxCount > 0x7FFFFFFFU / idxSize)
                {
                    D(("[ogles2_vk] DrawIndexed: index count too large\n"));
                    break;
                }

                /* Validate index buffer offset within memory */
                if (boundIndexBuffer->boundOffset + boundIndexOffset >=
                    boundIndexBuffer->boundMemory->size)
                {
                    D(("[ogles2_vk] DrawIndexed: offset past end of memory\n"));
                    break;
                }

                ibBase = (uint8_t *)boundIndexBuffer->boundMemory->data
                         + boundIndexBuffer->boundOffset + boundIndexOffset;
                VkDeviceSize ibAvail = boundIndexBuffer->boundMemory->size
                                       - boundIndexBuffer->boundOffset
                                       - boundIndexOffset;
                VkDeviceSize ibRequired = ((VkDeviceSize)firstIndex
                                          + (VkDeviceSize)idxCount)
                                          * idxSize;

                if (ibRequired > ibAvail)
                {
                    D(("[ogles2_vk] DrawIndexed: index buffer overflow "
                                       "(need %lu, have %lu)\n",
                                       (unsigned long)ibRequired,
                                       (unsigned long)ibAvail));
                    break;
                }
            }

            /* Upload push constants as vec4 array (SPIRV-Cross flatten-ubo) */
            if (pushConstDirty && curPipeline && curPipeline->glProgram)
            {
                if (curPipeline->pcArrayLoc >= 0 && curPipeline->pcArraySize > 0)
                {
                    /* Pad push constant data to vec4-aligned size for upload */
                    uint32_t uploadBytes = curPipeline->pcArraySize * 16;
                    float pcPadded[32]; /* 32 floats = 8 vec4s = 128 bytes max */
                    memset(pcPadded, 0, sizeof(pcPadded));

                    uint32_t copyBytes = pushConstSize;
                    if (copyBytes > uploadBytes)
                        copyBytes = uploadBytes;
                    if (copyBytes > sizeof(pcPadded))
                        copyBytes = sizeof(pcPadded);
                    memcpy(pcPadded, pushConstData, copyBytes);

                    IOGLES2->glUniform4fv(curPipeline->pcArrayLoc,
                        (int32)curPipeline->pcArraySize, pcPadded);
                }
                pushConstDirty = 0;
            }

            /* Bind textures from descriptor sets */
            if (curPipeline && curPipeline->glProgram)
            {
                uint32_t texUnit = 0;

                for (uint32_t s = 0; s < OGLES2VK_MAX_DESCRIPTOR_SETS && texUnit < OGLES2VK_MAX_BOUND_TEXTURES; s++)
                {
                    OGLES2VKDescriptorSet *dset = boundDescSets[s];
                    if (!dset) continue;

                    for (uint32_t b = 0; b < dset->bindingCount && texUnit < OGLES2VK_MAX_BOUND_TEXTURES; b++)
                    {
                        OGLES2VKDescriptorBinding *db = &dset->bindings[b];

                        if (db->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                            continue;
                        if (!db->imageView || !db->imageView->image || !db->sampler)
                            continue;

                        OGLES2VKImage   *img = db->imageView->image;
                        OGLES2VKSampler *sam = db->sampler;

                        if (!img->boundMemory || !img->boundMemory->data)
                        {
                            D(("[ogles2_vk] Texture s%u b%u: no bound memory\n",
                                               (unsigned)s, (unsigned)b));
                            continue;
                        }

                        /* Only support R8G8B8A8 and B8G8R8A8 RGBA formats (4 bpp) */
                        uint32_t bpp = 4;
                        if (img->format != VK_FORMAT_R8G8B8A8_UNORM &&
                            img->format != VK_FORMAT_R8G8B8A8_SRGB &&
                            img->format != VK_FORMAT_B8G8R8A8_UNORM &&
                            img->format != VK_FORMAT_B8G8R8A8_SRGB)
                        {
                            D(("[ogles2_vk] Texture s%u b%u: "
                                               "unsupported format %u, skipping\n",
                                               (unsigned)s, (unsigned)b,
                                               (unsigned)img->format));
                            continue;
                        }

                        /* Validate texture data fits within memory */
                        VkDeviceSize texDataSize = (VkDeviceSize)img->width
                                                   * img->height * bpp;
                        if (img->boundOffset + texDataSize > img->boundMemory->size)
                        {
                            D(("[ogles2_vk] Texture s%u b%u: "
                                               "data overflow (%lu + %lu > %lu)\n",
                                               (unsigned)s, (unsigned)b,
                                               (unsigned long)img->boundOffset,
                                               (unsigned long)texDataSize,
                                               (unsigned long)img->boundMemory->size));
                            continue;
                        }

                        const void *texData = (const uint8_t *)img->boundMemory->data
                                              + img->boundOffset;

                        /* Prevent GL texture leak: check capacity before creating */
                        if (glTextureCount >= OGLES2VK_MAX_BOUND_TEXTURES)
                        {
                            D(("[ogles2_vk] Texture s%u b%u: "
                                               "max texture units reached (%u)\n",
                                               (unsigned)s, (unsigned)b,
                                               (unsigned)OGLES2VK_MAX_BOUND_TEXTURES));
                            continue;
                        }

                        /* Create GL texture */
                        uint32_t glTex = 0;
                        IOGLES2->glGenTextures(1, (uint32 *)&glTex);
                        if (!glTex)
                        {
                            D(("[ogles2_vk] glGenTextures failed "
                                               "for s%u b%u\n",
                                               (unsigned)s, (unsigned)b));
                            continue;
                        }

                        IOGLES2->glActiveTexture(GL_TEXTURE0 + texUnit);
                        IOGLES2->glBindTexture(GL_TEXTURE_2D, glTex);

                        /* Set pixel alignment to 1 for tightly-packed data */
                        IOGLES2->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                        /* Upload texture data */
                        IOGLES2->glTexImage2D(GL_TEXTURE_2D, 0,
                            GL_RGBA,
                            (int32)img->width, (int32)img->height, 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, texData);

                        /* Apply sampler state */
                        IOGLES2->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER, ogles2vk_MapFilter(sam->magFilter));
                        IOGLES2->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER, ogles2vk_MapFilter(sam->minFilter));
                        IOGLES2->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_S, ogles2vk_MapAddressMode(sam->addressModeU));
                        IOGLES2->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_T, ogles2vk_MapAddressMode(sam->addressModeV));

                        /* Set sampler uniform from SPIRV-Cross reflection */
                        if (texUnit < curPipeline->samplerCount &&
                            curPipeline->samplerLocs[texUnit] >= 0)
                        {
                            IOGLES2->glUniform1i(curPipeline->samplerLocs[texUnit],
                                                 (int32)texUnit);
                        }
                        else
                        {
                            D(("[ogles2_vk] Sampler at texUnit %u: "
                                               "no stored location\n", (unsigned)texUnit));
                        }

                        if (glTextureCount < OGLES2VK_MAX_BOUND_TEXTURES)
                            glTextures[glTextureCount++] = glTex;

                        texUnit++;
                    }
                }
            }

            /* Set up vertex buffers -> GL VBOs */
            if (vbDirty && curPipeline && curPipeline->vertexBindingCount > 0)
            {
                /* Disable previously enabled vertex attributes */
                for (uint32_t ea = 0; ea < enabledAttribCount; ea++)
                    IOGLES2->glDisableVertexAttribArray(enabledAttribs[ea]);
                enabledAttribCount = 0;

                /* Delete previously created VBOs */
                if (glVBOCount > 0)
                    IOGLES2->glDeleteBuffers((int32)glVBOCount, (const uint32 *)glVBOs);
                glVBOCount = 0;

                /* Create VBOs and configure vertex attributes */
                for (uint32_t b = 0; b < curPipeline->vertexBindingCount; b++)
                {
                    uint32_t binding = curPipeline->vertexBindings[b].binding;
                    uint32_t stride  = curPipeline->vertexBindings[b].stride;

                    if (binding >= OGLES2VK_MAX_VERTEX_BINDINGS || !boundVBs[binding])
                    {
                        D(("[ogles2_vk] VB binding %u: no buffer bound\n",
                                           (unsigned)binding));
                        continue;
                    }

                    OGLES2VKBuffer *buf = boundVBs[binding];
                    if (!buf->boundMemory || !buf->boundMemory->data)
                    {
                        D(("[ogles2_vk] VB binding %u: buffer has no memory\n",
                                           (unsigned)binding));
                        continue;
                    }

                    VkDeviceSize vbOffset = boundVBOffsets[binding];

                    /* Validate that offsets are within memory allocation */
                    if (buf->boundOffset + vbOffset >= buf->boundMemory->size)
                    {
                        D(("[ogles2_vk] VB binding %u: offset past end of memory\n",
                                           (unsigned)binding));
                        continue;
                    }

                    uint8_t *data = (uint8_t *)buf->boundMemory->data
                                    + buf->boundOffset + vbOffset;
                    VkDeviceSize dataSize = buf->size;
                    if (vbOffset < dataSize)
                        dataSize -= vbOffset;
                    else
                        dataSize = 0;

                    /* Clamp to actual available memory */
                    VkDeviceSize maxAvail = buf->boundMemory->size
                                            - buf->boundOffset - vbOffset;
                    if (dataSize > maxAvail)
                        dataSize = maxAvail;

                    if (dataSize == 0)
                        continue;

                    /* Clamp to int32 max for glBufferData */
                    if (dataSize > 0x7FFFFFFF)
                        dataSize = 0x7FFFFFFF;

                    /* Create and upload GL VBO */
                    uint32_t vbo = 0;
                    IOGLES2->glGenBuffers(1, (uint32 *)&vbo);
                    if (!vbo)
                    {
                        D(("[ogles2_vk] glGenBuffers failed for binding %u\n",
                                           (unsigned)binding));
                        continue;
                    }

                    IOGLES2->glBindBuffer(GL_ARRAY_BUFFER, vbo);
                    IOGLES2->glBufferData(GL_ARRAY_BUFFER, (int32)dataSize,
                                          data, GL_STATIC_DRAW);

                    if (glVBOCount < OGLES2VK_MAX_VERTEX_BINDINGS)
                        glVBOs[glVBOCount++] = vbo;

                    /* Configure vertex attributes that use this binding */
                    for (uint32_t a = 0; a < curPipeline->vertexAttributeCount; a++)
                    {
                        VkVertexInputAttributeDescription *attr =
                            &curPipeline->vertexAttributes[a];
                        if (attr->binding != binding)
                            continue;

                        int32 components;
                        uint32_t glType;
                        uint8_t  glNormalized;
                        if (!ogles2vk_FormatToAttrib(attr->format, &components, &glType, &glNormalized))
                            continue;

                        IOGLES2->glVertexAttribPointer(
                            attr->location, components, glType, glNormalized,
                            (int32)stride, (const void *)(uintptr_t)attr->offset);
                        IOGLES2->glEnableVertexAttribArray(attr->location);

                        if (enabledAttribCount < OGLES2VK_MAX_VERTEX_ATTRIBUTES)
                            enabledAttribs[enabledAttribCount++] = attr->location;
                    }
                }
                vbDirty = 0;
            }

            /* Issue draw call(s) -- loop for instancing */
            {
                uint32_t instCount = isIndexed
                    ? c->drawIndexed.instanceCount
                    : c->draw.instanceCount;
                uint32_t firstInst = isIndexed
                    ? c->drawIndexed.firstInstance
                    : c->draw.firstInstance;
                if (instCount == 0) instCount = 1;

                /* Look up gl_InstanceID uniform for instancing */
                int32 instIdLoc = -1;
                if (instCount > 1 && curPipeline && curPipeline->glProgram)
                    instIdLoc = IOGLES2->glGetUniformLocation(
                        curPipeline->glProgram, "u_InstanceIndex");

                if (isIndexed)
                {
                    /* Create and upload GL index buffer object */
                    if (glIBO)
                    {
                        IOGLES2->glDeleteBuffers(1, (const uint32 *)&glIBO);
                        glIBO = 0;
                    }

                    IOGLES2->glGenBuffers(1, (uint32 *)&glIBO);
                    if (!glIBO)
                    {
                        D(("[ogles2_vk] DrawIndexed: glGenBuffers "
                                           "failed for IBO\n"));
                        break;
                    }

                    IOGLES2->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glIBO);

                    uint8_t *ibDrawData = ibBase
                                          + (VkDeviceSize)firstIndex * idxSize;
                    int32 ibDrawSize = (int32)(idxCount * idxSize);
                    IOGLES2->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibDrawSize,
                                          ibDrawData, GL_STATIC_DRAW);
                }

                for (uint32_t inst = 0; inst < instCount; inst++)
                {
                    /* Set gl_InstanceIndex uniform for instanced draws */
                    if (instIdLoc >= 0)
                        IOGLES2->glUniform1i(instIdLoc, (int32)(firstInst + inst));

                    if (isIndexed)
                    {
                        if (vertexOffset != 0)
                        {
                            IOGLES2->glDrawElementsBaseVertexOES(
                                ogles2vk_MapTopology(curTopology),
                                (int32)idxCount, glIdxType,
                                (const void *)0, vertexOffset);
                        }
                        else
                        {
                            IOGLES2->glDrawElements(
                                ogles2vk_MapTopology(curTopology),
                                (int32)idxCount, glIdxType,
                                (const void *)0);
                        }
                    }
                    else
                    {
                        IOGLES2->glDrawArrays(
                            ogles2vk_MapTopology(curTopology),
                            (int32)c->draw.firstVertex,
                            (int32)c->draw.vertexCount);
                    }
                }
            }
            break;
        }

        case OGLES2VK_CMD_SET_CULL_MODE:
            if (c->setCullMode.cullMode == VK_CULL_MODE_NONE)
                IOGLES2->glDisable(GL_CULL_FACE);
            else
            {
                IOGLES2->glEnable(GL_CULL_FACE);
                if (c->setCullMode.cullMode & VK_CULL_MODE_BACK_BIT)
                    IOGLES2->glCullFace(GL_BACK);
                else
                    IOGLES2->glCullFace(GL_FRONT);
            }
            break;

        case OGLES2VK_CMD_SET_DEPTH_TEST_ENABLE:
            if (c->setDepthTestEnable.enable)
                IOGLES2->glEnable(GL_DEPTH_TEST);
            else
                IOGLES2->glDisable(GL_DEPTH_TEST);
            break;

        case OGLES2VK_CMD_SET_DEPTH_WRITE_ENABLE:
            IOGLES2->glDepthMask(c->setDepthWriteEnable.enable ? GL_TRUE : GL_FALSE);
            break;

        case OGLES2VK_CMD_SET_DEPTH_COMPARE_OP:
            IOGLES2->glDepthFunc(ogles2vk_MapDepthFunc(c->setDepthCompareOp.compareOp));
            break;

        case OGLES2VK_CMD_SET_PRIMITIVE_TOPOLOGY:
            curTopology = c->setPrimitiveTopology.topology;
            break;

        case OGLES2VK_CMD_SET_FRONT_FACE:
            IOGLES2->glFrontFace(
                c->setFrontFace.frontFace == VK_FRONT_FACE_CLOCKWISE ? GL_CW : GL_CCW);
            break;

        case OGLES2VK_CMD_BIND_VERTEX_BUFFERS:
        {
            uint32_t first = c->bindVertexBuffers.firstBinding;
            uint32_t count = c->bindVertexBuffers.bindingCount;

            for (uint32_t b = 0; b < count && (first + b) < OGLES2VK_MAX_VERTEX_BINDINGS; b++)
            {
                boundVBs[first + b] = c->bindVertexBuffers.buffers[b];
                boundVBOffsets[first + b] = c->bindVertexBuffers.offsets[b];
            }
            vbDirty = 1;

            D(("[ogles2_vk] BindVertexBuffers: first=%u count=%u\n",
                               (unsigned)first, (unsigned)count));
            break;
        }

        case OGLES2VK_CMD_BIND_INDEX_BUFFER:
            boundIndexBuffer = c->bindIndexBuffer.buffer;
            boundIndexOffset = c->bindIndexBuffer.offset;
            boundIndexType   = c->bindIndexBuffer.indexType;
            if (boundIndexType != VK_INDEX_TYPE_UINT16 &&
                boundIndexType != VK_INDEX_TYPE_UINT32)
            {
                D(("[ogles2_vk] BindIndexBuffer: unsupported "
                                   "type %u, defaulting to UINT32\n",
                                   (unsigned)boundIndexType));
                boundIndexType = VK_INDEX_TYPE_UINT32;
            }
            D(("[ogles2_vk] BindIndexBuffer: type=%s offset=%lu\n",
                               boundIndexType == VK_INDEX_TYPE_UINT16
                                   ? "UINT16" : "UINT32",
                               (unsigned long)boundIndexOffset));
            break;

        case OGLES2VK_CMD_BIND_DESCRIPTOR_SETS:
        {
            uint32_t first = c->bindDescriptorSets.firstSet;
            uint32_t count = c->bindDescriptorSets.setCount;
            for (uint32_t s = 0; s < count && (first + s) < OGLES2VK_MAX_DESCRIPTOR_SETS; s++)
                boundDescSets[first + s] = (OGLES2VKDescriptorSet *)c->bindDescriptorSets.sets[s];

            D(("[ogles2_vk] BindDescriptorSets: first=%u count=%u\n",
                               (unsigned)first, (unsigned)count));
            break;
        }

        /* ============================================================
        ** Legacy render pass
        ** ============================================================ */

        case OGLES2VK_CMD_BEGIN_RENDER_PASS:
        {
            /* Translate legacy render pass to GL clear.
            ** The command payload stores clear values from the app. Since we
            ** don't have SWVKRenderPass/SWVKFramebuffer structs in the GPU ICD,
            ** we just apply the first clear value as a color clear. */
            if (c->beginRenderPass.clearValueCount > 0)
            {
                const VkClearValue *cv = &c->beginRenderPass.clearValues[0];
                IOGLES2->glClearColor(
                    cv->color.float32[0], cv->color.float32[1],
                    cv->color.float32[2], cv->color.float32[3]);

                uint32_t clearBits = GL_COLOR_BUFFER_BIT;

                /* If second clear value exists, treat as depth */
                if (c->beginRenderPass.clearValueCount > 1)
                {
                    IOGLES2->glClearDepthf(
                        c->beginRenderPass.clearValues[1].depthStencil.depth);
                    clearBits |= GL_DEPTH_BUFFER_BIT;
                }

                IOGLES2->glClear(clearBits);
            }
            break;
        }

        case OGLES2VK_CMD_END_RENDER_PASS:
            IOGLES2->glFinish();
            break;

        case OGLES2VK_CMD_NEXT_SUBPASS:
            /* Single subpass only -- no-op */
            break;

        /* ============================================================
        ** Remaining dynamic state
        ** ============================================================ */

        case OGLES2VK_CMD_SET_STENCIL_TEST_ENABLE:
            if (c->setBoolEnable.enable)
                IOGLES2->glEnable(GL_STENCIL_TEST);
            else
                IOGLES2->glDisable(GL_STENCIL_TEST);
            break;

        case OGLES2VK_CMD_SET_STENCIL_OP:
        {
            uint32_t glFail  = ogles2vk_MapStencilOp(c->setStencilOp.failOp);
            uint32_t glZFail = ogles2vk_MapStencilOp(c->setStencilOp.depthFailOp);
            uint32_t glZPass = ogles2vk_MapStencilOp(c->setStencilOp.passOp);

            if (c->setStencilOp.faceMask == VK_STENCIL_FACE_FRONT_BIT)
                IOGLES2->glStencilOpSeparate(GL_FRONT, glFail, glZFail, glZPass);
            else if (c->setStencilOp.faceMask == VK_STENCIL_FACE_BACK_BIT)
                IOGLES2->glStencilOpSeparate(GL_BACK, glFail, glZFail, glZPass);
            else
                IOGLES2->glStencilOp(glFail, glZFail, glZPass);
            break;
        }

        case OGLES2VK_CMD_SET_RASTERIZER_DISCARD_ENABLE:
            if (c->setBoolEnable.enable)
                IOGLES2->glEnable(GL_RASTERIZER_DISCARD);
            else
                IOGLES2->glDisable(GL_RASTERIZER_DISCARD);
            break;

        case OGLES2VK_CMD_SET_DEPTH_BIAS_ENABLE:
            if (c->setBoolEnable.enable)
                IOGLES2->glEnable(GL_POLYGON_OFFSET_FILL);
            else
                IOGLES2->glDisable(GL_POLYGON_OFFSET_FILL);
            break;

        case OGLES2VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE:
            /* No GL ES 2.0 equivalent -- no-op */
            break;

        case OGLES2VK_CMD_SET_PRIMITIVE_RESTART_ENABLE:
            /* No GL ES 2.0 equivalent -- no-op */
            break;

        /* ============================================================
        ** Transfer commands (host-memory operations)
        ** ============================================================ */

        case OGLES2VK_CMD_COPY_BUFFER:
        {
            OGLES2VKBuffer *src = c->copyBuffer.src;
            OGLES2VKBuffer *dst = c->copyBuffer.dst;
            if (src && src->boundMemory && src->boundMemory->data &&
                dst && dst->boundMemory && dst->boundMemory->data)
            {
                VkDeviceSize srcOff = src->boundOffset + c->copyBuffer.srcOffset;
                VkDeviceSize dstOff = dst->boundOffset + c->copyBuffer.dstOffset;
                VkDeviceSize cpSize = c->copyBuffer.size;

                if (srcOff + cpSize <= src->boundMemory->size &&
                    dstOff + cpSize <= dst->boundMemory->size)
                {
                    memcpy((uint8_t *)dst->boundMemory->data + dstOff,
                           (uint8_t *)src->boundMemory->data + srcOff,
                           (size_t)cpSize);
                }
                else
                {
                    D(("[ogles2_vk] CopyBuffer: out of bounds\n"));
                }
            }
            break;
        }

        case OGLES2VK_CMD_COPY_BUFFER_TO_IMAGE:
        {
            OGLES2VKBuffer *srcBuf = c->copyBufferToImage.srcBuffer;
            OGLES2VKImage  *dstImg = c->copyBufferToImage.dstImage;
            if (srcBuf && srcBuf->boundMemory && srcBuf->boundMemory->data &&
                dstImg && dstImg->boundMemory && dstImg->boundMemory->data)
            {
                uint8_t *bufBase = (uint8_t *)srcBuf->boundMemory->data
                                   + srcBuf->boundOffset
                                   + c->copyBufferToImage.bufferOffset;
                uint8_t *imgBase = (uint8_t *)dstImg->boundMemory->data
                                   + dstImg->boundOffset;

                uint32_t bpp = ogles2vk_FormatBpp(dstImg->format);
                uint32_t imgW = dstImg->width;
                uint32_t extW = c->copyBufferToImage.imageExtent.width;
                uint32_t extH = c->copyBufferToImage.imageExtent.height;
                int32_t  offX = c->copyBufferToImage.imageOffset.x;
                int32_t  offY = c->copyBufferToImage.imageOffset.y;
                uint32_t bufRowLen = c->copyBufferToImage.bufferRowLength;
                if (bufRowLen == 0) bufRowLen = extW;

                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t imgRow = (uint32_t)offY + row;
                    if (imgRow >= dstImg->height) break;

                    uint8_t *bufRow = bufBase + (size_t)row * bufRowLen * bpp;
                    uint8_t *imgDst = imgBase + ((size_t)imgRow * imgW + (uint32_t)offX) * bpp;
                    uint32_t rowBytes = extW * bpp;

                    VkDeviceSize imgEnd = (VkDeviceSize)(imgDst + rowBytes
                        - (uint8_t *)dstImg->boundMemory->data);
                    if (imgEnd <= dstImg->boundMemory->size)
                        memcpy(imgDst, bufRow, rowBytes);
                }
            }
            break;
        }

        case OGLES2VK_CMD_COPY_IMAGE_TO_BUFFER:
        {
            OGLES2VKImage  *srcImg = c->copyImageToBuffer.srcImage;
            OGLES2VKBuffer *dstBuf = c->copyImageToBuffer.dstBuffer;
            if (srcImg && srcImg->boundMemory && srcImg->boundMemory->data &&
                dstBuf && dstBuf->boundMemory && dstBuf->boundMemory->data)
            {
                uint8_t *imgBase = (uint8_t *)srcImg->boundMemory->data
                                   + srcImg->boundOffset;
                uint8_t *bufBase = (uint8_t *)dstBuf->boundMemory->data
                                   + dstBuf->boundOffset
                                   + c->copyImageToBuffer.bufferOffset;

                uint32_t bpp = ogles2vk_FormatBpp(srcImg->format);
                uint32_t imgW = srcImg->width;
                uint32_t extW = c->copyImageToBuffer.imageExtent.width;
                uint32_t extH = c->copyImageToBuffer.imageExtent.height;
                int32_t  offX = c->copyImageToBuffer.imageOffset.x;
                int32_t  offY = c->copyImageToBuffer.imageOffset.y;
                uint32_t bufRowLen = c->copyImageToBuffer.bufferRowLength;
                if (bufRowLen == 0) bufRowLen = extW;

                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t imgRow = (uint32_t)offY + row;
                    if (imgRow >= srcImg->height) break;

                    uint8_t *imgSrc = imgBase + ((size_t)imgRow * imgW + (uint32_t)offX) * bpp;
                    uint8_t *bufRow = bufBase + (size_t)row * bufRowLen * bpp;
                    uint32_t rowBytes = extW * bpp;

                    VkDeviceSize imgEnd = (VkDeviceSize)(imgSrc + rowBytes
                        - (uint8_t *)srcImg->boundMemory->data);
                    if (imgEnd <= srcImg->boundMemory->size)
                        memcpy(bufRow, imgSrc, rowBytes);
                }
            }
            break;
        }

        case OGLES2VK_CMD_COPY_IMAGE:
        {
            OGLES2VKImage *src = c->copyImage.src;
            OGLES2VKImage *dst = c->copyImage.dst;
            if (src && src->boundMemory && src->boundMemory->data &&
                dst && dst->boundMemory && dst->boundMemory->data)
            {
                uint32_t bpp = ogles2vk_FormatBpp(src->format);
                uint32_t extW = c->copyImage.extent.width;
                uint32_t extH = c->copyImage.extent.height;

                for (uint32_t row = 0; row < extH; row++)
                {
                    uint32_t srcRow = (uint32_t)c->copyImage.srcOffset.y + row;
                    uint32_t dstRow = (uint32_t)c->copyImage.dstOffset.y + row;
                    if (srcRow >= src->height || dstRow >= dst->height) break;

                    uint8_t *srcPtr = (uint8_t *)src->boundMemory->data
                        + src->boundOffset
                        + ((size_t)srcRow * src->width + (uint32_t)c->copyImage.srcOffset.x) * bpp;
                    uint8_t *dstPtr = (uint8_t *)dst->boundMemory->data
                        + dst->boundOffset
                        + ((size_t)dstRow * dst->width + (uint32_t)c->copyImage.dstOffset.x) * bpp;

                    memcpy(dstPtr, srcPtr, extW * bpp);
                }
            }
            break;
        }

        case OGLES2VK_CMD_FILL_BUFFER:
        {
            OGLES2VKBuffer *buf = c->fillBuffer.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                VkDeviceSize off = buf->boundOffset + c->fillBuffer.offset;
                VkDeviceSize fillSize = c->fillBuffer.size;

                if (fillSize == VK_WHOLE_SIZE)
                    fillSize = buf->size - c->fillBuffer.offset;
                fillSize &= ~3ULL; /* align to 4 bytes */

                if (off + fillSize <= buf->boundMemory->size)
                {
                    uint32_t *dst = (uint32_t *)((uint8_t *)buf->boundMemory->data + off);
                    uint32_t val = c->fillBuffer.data;
                    size_t count = (size_t)(fillSize / 4);
                    for (size_t j = 0; j < count; j++)
                        dst[j] = val;
                }
                else
                {
                    D(("[ogles2_vk] FillBuffer: out of bounds\n"));
                }
            }
            break;
        }

        case OGLES2VK_CMD_UPDATE_BUFFER:
        {
            OGLES2VKBuffer *buf = c->updateBuffer.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data)
            {
                VkDeviceSize off = buf->boundOffset + c->updateBuffer.offset;
                VkDeviceSize upSize = c->updateBuffer.size;

                if (off + upSize <= buf->boundMemory->size)
                {
                    memcpy((uint8_t *)buf->boundMemory->data + off,
                           c->updateBuffer.data, (size_t)upSize);
                }
                else
                {
                    D(("[ogles2_vk] UpdateBuffer: out of bounds\n"));
                }
            }
            break;
        }

        case OGLES2VK_CMD_PIPELINE_BARRIER:
            /* Barriers are execution ordering hints -- no-op for single-queue */
            break;

        /* 100% API coverage: Dynamic state (no-ops for OGLES2 backend) */
        case OGLES2VK_CMD_SET_LINE_WIDTH:
        case OGLES2VK_CMD_SET_DEPTH_BIAS:
        case OGLES2VK_CMD_SET_BLEND_CONSTANTS:
        case OGLES2VK_CMD_SET_DEPTH_BOUNDS_VAL:
        case OGLES2VK_CMD_SET_STENCIL_COMPARE_MASK:
        case OGLES2VK_CMD_SET_STENCIL_WRITE_MASK:
        case OGLES2VK_CMD_SET_STENCIL_REFERENCE:
            break;

        /* Indirect draw: read params from buffer memory, dispatch GL draw */
        case OGLES2VK_CMD_DRAW_INDIRECT:
        {
            OGLES2VKBuffer *buf = c->drawIndirect.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data && curPipeline)
            {
                VkDeviceSize off = buf->boundOffset + c->drawIndirect.offset;
                uint32_t drawCount = c->drawIndirect.drawCount;
                uint32_t stride = c->drawIndirect.stride;
                if (stride == 0) stride = 16; /* VkDrawIndirectCommand is 16 bytes */

                for (uint32_t d = 0; d < drawCount; d++)
                {
                    if (off + 16 > buf->boundMemory->size) break;
                    const uint32_t *params = (const uint32_t *)
                        ((uint8_t *)buf->boundMemory->data + off);
                    uint32_t vertCount = params[0];
                    uint32_t instCount = params[1];
                    uint32_t firstVert = params[2];
                    /* params[3] = firstInstance (used for gl_InstanceIndex) */
                    uint32_t firstInst = params[3];

                    if (instCount == 0) instCount = 1;

                    int32 instIdLoc = -1;
                    if (instCount > 1 && curPipeline->glProgram)
                        instIdLoc = IOGLES2->glGetUniformLocation(
                            curPipeline->glProgram, "u_InstanceIndex");

                    for (uint32_t inst = 0; inst < instCount; inst++)
                    {
                        if (instIdLoc >= 0)
                            IOGLES2->glUniform1i(instIdLoc, (int32)(firstInst + inst));
                        IOGLES2->glDrawArrays(
                            ogles2vk_MapTopology(curTopology),
                            (int32)firstVert, (int32)vertCount);
                    }
                    off += stride;
                }
            }
            break;
        }

        case OGLES2VK_CMD_DRAW_INDEXED_INDIRECT:
        {
            OGLES2VKBuffer *buf = c->drawIndirect.buffer;
            if (buf && buf->boundMemory && buf->boundMemory->data && curPipeline
                && boundIndexBuffer && boundIndexBuffer->boundMemory)
            {
                VkDeviceSize off = buf->boundOffset + c->drawIndirect.offset;
                uint32_t drawCount = c->drawIndirect.drawCount;
                uint32_t stride = c->drawIndirect.stride;
                if (stride == 0) stride = 20; /* VkDrawIndexedIndirectCommand is 20 bytes */

                for (uint32_t d = 0; d < drawCount; d++)
                {
                    if (off + 20 > buf->boundMemory->size) break;
                    const uint32_t *params = (const uint32_t *)
                        ((uint8_t *)buf->boundMemory->data + off);
                    uint32_t idxCount  = params[0];
                    uint32_t instCount = params[1];
                    uint32_t firstIdx  = params[2];
                    int32_t  vtxOff    = (int32_t)params[3];
                    uint32_t firstInst = params[4];

                    if (instCount == 0) instCount = 1;

                    uint32_t idxSize = (boundIndexType == VK_INDEX_TYPE_UINT16) ? 2 : 4;
                    uint32_t glIdxType = (boundIndexType == VK_INDEX_TYPE_UINT16)
                                         ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

                    uint8_t *ibBase = (uint8_t *)boundIndexBuffer->boundMemory->data
                                      + boundIndexBuffer->boundOffset + boundIndexOffset;
                    uint8_t *ibData = ibBase + (VkDeviceSize)firstIdx * idxSize;
                    int32 ibBytes = (int32)(idxCount * idxSize);

                    /* Create temp IBO */
                    uint32_t tmpIBO = 0;
                    IOGLES2->glGenBuffers(1, (uint32 *)&tmpIBO);
                    if (tmpIBO)
                    {
                        IOGLES2->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tmpIBO);
                        IOGLES2->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibBytes,
                                              ibData, GL_STATIC_DRAW);

                        int32 instIdLoc = -1;
                        if (instCount > 1 && curPipeline->glProgram)
                            instIdLoc = IOGLES2->glGetUniformLocation(
                                curPipeline->glProgram, "u_InstanceIndex");

                        for (uint32_t inst = 0; inst < instCount; inst++)
                        {
                            if (instIdLoc >= 0)
                                IOGLES2->glUniform1i(instIdLoc, (int32)(firstInst + inst));
                            if (vtxOff != 0)
                                IOGLES2->glDrawElementsBaseVertexOES(
                                    ogles2vk_MapTopology(curTopology),
                                    (int32)idxCount, glIdxType,
                                    (const void *)0, vtxOff);
                            else
                                IOGLES2->glDrawElements(
                                    ogles2vk_MapTopology(curTopology),
                                    (int32)idxCount, glIdxType,
                                    (const void *)0);
                        }
                        IOGLES2->glDeleteBuffers(1, (const uint32 *)&tmpIBO);
                    }
                    off += stride;
                }
            }
            break;
        }
        }
    }

    /* Clean up GL resources created during execution */
    for (uint32_t ea = 0; ea < enabledAttribCount; ea++)
        IOGLES2->glDisableVertexAttribArray(enabledAttribs[ea]);

    if (glVBOCount > 0)
    {
        IOGLES2->glDeleteBuffers((int32)glVBOCount, (const uint32 *)glVBOs);
        IOGLES2->glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    /* Clean up GL IBO */
    if (glIBO)
    {
        IOGLES2->glDeleteBuffers(1, (const uint32 *)&glIBO);
        IOGLES2->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    /* Clean up GL textures */
    if (glTextureCount > 0)
    {
        IOGLES2->glDeleteTextures((int32)glTextureCount, (const uint32 *)glTextures);
        IOGLES2->glActiveTexture(GL_TEXTURE0);
        IOGLES2->glBindTexture(GL_TEXTURE_2D, 0);
    }
}
