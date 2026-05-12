/*
** 24_proc_addr -- vkGetDeviceProcAddr / vkGetInstanceProcAddr ABI test
**
** Resolves device functions via vkGetDeviceProcAddr and calls each one
** as a raw C-ABI PFN_vk* pointer (no Self in r3). For each test we
** also run the equivalent inline-macro call (IVulkan-> dispatch) and
** compare outputs.
**
** Why this exists:
**   AmigaOS interfaces use APICALL (libcall ABI) -- the dispatch path
**   puts Self in r3 and shifts every visible arg up by one register.
**   The DISPATCH table in each ICD returns APICALL trampolines for
**   the IVulkan-> dispatch path; the RAW table returns standard
**   PFN_vk* pointers for vkGetDeviceProcAddr callers (ImGui's Vulkan
**   backend, vulkaninfo, anything portable). If the two tables drift
**   out of sync -- e.g. someone adds a DISPATCH entry without the
**   matching RAW entry, then vkGetDeviceProcAddr falls back to the
**   trampoline -- the caller gets a wrong-ABI pointer and arguments
**   shift by one slot at every call. This example catches that.
**
** Output format follows the qemu-runner test parser convention so
** dev_cycle.py --test can pick it up:
**   Test N: description ... PASS|FAIL
**   Results: N/M passed
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o 24_proc_addr 24_proc_addr.c -lvulkan_loader -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_test = 0;
static int g_pass = 0;

#define TEST(desc, cond) do { \
    g_test++; \
    if (cond) { printf("Test %d: %s ... PASS\n", g_test, desc); g_pass++; } \
    else      { printf("Test %d: %s ... FAIL\n", g_test, desc); } \
} while (0)

#define SETUP_FAIL(msg) do { \
    printf("SETUP FAIL: %s\n", msg); \
    return 2; \
} while (0)

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("=== 24_proc_addr -- vkGetDeviceProcAddr ABI test ===\n\n");

    if (!IVulkan) SETUP_FAIL("vulkan.library not available");

    /* ------------------------------------------------------------ */
    /* Setup: instance + device via the inline-macro path           */
    /* ------------------------------------------------------------ */
    VkApplicationInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
    ai.pApplicationName = "24_proc_addr";

    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS)
        SETUP_FAIL("vkCreateInstance failed");

    uint32_t pdc = 1;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(instance, &pdc, &physDev) != VK_SUCCESS || pdc < 1)
        SETUP_FAIL("vkEnumeratePhysicalDevices: no devices");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physDev, &dci, NULL, &device) != VK_SUCCESS)
        SETUP_FAIL("vkCreateDevice failed");

    printf("Setup OK: instance=%p, physDev=%p, device=%p\n\n",
           (void*)instance, (void*)physDev, (void*)device);

    /* ------------------------------------------------------------ */
    /* Group 1: Device control (1-arg functions)                    */
    /* ------------------------------------------------------------ */
    printf("--- Group 1: device control ---\n");
    {
        PFN_vkVoidFunction raw = vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
        TEST("vkDeviceWaitIdle resolved", raw != NULL);
        if (raw) {
            PFN_vkDeviceWaitIdle fn = (PFN_vkDeviceWaitIdle)raw;
            TEST("vkDeviceWaitIdle raw call == VK_SUCCESS",
                 fn(device) == VK_SUCCESS);
        } else {
            TEST("vkDeviceWaitIdle raw call == VK_SUCCESS", 0);
        }
        TEST("vkDeviceWaitIdle inline call == VK_SUCCESS (control)",
             vkDeviceWaitIdle(device) == VK_SUCCESS);
    }

    /* ------------------------------------------------------------ */
    /* Group 2: Queue / out-pointer args                            */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 2: out-pointer args ---\n");
    {
        PFN_vkVoidFunction raw = vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
        TEST("vkGetDeviceQueue resolved", raw != NULL);
        VkQueue qRaw = VK_NULL_HANDLE, qInline = VK_NULL_HANDLE;
        if (raw) {
            PFN_vkGetDeviceQueue fn = (PFN_vkGetDeviceQueue)raw;
            fn(device, 0, 0, &qRaw);
        }
        vkGetDeviceQueue(device, 0, 0, &qInline);
        TEST("vkGetDeviceQueue raw produced non-NULL queue",
             qRaw != VK_NULL_HANDLE);
        TEST("vkGetDeviceQueue raw handle == inline handle",
             qRaw == qInline);
    }

    /* ------------------------------------------------------------ */
    /* Group 3: Memory create/destroy                               */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 3: memory ---\n");
    {
        PFN_vkVoidFunction rawAlloc = vkGetDeviceProcAddr(device, "vkAllocateMemory");
        PFN_vkVoidFunction rawFree  = vkGetDeviceProcAddr(device, "vkFreeMemory");
        TEST("vkAllocateMemory resolved", rawAlloc != NULL);
        TEST("vkFreeMemory resolved",     rawFree  != NULL);

        VkMemoryAllocateInfo mai;
        memset(&mai, 0, sizeof(mai));
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = 4096;
        mai.memoryTypeIndex = 0;

        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkResult ar = VK_ERROR_INITIALIZATION_FAILED;
        if (rawAlloc) {
            PFN_vkAllocateMemory fn = (PFN_vkAllocateMemory)rawAlloc;
            ar = fn(device, &mai, NULL, &mem);
        }
        TEST("vkAllocateMemory raw call == VK_SUCCESS", ar == VK_SUCCESS);
        TEST("vkAllocateMemory raw produced non-NULL handle",
             mem != VK_NULL_HANDLE);

        if (rawFree && mem != VK_NULL_HANDLE) {
            PFN_vkFreeMemory fn = (PFN_vkFreeMemory)rawFree;
            fn(device, mem, NULL);
        }
    }

    /* ------------------------------------------------------------ */
    /* Group 4: Command pool                                        */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 4: command pool ---\n");
    {
        PFN_vkVoidFunction raw = vkGetDeviceProcAddr(device, "vkCreateCommandPool");
        TEST("vkCreateCommandPool resolved", raw != NULL);

        VkCommandPoolCreateInfo cpci;
        memset(&cpci, 0, sizeof(cpci));
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkCommandPool poolRaw = VK_NULL_HANDLE;
        VkResult cr = VK_ERROR_INITIALIZATION_FAILED;
        if (raw) {
            PFN_vkCreateCommandPool fn = (PFN_vkCreateCommandPool)raw;
            cr = fn(device, &cpci, NULL, &poolRaw);
        }
        TEST("vkCreateCommandPool raw call == VK_SUCCESS", cr == VK_SUCCESS);
        TEST("vkCreateCommandPool raw produced non-NULL handle",
             poolRaw != VK_NULL_HANDLE);

        if (poolRaw != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, poolRaw, NULL);
    }

    /* ------------------------------------------------------------ */
    /* Group 5: Synchronisation                                     */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 5: synchronisation ---\n");
    {
        PFN_vkVoidFunction raw = vkGetDeviceProcAddr(device, "vkCreateFence");
        TEST("vkCreateFence resolved", raw != NULL);

        VkFenceCreateInfo fci;
        memset(&fci, 0, sizeof(fci));
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fenceRaw = VK_NULL_HANDLE;
        VkResult fr = VK_ERROR_INITIALIZATION_FAILED;
        if (raw) {
            PFN_vkCreateFence fn = (PFN_vkCreateFence)raw;
            fr = fn(device, &fci, NULL, &fenceRaw);
        }
        TEST("vkCreateFence raw call == VK_SUCCESS", fr == VK_SUCCESS);
        TEST("vkCreateFence raw produced non-NULL handle",
             fenceRaw != VK_NULL_HANDLE);

        if (fenceRaw != VK_NULL_HANDLE)
            vkDestroyFence(device, fenceRaw, NULL);
    }

    /* ------------------------------------------------------------ */
    /* Group 6: WSI -- the original bug case                        */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 6: WSI swapchain ---\n");
    {
        struct Screen *screen = LockPubScreen(NULL);
        struct Window *window = NULL;
        if (screen) {
            window = OpenWindowTags(NULL,
                WA_Title,       (Tag)"24_proc_addr",
                WA_Width,       320, WA_Height, 200,
                WA_DragBar,     TRUE, WA_CloseGadget, TRUE,
                WA_Activate,    TRUE, WA_PubScreen, (Tag)screen,
                TAG_DONE);
        }
        if (!window) {
            printf("  WSI group skipped (no window)\n");
            if (screen) UnlockPubScreen(NULL, screen);
            goto wsi_done;
        }

        VkAmigaSurfaceCreateInfoAMIGA sci;
        memset(&sci, 0, sizeof(sci));
        sci.sType = VK_STRUCTURE_TYPE_AMIGA_SURFACE_CREATE_INFO;
        sci.pScreen = screen;
        sci.pWindow = window;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateAmigaSurfaceAMIGA(instance, &sci, NULL, &surface) != VK_SUCCESS) {
            printf("  WSI group skipped (no surface)\n");
            CloseWindow(window);
            UnlockPubScreen(NULL, screen);
            goto wsi_done;
        }

        VkSwapchainCreateInfoKHR swci;
        memset(&swci, 0, sizeof(swci));
        swci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swci.surface          = surface;
        swci.minImageCount    = 2;
        swci.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
        swci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swci.imageExtent.width  = 64;
        swci.imageExtent.height = 64;
        swci.imageArrayLayers = 1;
        swci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swci.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swci.clipped          = VK_TRUE;

        PFN_vkVoidFunction raw = vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
        TEST("vkCreateSwapchainKHR resolved", raw != NULL);

        VkSwapchainKHR scRaw = VK_NULL_HANDLE;
        VkResult rRaw = VK_ERROR_INITIALIZATION_FAILED;
        if (raw) {
            /* This is the exact call site afxgroup flagged on PR #1.
            ** Under the old trampoline-fallback bug, the function
            ** received pCreateInfo==NULL and early-returned
            ** VK_ERROR_INITIALIZATION_FAILED. With the corrected RAW
            ** table this resolves to the raw (non-APICALL)
            ** ogles2vk_CreateSwapchainKHR / swvk_CreateSwapchainKHR
            ** and returns VK_SUCCESS. */
            PFN_vkCreateSwapchainKHR fn = (PFN_vkCreateSwapchainKHR)raw;
            rRaw = fn(device, &swci, NULL, &scRaw);
        }
        TEST("vkCreateSwapchainKHR raw call == VK_SUCCESS",
             rRaw == VK_SUCCESS);
        TEST("vkCreateSwapchainKHR raw produced non-NULL swapchain",
             scRaw != VK_NULL_HANDLE);

        VkSwapchainKHR scInline = VK_NULL_HANDLE;
        VkResult rInline = vkCreateSwapchainKHR(device, &swci, NULL, &scInline);
        TEST("vkCreateSwapchainKHR inline control == VK_SUCCESS",
             rInline == VK_SUCCESS);

        /* Acquire-next-image exercises one more 4-arg device function
        ** with a uint64 in the middle -- a different register-layout
        ** pattern that would also drift if the table fell back to a
        ** trampoline. */
        if (scRaw != VK_NULL_HANDLE) {
            PFN_vkVoidFunction rawAcq =
                vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
            TEST("vkAcquireNextImageKHR resolved", rawAcq != NULL);
            if (rawAcq) {
                PFN_vkAcquireNextImageKHR fn =
                    (PFN_vkAcquireNextImageKHR)rawAcq;
                uint32_t idx = 0xDEAD;
                VkResult ar = fn(device, scRaw, 0, VK_NULL_HANDLE,
                                 VK_NULL_HANDLE, &idx);
                TEST("vkAcquireNextImageKHR raw call returned image index",
                     ar == VK_SUCCESS && idx < 16);
            }
        }

        if (scRaw    != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, scRaw,    NULL);
        if (scInline != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, scInline, NULL);
        if (surface  != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, NULL);

        CloseWindow(window);
        UnlockPubScreen(NULL, screen);
    }
    wsi_done:

    /* ------------------------------------------------------------ */
    /* Group 7: Negative case -- unknown name must return NULL.     */
    /* If this fails the trampoline fallback is still in place.     */
    /* ------------------------------------------------------------ */
    printf("\n--- Group 7: negative case ---\n");
    {
        PFN_vkVoidFunction raw =
            vkGetDeviceProcAddr(device, "vkThisFunctionDoesNotExist");
        TEST("unknown name returns NULL (no trampoline fallback)",
             raw == NULL);
    }

    /* ------------------------------------------------------------ */

    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    printf("\nResults: %d/%d passed\n", g_pass, g_test);
    return (g_pass == g_test) ? 0 : 1;
}
