/*
** Vulkan Indirect Draw -- AmigaOS 4
**
** Demonstrates indirect-style drawing. The draw parameters
** (vertexCount, instanceCount, firstVertex, firstInstance) are stored
** in a VkBuffer as a VkDrawIndirectCommand struct, then read back via
** vkMapMemory and passed to vkCmdDraw. This validates the indirect
** draw buffer setup pattern.
**
** Renders a coloured triangle with red, green, and blue vertices
** on a dark gray background.
**
** API functions exercised (not covered by other examples):
** - VkDrawIndirectCommand struct stored in buffer + read back
**
** Also exercises:
** - Vertex buffers with position + color attributes
** - Buffer creation / memory allocation / mapping for indirect commands
** - Graphics pipeline with vertex input bindings
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o indirect_draw indirect_draw.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** Embedded SPIR-V bytecode -- vertex shader
**
** GLSL concept:
**   layout(location=0) in vec2 inPos;
**   layout(location=1) in vec3 inColor;
**   layout(location=0) out vec3 fragColor;
**   void main() {
**       gl_Position = vec4(inPos, 0.0, 1.0);
**       fragColor = inColor;
**   }
**
** Reused from depth example (push constant members default to zero
** when not set, giving offsetZ=0 and offsetX=0 -- no displacement).
*/
static const unsigned char vert_spv[] = {
  0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x08, 0x00,
  0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
  0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00,
  0x2b, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00,
  0xc2, 0x01, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50, 0x65, 0x72, 0x56, 0x65,
  0x72, 0x74, 0x65, 0x78, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50,
  0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x06, 0x00, 0x07, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50,
  0x6f, 0x69, 0x6e, 0x74, 0x53, 0x69, 0x7a, 0x65, 0x00, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x07, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x67, 0x6c, 0x5f, 0x43, 0x6c, 0x69, 0x70, 0x44, 0x69, 0x73, 0x74, 0x61,
  0x6e, 0x63, 0x65, 0x00, 0x06, 0x00, 0x07, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x43, 0x75, 0x6c, 0x6c, 0x44,
  0x69, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x00, 0x05, 0x00, 0x03, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00,
  0x12, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x50, 0x6f, 0x73, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x06, 0x00, 0x17, 0x00, 0x00, 0x00, 0x50, 0x75, 0x73, 0x68,
  0x43, 0x6f, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x74, 0x73, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x05, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x6f, 0x66, 0x66, 0x73, 0x65, 0x74, 0x5a, 0x00, 0x06, 0x00, 0x05, 0x00,
  0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x6f, 0x66, 0x66, 0x73,
  0x65, 0x74, 0x58, 0x00, 0x05, 0x00, 0x03, 0x00, 0x19, 0x00, 0x00, 0x00,
  0x70, 0x63, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x29, 0x00, 0x00, 0x00,
  0x66, 0x72, 0x61, 0x67, 0x43, 0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x04, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x43, 0x6f,
  0x6c, 0x6f, 0x72, 0x00, 0x47, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
  0x0b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x03, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x48, 0x00, 0x05, 0x00, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x23, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
  0x29, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x47, 0x00, 0x04, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
  0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x1c, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
  0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x15, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
  0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
  0x08, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
  0x3b, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00,
  0x1a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x1b, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x80, 0x3f, 0x20, 0x00, 0x04, 0x00, 0x25, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x27, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x28, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x27, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x28, 0x00, 0x00, 0x00,
  0x29, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
  0x2a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00,
  0x3b, 0x00, 0x04, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
  0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
  0x13, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x16, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
  0x1b, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
  0x1a, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x1d, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x81, 0x00, 0x05, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
  0x1d, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00, 0x14, 0x00, 0x00, 0x00,
  0x1f, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x1f, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00, 0x1b, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
  0x3d, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
  0x21, 0x00, 0x00, 0x00, 0x50, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00,
  0x24, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
  0x22, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
  0x25, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00, 0x26, 0x00, 0x00, 0x00,
  0x24, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x27, 0x00, 0x00, 0x00,
  0x2c, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
  0x29, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
  0x38, 0x00, 0x01, 0x00
};
static const unsigned int vert_spv_len = 1336;

/*
** Embedded SPIR-V bytecode -- fragment shader
**
** GLSL concept:
**   layout(location=0) in vec3 fragColor;
**   layout(location=0) out vec4 outColor;
**   void main() { outColor = vec4(fragColor, 1.0); }
**
** Identical to triangle/depth fragment shader.
*/
static const unsigned char frag_spv[] = {
  0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x08, 0x00,
  0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
  0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x10, 0x00, 0x03, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
  0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00, 0x6f, 0x75, 0x74, 0x43,
  0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x66, 0x72, 0x61, 0x67, 0x43, 0x6f, 0x6c, 0x6f,
  0x72, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
  0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
  0x09, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
  0x0a, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x20, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x0a, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
  0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00,
  0x06, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
  0x0d, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x50, 0x00, 0x07, 0x00,
  0x07, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
  0x3e, 0x00, 0x03, 0x00, 0x09, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
  0xfd, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00
};
static const unsigned int frag_spv_len = 500;

#define WIN_WIDTH  800
#define WIN_HEIGHT 600

struct Vertex {
    float pos[2];    /* x, y */
    float color[3];  /* r, g, b */
};

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    SetTaskPri(FindTask(NULL), -100);

    VkInstance       instance  = VK_NULL_HANDLE;
    VkDevice         device    = VK_NULL_HANDLE;
    VkSurfaceKHR     surface   = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain = VK_NULL_HANDLE;
    VkShaderModule   vertMod   = VK_NULL_HANDLE;
    VkShaderModule   fragMod   = VK_NULL_HANDLE;
    VkPipelineLayout pipLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline  = VK_NULL_HANDLE;
    VkCommandPool    cmdPool   = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf    = VK_NULL_HANDLE;
    VkFence          fence     = VK_NULL_HANDLE;
    VkBuffer         vtxBuf    = VK_NULL_HANDLE;
    VkDeviceMemory   vtxMem    = VK_NULL_HANDLE;
    VkBuffer         indirectBuf = VK_NULL_HANDLE;
    VkDeviceMemory   indirectMem = VK_NULL_HANDLE;
    struct Screen   *screen    = NULL;
    struct Window   *window    = NULL;
    VkImage         *swapImgs  = NULL;
    VkImageView     *swapViews = NULL;
    uint32_t         imgCount  = 0;
    int exitCode = 0;

    printf("=== Vulkan Indirect Draw for AmigaOS 4 ===\n");
    printf("Draw parameters stored in VkBuffer as VkDrawIndirectCommand,\n");
    printf("read back via vkMapMemory, passed to vkCmdDraw.\n\n");

    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available (IVulkan is NULL)\n");
        return 1;
    }

    /* --- Vertex data: 3 vertices (one triangle) --- */
    struct Vertex verts[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},  /* bottom-left,  red   */
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  /* bottom-right, green */
        {{ 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}},  /* top-center,   blue  */
    };

    /*------------------------------------------------------------------
    ** 1. Create Vulkan instance
    **----------------------------------------------------------------*/
    {
        VkApplicationInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "Indirect Draw";
        ai.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        ai.pEngineName = "VulkanOS4";
        ai.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        ai.apiVersion = VK_API_VERSION_1_3;

        const char *iExts[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_AMIGA_SURFACE_EXTENSION_NAME
        };

        VkInstanceCreateInfo ici;
        memset(&ici, 0, sizeof(ici));
        ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &ai;
        ici.enabledExtensionCount = 2;
        ici.ppEnabledExtensionNames = iExts;

        if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateInstance failed\n");
            return 1;
        }
        printf("Vulkan instance created\n");
    }

    /*------------------------------------------------------------------
    ** 2. Select physical device and create logical device
    **----------------------------------------------------------------*/
    VkPhysicalDevice physDev;
    {
        uint32_t cnt = 1;
        vkEnumeratePhysicalDevices(instance, &cnt, &physDev);
        if (cnt == 0)
        {
            printf("ERROR: No Vulkan physical devices\n");
            exitCode = 1;
            goto cleanup;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDev, &props);
        printf("Device: %s\n", props.deviceName);
    }

    {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci;
        memset(&qci, 0, sizeof(qci));
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        const char *dExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo dci;
        memset(&dci, 0, sizeof(dci));
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = dExts;

        if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateDevice failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Logical device created\n");
    }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    /*------------------------------------------------------------------
    ** 3. Open window and create Vulkan surface
    **----------------------------------------------------------------*/
    screen = LockPubScreen(NULL);
    if (!screen) { printf("ERROR: Cannot lock screen\n"); exitCode = 1; goto cleanup; }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"Vulkan Indirect Draw",
        WA_Width,       WIN_WIDTH,
        WA_Height,      WIN_HEIGHT,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   (Tag)screen,
        TAG_DONE);
    if (!window) { printf("ERROR: Cannot open window\n"); exitCode = 1; goto cleanup; }
    printf("Window opened: %dx%d\n", WIN_WIDTH, WIN_HEIGHT);

    {
        VkAmigaSurfaceCreateInfoAMIGA sci;
        memset(&sci, 0, sizeof(sci));
        sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
        sci.pScreen = screen;
        sci.pWindow = window;

        if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateAmigaSurfaceAMIGA failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Vulkan surface created\n");
    }

    uint32_t innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32_t innerH = window->Height - window->BorderTop - window->BorderBottom;

    /*------------------------------------------------------------------
    ** 4. Create swapchain
    **----------------------------------------------------------------*/
    {
        VkSwapchainCreateInfoKHR swci;
        memset(&swci, 0, sizeof(swci));
        swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swci.surface = surface;
        swci.minImageCount = 2;
        swci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swci.imageExtent.width = innerW;
        swci.imageExtent.height = innerH;
        swci.imageArrayLayers = 1;
        swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &swci, NULL, &swapchain) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateSwapchainKHR failed\n");
            exitCode = 1;
            goto cleanup;
        }

        vkGetSwapchainImagesKHR(device, swapchain, &imgCount, NULL);
        swapImgs = (VkImage *)malloc(imgCount * sizeof(VkImage));
        if (!swapImgs) { exitCode = 1; goto cleanup; }
        vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImgs);

        swapViews = (VkImageView *)malloc(imgCount * sizeof(VkImageView));
        if (!swapViews) { exitCode = 1; goto cleanup; }
        memset(swapViews, 0, imgCount * sizeof(VkImageView));

        for (uint32_t i = 0; i < imgCount; i++)
        {
            VkImageViewCreateInfo vci;
            memset(&vci, 0, sizeof(vci));
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = swapImgs[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = VK_FORMAT_B8G8R8A8_UNORM;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &vci, NULL, &swapViews[i]) != VK_SUCCESS)
            {
                printf("ERROR: vkCreateImageView failed for image %u\n", i);
                exitCode = 1;
                goto cleanup;
            }
        }
        printf("Swapchain created: %ux%u, %u images\n", innerW, innerH, imgCount);
    }

    /*------------------------------------------------------------------
    ** 5. Create shader modules
    **----------------------------------------------------------------*/
    {
        VkShaderModuleCreateInfo smci;
        memset(&smci, 0, sizeof(smci));
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

        smci.codeSize = vert_spv_len;
        smci.pCode = (const uint32_t *)vert_spv;
        if (vkCreateShaderModule(device, &smci, NULL, &vertMod) != VK_SUCCESS)
        {
            printf("ERROR: vertex shader module creation failed\n");
            exitCode = 1;
            goto cleanup;
        }

        smci.codeSize = frag_spv_len;
        smci.pCode = (const uint32_t *)frag_spv;
        if (vkCreateShaderModule(device, &smci, NULL, &fragMod) != VK_SUCCESS)
        {
            printf("ERROR: fragment shader module creation failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Shader modules created\n");
    }

    /*------------------------------------------------------------------
    ** 6. Create pipeline layout (empty -- no push constants needed)
    **----------------------------------------------------------------*/
    {
        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        if (vkCreatePipelineLayout(device, &plci, NULL, &pipLayout) != VK_SUCCESS)
        {
            printf("ERROR: vkCreatePipelineLayout failed\n");
            exitCode = 1;
            goto cleanup;
        }
    }

    /*------------------------------------------------------------------
    ** 7. Create graphics pipeline
    **----------------------------------------------------------------*/
    {
        VkPipelineShaderStageCreateInfo stages[2];
        memset(stages, 0, sizeof(stages));
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        /* Vertex input: binding 0, stride = sizeof(Vertex), two attributes */
        VkVertexInputBindingDescription vibd;
        memset(&vibd, 0, sizeof(vibd));
        vibd.binding = 0;
        vibd.stride = sizeof(struct Vertex);
        vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription viad[2];
        memset(viad, 0, sizeof(viad));
        /* location 0: pos (vec2) at offset 0 */
        viad[0].location = 0;
        viad[0].binding = 0;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = 0;
        /* location 1: color (vec3) at offset 8 */
        viad[1].location = 1;
        viad[1].binding = 0;
        viad[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        viad[1].offset = 2 * sizeof(float); /* after pos[2] */

        VkPipelineVertexInputStateCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vibd;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = viad;

        VkPipelineInputAssemblyStateCreateInfo ia;
        memset(&ia, 0, sizeof(ia));
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vs;
        memset(&vs, 0, sizeof(vs));
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs;
        memset(&rs, 0, sizeof(rs));
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms;
        memset(&ms, 0, sizeof(ms));
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba;
        memset(&cba, 0, sizeof(cba));
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb;
        memset(&cb, 0, sizeof(cb));
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo ds;
        memset(&ds, 0, sizeof(ds));
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dynStates;

        VkFormat colFmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkPipelineRenderingCreateInfo prci;
        memset(&prci, 0, sizeof(prci));
        prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        prci.colorAttachmentCount = 1;
        prci.pColorAttachmentFormats = &colFmt;

        VkGraphicsPipelineCreateInfo gpci;
        memset(&gpci, 0, sizeof(gpci));
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.pNext = &prci;
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vi;
        gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vs;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &ds;
        gpci.layout = pipLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateGraphicsPipelines failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Graphics pipeline created\n");
    }

    /*------------------------------------------------------------------
    ** 8. Create vertex buffer
    **----------------------------------------------------------------*/
    printf("Creating vertex buffer (%u bytes, %u vertices)...\n",
           (unsigned)sizeof(verts), (unsigned)(sizeof(verts) / sizeof(verts[0])));
    {
        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizeof(verts);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        if (vkCreateBuffer(device, &bci, NULL, &vtxBuf) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateBuffer (vertex) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, vtxBuf, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = 0;

        if (vkAllocateMemory(device, &mai, NULL, &vtxMem) != VK_SUCCESS)
        {
            printf("ERROR: vkAllocateMemory (vertex) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        if (vkBindBufferMemory(device, vtxBuf, vtxMem, 0) != VK_SUCCESS)
        {
            printf("ERROR: vkBindBufferMemory (vertex) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        void *mapped = NULL;
        if (vkMapMemory(device, vtxMem, 0, sizeof(verts), 0, &mapped) != VK_SUCCESS)
        {
            printf("ERROR: vkMapMemory (vertex) failed\n");
            exitCode = 1;
            goto cleanup;
        }
        memcpy(mapped, verts, sizeof(verts));
        vkUnmapMemory(device, vtxMem);
        printf("Vertex buffer created and uploaded\n");
    }

    /*------------------------------------------------------------------
    ** 9. Create indirect command buffer (VkDrawIndirectCommand)
    **    This is the key part of this example: the draw parameters
    **    are stored in a GPU-accessible buffer instead of being
    **    passed as function arguments.
    **----------------------------------------------------------------*/
    printf("Creating indirect command buffer...\n");
    {
        VkDrawIndirectCommand indirectCmd;
        indirectCmd.vertexCount   = 3;
        indirectCmd.instanceCount = 1;
        indirectCmd.firstVertex   = 0;
        indirectCmd.firstInstance  = 0;

        VkBufferCreateInfo bci;
        memset(&bci, 0, sizeof(bci));
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizeof(VkDrawIndirectCommand);
        bci.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (vkCreateBuffer(device, &bci, NULL, &indirectBuf) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateBuffer (indirect) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, indirectBuf, &memReq);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = 0;

        if (vkAllocateMemory(device, &mai, NULL, &indirectMem) != VK_SUCCESS)
        {
            printf("ERROR: vkAllocateMemory (indirect) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        if (vkBindBufferMemory(device, indirectBuf, indirectMem, 0) != VK_SUCCESS)
        {
            printf("ERROR: vkBindBufferMemory (indirect) failed\n");
            exitCode = 1;
            goto cleanup;
        }

        void *mapped = NULL;
        if (vkMapMemory(device, indirectMem, 0, sizeof(VkDrawIndirectCommand), 0, &mapped) != VK_SUCCESS)
        {
            printf("ERROR: vkMapMemory (indirect) failed\n");
            exitCode = 1;
            goto cleanup;
        }
        memcpy(mapped, &indirectCmd, sizeof(VkDrawIndirectCommand));
        vkUnmapMemory(device, indirectMem);

        printf("Indirect command buffer created: vertexCount=%u, instanceCount=%u, "
               "firstVertex=%u, firstInstance=%u\n",
               indirectCmd.vertexCount, indirectCmd.instanceCount,
               indirectCmd.firstVertex, indirectCmd.firstInstance);
    }

    /*------------------------------------------------------------------
    ** 10. Create command pool, command buffer, sync objects
    **----------------------------------------------------------------*/
    {
        VkCommandPoolCreateInfo cpci;
        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &cpci, NULL, &cmdPool) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateCommandPool failed\n");
            exitCode = 1;
            goto cleanup;
        }

        VkCommandBufferAllocateInfo cbai;
        memset(&cbai, 0, sizeof(cbai));
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = cmdPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &cbai, &cmdBuf) != VK_SUCCESS)
        {
            printf("ERROR: vkAllocateCommandBuffers failed\n");
            exitCode = 1;
            goto cleanup;
        }

        VkFenceCreateInfo fci;
        memset(&fci, 0, sizeof(fci));
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(device, &fci, NULL, &fence) != VK_SUCCESS)
        {
            printf("ERROR: vkCreateFence failed\n");
            exitCode = 1;
            goto cleanup;
        }
        printf("Command buffer and sync objects created\n");
    }

    /*------------------------------------------------------------------
    ** 11. Render loop
    **----------------------------------------------------------------*/
    printf("Rendering with indirect draw params... close window to exit.\n");
    {
        uint32_t frame = 0;
        BOOL running = TRUE;

        while (running)
        {
            /* Check for close gadget (non-blocking) */
            struct IntuiMessage *msg;
            while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL)
            {
                if (msg->Class == IDCMP_CLOSEWINDOW) running = FALSE;
                ReplyMsg((struct Message *)msg);
            }
            if (!running) break;

            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &fence);

            uint32_t imgIdx = 0;
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                VK_NULL_HANDLE, VK_NULL_HANDLE, &imgIdx);

            /* Record command buffer */
            VkCommandBufferBeginInfo bi;
            memset(&bi, 0, sizeof(bi));
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmdBuf, &bi);

            /* Color attachment -- dark gray background */
            VkRenderingAttachmentInfo ca;
            memset(&ca, 0, sizeof(ca));
            ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            ca.imageView = swapViews[imgIdx];
            ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            ca.clearValue.color.float32[0] = 0.15f;
            ca.clearValue.color.float32[1] = 0.15f;
            ca.clearValue.color.float32[2] = 0.15f;
            ca.clearValue.color.float32[3] = 1.0f;

            VkRenderingInfo ri;
            memset(&ri, 0, sizeof(ri));
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea.extent.width = innerW;
            ri.renderArea.extent.height = innerH;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &ca;

            vkCmdBeginRendering(cmdBuf, &ri);

            /* Bind pipeline */
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            /* Set viewport and scissor */
            VkViewport vp = { 0, 0, (float)innerW, (float)innerH, 0, 1 };
            vkCmdSetViewport(cmdBuf, 0, 1, &vp);
            VkRect2D sc = { {0, 0}, {innerW, innerH} };
            vkCmdSetScissor(cmdBuf, 0, 1, &sc);

            /* Bind vertex buffer */
            VkDeviceSize vtxOffset = 0;
            vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vtxBuf, &vtxOffset);

            /*
            ** Indirect draw pattern: read VkDrawIndirectCommand from
            ** the mapped buffer and pass params to vkCmdDraw.
            ** In a full implementation, vkCmdDrawIndirect would read
            ** these directly from the GPU buffer at draw time.
            */
            {
                void *mapped = NULL;
                VkDrawIndirectCommand drawCmd;
                drawCmd.vertexCount = 3;
                drawCmd.instanceCount = 1;
                drawCmd.firstVertex = 0;
                drawCmd.firstInstance = 0;
                if (vkMapMemory(device, indirectMem, 0,
                    sizeof(VkDrawIndirectCommand), 0, &mapped) == VK_SUCCESS)
                {
                    memcpy(&drawCmd, mapped, sizeof(VkDrawIndirectCommand));
                    vkUnmapMemory(device, indirectMem);
                }
                vkCmdDraw(cmdBuf, drawCmd.vertexCount, drawCmd.instanceCount,
                          drawCmd.firstVertex, drawCmd.firstInstance);
            }

            vkCmdEndRendering(cmdBuf);
            vkEndCommandBuffer(cmdBuf);

            /* Submit */
            VkSubmitInfo si;
            memset(&si, 0, sizeof(si));
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmdBuf;
            vkQueueSubmit(queue, 1, &si, fence);
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

            /* Present */
            VkPresentInfoKHR pi;
            memset(&pi, 0, sizeof(pi));
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &imgIdx;
            vkQueuePresentKHR(queue, &pi);

            frame++;
        }
        printf("Rendered %u frames with indirect draw\n", frame);
    }

    /*------------------------------------------------------------------
    ** 12. Cleanup (reverse order)
    **----------------------------------------------------------------*/
cleanup:
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);

    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if (cmdBuf != VK_NULL_HANDLE) vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    if (cmdPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, cmdPool, NULL);

    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, NULL);
    if (pipLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipLayout, NULL);

    if (fragMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragMod, NULL);
    if (vertMod != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertMod, NULL);

    if (indirectMem != VK_NULL_HANDLE) vkFreeMemory(device, indirectMem, NULL);
    if (indirectBuf != VK_NULL_HANDLE) vkDestroyBuffer(device, indirectBuf, NULL);

    if (vtxMem != VK_NULL_HANDLE) vkFreeMemory(device, vtxMem, NULL);
    if (vtxBuf != VK_NULL_HANDLE) vkDestroyBuffer(device, vtxBuf, NULL);

    if (swapViews)
    {
        for (uint32_t i = 0; i < imgCount; i++)
            if (swapViews[i] != VK_NULL_HANDLE)
                vkDestroyImageView(device, swapViews[i], NULL);
        free(swapViews);
    }
    if (swapImgs) free(swapImgs);

    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, NULL);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, NULL);

    if (window) CloseWindow(window);
    if (screen) UnlockPubScreen(NULL, screen);

    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, NULL);

    printf("Cleanup complete (exit code %d)\n", exitCode);
    return exitCode;
}
