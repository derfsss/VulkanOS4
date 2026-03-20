#ifndef VK_MATH_H
#define VK_MATH_H

/*
** vk_math.h -- Shared matrix math for VulkanOS4 examples
**
** Consolidates duplicate mat4/vec3 functions from multiple examples
** into a single shared header.
**
** All matrices are column-major (matching GLSL convention):
**   m[col * 4 + row]
**
** API design inspired by cglm (https://github.com/recp/cglm)
** by recp, MIT license. Implementation is original code tested
** on AmigaOS 4 (PowerPC big-endian).
**
** Functions:
**   vkm_mat4_identity(m)                    -- set to identity
**   vkm_mat4_multiply(out, a, b)            -- out = a * b
**   vkm_mat4_rotate_x(m, angle)             -- rotation around X
**   vkm_mat4_rotate_y(m, angle)             -- rotation around Y
**   vkm_mat4_perspective(m, fov, asp, n, f) -- perspective projection
**   vkm_mat4_translate(m, x, y, z)          -- translation matrix
*/

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*------------------------------------------------------------------------
** Set m to 4x4 identity matrix
**----------------------------------------------------------------------*/
static inline void vkm_mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/*------------------------------------------------------------------------
** Column-major 4x4 matrix multiply: out = a * b
** Safe when out aliases a or b (uses temp buffer)
**----------------------------------------------------------------------*/
static inline void vkm_mat4_multiply(float *out, const float *a, const float *b)
{
    float tmp[16];
    int c, r, k;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++)
        {
            float sum = 0.0f;
            for (k = 0; k < 4; k++)
                sum += a[k * 4 + r] * b[c * 4 + k];
            tmp[c * 4 + r] = sum;
        }
    memcpy(out, tmp, sizeof(tmp));
}

/*------------------------------------------------------------------------
** Rotation around X axis by angle (radians)
**----------------------------------------------------------------------*/
static inline void vkm_mat4_rotate_x(float *m, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    vkm_mat4_identity(m);
    m[5]  =  c;  m[9]  = -s;
    m[6]  =  s;  m[10] =  c;
}

/*------------------------------------------------------------------------
** Rotation around Y axis by angle (radians)
**----------------------------------------------------------------------*/
static inline void vkm_mat4_rotate_y(float *m, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    vkm_mat4_identity(m);
    m[0]  =  c;  m[8]  = s;
    m[2]  = -s;  m[10] = c;
}

/*------------------------------------------------------------------------
** Perspective projection (OpenGL-style clip space, z: [-1,1])
**   fovY:   field of view in radians (vertical)
**   aspect: width / height
**   nearZ, farZ: near and far clip planes
**----------------------------------------------------------------------*/
static inline void vkm_mat4_perspective(float *m, float fovY, float aspect,
                                  float nearZ, float farZ)
{
    float f = 1.0f / tanf(fovY * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (farZ + nearZ) / (nearZ - farZ);
    m[11] = -1.0f;
    m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
}

/*------------------------------------------------------------------------
** Translation matrix
**----------------------------------------------------------------------*/
static inline void vkm_mat4_translate(float *m, float x, float y, float z)
{
    vkm_mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

#endif /* VK_MATH_H */
