#ifndef OGLES2VK_OGLES2_H
#define OGLES2VK_OGLES2_H

/*
** ogles2vk_ogles2.h -- OGLES2 interface and GL constant definitions
**
** Extracted from ogles2vk_exec.c so other GPU ICD source files
** can call GL methods and use GL constants without duplication.
**
** The OGLES2IFace struct layout MUST match the real OGLES2IFace from the
** AmigaOS 4 SDK header (interfaces/ogles2.h). Method offsets are critical.
*/

#include <exec/types.h>
#include <exec/interfaces.h>

/*------------------------------------------------------------------------
** GL constants (consolidated from ogles2vk_exec.c)
**----------------------------------------------------------------------*/

/* Primitives */
#define GL_POINTS                0x0000
#define GL_LINES                 0x0001
#define GL_LINE_STRIP            0x0003
#define GL_TRIANGLES             0x0004
#define GL_TRIANGLE_STRIP        0x0005
#define GL_TRIANGLE_FAN          0x0006

/* Boolean */
#define GL_FALSE                 0
#define GL_TRUE                  1

/* State enable/disable */
#define GL_DEPTH_TEST            0x0B71
#define GL_CULL_FACE             0x0B44
#define GL_SCISSOR_TEST          0x0C11
#define GL_BLEND                 0x0BE2
#define GL_STENCIL_TEST          0x0B90
#define GL_RASTERIZER_DISCARD    0x8C89
#define GL_POLYGON_OFFSET_FILL   0x8037

/* Face culling */
#define GL_BACK                  0x0405
#define GL_FRONT                 0x0404
#define GL_FRONT_AND_BACK        0x0408
#define GL_CW                    0x0900
#define GL_CCW                   0x0901

/* Depth */
#define GL_LESS                  0x0201

/* Clear bits */
#define GL_COLOR_BUFFER_BIT      0x4000
#define GL_DEPTH_BUFFER_BIT      0x0100

/* Shader types */
#define GL_VERTEX_SHADER         0x8B31
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_INFO_LOG_LENGTH       0x8B84

/* Data types */
#define GL_FLOAT                 0x1406
#define GL_FLOAT_VEC2            0x8B50
#define GL_FLOAT_VEC3            0x8B51
#define GL_FLOAT_VEC4            0x8B52
#define GL_FLOAT_MAT2            0x8B5A
#define GL_FLOAT_MAT3            0x8B5B
#define GL_FLOAT_MAT4            0x8B5C
#define GL_UNSIGNED_SHORT        0x1403
#define GL_UNSIGNED_INT          0x1405
#define GL_UNSIGNED_BYTE         0x1401

/* Uniforms */
#define GL_ACTIVE_UNIFORMS       0x8B86
#define GL_SAMPLER_2D            0x8B5E

/* Buffers */
#define GL_ARRAY_BUFFER          0x8892
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_STATIC_DRAW           0x88E4

/* Textures */
#define GL_TEXTURE_2D            0x0DE1
#define GL_TEXTURE0              0x84C0
#define GL_RGBA                  0x1908
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801
#define GL_TEXTURE_WRAP_S        0x2802
#define GL_TEXTURE_WRAP_T        0x2803
#define GL_NEAREST               0x2600
#define GL_LINEAR                0x2601
#define GL_REPEAT                0x2901
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_MIRRORED_REPEAT       0x8370
#define GL_UNPACK_ALIGNMENT      0x0CF5

/* Stencil ops */
#define GL_KEEP                  0x1E00
#define GL_REPLACE               0x1E01
#define GL_ZERO                  0x0000

/* Shader binary */
#define GL_SHADER_BINARY_FORMAT_SPIR_V 0x9551

#endif /* OGLES2VK_OGLES2_H */
