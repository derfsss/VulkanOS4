/*
** vulkaninfo -- Vulkan Device Information Utility for AmigaOS 4
**
** Reports complete information about the Vulkan implementation:
** instance version, extensions, physical device properties, features,
** memory, queue families, device extensions, and format support.
**
** Part of the VulkanOS4 SDK.
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o vulkaninfo vulkaninfo.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <string.h>

/****************************************************************************/
/* Helpers                                                                  */
/****************************************************************************/

static const char *deviceTypeStr(VkPhysicalDeviceType type)
{
    switch (type)
    {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return "Other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "Unknown";
    }
}

static const char *boolStr(VkBool32 v) { return v ? "true" : "false"; }

static void printQueueFlags(VkQueueFlags flags)
{
    if (flags & VK_QUEUE_GRAPHICS_BIT) printf(" GRAPHICS");
    if (flags & VK_QUEUE_COMPUTE_BIT)  printf(" COMPUTE");
    if (flags & VK_QUEUE_TRANSFER_BIT) printf(" TRANSFER");
}

struct FormatEntry { VkFormat fmt; const char *name; };

static const struct FormatEntry g_formats[] = {
    { VK_FORMAT_B8G8R8A8_UNORM,     "B8G8R8A8_UNORM" },
    { VK_FORMAT_R8G8B8A8_UNORM,     "R8G8B8A8_UNORM" },
    { VK_FORMAT_D32_SFLOAT,          "D32_SFLOAT" },
    { VK_FORMAT_D16_UNORM,           "D16_UNORM" },
    { VK_FORMAT_R32_SFLOAT,          "R32_SFLOAT" },
    { VK_FORMAT_R32G32_SFLOAT,       "R32G32_SFLOAT" },
    { VK_FORMAT_R32G32B32_SFLOAT,    "R32G32B32_SFLOAT" },
    { VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT" },
};
#define NUM_FORMATS (sizeof(g_formats) / sizeof(g_formats[0]))

/****************************************************************************/
/* Main                                                                     */
/****************************************************************************/

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("vulkaninfo -- VulkanOS4 SDK\n");
    printf("===========================\n\n");

    if (!IVulkan)
    {
        printf("ERROR: vulkan.library not available.\n");
        printf("Install vulkan.library to LIBS: and try again.\n");
        return 1;
    }

    /* Instance version */
    uint32_t instVer = 0;
    vkEnumerateInstanceVersion(&instVer);
    printf("Vulkan Instance Version: %u.%u.%u\n\n",
        VK_API_VERSION_MAJOR(instVer),
        VK_API_VERSION_MINOR(instVer),
        VK_API_VERSION_PATCH(instVer));

    /* Instance extensions */
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &instExtCount, NULL);
    printf("Instance Extensions (%u):\n", instExtCount);
    if (instExtCount > 0)
    {
        VkExtensionProperties exts[16];
        uint32_t count = instExtCount < 16 ? instExtCount : 16;
        vkEnumerateInstanceExtensionProperties(NULL, &count, exts);
        for (uint32_t i = 0; i < count; i++)
            printf("    %-40s v%u\n", exts[i].extensionName, exts[i].specVersion);
    }
    printf("\n");

    /* Instance layers */
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    printf("Instance Layers: %u\n\n", layerCount);

    /* Create instance */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "vulkaninfo";
    ai.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&ici, NULL, &instance);
    if (result != VK_SUCCESS)
    {
        printf("ERROR: vkCreateInstance failed (VkResult %d)\n", (int)result);
        return 1;
    }

    /* Physical devices */
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, NULL);
    printf("Physical Devices: %u\n", devCount);

    if (devCount == 0)
    {
        printf("    (none found)\n");
        vkDestroyInstance(instance, NULL);
        return 0;
    }

    VkPhysicalDevice devices[8];
    uint32_t count = devCount < 8 ? devCount : 8;
    vkEnumeratePhysicalDevices(instance, &count, devices);

    for (uint32_t d = 0; d < count; d++)
    {
        printf("\n");
        printf("========================================\n");
        printf("Device %u\n", d);
        printf("========================================\n\n");

        /* Properties */
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);

        printf("Properties:\n");
        printf("    deviceName:      %s\n", props.deviceName);
        printf("    deviceType:      %s\n", deviceTypeStr(props.deviceType));
        printf("    apiVersion:      %u.%u.%u\n",
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion));
        printf("    driverVersion:   %u.%u.%u\n",
            VK_API_VERSION_MAJOR(props.driverVersion),
            VK_API_VERSION_MINOR(props.driverVersion),
            VK_API_VERSION_PATCH(props.driverVersion));
        printf("    vendorID:        0x%04X\n", props.vendorID);
        printf("    deviceID:        0x%04X\n", props.deviceID);
        printf("\n");

        /* Limits */
        printf("Limits:\n");
        printf("    maxImageDimension2D:         %u\n", props.limits.maxImageDimension2D);
        printf("    maxImageDimension3D:         %u\n", props.limits.maxImageDimension3D);
        printf("    maxFramebufferWidth:         %u\n", props.limits.maxFramebufferWidth);
        printf("    maxFramebufferHeight:        %u\n", props.limits.maxFramebufferHeight);
        printf("    maxColorAttachments:         %u\n", props.limits.maxColorAttachments);
        printf("    maxViewports:                %u\n", props.limits.maxViewports);
        printf("    maxBoundDescriptorSets:      %u\n", props.limits.maxBoundDescriptorSets);
        printf("    maxPushConstantsSize:        %u\n", props.limits.maxPushConstantsSize);
        printf("    maxVertexInputAttributes:    %u\n", props.limits.maxVertexInputAttributes);
        printf("    maxVertexInputBindings:       %u\n", props.limits.maxVertexInputBindings);
        printf("    maxVertexOutputComponents:   %u\n", props.limits.maxVertexOutputComponents);
        printf("    maxFragmentInputComponents:  %u\n", props.limits.maxFragmentInputComponents);
        printf("    maxComputeWorkGroupSize:     %u x %u x %u\n",
            props.limits.maxComputeWorkGroupSize[0],
            props.limits.maxComputeWorkGroupSize[1],
            props.limits.maxComputeWorkGroupSize[2]);
        printf("    maxComputeSharedMemorySize:  %u\n", props.limits.maxComputeSharedMemorySize);
        printf("    maxMemoryAllocationCount:    %u\n", props.limits.maxMemoryAllocationCount);
        printf("    maxDrawIndexedIndexValue:    %u\n", props.limits.maxDrawIndexedIndexValue);
        printf("\n");

        /* Features */
        VkPhysicalDeviceFeatures feat;
        vkGetPhysicalDeviceFeatures(devices[d], &feat);

        printf("Features:\n");
        printf("    robustBufferAccess:       %s\n", boolStr(feat.robustBufferAccess));
        printf("    fullDrawIndexUint32:      %s\n", boolStr(feat.fullDrawIndexUint32));
        printf("    imageCubeArray:           %s\n", boolStr(feat.imageCubeArray));
        printf("    independentBlend:         %s\n", boolStr(feat.independentBlend));
        printf("    geometryShader:           %s\n", boolStr(feat.geometryShader));
        printf("    tessellationShader:       %s\n", boolStr(feat.tessellationShader));
        printf("    sampleRateShading:        %s\n", boolStr(feat.sampleRateShading));
        printf("    dualSrcBlend:             %s\n", boolStr(feat.dualSrcBlend));
        printf("    logicOp:                  %s\n", boolStr(feat.logicOp));
        printf("    multiDrawIndirect:        %s\n", boolStr(feat.multiDrawIndirect));
        printf("    depthClamp:               %s\n", boolStr(feat.depthClamp));
        printf("    depthBiasClamp:           %s\n", boolStr(feat.depthBiasClamp));
        printf("    fillModeNonSolid:         %s\n", boolStr(feat.fillModeNonSolid));
        printf("    depthBounds:              %s\n", boolStr(feat.depthBounds));
        printf("    wideLines:                %s\n", boolStr(feat.wideLines));
        printf("    largePoints:              %s\n", boolStr(feat.largePoints));
        printf("    multiViewport:            %s\n", boolStr(feat.multiViewport));
        printf("    samplerAnisotropy:        %s\n", boolStr(feat.samplerAnisotropy));
        printf("    textureCompressionBC:     %s\n", boolStr(feat.textureCompressionBC));
        printf("    shaderFloat64:            %s\n", boolStr(feat.shaderFloat64));
        printf("    shaderInt64:              %s\n", boolStr(feat.shaderInt64));
        printf("    shaderInt16:              %s\n", boolStr(feat.shaderInt16));
        printf("\n");

        /* Memory */
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(devices[d], &mem);

        printf("Memory Heaps (%u):\n", mem.memoryHeapCount);
        for (uint32_t h = 0; h < mem.memoryHeapCount; h++)
        {
            printf("    Heap %u: %llu MB", h,
                (unsigned long long)(mem.memoryHeaps[h].size / (1024 * 1024)));
            if (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                printf(" [device-local]");
            printf("\n");
        }
        printf("\n");

        printf("Memory Types (%u):\n", mem.memoryTypeCount);
        for (uint32_t t = 0; t < mem.memoryTypeCount; t++)
        {
            VkMemoryPropertyFlags f = mem.memoryTypes[t].propertyFlags;
            printf("    Type %u (heap %u):", t, mem.memoryTypes[t].heapIndex);
            if (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)  printf(" device-local");
            if (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)  printf(" host-visible");
            if (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) printf(" host-coherent");
            if (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)   printf(" host-cached");
            printf("\n");
        }
        printf("\n");

        /* Queue families */
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qfCount, NULL);

        VkQueueFamilyProperties qfProps[8];
        uint32_t qc = qfCount < 8 ? qfCount : 8;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qc, qfProps);

        printf("Queue Families (%u):\n", qfCount);
        for (uint32_t q = 0; q < qc; q++)
        {
            printf("    Family %u: %u queue(s) --", q, qfProps[q].queueCount);
            printQueueFlags(qfProps[q].queueFlags);
            printf("\n");
        }
        printf("\n");

        /* Device extensions */
        uint32_t dExtCount = 0;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &dExtCount, NULL);
        printf("Device Extensions (%u):\n", dExtCount);
        if (dExtCount > 0)
        {
            VkExtensionProperties dExts[16];
            uint32_t dc = dExtCount < 16 ? dExtCount : 16;
            vkEnumerateDeviceExtensionProperties(devices[d], NULL, &dc, dExts);
            for (uint32_t e = 0; e < dc; e++)
                printf("    %-40s v%u\n", dExts[e].extensionName, dExts[e].specVersion);
        }
        printf("\n");

        /* Format support */
        printf("Format Support:\n");
        printf("    %-24s %10s %10s %10s\n",
            "Format", "Linear", "Optimal", "Buffer");
        printf("    %-24s %10s %10s %10s\n",
            "------", "------", "-------", "------");
        for (uint32_t f = 0; f < NUM_FORMATS; f++)
        {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(devices[d], g_formats[f].fmt, &fp);
            printf("    %-24s 0x%08X 0x%08X 0x%08X\n",
                g_formats[f].name,
                fp.linearTilingFeatures,
                fp.optimalTilingFeatures,
                fp.bufferFeatures);
        }
        printf("\n");
    }

    printf("===========================\n");
    printf("vulkaninfo complete.\n");

    vkDestroyInstance(instance, NULL);
    return 0;
}
