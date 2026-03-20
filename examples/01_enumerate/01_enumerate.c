/*
** Vulkan Enumerate -- AmigaOS 4
**
** Prints Vulkan instance info, physical device properties, features,
** memory properties, queue families, and supported extensions.
** No window or rendering -- pure CLI information tool.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o enumerate enumerate.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== VulkanOS4 Device Enumeration ===\n\n");

    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available\n");
        return 1;
    }

    /* Instance extensions */
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &instExtCount, NULL);
    printf("Instance extensions: %u\n", instExtCount);

    if (instExtCount > 0)
    {
        VkExtensionProperties exts[8];
        uint32_t count = instExtCount < 8 ? instExtCount : 8;
        vkEnumerateInstanceExtensionProperties(NULL, &count, exts);
        for (uint32_t i = 0; i < count; i++)
            printf("  %s (v%u)\n", exts[i].extensionName, exts[i].specVersion);
    }

    /* Instance version */
    uint32_t apiVersion = 0;
    vkEnumerateInstanceVersion(&apiVersion);
    printf("\nInstance API version: %u.%u.%u\n",
        VK_API_VERSION_MAJOR(apiVersion),
        VK_API_VERSION_MINOR(apiVersion),
        VK_API_VERSION_PATCH(apiVersion));

    /* Create instance */
    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "enumerate";
    appInfo.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceCI;
    memset(&instanceCI, 0, sizeof(instanceCI));
    instanceCI.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instanceCI, NULL, &instance);
    if (result != VK_SUCCESS)
    {
        printf("ERROR: vkCreateInstance failed (%d)\n", (int)result);
        return 1;
    }

    /* Enumerate physical devices */
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    printf("\nPhysical devices: %u\n", deviceCount);

    if (deviceCount == 0)
    {
        printf("No devices found.\n");
        vkDestroyInstance(instance, NULL);
        return 0;
    }

    VkPhysicalDevice devices[4];
    uint32_t devCount = deviceCount < 4 ? deviceCount : 4;
    vkEnumeratePhysicalDevices(instance, &devCount, devices);

    for (uint32_t d = 0; d < devCount; d++)
    {
        printf("\n--- Device %u ---\n", d);

        /* Properties */
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);

        const char *typeStr = "Unknown";
        switch (props.deviceType)
        {
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeStr = "CPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeStr = "Virtual GPU"; break;
            default: break;
        }

        printf("Device name:    %s\n", props.deviceName);
        printf("Device type:    %s\n", typeStr);
        printf("API version:    %u.%u.%u\n",
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion));
        printf("Driver version: %u.%u.%u\n",
            VK_API_VERSION_MAJOR(props.driverVersion),
            VK_API_VERSION_MINOR(props.driverVersion),
            VK_API_VERSION_PATCH(props.driverVersion));
        printf("Vendor ID:      0x%04X\n", props.vendorID);
        printf("Device ID:      0x%04X\n", props.deviceID);

        /* Key limits */
        printf("\nLimits:\n");
        printf("  maxImageDimension2D:    %u\n", props.limits.maxImageDimension2D);
        printf("  maxFramebufferWidth:    %u\n", props.limits.maxFramebufferWidth);
        printf("  maxFramebufferHeight:   %u\n", props.limits.maxFramebufferHeight);
        printf("  maxViewports:           %u\n", props.limits.maxViewports);
        printf("  maxColorAttachments:    %u\n", props.limits.maxColorAttachments);
        printf("  maxBoundDescriptorSets: %u\n", props.limits.maxBoundDescriptorSets);
        printf("  maxPushConstantsSize:   %u\n", props.limits.maxPushConstantsSize);
        printf("  maxVertexInputAttributes: %u\n", props.limits.maxVertexInputAttributes);

        /* Features */
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(devices[d], &features);

        printf("\nFeatures:\n");
        printf("  fullDrawIndexUint32:  %s\n", features.fullDrawIndexUint32 ? "YES" : "no");
        printf("  geometryShader:       %s\n", features.geometryShader ? "YES" : "no");
        printf("  tessellationShader:   %s\n", features.tessellationShader ? "YES" : "no");
        printf("  multiViewport:        %s\n", features.multiViewport ? "YES" : "no");
        printf("  samplerAnisotropy:    %s\n", features.samplerAnisotropy ? "YES" : "no");
        printf("  wideLines:            %s\n", features.wideLines ? "YES" : "no");
        printf("  largePoints:          %s\n", features.largePoints ? "YES" : "no");

        /* Memory properties */
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(devices[d], &memProps);

        printf("\nMemory heaps: %u\n", memProps.memoryHeapCount);
        for (uint32_t h = 0; h < memProps.memoryHeapCount; h++)
        {
            printf("  Heap %u: %llu MB%s\n", h,
                (unsigned long long)(memProps.memoryHeaps[h].size / (1024 * 1024)),
                (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    ? " (device-local)" : "");
        }

        printf("Memory types: %u\n", memProps.memoryTypeCount);
        for (uint32_t t = 0; t < memProps.memoryTypeCount; t++)
        {
            VkMemoryPropertyFlags f = memProps.memoryTypes[t].propertyFlags;
            printf("  Type %u (heap %u):", t, memProps.memoryTypes[t].heapIndex);
            if (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)  printf(" device-local");
            if (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)  printf(" host-visible");
            if (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) printf(" host-coherent");
            printf("\n");
        }

        /* Queue families */
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queueFamilyCount, NULL);

        VkQueueFamilyProperties qfProps[4];
        uint32_t qfCount = queueFamilyCount < 4 ? queueFamilyCount : 4;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qfCount, qfProps);

        printf("\nQueue families: %u\n", queueFamilyCount);
        for (uint32_t q = 0; q < qfCount; q++)
        {
            printf("  Family %u: %u queue(s) -", q, qfProps[q].queueCount);
            if (qfProps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) printf(" Graphics");
            if (qfProps[q].queueFlags & VK_QUEUE_COMPUTE_BIT)  printf(" Compute");
            if (qfProps[q].queueFlags & VK_QUEUE_TRANSFER_BIT) printf(" Transfer");
            printf("\n");
        }

        /* Device extensions */
        uint32_t devExtCount = 0;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &devExtCount, NULL);
        printf("\nDevice extensions: %u\n", devExtCount);

        if (devExtCount > 0)
        {
            VkExtensionProperties devExts[8];
            uint32_t deCount = devExtCount < 8 ? devExtCount : 8;
            vkEnumerateDeviceExtensionProperties(devices[d], NULL, &deCount, devExts);
            for (uint32_t e = 0; e < deCount; e++)
                printf("  %s (v%u)\n", devExts[e].extensionName, devExts[e].specVersion);
        }

        /* Format support (key formats) */
        printf("\nFormat support:\n");
        struct { VkFormat fmt; const char *name; } formats[] = {
            { VK_FORMAT_B8G8R8A8_UNORM,    "B8G8R8A8_UNORM" },
            { VK_FORMAT_R8G8B8A8_UNORM,    "R8G8B8A8_UNORM" },
            { VK_FORMAT_D32_SFLOAT,         "D32_SFLOAT" },
            { VK_FORMAT_D16_UNORM,          "D16_UNORM" },
            { VK_FORMAT_R32G32B32_SFLOAT,   "R32G32B32_SFLOAT" },
            { VK_FORMAT_R32G32B32A32_SFLOAT,"R32G32B32A32_SFLOAT" },
        };
        for (uint32_t f = 0; f < sizeof(formats)/sizeof(formats[0]); f++)
        {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(devices[d], formats[f].fmt, &fp);
            printf("  %-22s linear=0x%04X optimal=0x%04X buffer=0x%04X\n",
                formats[f].name,
                fp.linearTilingFeatures,
                fp.optimalTilingFeatures,
                fp.bufferFeatures);
        }
    }

    printf("\n=== Enumeration complete ===\n");

    vkDestroyInstance(instance, NULL);
    return 0;
}
