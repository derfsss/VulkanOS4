/*
** VulkanPrefs -- Vulkan ICD Preferences Tool for AmigaOS 4
**
** Manages ICD priority, enable/disable, and defaults when multiple
** Vulkan ICDs are installed (e.g., software_vk + ogles2_vk).
**
** Usage:
**   VulkanPrefs              Show current configuration
**   VulkanPrefs LIST         List all ICDs with availability
**   VulkanPrefs PRIORITY x,y Set priority order
**   VulkanPrefs ENABLE x     Enable an ICD
**   VulkanPrefs DISABLE x    Disable an ICD
**   VulkanPrefs RESET        Delete prefs (revert to defaults)
**
** Prefs stored in ENVARC:Vulkan/vulkan.prefs
**
** Compile: ppc-amigaos-gcc -mcrt=newlib -D__USE_INLINE__
**          -o VulkanPrefs VulkanPrefs.c -lauto
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/exall.h>
#include <dos/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFS_DIR      "ENVARC:Vulkan"
#define PREFS_FILE     "ENVARC:Vulkan/vulkan.prefs"
#define PREFS_ENV_FILE "ENV:Vulkan/vulkan.prefs"
#define ICD_DIR        "DEVS:Vulkan/icd.d"
#define ICD_LIB_DIR    "LIBS:Vulkan"
#define MAX_ICDS       8
#define MAX_NAME       64
#define MAX_PATH       256
#define MAX_LINE       512

/****************************************************************************/
/* ICD info                                                                 */
/****************************************************************************/

typedef struct ICDInfo {
    char name[MAX_NAME];          /* e.g., "software_vk" */
    char libraryPath[MAX_PATH];   /* e.g., "LIBS:Vulkan/software_vk.library" */
    char apiVersion[32];          /* e.g., "1.3.0" */
    int  available;               /* 1 if library file exists */
    int  enabled;                 /* 1 if not in disabled list */
    int  priority;                /* 0 = highest priority */
} ICDInfo;

typedef struct PrefsState {
    ICDInfo icds[MAX_ICDS];
    int     icdCount;
    char    priorityStr[MAX_LINE];
    char    disabledStr[MAX_LINE];
    char    defaultStr[MAX_NAME];
    int     prefsLoaded;
} PrefsState;

/****************************************************************************/
/* JSON parsing (minimal -- extract library_path and api_version)            */
/****************************************************************************/

static int json_extract_string(const char *json, const char *key,
                                char *out, int outLen)
{
    char search[80];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return 0;

    p += strlen(search);
    while (*p && *p != ':') p++;
    if (!*p) return 0;
    p++;

    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != '"') return 0;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < outLen - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

/****************************************************************************/
/* Extract ICD name from library path                                       */
/* "LIBS:Vulkan/software_vk.library" -> "software_vk"                       */
/****************************************************************************/

static void extract_icd_name(const char *libPath, char *name, int nameLen)
{
    /* Find last '/' or ':' */
    const char *start = libPath;
    const char *p = libPath;
    while (*p)
    {
        if (*p == '/' || *p == ':') start = p + 1;
        p++;
    }

    /* Copy up to ".library" */
    int i = 0;
    while (start[i] && start[i] != '.' && i < nameLen - 1)
    {
        name[i] = start[i];
        i++;
    }
    name[i] = '\0';
}

/****************************************************************************/
/* Check if a file exists                                                   */
/****************************************************************************/

static int file_exists(const char *path)
{
    BPTR lock = Lock(path, SHARED_LOCK);
    if (lock)
    {
        UnLock(lock);
        return 1;
    }
    return 0;
}

/****************************************************************************/
/* Read a file into a buffer                                                */
/****************************************************************************/

static char *read_file(const char *path, int *outSize)
{
    BPTR fh = Open(path, MODE_OLDFILE);
    if (!fh) return NULL;

    /* Get file size via ExamineObjectTags */
    struct ExamineData *exd = ExamineObjectTags(EX_FileHandleInput, (Tag)fh, TAG_DONE);
    int64 size64 = exd ? exd->FileSize : 0;
    if (exd) FreeDosObject(DOS_EXAMINEDATA, exd);
    int size = (int)size64;
    if (size <= 0 || size > 8192)
    {
        Close(fh);
        return NULL;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) { Close(fh); return NULL; }

    int32 bytesRead = Read(fh, buf, size);
    Close(fh);
    if (bytesRead <= 0) { free(buf); return NULL; }
    buf[bytesRead] = '\0';

    if (outSize) *outSize = size;
    return buf;
}

/****************************************************************************/
/* Scan for ICD manifests                                                   */
/****************************************************************************/

static void scan_icds(PrefsState *state)
{
    state->icdCount = 0;

    APTR context = ObtainDirContextTags(
        EX_StringNameInput, (Tag)ICD_DIR,
        EX_DataFields, EXF_NAME | EXF_TYPE,
        TAG_DONE);

    if (!context)
    {
        printf("  (cannot scan %s)\n", ICD_DIR);
        return;
    }

    struct ExamineData *data;
    while ((data = ExamineDir(context)) != NULL && state->icdCount < MAX_ICDS)
    {
        /* Only process .json files */
        if (!data->Name || !strstr(data->Name, ".json"))
            continue;

        /* Read the JSON manifest */
        char jsonPath[MAX_PATH];
        snprintf(jsonPath, sizeof(jsonPath), "%s/%s", ICD_DIR, data->Name);

        int jsonSize = 0;
        char *json = read_file(jsonPath, &jsonSize);
        if (!json) continue;

        ICDInfo *icd = &state->icds[state->icdCount];
        memset(icd, 0, sizeof(ICDInfo));

        /* Extract library path and API version */
        if (json_extract_string(json, "library_path", icd->libraryPath, MAX_PATH) &&
            json_extract_string(json, "api_version", icd->apiVersion, 32))
        {
            extract_icd_name(icd->libraryPath, icd->name, MAX_NAME);
            icd->available = file_exists(icd->libraryPath);
            icd->enabled = 1;  /* Default: enabled */
            icd->priority = state->icdCount;
            state->icdCount++;
        }

        free(json);
    }

    ReleaseDirContext(context);
}

/****************************************************************************/
/* Read preferences file                                                    */
/****************************************************************************/

static void read_prefs(PrefsState *state)
{
    state->prefsLoaded = 0;
    state->priorityStr[0] = '\0';
    state->disabledStr[0] = '\0';
    state->defaultStr[0] = '\0';

    int size = 0;
    char *buf = read_file(PREFS_FILE, &size);
    if (!buf)
    {
        /* Try ENV: as fallback */
        buf = read_file(PREFS_ENV_FILE, &size);
    }
    if (!buf) return;

    /* Parse line by line */
    char *line = buf;
    while (line && *line)
    {
        /* Skip comments and blank lines */
        if (*line == '#' || *line == '\n' || *line == '\r')
        {
            line = strchr(line, '\n');
            if (line) line++;
            continue;
        }

        if (strncmp(line, "PRIORITY=", 9) == 0)
        {
            char *val = line + 9;
            char *end = strchr(val, '\n');
            int len = end ? (int)(end - val) : (int)strlen(val);
            if (len >= MAX_LINE) len = MAX_LINE - 1;
            strncpy(state->priorityStr, val, len);
            state->priorityStr[len] = '\0';
            /* Strip trailing \r */
            len = strlen(state->priorityStr);
            while (len > 0 && (state->priorityStr[len-1] == '\r' || state->priorityStr[len-1] == '\n'))
                state->priorityStr[--len] = '\0';
        }
        else if (strncmp(line, "DISABLED=", 9) == 0)
        {
            char *val = line + 9;
            char *end = strchr(val, '\n');
            int len = end ? (int)(end - val) : (int)strlen(val);
            if (len >= MAX_LINE) len = MAX_LINE - 1;
            strncpy(state->disabledStr, val, len);
            state->disabledStr[len] = '\0';
            len = strlen(state->disabledStr);
            while (len > 0 && (state->disabledStr[len-1] == '\r' || state->disabledStr[len-1] == '\n'))
                state->disabledStr[--len] = '\0';
        }
        else if (strncmp(line, "DEFAULT=", 8) == 0)
        {
            char *val = line + 8;
            char *end = strchr(val, '\n');
            int len = end ? (int)(end - val) : (int)strlen(val);
            if (len >= MAX_NAME) len = MAX_NAME - 1;
            strncpy(state->defaultStr, val, len);
            state->defaultStr[len] = '\0';
            len = strlen(state->defaultStr);
            while (len > 0 && (state->defaultStr[len-1] == '\r' || state->defaultStr[len-1] == '\n'))
                state->defaultStr[--len] = '\0';
        }

        line = strchr(line, '\n');
        if (line) line++;
    }

    free(buf);
    state->prefsLoaded = 1;

    /* Apply disabled list (tokenize to avoid substring false positives) */
    if (state->disabledStr[0])
    {
        char disTmp[MAX_LINE];
        strncpy(disTmp, state->disabledStr, MAX_LINE - 1);
        disTmp[MAX_LINE - 1] = '\0';
        char *tok = strtok(disTmp, ",");
        while (tok)
        {
            for (int i = 0; i < state->icdCount; i++)
            {
                if (strcmp(state->icds[i].name, tok) == 0)
                    state->icds[i].enabled = 0;
            }
            tok = strtok(NULL, ",");
        }
    }

    /* Apply priority order */
    if (state->priorityStr[0])
    {
        char tmp[MAX_LINE];
        strncpy(tmp, state->priorityStr, MAX_LINE - 1);
        tmp[MAX_LINE - 1] = '\0';

        /* First, set all priorities to a high value (unlisted = low priority) */
        for (int i = 0; i < state->icdCount; i++)
            state->icds[i].priority = MAX_ICDS + i;

        int prio = 0;
        char *tok = strtok(tmp, ",");
        while (tok && prio < MAX_ICDS)
        {
            /* Find this ICD and set its priority */
            for (int i = 0; i < state->icdCount; i++)
            {
                if (strcmp(state->icds[i].name, tok) == 0)
                {
                    state->icds[i].priority = prio;
                    break;
                }
            }
            prio++;
            tok = strtok(NULL, ",");
        }
    }
}

/****************************************************************************/
/* Write preferences file                                                   */
/****************************************************************************/

static int write_prefs(PrefsState *state)
{
    /* Ensure directory exists */
    BPTR lock = CreateDir(PREFS_DIR);
    if (lock) UnLock(lock);

    /* Also create ENV: version */
    lock = CreateDir("ENV:Vulkan");
    if (lock) UnLock(lock);

    /* Build priority string from current order */
    char prioBuf[MAX_LINE];
    prioBuf[0] = '\0';

    /* Sort ICDs by priority */
    ICDInfo sorted[MAX_ICDS];
    memcpy(sorted, state->icds, state->icdCount * sizeof(ICDInfo));
    for (int i = 0; i < state->icdCount - 1; i++)
    {
        for (int j = i + 1; j < state->icdCount; j++)
        {
            if (sorted[j].priority < sorted[i].priority)
            {
                ICDInfo tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    for (int i = 0; i < state->icdCount; i++)
    {
        if (i > 0) strncat(prioBuf, ",", MAX_LINE - strlen(prioBuf) - 1);
        strncat(prioBuf, sorted[i].name, MAX_LINE - strlen(prioBuf) - 1);
    }

    /* Build disabled string */
    char disBuf[MAX_LINE];
    disBuf[0] = '\0';
    for (int i = 0; i < state->icdCount; i++)
    {
        if (!state->icds[i].enabled)
        {
            if (disBuf[0]) strncat(disBuf, ",", MAX_LINE - strlen(disBuf) - 1);
            strncat(disBuf, state->icds[i].name, MAX_LINE - strlen(disBuf) - 1);
        }
    }

    /* Write to both ENVARC: and ENV: */
    const char *paths[] = { PREFS_FILE, PREFS_ENV_FILE };
    for (int p = 0; p < 2; p++)
    {
        BPTR fh = Open(paths[p], MODE_NEWFILE);
        if (!fh)
        {
            if (p == 0)
            {
                printf("ERROR: Cannot write %s\n", paths[p]);
                return 0;
            }
            continue;  /* ENV: failure is non-fatal */
        }

        FPrintf(fh, "# VulkanOS4 ICD Preferences\n");
        FPrintf(fh, "# Generated by VulkanPrefs\n");
        FPrintf(fh, "PRIORITY=%s\n", (CONST_STRPTR)prioBuf);
        FPrintf(fh, "DISABLED=%s\n", (CONST_STRPTR)disBuf);
        FPrintf(fh, "DEFAULT=%s\n", (CONST_STRPTR)state->defaultStr);
        Close(fh);
    }

    return 1;
}

/****************************************************************************/
/* Display current configuration                                            */
/****************************************************************************/

static void show_config(PrefsState *state)
{
    printf("VulkanOS4 ICD Configuration\n");
    printf("===========================\n\n");

    if (state->icdCount == 0)
    {
        printf("No ICDs found in %s\n", ICD_DIR);
        return;
    }

    /* Sort by priority for display */
    ICDInfo sorted[MAX_ICDS];
    memcpy(sorted, state->icds, state->icdCount * sizeof(ICDInfo));
    for (int i = 0; i < state->icdCount - 1; i++)
    {
        for (int j = i + 1; j < state->icdCount; j++)
        {
            if (sorted[j].priority < sorted[i].priority)
            {
                ICDInfo tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    printf("Priority  ICD Name          Status      API      Library\n");
    printf("--------  ----------------  ----------  -------  ---------------------------\n");

    for (int i = 0; i < state->icdCount; i++)
    {
        const char *status;
        if (!sorted[i].available)
            status = "NOT FOUND";
        else if (!sorted[i].enabled)
            status = "DISABLED";
        else
            status = "ACTIVE";

        printf("  %d       %-16s  %-10s  %-7s  %s\n",
            i + 1,
            sorted[i].name,
            status,
            sorted[i].apiVersion,
            sorted[i].libraryPath);
    }

    printf("\n");
    if (state->prefsLoaded)
        printf("Preferences: %s\n", PREFS_FILE);
    else
        printf("Preferences: (using defaults -- no prefs file)\n");
}

/****************************************************************************/
/* Main                                                                     */
/****************************************************************************/

int main(int argc, char **argv)
{
    PrefsState state;
    memset(&state, 0, sizeof(state));

    /* Scan for installed ICDs */
    scan_icds(&state);

    /* Read existing preferences */
    read_prefs(&state);

    if (argc < 2)
    {
        /* No arguments: show current configuration */
        show_config(&state);
        return 0;
    }

    const char *cmd = argv[1];

    if (strcasecmp(cmd, "LIST") == 0)
    {
        show_config(&state);
    }
    else if (strcasecmp(cmd, "PRIORITY") == 0)
    {
        if (argc < 3)
        {
            printf("Usage: VulkanPrefs PRIORITY name1,name2,...\n");
            printf("Example: VulkanPrefs PRIORITY ogles2_vk,software_vk\n");
            return 1;
        }

        /* Parse priority list and assign priorities */
        char tmp[MAX_LINE];
        strncpy(tmp, argv[2], MAX_LINE - 1);
        tmp[MAX_LINE - 1] = '\0';

        int prio = 0;
        char *tok = strtok(tmp, ",");
        while (tok)
        {
            int found = 0;
            for (int i = 0; i < state.icdCount; i++)
            {
                if (strcmp(state.icds[i].name, tok) == 0)
                {
                    state.icds[i].priority = prio++;
                    found = 1;
                    break;
                }
            }
            if (!found)
                printf("WARNING: ICD '%s' not found, skipping\n", tok);
            tok = strtok(NULL, ",");
        }

        if (write_prefs(&state))
        {
            printf("Priority updated.\n");
            show_config(&state);
        }
    }
    else if (strcasecmp(cmd, "ENABLE") == 0)
    {
        if (argc < 3)
        {
            printf("Usage: VulkanPrefs ENABLE icd_name\n");
            return 1;
        }

        int found = 0;
        for (int i = 0; i < state.icdCount; i++)
        {
            if (strcmp(state.icds[i].name, argv[2]) == 0)
            {
                state.icds[i].enabled = 1;
                found = 1;
                break;
            }
        }

        if (!found)
        {
            printf("ICD '%s' not found.\n", argv[2]);
            return 1;
        }

        if (write_prefs(&state))
        {
            printf("ICD '%s' enabled.\n", argv[2]);
            show_config(&state);
        }
    }
    else if (strcasecmp(cmd, "DISABLE") == 0)
    {
        if (argc < 3)
        {
            printf("Usage: VulkanPrefs DISABLE icd_name\n");
            return 1;
        }

        int found = 0;
        for (int i = 0; i < state.icdCount; i++)
        {
            if (strcmp(state.icds[i].name, argv[2]) == 0)
            {
                state.icds[i].enabled = 0;
                found = 1;
                break;
            }
        }

        if (!found)
        {
            printf("ICD '%s' not found.\n", argv[2]);
            return 1;
        }

        if (write_prefs(&state))
        {
            printf("ICD '%s' disabled.\n", argv[2]);
            show_config(&state);
        }
    }
    else if (strcasecmp(cmd, "RESET") == 0)
    {
        Delete(PREFS_FILE);
        Delete(PREFS_ENV_FILE);
        printf("Preferences reset to defaults.\n");

        /* Re-read with defaults */
        memset(&state, 0, sizeof(state));
        scan_icds(&state);
        show_config(&state);
    }
    else
    {
        printf("Unknown command: %s\n\n", cmd);
        printf("Usage:\n");
        printf("  VulkanPrefs              Show current configuration\n");
        printf("  VulkanPrefs LIST         List all ICDs with availability\n");
        printf("  VulkanPrefs PRIORITY x,y Set priority order\n");
        printf("  VulkanPrefs ENABLE x     Enable an ICD\n");
        printf("  VulkanPrefs DISABLE x    Disable an ICD\n");
        printf("  VulkanPrefs RESET        Delete prefs (revert to defaults)\n");
        return 1;
    }

    return 0;
}
