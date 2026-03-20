#ifndef VULKAN_AMIGA_H
#define VULKAN_AMIGA_H

/*
** VK_AMIGA_surface - Vulkan Window System Integration for AmigaOS 4
**
** This extension provides the ability to create VkSurfaceKHR objects
** from AmigaOS Intuition Screens and Windows.
**
** Extension name: VK_AMIGA_surface
** Extension number: 400 (private/unregistered; register with Khronos before release)
** Required extensions: VK_KHR_surface
*/

#include <vulkan/vulkan_core.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_AMIGA_SURFACE_SPEC_VERSION     1
#define VK_AMIGA_SURFACE_EXTENSION_NAME   "VK_AMIGA_surface"

typedef VkFlags VkAmigaSurfaceCreateFlagsAMIGA;

/*
** VkAmigaSurfaceCreateInfoAMIGA
**
** Create a Vulkan surface from an AmigaOS Intuition Window.
** The surface will render into the window's visible area.
**
** If pScreen is provided and pWindow is NULL, a fullscreen surface
** is created that covers the entire screen.
*/
typedef struct VkAmigaSurfaceCreateInfoAMIGA {
    VkStructureType                sType;
    const void*                    pNext;
    VkAmigaSurfaceCreateFlagsAMIGA flags;
    struct Screen*                 pScreen;
    struct Window*                 pWindow;
} VkAmigaSurfaceCreateInfoAMIGA;

typedef VkResult (VKAPI_PTR *PFN_vkCreateAmigaSurfaceAMIGA)(
    VkInstance                             instance,
    const VkAmigaSurfaceCreateInfoAMIGA*   pCreateInfo,
    const VkAllocationCallbacks*           pAllocator,
    VkSurfaceKHR*                          pSurface);

typedef VkBool32 (VKAPI_PTR *PFN_vkGetPhysicalDeviceAmigaPresentationSupportAMIGA)(
    VkPhysicalDevice    physicalDevice,
    uint32_t            queueFamilyIndex,
    struct Screen*      screen);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_AMIGA_H */
