#ifndef VK_PLATFORM_H
#define VK_PLATFORM_H

/*
** Vulkan platform definitions for AmigaOS 4
** Defines types and calling conventions for the Vulkan API
*/

#include <exec/types.h>

/* AmigaOS 4 uses APICALL for all library interface functions.
** For Vulkan API functions called through the interface, we use VKAPI_ATTR
** and VKAPI_CALL which map to nothing (handled by the interface dispatch).
** For function pointers (PFN_vk*), VKAPI_PTR maps to nothing on AmigaOS.
*/
#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR

/* Vulkan requires these exact-width types */
#include <stdint.h>
#include <stddef.h>

#endif /* VK_PLATFORM_H */
