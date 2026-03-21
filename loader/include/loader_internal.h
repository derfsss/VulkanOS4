#ifndef LOADER_INTERNAL_H
#define LOADER_INTERNAL_H

/*
** loader_internal.h -- Internal structures for vulkan.library loader
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <interfaces/vulkan.h>

/*------------------------------------------------------------------------
** Version info
**----------------------------------------------------------------------*/
#define LIBNAME     "vulkan.library"
#define LIBVER      1
#define LIBREV      2
#define LIBVSTRING  "vulkan.library 1.2 (21.03.2026)"

/*------------------------------------------------------------------------
** Library base structure
**----------------------------------------------------------------------*/
struct VulkanLibBase
{
    struct Library          lib_Lib;
    BPTR                    lib_SegList;
    struct ExecIFace       *lib_IExec;
    struct SignalSemaphore  lib_Lock;     /* Thread safety for loader state */
};

/*------------------------------------------------------------------------
** ICD state -- one per loaded ICD
**----------------------------------------------------------------------*/
#define MAX_ICDS 4

struct ICDState
{
    struct Library              *library;       /* ICD AmigaOS library */
    struct Interface            *iface;         /* ICD "main" interface */
    PFN_vk_icdGetInstanceProcAddr GetInstanceProcAddr;
    VkInstance                   instance;      /* ICD's VkInstance handle */
    VkPhysicalDevice            *physDevices;   /* Array of physical devices from this ICD */
    uint32_t                     physDevCount;
    char                         libraryPath[256];
    int32_t                      priority;      /* From prefs (lower = higher priority) */
    uint32_t                     disabled;      /* 1 = disabled by prefs */
};

/*------------------------------------------------------------------------
** Loader global state
**----------------------------------------------------------------------*/
struct LoaderState
{
    struct ICDState  icds[MAX_ICDS];
    uint32_t         icdCount;
    uint32_t         activeICD;      /* Index of currently active ICD */
    VkBool32         initialized;
};

/*------------------------------------------------------------------------
** Loader surface (owned by loader, not ICD)
**----------------------------------------------------------------------*/
struct LoaderSurface
{
    struct Screen *screen;
    struct Window *window;
    uint32_t       width;
    uint32_t       height;
};

/*------------------------------------------------------------------------
** Externs -- shared across loader source files
**----------------------------------------------------------------------*/
extern struct ExecIFace    *IExec;
extern struct DOSIFace     *IDOS;
extern struct Library      *DOSBase;
extern struct LoaderState   g_loaderState;

/*------------------------------------------------------------------------
** Loader-implemented Vulkan functions (called through VulkanIFace)
** These use the AmigaOS interface calling convention:
** first parameter is struct VulkanIFace *Self
**----------------------------------------------------------------------*/

/* Instance */
VkResult APICALL Loader_vkCreateInstance(struct VulkanIFace *Self, const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
void     APICALL Loader_vkDestroyInstance(struct VulkanIFace *Self, VkInstance instance, const VkAllocationCallbacks *pAllocator);
VkResult APICALL Loader_vkEnumerateInstanceVersion(struct VulkanIFace *Self, uint32_t *pApiVersion);
VkResult APICALL Loader_vkEnumerateInstanceExtensionProperties(struct VulkanIFace *Self, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
VkResult APICALL Loader_vkEnumerateInstanceLayerProperties(struct VulkanIFace *Self, uint32_t *pPropertyCount, VkLayerProperties *pProperties);
VkResult APICALL Loader_vkEnumeratePhysicalDevices(struct VulkanIFace *Self, VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
PFN_vkVoidFunction APICALL Loader_vkGetInstanceProcAddr(struct VulkanIFace *Self, VkInstance instance, const char *pName);
PFN_vkVoidFunction APICALL Loader_vkGetDeviceProcAddr(struct VulkanIFace *Self, VkDevice device, const char *pName);

/* WSI - loader-owned */
VkResult APICALL Loader_vkCreateAmigaSurfaceAMIGA(struct VulkanIFace *Self, VkInstance instance, const VkAmigaSurfaceCreateInfoAMIGA *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
void     APICALL Loader_vkDestroySurfaceKHR(struct VulkanIFace *Self, VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
VkBool32 APICALL Loader_vkGetPhysicalDeviceAmigaPresentationSupportAMIGA(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct Screen *screen);

/* Loader utility functions */
uint32   APICALL Loader_VkAmigaGetLoaderVersion(struct VulkanIFace *Self);
VkResult APICALL Loader_VkAmigaSetICDSearchPath(struct VulkanIFace *Self, const char *path);
VkResult APICALL Loader_VkAmigaSetLayerSearchPath(struct VulkanIFace *Self, const char *path);

/* ICD discovery */
int  LoaderDiscoverICDs(void);
void LoaderUnloadICDs(void);
int  LoaderPopulateIFace(struct VulkanIFace *iface);

#endif /* LOADER_INTERNAL_H */
