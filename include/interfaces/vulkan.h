#ifndef VULKAN_INTERFACE_DEF_H
#define VULKAN_INTERFACE_DEF_H

/*
** VulkanIFace - AmigaOS 4 interface struct for vulkan.library
**
** Hand-written following idltool output conventions.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_EXEC_H
#include <exec/exec.h>
#endif
#ifndef EXEC_INTERFACES_H
#include <exec/interfaces.h>
#endif
#ifndef VULKAN_VULKAN_H
#include <vulkan/vulkan.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct VulkanIFace
{
    struct InterfaceData Data;

    /* Standard AmigaOS interface methods */
    uint32 APICALL (*Obtain)(struct VulkanIFace *Self);
    uint32 APICALL (*Release)(struct VulkanIFace *Self);
    void   APICALL (*Expunge)(struct VulkanIFace *Self);
    struct Interface * APICALL (*Clone)(struct VulkanIFace *Self);

    /* AmigaOS-specific loader functions */
    uint32   APICALL (*VkAmigaGetLoaderVersion)(struct VulkanIFace *Self);
    VkResult APICALL (*VkAmigaSetICDSearchPath)(struct VulkanIFace *Self, const char *path);
    VkResult APICALL (*VkAmigaSetLayerSearchPath)(struct VulkanIFace *Self, const char *path);

    /* Vulkan 1.0 - Instance */
    VkResult APICALL (*vkCreateInstance)(struct VulkanIFace *Self, const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
    void     APICALL (*vkDestroyInstance)(struct VulkanIFace *Self, VkInstance instance, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkEnumerateInstanceVersion)(struct VulkanIFace *Self, uint32_t *pApiVersion);
    VkResult APICALL (*vkEnumerateInstanceExtensionProperties)(struct VulkanIFace *Self, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
    VkResult APICALL (*vkEnumerateInstanceLayerProperties)(struct VulkanIFace *Self, uint32_t *pPropertyCount, VkLayerProperties *pProperties);
    VkResult APICALL (*vkEnumeratePhysicalDevices)(struct VulkanIFace *Self, VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
    void     APICALL (*vkGetPhysicalDeviceProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
    void     APICALL (*vkGetPhysicalDeviceFeatures)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
    void     APICALL (*vkGetPhysicalDeviceQueueFamilyProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
    void     APICALL (*vkGetPhysicalDeviceMemoryProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
    void     APICALL (*vkGetPhysicalDeviceFormatProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties);
    PFN_vkVoidFunction APICALL (*vkGetInstanceProcAddr)(struct VulkanIFace *Self, VkInstance instance, const char *pName);
    PFN_vkVoidFunction APICALL (*vkGetDeviceProcAddr)(struct VulkanIFace *Self, VkDevice device, const char *pName);

    /* Device extension enumeration */
    VkResult APICALL (*vkEnumerateDeviceExtensionProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);

    /* Vulkan 1.0 - Device */
    VkResult APICALL (*vkCreateDevice)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
    void     APICALL (*vkDestroyDevice)(struct VulkanIFace *Self, VkDevice device, const VkAllocationCallbacks *pAllocator);
    void     APICALL (*vkGetDeviceQueue)(struct VulkanIFace *Self, VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
    VkResult APICALL (*vkDeviceWaitIdle)(struct VulkanIFace *Self, VkDevice device);
    VkResult APICALL (*vkQueueWaitIdle)(struct VulkanIFace *Self, VkQueue queue);

    /* Memory */
    VkResult APICALL (*vkAllocateMemory)(struct VulkanIFace *Self, VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
    void     APICALL (*vkFreeMemory)(struct VulkanIFace *Self, VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkMapMemory)(struct VulkanIFace *Self, VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData);
    void     APICALL (*vkUnmapMemory)(struct VulkanIFace *Self, VkDevice device, VkDeviceMemory memory);

    /* Buffer / Image */
    VkResult APICALL (*vkCreateBuffer)(struct VulkanIFace *Self, VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
    void     APICALL (*vkDestroyBuffer)(struct VulkanIFace *Self, VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
    void     APICALL (*vkGetBufferMemoryRequirements)(struct VulkanIFace *Self, VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
    VkResult APICALL (*vkBindBufferMemory)(struct VulkanIFace *Self, VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    VkResult APICALL (*vkCreateImage)(struct VulkanIFace *Self, VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage);
    void     APICALL (*vkDestroyImage)(struct VulkanIFace *Self, VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
    void     APICALL (*vkGetImageMemoryRequirements)(struct VulkanIFace *Self, VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
    VkResult APICALL (*vkBindImageMemory)(struct VulkanIFace *Self, VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
    VkResult APICALL (*vkCreateImageView)(struct VulkanIFace *Self, VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView);
    void     APICALL (*vkDestroyImageView)(struct VulkanIFace *Self, VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);

    /* Sampler */
    VkResult APICALL (*vkCreateSampler)(struct VulkanIFace *Self, VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
    void     APICALL (*vkDestroySampler)(struct VulkanIFace *Self, VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);

    /* Shader / Pipeline */
    VkResult APICALL (*vkCreateShaderModule)(struct VulkanIFace *Self, VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
    void     APICALL (*vkDestroyShaderModule)(struct VulkanIFace *Self, VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkCreatePipelineLayout)(struct VulkanIFace *Self, VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
    void     APICALL (*vkDestroyPipelineLayout)(struct VulkanIFace *Self, VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkCreateGraphicsPipelines)(struct VulkanIFace *Self, VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
    void     APICALL (*vkDestroyPipeline)(struct VulkanIFace *Self, VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkCreatePipelineCache)(struct VulkanIFace *Self, VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache);
    void     APICALL (*vkDestroyPipelineCache)(struct VulkanIFace *Self, VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator);

    /* Descriptor sets */
    VkResult APICALL (*vkCreateDescriptorSetLayout)(struct VulkanIFace *Self, VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
    void     APICALL (*vkDestroyDescriptorSetLayout)(struct VulkanIFace *Self, VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkCreateDescriptorPool)(struct VulkanIFace *Self, VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
    void     APICALL (*vkDestroyDescriptorPool)(struct VulkanIFace *Self, VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkAllocateDescriptorSets)(struct VulkanIFace *Self, VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
    VkResult APICALL (*vkFreeDescriptorSets)(struct VulkanIFace *Self, VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);
    void     APICALL (*vkUpdateDescriptorSets)(struct VulkanIFace *Self, VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);

    /* Command buffer */
    VkResult APICALL (*vkCreateCommandPool)(struct VulkanIFace *Self, VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
    void     APICALL (*vkDestroyCommandPool)(struct VulkanIFace *Self, VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkAllocateCommandBuffers)(struct VulkanIFace *Self, VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
    void     APICALL (*vkFreeCommandBuffers)(struct VulkanIFace *Self, VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
    VkResult APICALL (*vkBeginCommandBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
    VkResult APICALL (*vkEndCommandBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer);

    /* Command recording */
    void APICALL (*vkCmdBindPipeline)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
    void APICALL (*vkCmdSetViewport)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports);
    void APICALL (*vkCmdSetScissor)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors);
    void APICALL (*vkCmdDraw)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
    void APICALL (*vkCmdBeginRendering)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo);
    void APICALL (*vkCmdEndRendering)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer);
    void APICALL (*vkCmdPushConstants)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues);
    void APICALL (*vkCmdBindVertexBuffers)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
    void APICALL (*vkCmdBindIndexBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
    void APICALL (*vkCmdDrawIndexed)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
    void APICALL (*vkCmdBindDescriptorSets)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);

    /* Synchronisation */
    VkResult APICALL (*vkCreateFence)(struct VulkanIFace *Self, VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
    void     APICALL (*vkDestroyFence)(struct VulkanIFace *Self, VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkWaitForFences)(struct VulkanIFace *Self, VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
    VkResult APICALL (*vkResetFences)(struct VulkanIFace *Self, VkDevice device, uint32_t fenceCount, const VkFence *pFences);
    VkResult APICALL (*vkCreateSemaphore)(struct VulkanIFace *Self, VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore);
    void     APICALL (*vkDestroySemaphore)(struct VulkanIFace *Self, VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkQueueSubmit)(struct VulkanIFace *Self, VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);

    /* WSI - Surface (loader-owned) */
    void     APICALL (*vkDestroySurfaceKHR)(struct VulkanIFace *Self, VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkGetPhysicalDeviceSurfaceSupportKHR)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported);
    VkResult APICALL (*vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
    VkResult APICALL (*vkGetPhysicalDeviceSurfaceFormatsKHR)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats);
    VkResult APICALL (*vkGetPhysicalDeviceSurfacePresentModesKHR)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes);

    /* WSI - Swapchain (ICD-owned) */
    VkResult APICALL (*vkCreateSwapchainKHR)(struct VulkanIFace *Self, VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
    void     APICALL (*vkDestroySwapchainKHR)(struct VulkanIFace *Self, VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkGetSwapchainImagesKHR)(struct VulkanIFace *Self, VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages);
    VkResult APICALL (*vkAcquireNextImageKHR)(struct VulkanIFace *Self, VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);
    VkResult APICALL (*vkQueuePresentKHR)(struct VulkanIFace *Self, VkQueue queue, const VkPresentInfoKHR *pPresentInfo);

    /* WSI - AmigaOS extension */
    VkResult APICALL (*vkCreateAmigaSurfaceAMIGA)(struct VulkanIFace *Self, VkInstance instance, const VkAmigaSurfaceCreateInfoAMIGA *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
    VkBool32 APICALL (*vkGetPhysicalDeviceAmigaPresentationSupportAMIGA)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct Screen *screen);

    /* Memory, command buffer, and pipeline cache management */
    VkResult APICALL (*vkFlushMappedMemoryRanges)(struct VulkanIFace *Self, VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
    VkResult APICALL (*vkInvalidateMappedMemoryRanges)(struct VulkanIFace *Self, VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
    VkResult APICALL (*vkResetCommandBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
    void     APICALL (*vkCmdPipelineBarrier)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void *pImageMemoryBarriers);
    VkResult APICALL (*vkResetCommandPool)(struct VulkanIFace *Self, VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
    void     APICALL (*vkTrimCommandPool)(struct VulkanIFace *Self, VkDevice device, VkCommandPool commandPool, VkFlags flags);
    VkResult APICALL (*vkGetFenceStatus)(struct VulkanIFace *Self, VkDevice device, VkFence fence);
    VkResult APICALL (*vkGetPipelineCacheData)(struct VulkanIFace *Self, VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData);
    VkResult APICALL (*vkMergePipelineCaches)(struct VulkanIFace *Self, VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches);
    VkResult APICALL (*vkResetDescriptorPool)(struct VulkanIFace *Self, VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);

    /* Legacy render pass */
    VkResult APICALL (*vkCreateRenderPass)(struct VulkanIFace *Self, VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
    void     APICALL (*vkDestroyRenderPass)(struct VulkanIFace *Self, VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
    VkResult APICALL (*vkCreateFramebuffer)(struct VulkanIFace *Self, VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
    void     APICALL (*vkDestroyFramebuffer)(struct VulkanIFace *Self, VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
    void     APICALL (*vkCmdBeginRenderPass)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents);
    void     APICALL (*vkCmdEndRenderPass)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer);
    void     APICALL (*vkCmdNextSubpass)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkSubpassContents contents);

    /* Vulkan 1.3 dynamic state */
    void APICALL (*vkCmdSetCullMode)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkCullModeFlags cullMode);
    void APICALL (*vkCmdSetFrontFace)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkFrontFace frontFace);
    void APICALL (*vkCmdSetPrimitiveTopology)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkPrimitiveTopology primitiveTopology);
    void APICALL (*vkCmdSetViewportWithCount)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t viewportCount, const VkViewport *pViewports);
    void APICALL (*vkCmdSetScissorWithCount)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t scissorCount, const VkRect2D *pScissors);
    void APICALL (*vkCmdBindVertexBuffers2)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes, const VkDeviceSize *pStrides);
    void APICALL (*vkCmdSetDepthTestEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 depthTestEnable);
    void APICALL (*vkCmdSetDepthWriteEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 depthWriteEnable);
    void APICALL (*vkCmdSetDepthCompareOp)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkCompareOp depthCompareOp);
    void APICALL (*vkCmdSetDepthBoundsTestEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 depthBoundsTestEnable);
    void APICALL (*vkCmdSetStencilTestEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 stencilTestEnable);
    void APICALL (*vkCmdSetStencilOp)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
    void APICALL (*vkCmdSetRasterizerDiscardEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 rasterizerDiscardEnable);
    void APICALL (*vkCmdSetDepthBiasEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 depthBiasEnable);
    void APICALL (*vkCmdSetPrimitiveRestartEnable)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBool32 primitiveRestartEnable);

    /* Transfer commands */
    void APICALL (*vkCmdCopyBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions);
    void APICALL (*vkCmdCopyBufferToImage)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
    void APICALL (*vkCmdCopyImageToBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions);
    void APICALL (*vkCmdCopyImage)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions);
    void APICALL (*vkCmdFillBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data);
    void APICALL (*vkCmdUpdateBuffer)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData);

    /* Vulkan 1.1/1.2/1.3 wrappers */
    void     APICALL (*vkGetPhysicalDeviceProperties2)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties);
    void     APICALL (*vkGetPhysicalDeviceFeatures2)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures);
    void     APICALL (*vkGetPhysicalDeviceQueueFamilyProperties2)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2 *pQueueFamilyProperties);
    void     APICALL (*vkGetPhysicalDeviceMemoryProperties2)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
    void     APICALL (*vkGetPhysicalDeviceFormatProperties2)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2 *pFormatProperties);
    void     APICALL (*vkGetBufferMemoryRequirements2)(struct VulkanIFace *Self, VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pMemoryRequirements);
    void     APICALL (*vkGetImageMemoryRequirements2)(struct VulkanIFace *Self, VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo, VkMemoryRequirements2 *pMemoryRequirements);
    VkResult APICALL (*vkBindBufferMemory2)(struct VulkanIFace *Self, VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos);
    VkResult APICALL (*vkBindImageMemory2)(struct VulkanIFace *Self, VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos);
    void     APICALL (*vkCmdPipelineBarrier2)(struct VulkanIFace *Self, VkCommandBuffer commandBuffer, const void *pDependencyInfo);
    VkResult APICALL (*vkQueueSubmit2)(struct VulkanIFace *Self, VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence);
    VkResult APICALL (*vkGetPhysicalDeviceImageFormatProperties)(struct VulkanIFace *Self, VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *pImageFormatProperties);
};

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_INTERFACE_DEF_H */
