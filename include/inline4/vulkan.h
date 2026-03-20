#ifndef INLINE4_VULKAN_H
#define INLINE4_VULKAN_H

/*
** Inline macros for vulkan.library "main" interface
** Maps standard Vulkan function names to IVulkan->Method() calls.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef VULKAN_VULKAN_H
#include <vulkan/vulkan.h>
#endif
#include <interfaces/vulkan.h>

/* AmigaOS-specific loader functions */
#define VkAmigaGetLoaderVersion() IVulkan->VkAmigaGetLoaderVersion()
#define VkAmigaSetICDSearchPath(path) IVulkan->VkAmigaSetICDSearchPath((path))
#define VkAmigaSetLayerSearchPath(path) IVulkan->VkAmigaSetLayerSearchPath((path))

/* Instance */
#define vkCreateInstance(pCreateInfo, pAllocator, pInstance) IVulkan->vkCreateInstance((pCreateInfo), (pAllocator), (pInstance))
#define vkDestroyInstance(instance, pAllocator) IVulkan->vkDestroyInstance((instance), (pAllocator))
#define vkEnumerateInstanceVersion(pApiVersion) IVulkan->vkEnumerateInstanceVersion((pApiVersion))
#define vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties) IVulkan->vkEnumerateInstanceExtensionProperties((pLayerName), (pPropertyCount), (pProperties))
#define vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties) IVulkan->vkEnumerateInstanceLayerProperties((pPropertyCount), (pProperties))
#define vkEnumeratePhysicalDevices(instance, pCount, pDevs) IVulkan->vkEnumeratePhysicalDevices((instance), (pCount), (pDevs))
#define vkGetPhysicalDeviceProperties(pd, pProps) IVulkan->vkGetPhysicalDeviceProperties((pd), (pProps))
#define vkGetPhysicalDeviceFeatures(pd, pFeatures) IVulkan->vkGetPhysicalDeviceFeatures((pd), (pFeatures))
#define vkGetPhysicalDeviceQueueFamilyProperties(pd, pCount, pProps) IVulkan->vkGetPhysicalDeviceQueueFamilyProperties((pd), (pCount), (pProps))
#define vkGetPhysicalDeviceMemoryProperties(pd, pProps) IVulkan->vkGetPhysicalDeviceMemoryProperties((pd), (pProps))
#define vkGetPhysicalDeviceFormatProperties(pd, fmt, pProps) IVulkan->vkGetPhysicalDeviceFormatProperties((pd), (fmt), (pProps))
#define vkGetInstanceProcAddr(instance, pName) IVulkan->vkGetInstanceProcAddr((instance), (pName))
#define vkGetDeviceProcAddr(device, pName) IVulkan->vkGetDeviceProcAddr((device), (pName))

/* Device extension enumeration */
#define vkEnumerateDeviceExtensionProperties(pd, pLayer, pCnt, pProps) IVulkan->vkEnumerateDeviceExtensionProperties((pd), (pLayer), (pCnt), (pProps))

/* Device */
#define vkCreateDevice(pd, pCI, pAlloc, pDev) IVulkan->vkCreateDevice((pd), (pCI), (pAlloc), (pDev))
#define vkDestroyDevice(dev, pAlloc) IVulkan->vkDestroyDevice((dev), (pAlloc))
#define vkGetDeviceQueue(dev, qfi, qi, pQ) IVulkan->vkGetDeviceQueue((dev), (qfi), (qi), (pQ))
#define vkDeviceWaitIdle(dev) IVulkan->vkDeviceWaitIdle((dev))
#define vkQueueWaitIdle(q) IVulkan->vkQueueWaitIdle((q))

/* Memory */
#define vkAllocateMemory(dev, pAI, pAlloc, pMem) IVulkan->vkAllocateMemory((dev), (pAI), (pAlloc), (pMem))
#define vkFreeMemory(dev, mem, pAlloc) IVulkan->vkFreeMemory((dev), (mem), (pAlloc))
#define vkMapMemory(dev, mem, off, sz, flags, ppData) IVulkan->vkMapMemory((dev), (mem), (off), (sz), (flags), (ppData))
#define vkUnmapMemory(dev, mem) IVulkan->vkUnmapMemory((dev), (mem))

/* Buffer / Image */
#define vkCreateBuffer(dev, pCI, pAlloc, pBuf) IVulkan->vkCreateBuffer((dev), (pCI), (pAlloc), (pBuf))
#define vkDestroyBuffer(dev, buf, pAlloc) IVulkan->vkDestroyBuffer((dev), (buf), (pAlloc))
#define vkGetBufferMemoryRequirements(dev, buf, pReq) IVulkan->vkGetBufferMemoryRequirements((dev), (buf), (pReq))
#define vkBindBufferMemory(dev, buf, mem, off) IVulkan->vkBindBufferMemory((dev), (buf), (mem), (off))
#define vkCreateImage(dev, pCI, pAlloc, pImg) IVulkan->vkCreateImage((dev), (pCI), (pAlloc), (pImg))
#define vkDestroyImage(dev, img, pAlloc) IVulkan->vkDestroyImage((dev), (img), (pAlloc))
#define vkGetImageMemoryRequirements(dev, img, pReq) IVulkan->vkGetImageMemoryRequirements((dev), (img), (pReq))
#define vkBindImageMemory(dev, img, mem, off) IVulkan->vkBindImageMemory((dev), (img), (mem), (off))
#define vkCreateImageView(dev, pCI, pAlloc, pView) IVulkan->vkCreateImageView((dev), (pCI), (pAlloc), (pView))
#define vkDestroyImageView(dev, iv, pAlloc) IVulkan->vkDestroyImageView((dev), (iv), (pAlloc))

/* Sampler */
#define vkCreateSampler(dev, pCI, pAlloc, pSamp) IVulkan->vkCreateSampler((dev), (pCI), (pAlloc), (pSamp))
#define vkDestroySampler(dev, samp, pAlloc) IVulkan->vkDestroySampler((dev), (samp), (pAlloc))

/* Shader / Pipeline */
#define vkCreateShaderModule(dev, pCI, pAlloc, pSM) IVulkan->vkCreateShaderModule((dev), (pCI), (pAlloc), (pSM))
#define vkDestroyShaderModule(dev, sm, pAlloc) IVulkan->vkDestroyShaderModule((dev), (sm), (pAlloc))
#define vkCreatePipelineLayout(dev, pCI, pAlloc, pPL) IVulkan->vkCreatePipelineLayout((dev), (pCI), (pAlloc), (pPL))
#define vkDestroyPipelineLayout(dev, pl, pAlloc) IVulkan->vkDestroyPipelineLayout((dev), (pl), (pAlloc))
#define vkCreateGraphicsPipelines(dev, cache, cnt, pCIs, pAlloc, pPipes) IVulkan->vkCreateGraphicsPipelines((dev), (cache), (cnt), (pCIs), (pAlloc), (pPipes))
#define vkDestroyPipeline(dev, pipe, pAlloc) IVulkan->vkDestroyPipeline((dev), (pipe), (pAlloc))
#define vkCreatePipelineCache(dev, pCI, pAlloc, pPC) IVulkan->vkCreatePipelineCache((dev), (pCI), (pAlloc), (pPC))
#define vkDestroyPipelineCache(dev, pc, pAlloc) IVulkan->vkDestroyPipelineCache((dev), (pc), (pAlloc))

/* Descriptor sets */
#define vkCreateDescriptorSetLayout(dev, pCI, pAlloc, pSL) IVulkan->vkCreateDescriptorSetLayout((dev), (pCI), (pAlloc), (pSL))
#define vkDestroyDescriptorSetLayout(dev, sl, pAlloc) IVulkan->vkDestroyDescriptorSetLayout((dev), (sl), (pAlloc))
#define vkCreateDescriptorPool(dev, pCI, pAlloc, pDP) IVulkan->vkCreateDescriptorPool((dev), (pCI), (pAlloc), (pDP))
#define vkDestroyDescriptorPool(dev, dp, pAlloc) IVulkan->vkDestroyDescriptorPool((dev), (dp), (pAlloc))
#define vkAllocateDescriptorSets(dev, pAI, pSets) IVulkan->vkAllocateDescriptorSets((dev), (pAI), (pSets))
#define vkFreeDescriptorSets(dev, dp, cnt, pSets) IVulkan->vkFreeDescriptorSets((dev), (dp), (cnt), (pSets))
#define vkUpdateDescriptorSets(dev, writeCnt, pWrites, copyCnt, pCopies) IVulkan->vkUpdateDescriptorSets((dev), (writeCnt), (pWrites), (copyCnt), (pCopies))

/* Command buffer */
#define vkCreateCommandPool(dev, pCI, pAlloc, pCP) IVulkan->vkCreateCommandPool((dev), (pCI), (pAlloc), (pCP))
#define vkDestroyCommandPool(dev, cp, pAlloc) IVulkan->vkDestroyCommandPool((dev), (cp), (pAlloc))
#define vkAllocateCommandBuffers(dev, pAI, pCBs) IVulkan->vkAllocateCommandBuffers((dev), (pAI), (pCBs))
#define vkFreeCommandBuffers(dev, cp, cnt, pCBs) IVulkan->vkFreeCommandBuffers((dev), (cp), (cnt), (pCBs))
#define vkBeginCommandBuffer(cb, pBI) IVulkan->vkBeginCommandBuffer((cb), (pBI))
#define vkEndCommandBuffer(cb) IVulkan->vkEndCommandBuffer((cb))

/* Command recording */
#define vkCmdBindPipeline(cb, bp, pipe) IVulkan->vkCmdBindPipeline((cb), (bp), (pipe))
#define vkCmdSetViewport(cb, first, cnt, pVPs) IVulkan->vkCmdSetViewport((cb), (first), (cnt), (pVPs))
#define vkCmdSetScissor(cb, first, cnt, pScs) IVulkan->vkCmdSetScissor((cb), (first), (cnt), (pScs))
#define vkCmdDraw(cb, vtxCnt, instCnt, firstVtx, firstInst) IVulkan->vkCmdDraw((cb), (vtxCnt), (instCnt), (firstVtx), (firstInst))
#define vkCmdBeginRendering(cb, pRI) IVulkan->vkCmdBeginRendering((cb), (pRI))
#define vkCmdEndRendering(cb) IVulkan->vkCmdEndRendering((cb))
#define vkCmdPushConstants(cb, layout, stageFlags, offset, size, pValues) IVulkan->vkCmdPushConstants((cb), (layout), (stageFlags), (offset), (size), (pValues))
#define vkCmdBindVertexBuffers(cb, firstBinding, bindingCount, pBuffers, pOffsets) IVulkan->vkCmdBindVertexBuffers((cb), (firstBinding), (bindingCount), (pBuffers), (pOffsets))
#define vkCmdBindIndexBuffer(cb, buffer, offset, indexType) IVulkan->vkCmdBindIndexBuffer((cb), (buffer), (offset), (indexType))
#define vkCmdDrawIndexed(cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance) IVulkan->vkCmdDrawIndexed((cb), (indexCount), (instanceCount), (firstIndex), (vertexOffset), (firstInstance))
#define vkCmdBindDescriptorSets(cb, bp, layout, firstSet, setCnt, pSets, dynOffCnt, pDynOffs) IVulkan->vkCmdBindDescriptorSets((cb), (bp), (layout), (firstSet), (setCnt), (pSets), (dynOffCnt), (pDynOffs))

/* Synchronisation */
#define vkCreateFence(dev, pCI, pAlloc, pF) IVulkan->vkCreateFence((dev), (pCI), (pAlloc), (pF))
#define vkDestroyFence(dev, f, pAlloc) IVulkan->vkDestroyFence((dev), (f), (pAlloc))
#define vkWaitForFences(dev, cnt, pFs, waitAll, timeout) IVulkan->vkWaitForFences((dev), (cnt), (pFs), (waitAll), (timeout))
#define vkResetFences(dev, cnt, pFs) IVulkan->vkResetFences((dev), (cnt), (pFs))
#define vkCreateSemaphore(dev, pCI, pAlloc, pS) IVulkan->vkCreateSemaphore((dev), (pCI), (pAlloc), (pS))
#define vkDestroySemaphore(dev, s, pAlloc) IVulkan->vkDestroySemaphore((dev), (s), (pAlloc))
#define vkQueueSubmit(q, cnt, pSubmits, fence) IVulkan->vkQueueSubmit((q), (cnt), (pSubmits), (fence))

/* WSI - Surface */
#define vkDestroySurfaceKHR(inst, surf, pAlloc) IVulkan->vkDestroySurfaceKHR((inst), (surf), (pAlloc))
#define vkGetPhysicalDeviceSurfaceSupportKHR(pd, qfi, surf, pSup) IVulkan->vkGetPhysicalDeviceSurfaceSupportKHR((pd), (qfi), (surf), (pSup))
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, pCaps) IVulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR((pd), (surf), (pCaps))
#define vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, pCnt, pFmts) IVulkan->vkGetPhysicalDeviceSurfaceFormatsKHR((pd), (surf), (pCnt), (pFmts))
#define vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surf, pCnt, pModes) IVulkan->vkGetPhysicalDeviceSurfacePresentModesKHR((pd), (surf), (pCnt), (pModes))

/* WSI - Swapchain */
#define vkCreateSwapchainKHR(dev, pCI, pAlloc, pSC) IVulkan->vkCreateSwapchainKHR((dev), (pCI), (pAlloc), (pSC))
#define vkDestroySwapchainKHR(dev, sc, pAlloc) IVulkan->vkDestroySwapchainKHR((dev), (sc), (pAlloc))
#define vkGetSwapchainImagesKHR(dev, sc, pCnt, pImgs) IVulkan->vkGetSwapchainImagesKHR((dev), (sc), (pCnt), (pImgs))
#define vkAcquireNextImageKHR(dev, sc, timeout, sem, fence, pIdx) IVulkan->vkAcquireNextImageKHR((dev), (sc), (timeout), (sem), (fence), (pIdx))
#define vkQueuePresentKHR(q, pPI) IVulkan->vkQueuePresentKHR((q), (pPI))

/* WSI - AmigaOS extension */
#define vkCreateAmigaSurfaceAMIGA(inst, pCI, pAlloc, pSurf) IVulkan->vkCreateAmigaSurfaceAMIGA((inst), (pCI), (pAlloc), (pSurf))
#define vkGetPhysicalDeviceAmigaPresentationSupportAMIGA(pd, qfi, scr) IVulkan->vkGetPhysicalDeviceAmigaPresentationSupportAMIGA((pd), (qfi), (scr))

/* Memory, command buffer, and pipeline cache management */
#define vkFlushMappedMemoryRanges(dev, cnt, pRanges) IVulkan->vkFlushMappedMemoryRanges((dev), (cnt), (pRanges))
#define vkInvalidateMappedMemoryRanges(dev, cnt, pRanges) IVulkan->vkInvalidateMappedMemoryRanges((dev), (cnt), (pRanges))
#define vkResetCommandBuffer(cb, flags) IVulkan->vkResetCommandBuffer((cb), (flags))
#define vkCmdPipelineBarrier(cb, srcStage, dstStage, dep, memCnt, pMem, bufCnt, pBuf, imgCnt, pImg) IVulkan->vkCmdPipelineBarrier((cb), (srcStage), (dstStage), (dep), (memCnt), (pMem), (bufCnt), (pBuf), (imgCnt), (pImg))
#define vkResetCommandPool(dev, cp, flags) IVulkan->vkResetCommandPool((dev), (cp), (flags))
#define vkTrimCommandPool(dev, cp, flags) IVulkan->vkTrimCommandPool((dev), (cp), (flags))
#define vkGetFenceStatus(dev, fence) IVulkan->vkGetFenceStatus((dev), (fence))
#define vkGetPipelineCacheData(dev, pc, pSize, pData) IVulkan->vkGetPipelineCacheData((dev), (pc), (pSize), (pData))
#define vkMergePipelineCaches(dev, dst, srcCnt, pSrcs) IVulkan->vkMergePipelineCaches((dev), (dst), (srcCnt), (pSrcs))
#define vkResetDescriptorPool(dev, dp, flags) IVulkan->vkResetDescriptorPool((dev), (dp), (flags))

/* Legacy render pass */
#define vkCreateRenderPass(dev, pCI, pAlloc, pRP) IVulkan->vkCreateRenderPass((dev), (pCI), (pAlloc), (pRP))
#define vkDestroyRenderPass(dev, rp, pAlloc) IVulkan->vkDestroyRenderPass((dev), (rp), (pAlloc))
#define vkCreateFramebuffer(dev, pCI, pAlloc, pFB) IVulkan->vkCreateFramebuffer((dev), (pCI), (pAlloc), (pFB))
#define vkDestroyFramebuffer(dev, fb, pAlloc) IVulkan->vkDestroyFramebuffer((dev), (fb), (pAlloc))
#define vkCmdBeginRenderPass(cb, pBI, contents) IVulkan->vkCmdBeginRenderPass((cb), (pBI), (contents))
#define vkCmdEndRenderPass(cb) IVulkan->vkCmdEndRenderPass((cb))
#define vkCmdNextSubpass(cb, contents) IVulkan->vkCmdNextSubpass((cb), (contents))

/* Vulkan 1.3 dynamic state */
#define vkCmdSetCullMode(cb, cullMode) IVulkan->vkCmdSetCullMode((cb), (cullMode))
#define vkCmdSetFrontFace(cb, frontFace) IVulkan->vkCmdSetFrontFace((cb), (frontFace))
#define vkCmdSetPrimitiveTopology(cb, topology) IVulkan->vkCmdSetPrimitiveTopology((cb), (topology))
#define vkCmdSetViewportWithCount(cb, cnt, pVPs) IVulkan->vkCmdSetViewportWithCount((cb), (cnt), (pVPs))
#define vkCmdSetScissorWithCount(cb, cnt, pScs) IVulkan->vkCmdSetScissorWithCount((cb), (cnt), (pScs))
#define vkCmdBindVertexBuffers2(cb, first, cnt, pBufs, pOffs, pSizes, pStrides) IVulkan->vkCmdBindVertexBuffers2((cb), (first), (cnt), (pBufs), (pOffs), (pSizes), (pStrides))
#define vkCmdSetDepthTestEnable(cb, enable) IVulkan->vkCmdSetDepthTestEnable((cb), (enable))
#define vkCmdSetDepthWriteEnable(cb, enable) IVulkan->vkCmdSetDepthWriteEnable((cb), (enable))
#define vkCmdSetDepthCompareOp(cb, compareOp) IVulkan->vkCmdSetDepthCompareOp((cb), (compareOp))
#define vkCmdSetDepthBoundsTestEnable(cb, enable) IVulkan->vkCmdSetDepthBoundsTestEnable((cb), (enable))
#define vkCmdSetStencilTestEnable(cb, enable) IVulkan->vkCmdSetStencilTestEnable((cb), (enable))
#define vkCmdSetStencilOp(cb, faceMask, failOp, passOp, depthFailOp, compareOp) IVulkan->vkCmdSetStencilOp((cb), (faceMask), (failOp), (passOp), (depthFailOp), (compareOp))
#define vkCmdSetRasterizerDiscardEnable(cb, enable) IVulkan->vkCmdSetRasterizerDiscardEnable((cb), (enable))
#define vkCmdSetDepthBiasEnable(cb, enable) IVulkan->vkCmdSetDepthBiasEnable((cb), (enable))
#define vkCmdSetPrimitiveRestartEnable(cb, enable) IVulkan->vkCmdSetPrimitiveRestartEnable((cb), (enable))

/* Transfer commands */
#define vkCmdCopyBuffer(cb, src, dst, cnt, pRegions) IVulkan->vkCmdCopyBuffer((cb), (src), (dst), (cnt), (pRegions))
#define vkCmdCopyBufferToImage(cb, srcBuf, dstImg, layout, cnt, pRegions) IVulkan->vkCmdCopyBufferToImage((cb), (srcBuf), (dstImg), (layout), (cnt), (pRegions))
#define vkCmdCopyImageToBuffer(cb, srcImg, layout, dstBuf, cnt, pRegions) IVulkan->vkCmdCopyImageToBuffer((cb), (srcImg), (layout), (dstBuf), (cnt), (pRegions))
#define vkCmdCopyImage(cb, srcImg, srcLayout, dstImg, dstLayout, cnt, pRegions) IVulkan->vkCmdCopyImage((cb), (srcImg), (srcLayout), (dstImg), (dstLayout), (cnt), (pRegions))
#define vkCmdFillBuffer(cb, buf, off, sz, data) IVulkan->vkCmdFillBuffer((cb), (buf), (off), (sz), (data))
#define vkCmdUpdateBuffer(cb, buf, off, sz, pData) IVulkan->vkCmdUpdateBuffer((cb), (buf), (off), (sz), (pData))

/* Vulkan 1.1/1.2/1.3 wrappers */
#define vkGetPhysicalDeviceProperties2(pd, pProps) IVulkan->vkGetPhysicalDeviceProperties2((pd), (pProps))
#define vkGetPhysicalDeviceFeatures2(pd, pFeatures) IVulkan->vkGetPhysicalDeviceFeatures2((pd), (pFeatures))
#define vkGetPhysicalDeviceQueueFamilyProperties2(pd, pCnt, pProps) IVulkan->vkGetPhysicalDeviceQueueFamilyProperties2((pd), (pCnt), (pProps))
#define vkGetPhysicalDeviceMemoryProperties2(pd, pProps) IVulkan->vkGetPhysicalDeviceMemoryProperties2((pd), (pProps))
#define vkGetPhysicalDeviceFormatProperties2(pd, fmt, pProps) IVulkan->vkGetPhysicalDeviceFormatProperties2((pd), (fmt), (pProps))
#define vkGetBufferMemoryRequirements2(dev, pInfo, pReq) IVulkan->vkGetBufferMemoryRequirements2((dev), (pInfo), (pReq))
#define vkGetImageMemoryRequirements2(dev, pInfo, pReq) IVulkan->vkGetImageMemoryRequirements2((dev), (pInfo), (pReq))
#define vkBindBufferMemory2(dev, cnt, pInfos) IVulkan->vkBindBufferMemory2((dev), (cnt), (pInfos))
#define vkBindImageMemory2(dev, cnt, pInfos) IVulkan->vkBindImageMemory2((dev), (cnt), (pInfos))
#define vkCmdPipelineBarrier2(cb, pDepInfo) IVulkan->vkCmdPipelineBarrier2((cb), (pDepInfo))
#define vkQueueSubmit2(q, cnt, pSubmits, fence) IVulkan->vkQueueSubmit2((q), (cnt), (pSubmits), (fence))
#define vkGetPhysicalDeviceImageFormatProperties(pd, fmt, type, tiling, usage, flags, pProps) IVulkan->vkGetPhysicalDeviceImageFormatProperties((pd), (fmt), (type), (tiling), (usage), (flags), (pProps))

#endif /* INLINE4_VULKAN_H */
