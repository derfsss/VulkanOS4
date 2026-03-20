#ifndef PROTO_VULKAN_H
#define PROTO_VULKAN_H

/*
** Proto header for vulkan.library
** Ties together the library base, interface, and inline macros.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef VULKAN_VULKAN_H
#include <vulkan/vulkan.h>
#endif

/*
** Library base - vulkan.library
*/
#ifndef __NOLIBBASE__
 extern struct Library * VulkanBase;
#endif

#ifdef __amigaos4__
 #include <interfaces/vulkan.h>
 #ifdef __USE_INLINE__
  #include <inline4/vulkan.h>
 #endif
 #ifndef CLIB_VULKAN_PROTOS_H
  #define CLIB_VULKAN_PROTOS_H 1
 #endif
 #ifndef __NOGLOBALIFACE__
  extern struct VulkanIFace * IVulkan;
 #endif
#endif /* __amigaos4__ */

#endif /* PROTO_VULKAN_H */
