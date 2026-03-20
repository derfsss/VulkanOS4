#ifndef VK_ICD_H
#define VK_ICD_H

/*
** Vulkan ICD (Installable Client Driver) interface for AmigaOS 4
**
** GPU driver authors implement this interface in their driver library
** (e.g., radeon_vk.library, software_vk.library) and register it via
** a JSON manifest file in DEVS:Vulkan/icd.d/
*/

#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_ICD_VERSION  1

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vk_icdGetInstanceProcAddr)(
    VkInstance instance, const char *pName);

typedef VkResult (VKAPI_PTR *PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(
    uint32_t *pSupportedVersion);

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vk_icdGetPhysicalDeviceProcAddr)(
    VkInstance instance, const char *pName);

#ifdef __cplusplus
}
#endif

#endif /* VK_ICD_H */
