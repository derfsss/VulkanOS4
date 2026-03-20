/*
** test_w3dn_init.c -- W3D Nova Context Creation + GPU Enumeration Test
**
** Standalone test: calls W3D Nova API directly (not through Vulkan)
** to verify that the GPU driver works on the X5000/RX560.
**
** Tests:
**   1. Open Warp3DNova.library, get IW3DNova interface
**   2. Enumerate GPUs via W3DN_GetGPUsList
**   3. Print GPU name, board number
**   4. Query capabilities: max tex width/height, render width/height, tex units
**   5. Create a W3D Nova context on the default public screen
**   6. Print driver name, board number from context
**   7. Destroy context
**   8. Free GPU list
**   9. Close library
**
** Compile:
**   ppc-amigaos-gcc -mcrt=newlib -O2 -Wall -D__USE_INLINE__
**       -o test_w3dn_init test_w3dn_init.c -lauto
**
** (Warp3DNova.library is opened manually since the Enhancer SDK proto
**  headers are not in the base cross-compile SDK path.)
*/

#include <proto/exec.h>
#include <proto/intuition.h>

#include <Warp3DNova/Warp3DNova.h>

#include <stdio.h>
#include <string.h>

/****************************************************************************/
/* Minimal IW3DNova interface definition                                    */
/*                                                                          */
/* We define only the methods we need. The layout must match the real       */
/* interface: standard Obtain/Release/Expunge/Clone followed by library     */
/* methods in IDL order.                                                    */
/****************************************************************************/

struct IW3DNovaIFace
{
    struct InterfaceData Data;

    uint32 APICALL (*Obtain)(struct IW3DNovaIFace *Self);
    uint32 APICALL (*Release)(struct IW3DNovaIFace *Self);
    void   APICALL (*Expunge)(struct IW3DNovaIFace *Self);
    struct Interface * APICALL (*Clone)(struct IW3DNovaIFace *Self);

    /* Library methods (IDL order) */
    W3DN_Gpu * APICALL (*W3DN_GetGPUsList)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_Gpu * APICALL (*W3DN_GetGPUsListTags)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, ...);
    void APICALL (*W3DN_FreeGPUsList)(struct IW3DNovaIFace *Self,
        W3DN_Gpu *gpusList);
    W3DN_ScreenMode * APICALL (*W3DN_GetScreenModeList)(
        struct IW3DNovaIFace *Self, W3DN_ErrorCode *errCode,
        struct TagItem *tags);
    W3DN_ScreenMode * APICALL (*W3DN_GetScreenModeListTags)(
        struct IW3DNovaIFace *Self, W3DN_ErrorCode *errCode, ...);
    void APICALL (*W3DN_FreeScreenModeList)(struct IW3DNovaIFace *Self,
        W3DN_ScreenMode *screenModeList);
    uint32 APICALL (*W3DN_BestModeID)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, struct TagItem *tags);
    uint32 APICALL (*W3DN_BestModeIDTags)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, ...);
    uint32 APICALL (*W3DN_Query)(struct IW3DNovaIFace *Self,
        W3DN_Gpu *gpu, W3DN_CapQuery query);
    /* W3DN_GetTexFmtInfo (taglist) */
    void *_W3DN_GetTexFmtInfo;
    /* W3DN_GetTexFmtInfoTags (varargs) */
    void *_W3DN_GetTexFmtInfoTags;
    /* W3DN_GetBMFmtInfo (taglist) */
    void *_W3DN_GetBMFmtInfo;
    /* W3DN_GetBMFmtInfoTags (varargs) */
    void *_W3DN_GetBMFmtInfoTags;
    /* W3DN_CreateContext */
    W3DN_Context * APICALL (*W3DN_CreateContext)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_Context * APICALL (*W3DN_CreateContextTags)(
        struct IW3DNovaIFace *Self, W3DN_ErrorCode *errCode, ...);
    /* W3DN_GetErrorString */
    const char * APICALL (*W3DN_GetErrorString)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode errCode);
};

/****************************************************************************/
/* Error code to string helper                                              */
/****************************************************************************/

static const char *w3dn_errstr(W3DN_ErrorCode err)
{
    switch (err)
    {
    case W3DNEC_SUCCESS:            return "SUCCESS";
    case W3DNEC_ILLEGALINPUT:       return "ILLEGAL_INPUT";
    case W3DNEC_UNSUPPORTED:        return "UNSUPPORTED";
    case W3DNEC_NOMEMORY:           return "NO_MEMORY";
    case W3DNEC_NOVRAM:             return "NO_VRAM";
    case W3DNEC_NODRIVER:           return "NO_DRIVER";
    case W3DNEC_ILLEGALBITMAP:      return "ILLEGAL_BITMAP";
    case W3DNEC_NOBITMAP:           return "NO_BITMAP";
    case W3DNEC_UNKNOWNERROR:       return "UNKNOWN_ERROR";
    default:                        return "?";
    }
}

/****************************************************************************/
/* Main                                                                     */
/****************************************************************************/

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct Library        *W3DNovaBase = NULL;
    struct IW3DNovaIFace  *iw3d        = NULL;
    W3DN_Gpu              *gpuList     = NULL;
    W3DN_Context          *ctx         = NULL;
    struct Screen         *screen      = NULL;
    W3DN_ErrorCode         err;
    int                    exitCode    = 0;

    printf("=== W3D Nova Init Test ===\n");

    /*----------------------------------------------------------------------
    ** 1. Open Warp3DNova.library
    **--------------------------------------------------------------------*/
    printf("Opening Warp3DNova.library... ");
    W3DNovaBase = OpenLibrary("Warp3DNova.library", 0);
    if (!W3DNovaBase)
    {
        printf("FAILED\n");
        printf("ERROR: Warp3DNova.library not found.\n");
        printf("Make sure the Enhancer Software is installed.\n");
        return 1;
    }

    iw3d = (struct IW3DNovaIFace *)GetInterface(W3DNovaBase, "main", 1, NULL);
    if (!iw3d)
    {
        printf("FAILED (no interface)\n");
        printf("ERROR: Could not get IW3DNova interface.\n");
        CloseLibrary(W3DNovaBase);
        return 1;
    }
    printf("OK (v%lu)\n", (unsigned long)W3DNovaBase->lib_Version);

    /*----------------------------------------------------------------------
    ** 2. Enumerate GPUs
    **--------------------------------------------------------------------*/
    printf("Enumerating GPUs...\n");

    err = W3DNEC_SUCCESS;
    gpuList = iw3d->W3DN_GetGPUsList(&err, NULL);
    if (!gpuList || err != W3DNEC_SUCCESS)
    {
        printf("  FAILED (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /*----------------------------------------------------------------------
    ** 3-4. Print GPU info and capabilities
    **--------------------------------------------------------------------*/
    {
        W3DN_Gpu *gpu;
        int idx = 0;

        for (gpu = gpuList; gpu != NULL; gpu = gpu->next, idx++)
        {
            printf("  GPU %d: %s (board %lu)\n",
                idx, gpu->name, (unsigned long)gpu->boardNum);
            printf("    Library: %s\n", gpu->libName);

            printf("  Capabilities:\n");
            printf("    Max texture width:  %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXTEXWIDTH));
            printf("    Max texture height: %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXTEXHEIGHT));
            printf("    Max render width:   %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXRENDERWIDTH));
            printf("    Max render height:  %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXRENDERHEIGHT));
            printf("    Max texture units:  %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXTEXUNITS));
            printf("    Max vertex attribs: %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXVERTEXATTRIBS));
            printf("    Max colour buffers: %lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXCOLOURBUFFERS));
            printf("    Max varying vectors:%lu\n",
                (unsigned long)iw3d->W3DN_Query(gpu, W3DN_Q_MAXVARYINGVECTORS));

            /* Feature queries */
            printf("    Render to texture:  %s\n",
                iw3d->W3DN_Query(gpu, W3DN_Q_RENDERTOTEXTURE)
                    == W3DN_SUPPORTED ? "yes" : "no");
            printf("    Mipmapping:         %s\n",
                iw3d->W3DN_Query(gpu, W3DN_Q_MIPMAPPING)
                    == W3DN_SUPPORTED ? "yes" : "no");
            printf("    Stencil buffer:     %s\n",
                iw3d->W3DN_Query(gpu, W3DN_Q_STENCIL)
                    == W3DN_SUPPORTED ? "yes" : "no");
            printf("    3D textures:        %s\n",
                iw3d->W3DN_Query(gpu, W3DN_Q_TEXTURE_3D)
                    == W3DN_SUPPORTED ? "yes" : "no");
            printf("    Cube map textures:  %s\n",
                iw3d->W3DN_Query(gpu, W3DN_Q_TEXTURE_CUBEMAP)
                    == W3DN_SUPPORTED ? "yes" : "no");

            /* GPU endianness */
            {
                uint32 endian = iw3d->W3DN_Query(gpu, W3DN_Q_GPUENDIANNESS);
                const char *endStr;
                switch (endian)
                {
                case W3DN_BIGENDIAN:     endStr = "big-endian"; break;
                case W3DN_LITTLEENDIAN:  endStr = "little-endian"; break;
                default:                 endStr = "unknown"; break;
                }
                printf("    GPU endianness:     %s\n", endStr);
            }
        }
    }

    /*----------------------------------------------------------------------
    ** 5. Create a W3D Nova context on the default public screen
    **--------------------------------------------------------------------*/
    printf("Locking public screen... ");
    screen = LockPubScreen(NULL);
    if (!screen)
    {
        printf("FAILED\n");
        printf("ERROR: Cannot lock public screen.\n");
        exitCode = 1;
        goto cleanup;
    }
    printf("OK\n");

    printf("Creating context... ");
    {
        struct TagItem ctxTags[] = {
            { W3DNTag_Screen,     (Tag)screen },
            { W3DNTag_DriverType, W3DN_DRIVER_BEST },
            { TAG_DONE,           0 }
        };

        err = W3DNEC_SUCCESS;
        ctx = iw3d->W3DN_CreateContext(&err, ctxTags);
        if (!ctx || err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 6. Print context info
    **--------------------------------------------------------------------*/
    printf("  Driver: %s\n", ctx->drvName ? ctx->drvName : "(null)");
    printf("  Library: %s\n", ctx->libName ? ctx->libName : "(null)");
    printf("  Board:  %lu\n", (unsigned long)ctx->boardNum);

    /* Query a few capabilities through the context (same enum, but
    ** exercising the context's Query method rather than the library's) */
    printf("  Context max tex width:  %lu\n",
        (unsigned long)ctx->Query(W3DN_Q_MAXTEXWIDTH));
    printf("  Context max tex height: %lu\n",
        (unsigned long)ctx->Query(W3DN_Q_MAXTEXHEIGHT));

    /*----------------------------------------------------------------------
    ** 7-9. Cleanup
    **--------------------------------------------------------------------*/
cleanup:
    if (ctx)
    {
        printf("Destroying context... ");
        ctx->Destroy();
        printf("OK\n");
    }

    if (screen)
        UnlockPubScreen(NULL, screen);

    if (gpuList)
    {
        printf("Freeing GPU list... ");
        iw3d->W3DN_FreeGPUsList(gpuList);
        printf("OK\n");
    }

    if (iw3d)
        DropInterface((struct Interface *)iw3d);
    if (W3DNovaBase)
        CloseLibrary(W3DNovaBase);

    if (exitCode == 0)
        printf("=== Test PASSED ===\n");
    else
        printf("=== Test FAILED ===\n");

    return exitCode;
}
