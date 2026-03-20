/*
** vulkan_autoinit.c -- Auto-open stub for vulkan.library
**
** Compiled into libvulkan_loader.a. Applications that link with
** -lvulkan_loader get VulkanBase and IVulkan opened automatically
** before main() and closed after main() returns.
*/

#include <exec/exec.h>
#include <interfaces/exec.h>
#include <proto/exec.h>

/* Forward declarations for the Vulkan interface */
struct VulkanIFace;

struct Library *VulkanBase = NULL;
struct VulkanIFace *IVulkan = NULL;

/*
** Constructor: called by newlib CRT before main()
*/
void __init_vulkan(void) __attribute__((constructor, used));
void __init_vulkan(void)
{
    VulkanBase = IExec->OpenLibrary("vulkan.library", 1);
    if (VulkanBase) {
        IVulkan = (struct VulkanIFace *)
            IExec->GetInterface(VulkanBase, "main", 1, NULL);
    }
}

/*
** Destructor: called by newlib CRT after main() returns
*/
void __exit_vulkan(void) __attribute__((destructor, used));
void __exit_vulkan(void)
{
    if (IVulkan) {
        IExec->DropInterface((struct Interface *)IVulkan);
        IVulkan = NULL;
    }
    if (VulkanBase) {
        IExec->CloseLibrary(VulkanBase);
        VulkanBase = NULL;
    }
}
