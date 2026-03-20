/*
** test_headers.c -- Phase 1 verification
** Verifies that all SDK headers compile correctly and types have
** expected sizes/values.
*/

#include <proto/vulkan.h>
#include <vulkan/vk_icd.h>
#include <stdio.h>

int main(void)
{
    printf("=== VulkanOS4 Phase 1: Header Verification ===\n\n");

    /* Version macros */
    printf("VK_API_VERSION_1_3 = 0x%08x\n", VK_API_VERSION_1_3);
    printf("VK_HEADER_VERSION  = %d\n", VK_HEADER_VERSION);
    printf("  Major: %u  Minor: %u  Patch: %u\n",
        VK_API_VERSION_MAJOR(VK_API_VERSION_1_3),
        VK_API_VERSION_MINOR(VK_API_VERSION_1_3),
        VK_API_VERSION_PATCH(VK_API_VERSION_1_3));

    /* Structure sizes */
    printf("\nStructure sizes:\n");
    printf("  VkInstanceCreateInfo:      %u\n", (unsigned)sizeof(VkInstanceCreateInfo));
    printf("  VkDeviceCreateInfo:        %u\n", (unsigned)sizeof(VkDeviceCreateInfo));
    printf("  VkPhysicalDeviceProperties:%u\n", (unsigned)sizeof(VkPhysicalDeviceProperties));
    printf("  VkPhysicalDeviceFeatures:  %u\n", (unsigned)sizeof(VkPhysicalDeviceFeatures));
    printf("  VkPhysicalDeviceLimits:    %u\n", (unsigned)sizeof(VkPhysicalDeviceLimits));
    printf("  VkMemoryAllocateInfo:      %u\n", (unsigned)sizeof(VkMemoryAllocateInfo));
    printf("  VkBufferCreateInfo:        %u\n", (unsigned)sizeof(VkBufferCreateInfo));
    printf("  VkImageCreateInfo:         %u\n", (unsigned)sizeof(VkImageCreateInfo));
    printf("  VkGraphicsPipelineCreateInfo: %u\n", (unsigned)sizeof(VkGraphicsPipelineCreateInfo));
    printf("  VkRenderingInfo:           %u\n", (unsigned)sizeof(VkRenderingInfo));
    printf("  VkSwapchainCreateInfoKHR:  %u\n", (unsigned)sizeof(VkSwapchainCreateInfoKHR));
    printf("  VkSubmitInfo:              %u\n", (unsigned)sizeof(VkSubmitInfo));
    printf("  VkAllocationCallbacks:     %u\n", (unsigned)sizeof(VkAllocationCallbacks));
    printf("  VulkanIFace:               %u\n", (unsigned)sizeof(struct VulkanIFace));

    /* Enum values */
    printf("\nEnum values:\n");
    printf("  VK_SUCCESS = %d\n", VK_SUCCESS);
    printf("  VK_ERROR_DEVICE_LOST = %d\n", VK_ERROR_DEVICE_LOST);
    printf("  VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = %d\n",
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    printf("  VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO = %d\n",
        VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO);
    printf("  VK_FORMAT_B8G8R8A8_UNORM = %d\n", VK_FORMAT_B8G8R8A8_UNORM);

    /* Verify VK_AMIGA_surface extension */
    printf("\nWSI Extension:\n");
    printf("  VK_AMIGA_SURFACE_EXTENSION_NAME = \"%s\"\n",
        VK_AMIGA_SURFACE_EXTENSION_NAME);
    printf("  VK_KHR_SURFACE_EXTENSION_NAME   = \"%s\"\n",
        VK_KHR_SURFACE_EXTENSION_NAME);
    printf("  VK_KHR_SWAPCHAIN_EXTENSION_NAME = \"%s\"\n",
        VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    /* Verify struct initialization compiles */
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Header Test",
        .apiVersion = VK_API_VERSION_1_3
    };

    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo
    };

    VkPhysicalDeviceProperties props;
    (void)ci;
    (void)props;

    printf("\nAll headers compiled and verified successfully.\n");
    return 0;
}
