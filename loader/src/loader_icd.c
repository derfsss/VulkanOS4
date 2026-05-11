/*
** vulkan.library -- Vulkan Loader for AmigaOS 4
**
** loader_icd.c -- ICD discovery: scan JSON manifests, load ICD libraries
*/

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <interfaces/exec.h>
#include <interfaces/dos.h>
#include <string.h>
#include <stdio.h>

#include "loader_internal.h"

/****************************************************************************/
/* Minimal JSON value extraction                                            */
/* Only handles the flat ICD manifest format -- not a general JSON parser.  */
/*                                                                          */
/* Manifest format:                                                         */
/* {                                                                        */
/*     "ICD": {                                                             */
/*         "library_path": "LIBS:Vulkan/software_vk.library",              */
/*         "api_version": "1.3.0"                                           */
/*     }                                                                    */
/* }                                                                        */
/****************************************************************************/

/* Find a JSON string value by key. Returns pointer into buf, or NULL. */
static const char *json_find_string(const char *buf, const char *key,
                                    char *out, int outLen)
{
    /* Search for "key" : "value" */
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);

    const char *p = strstr(buf, searchKey);
    if (!p) return NULL;

    /* Skip past the key and find the colon */
    p += strlen(searchKey);
    while (*p && *p != ':') p++;
    if (!*p) return NULL;
    p++; /* skip colon */

    /* Skip whitespace and find opening quote */
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != '"') return NULL;
    p++; /* skip opening quote */

    /* Copy until closing quote */
    int i = 0;
    while (*p && *p != '"' && i < outLen - 1)
    {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return out;
}

/****************************************************************************/
/* Load a single ICD from a JSON manifest file                              */
/****************************************************************************/

static int LoadOneICD(const char *jsonPath)
{
    if (g_loaderState.icdCount >= MAX_ICDS)
        return 0;

    /* Read the JSON file */
    BPTR fh = IDOS->Open(jsonPath, MODE_OLDFILE);
    if (!fh)
    {
        IExec->DebugPrintF("[vulkan.library] Cannot open %s\n", jsonPath);
        return 0;
    }

    char buf[1024];
    int32 len = IDOS->Read(fh, buf, sizeof(buf) - 1);
    IDOS->Close(fh);

    if (len <= 0)
        return 0;

    buf[len] = '\0';

    /* Extract library_path */
    char libPath[256];
    if (!json_find_string(buf, "library_path", libPath, sizeof(libPath)))
    {
        IExec->DebugPrintF("[vulkan.library] No library_path in %s\n", jsonPath);
        return 0;
    }

    IExec->DebugPrintF("[vulkan.library] Found ICD: %s -> %s\n", jsonPath, libPath);

    /* Open the ICD library */
    struct Library *icdLib = IExec->OpenLibrary(libPath, 0);
    if (!icdLib)
    {
        IExec->DebugPrintF("[vulkan.library] Failed to open %s\n", libPath);
        return 0;
    }

    /* Get the ICD's "main" interface */
    struct Interface *icdIFace = IExec->GetInterface(icdLib, "main", 1, NULL);
    if (!icdIFace)
    {
        IExec->DebugPrintF("[vulkan.library] No 'main' interface in %s\n", libPath);
        IExec->CloseLibrary(icdLib);
        return 0;
    }

    /* Resolve vk_icdGetInstanceProcAddr from the interface.
    ** The ICD's "main" interface has:
    **   [0] Obtain, [1] Release, [2] Expunge, [3] Clone,
    **   [4] vk_icdGetInstanceProcAddr,
    **   [5] vk_icdNegotiateLoaderICDInterfaceVersion,
    **   [6] vk_icdGetPhysicalDeviceProcAddr
    **
    ** We access it via the interface's function table.
    ** For simplicity, we use a struct cast matching the ICD's interface layout.
    */
    struct ICDMainIFace {
        struct InterfaceData Data;
        uint32 APICALL (*Obtain)(struct ICDMainIFace *Self);
        uint32 APICALL (*Release)(struct ICDMainIFace *Self);
        void   APICALL (*Expunge)(struct ICDMainIFace *Self);
        struct Interface * APICALL (*Clone)(struct ICDMainIFace *Self);
        PFN_vkVoidFunction APICALL (*vk_icdGetInstanceProcAddr)(struct ICDMainIFace *Self, VkInstance instance, const char *pName);
        VkResult APICALL (*vk_icdNegotiateLoaderICDInterfaceVersion)(struct ICDMainIFace *Self, uint32_t *pSupportedVersion);
        PFN_vkVoidFunction APICALL (*vk_icdGetPhysicalDeviceProcAddr)(struct ICDMainIFace *Self, VkInstance instance, const char *pName);
    };

    struct ICDMainIFace *icdMain = (struct ICDMainIFace *)icdIFace;

    /* Negotiate interface version */
    uint32_t icdVersion = 3;  /* We support up to version 3 */
    if (icdMain->vk_icdNegotiateLoaderICDInterfaceVersion)
    {
        /* APICALL passes Self implicitly -- only pass visible args */
        icdMain->vk_icdNegotiateLoaderICDInterfaceVersion(&icdVersion);
        IExec->DebugPrintF("[vulkan.library] ICD interface version: %lu\n",
                           (unsigned long)icdVersion);
    }

    /* Store the ICD state */
    struct ICDState *state = &g_loaderState.icds[g_loaderState.icdCount];
    state->library = icdLib;
    state->iface   = icdIFace;
    strncpy(state->libraryPath, libPath, sizeof(state->libraryPath) - 1);
    state->libraryPath[sizeof(state->libraryPath) - 1] = '\0';
    state->instance = NULL;
    state->physDevices = NULL;
    state->physDevCount = 0;

    /* For vk_icdGetInstanceProcAddr, we need to call through the AmigaOS
    ** interface (which prepends Self). We store the interface pointer and
    ** create a wrapper function that handles the calling convention.
    ** Currently, we store the raw function pointer and cast it.
    ** The ICD's function implementations will receive Self as the first
    ** argument, which is how AmigaOS interfaces work. */
    state->GetInstanceProcAddr = NULL;  /* Set below after we have a wrapper */

    /* We call through the interface struct directly in
    ** LoaderPopulateIFace rather than through GetInstanceProcAddr.
    ** Store the interface pointer itself. */

    g_loaderState.icdCount++;

    IExec->DebugPrintF("[vulkan.library] ICD loaded: %s (version %lu)\n",
                       libPath, (unsigned long)icdVersion);

    return 1;
}

/****************************************************************************/
/* Extract short ICD name from library path (e.g. "software_vk")           */
/****************************************************************************/

static void ExtractICDName(const char *libPath, char *out, int outLen)
{
    /* Find last '/' or ':' */
    const char *base = libPath;
    for (const char *p = libPath; *p; p++)
    {
        if (*p == '/' || *p == ':')
            base = p + 1;
    }

    /* Copy up to ".library" or end */
    int i = 0;
    while (base[i] && i < outLen - 1)
    {
        if (base[i] == '.' && strncmp(base + i, ".library", 8) == 0)
            break;
        out[i] = base[i];
        i++;
    }
    out[i] = '\0';
}

/****************************************************************************/
/* Read VulkanPrefs prefs file (ENVARC:Vulkan/vulkan.prefs)                */
/* Applies DISABLED= and PRIORITY= settings to loaded ICDs.               */
/****************************************************************************/

static void LoaderApplyPrefs(void)
{
    /* Try ENVARC: first, then ENV: */
    const char *prefsPath = "ENVARC:Vulkan/vulkan.prefs";
    BPTR fh = IDOS->Open(prefsPath, MODE_OLDFILE);
    if (!fh)
    {
        prefsPath = "ENV:Vulkan/vulkan.prefs";
        fh = IDOS->Open(prefsPath, MODE_OLDFILE);
    }

    if (!fh)
        return; /* No prefs file -- use defaults */

    char buf[1024];
    int32 len = IDOS->Read(fh, buf, sizeof(buf) - 1);
    IDOS->Close(fh);

    if (len <= 0)
        return;

    buf[len] = '\0';
    IExec->DebugPrintF("[vulkan.library] Reading prefs: %s\n", prefsPath);

    /* Parse DISABLED= line */
    const char *disLine = strstr(buf, "DISABLED=");
    if (disLine)
    {
        disLine += 9; /* skip "DISABLED=" */
        /* Parse comma-separated ICD names */
        char disNames[512];
        int di = 0;
        while (*disLine && *disLine != '\n' && *disLine != '\r'
               && di < (int)sizeof(disNames) - 1)
            disNames[di++] = *disLine++;
        disNames[di] = '\0';

        /* Check each loaded ICD against the disabled list */
        for (uint32_t i = 0; i < g_loaderState.icdCount; i++)
        {
            char icdName[64];
            ExtractICDName(g_loaderState.icds[i].libraryPath,
                           icdName, sizeof(icdName));

            /* Search for icdName in the comma-separated disabled list */
            const char *p = disNames;
            while (*p)
            {
                /* Skip leading whitespace/commas */
                while (*p == ',' || *p == ' ') p++;
                if (!*p) break;

                /* Extract next name */
                const char *start = p;
                while (*p && *p != ',' && *p != ' ') p++;
                int nameLen = (int)(p - start);

                if (nameLen > 0 && nameLen == (int)strlen(icdName)
                    && strncmp(start, icdName, nameLen) == 0)
                {
                    g_loaderState.icds[i].disabled = 1;
                    IExec->DebugPrintF("[vulkan.library] ICD '%s' DISABLED by prefs\n",
                                       icdName);
                    break;
                }
            }
        }
    }

    /* Parse PRIORITY= line */
    const char *priLine = strstr(buf, "PRIORITY=");
    if (priLine)
    {
        priLine += 9; /* skip "PRIORITY=" */
        char priNames[512];
        int pi = 0;
        while (*priLine && *priLine != '\n' && *priLine != '\r'
               && pi < (int)sizeof(priNames) - 1)
            priNames[pi++] = *priLine++;
        priNames[pi] = '\0';

        /* Assign priority index based on position in the list.
        ** First name gets priority 0 (highest), second gets 1, etc.
        ** ICDs not in the list get a high priority value (100). */
        for (uint32_t i = 0; i < g_loaderState.icdCount; i++)
            g_loaderState.icds[i].priority = 100;

        int priIdx = 0;
        const char *p = priNames;
        while (*p)
        {
            while (*p == ',' || *p == ' ') p++;
            if (!*p) break;

            const char *start = p;
            while (*p && *p != ',' && *p != ' ') p++;
            int nameLen = (int)(p - start);

            for (uint32_t i = 0; i < g_loaderState.icdCount; i++)
            {
                char icdName[64];
                ExtractICDName(g_loaderState.icds[i].libraryPath,
                               icdName, sizeof(icdName));

                if (nameLen > 0 && nameLen == (int)strlen(icdName)
                    && strncmp(start, icdName, nameLen) == 0)
                {
                    g_loaderState.icds[i].priority = priIdx;
                    break;
                }
            }
            priIdx++;
        }
    }
}

/****************************************************************************/
/* Discover ICDs by scanning DEVS:Vulkan/icd.d/                            */
/****************************************************************************/

int LoaderDiscoverICDs(void)
{
    const char *searchDir = "DEVS:Vulkan/icd.d";
    int found = 0;

    IExec->DebugPrintF("[vulkan.library] Scanning %s for ICDs...\n", searchDir);

    /* Check for ENV:VK_ICD_FILENAMES override */
    char envOverride[512];
    if (IDOS->GetVar("VK_ICD_FILENAMES", envOverride, sizeof(envOverride), 0) > 0)
    {
        IExec->DebugPrintF("[vulkan.library] VK_ICD_FILENAMES override: %s\n",
                           envOverride);
        found += LoadOneICD(envOverride);
        return found;
    }

    /* Scan directory for .json files using AmigaOS 4 ExamineDir */
    APTR context = IDOS->ObtainDirContextTags(
        EX_StringNameInput, searchDir,
        EX_DataFields, EXF_NAME | EXF_TYPE,
        EX_MatchString, "#?.json",
        TAG_DONE);

    if (!context)
    {
        IExec->DebugPrintF("[vulkan.library] Cannot scan %s\n", searchDir);
        return 0;
    }

    struct ExamineData *exd;
    while ((exd = IDOS->ExamineDir(context)) != NULL)
    {
        if (EXD_IS_FILE(exd))
        {
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s",
                     searchDir, exd->Name);

            found += LoadOneICD(fullPath);
        }
    }

    IDOS->ReleaseDirContext(context);

    /* Apply VulkanPrefs settings (DISABLED=, PRIORITY=) */
    LoaderApplyPrefs();

    /* Unload disabled ICDs */
    for (uint32_t i = 0; i < g_loaderState.icdCount; )
    {
        if (g_loaderState.icds[i].disabled)
        {
            /* Close and remove this ICD */
            struct ICDState *icd = &g_loaderState.icds[i];
            if (icd->iface)
            {
                IExec->DropInterface(icd->iface);
                icd->iface = NULL;
            }
            if (icd->library)
            {
                IExec->CloseLibrary(icd->library);
                icd->library = NULL;
            }

            /* Shift remaining ICDs down */
            for (uint32_t j = i; j + 1 < g_loaderState.icdCount; j++)
                g_loaderState.icds[j] = g_loaderState.icds[j + 1];
            g_loaderState.icdCount--;
            found--;
        }
        else
        {
            i++;
        }
    }

    /* Sort ICDs by priority (lower value = higher priority).
    ** Default: GPU drivers have priority 100 (from prefs) or get sorted
    ** by the software-last heuristic below. Prefs-assigned priorities
    ** override the default sort. */
    if (g_loaderState.icdCount > 1)
    {
        for (uint32_t i = 0; i < g_loaderState.icdCount - 1; i++)
        {
            for (uint32_t j = i + 1; j < g_loaderState.icdCount; j++)
            {
                int swap = 0;

                if (g_loaderState.icds[i].priority != g_loaderState.icds[j].priority)
                {
                    /* Sort by prefs priority (lower = first) */
                    swap = (g_loaderState.icds[i].priority >
                            g_loaderState.icds[j].priority);
                }
                else
                {
                    /* Same priority: software goes last */
                    int iIsSoftware = (strstr(g_loaderState.icds[i].libraryPath,
                                              "software") != NULL);
                    int jIsSoftware = (strstr(g_loaderState.icds[j].libraryPath,
                                              "software") != NULL);
                    swap = (iIsSoftware && !jIsSoftware);
                }

                if (swap)
                {
                    struct ICDState tmp = g_loaderState.icds[i];
                    g_loaderState.icds[i] = g_loaderState.icds[j];
                    g_loaderState.icds[j] = tmp;
                }
            }
        }
    }

    /* RawDoFmt-style: use %ld instead of %d for ints. */
    IExec->DebugPrintF("[vulkan.library] Found %ld ICD(s)\n", (long)found);
    for (uint32_t i = 0; i < g_loaderState.icdCount; i++)
    {
        IExec->DebugPrintF("[vulkan.library] ICD %lu: %s (priority %ld)\n",
                           (unsigned long)i, g_loaderState.icds[i].libraryPath,
                           (long)g_loaderState.icds[i].priority);
    }

    return found;
}

/****************************************************************************/
/* Unload all ICDs                                                          */
/****************************************************************************/

void LoaderUnloadICDs(void)
{
    for (uint32_t i = 0; i < g_loaderState.icdCount; i++)
    {
        struct ICDState *icd = &g_loaderState.icds[i];

        if (icd->iface)
        {
            IExec->DropInterface(icd->iface);
            icd->iface = NULL;
        }
        if (icd->library)
        {
            IExec->CloseLibrary(icd->library);
            icd->library = NULL;
        }
    }

    g_loaderState.icdCount = 0;
    g_loaderState.initialized = VK_FALSE;

    IExec->DebugPrintF("[vulkan.library] All ICDs unloaded\n");
}
