/*
** test_w3dn_triangle.c -- W3D Nova Triangle Rendering Test
**
** Standalone test: calls W3D Nova API directly (not through Vulkan)
** to render a coloured triangle on the X5000/RX560. This verifies the
** complete GPU rendering pipeline.
**
** Tests:
**   - Context creation with hardware driver
**   - Framebuffer creation and bitmap binding
**   - VBO creation, locking, and vertex data upload
**   - SPIR-V shader compilation (vertex + fragment)
**   - Shader pipeline creation
**   - Render state object creation and configuration
**   - Viewport setup, render target binding
**   - Vertex attribute binding
**   - Clear + DrawArrays + Submit
**   - GPU synchronization (WaitDone)
**   - Bitmap blit to window for presentation
**
** Compile:
**   ppc-amigaos-gcc -mcrt=newlib -O2 -Wall -D__USE_INLINE__
**       -o test_w3dn_triangle test_w3dn_triangle.c -lauto
*/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <Warp3DNova/Warp3DNova.h>

#include <stdio.h>
#include <string.h>

/* Embedded SPIR-V bytecode (pre-compiled from GLSL) */
#include "shaders/vert_spv.h"
#include "shaders/frag_spv.h"

/****************************************************************************/
/* Window dimensions                                                        */
/****************************************************************************/

#define WIN_WIDTH  800
#define WIN_HEIGHT 600

/****************************************************************************/
/* Minimal IW3DNova interface definition                                    */
/*                                                                          */
/* Layout matches the real interface: standard Obtain/Release/Expunge/Clone */
/* followed by library methods in IDL order.                                */
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
    void *_W3DN_GetTexFmtInfo;
    void *_W3DN_GetTexFmtInfoTags;
    void *_W3DN_GetBMFmtInfo;
    void *_W3DN_GetBMFmtInfoTags;
    W3DN_Context * APICALL (*W3DN_CreateContext)(struct IW3DNovaIFace *Self,
        W3DN_ErrorCode *errCode, struct TagItem *tags);
    W3DN_Context * APICALL (*W3DN_CreateContextTags)(
        struct IW3DNovaIFace *Self, W3DN_ErrorCode *errCode, ...);
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
    case W3DNEC_SUCCESS:                    return "SUCCESS";
    case W3DNEC_ILLEGALINPUT:               return "ILLEGAL_INPUT";
    case W3DNEC_UNSUPPORTED:                return "UNSUPPORTED";
    case W3DNEC_NOMEMORY:                   return "NO_MEMORY";
    case W3DNEC_NOVRAM:                     return "NO_VRAM";
    case W3DNEC_NODRIVER:                   return "NO_DRIVER";
    case W3DNEC_ILLEGALBITMAP:              return "ILLEGAL_BITMAP";
    case W3DNEC_NOBITMAP:                   return "NO_BITMAP";
    case W3DNEC_NOTEXTURE:                  return "NO_TEXTURE";
    case W3DNEC_UNSUPPORTEDFMT:             return "UNSUPPORTED_FMT";
    case W3DNEC_NOZBUFFER:                  return "NO_ZBUFFER";
    case W3DNEC_NOSTENCILBUFFER:            return "NO_STENCIL";
    case W3DNEC_UNKNOWNERROR:               return "UNKNOWN_ERROR";
    case W3DNEC_INCOMPLETEFRAMEBUFFER:      return "INCOMPLETE_FB";
    case W3DNEC_TIMEOUT:                    return "TIMEOUT";
    case W3DNEC_QUEUEEMPTY:                 return "QUEUE_EMPTY";
    case W3DNEC_MISSINGVERTEXARRAYS:        return "MISSING_VERTEX_ARRAYS";
    case W3DNEC_FILENOTFOUND:               return "FILE_NOT_FOUND";
    case W3DNEC_SHADERSINCOMPATIBLE:        return "SHADERS_INCOMPATIBLE";
    case W3DNEC_IOERROR:                    return "IO_ERROR";
    case W3DNEC_CORRUPTSHADER:              return "CORRUPT_SHADER";
    case W3DNEC_INCOMPLETESHADERPIPELINE:   return "INCOMPLETE_PIPELINE";
    case W3DNEC_NOSHADERPIPELINE:           return "NO_SHADER_PIPELINE";
    case W3DNEC_SHADERERRORS:               return "SHADER_ERRORS";
    case W3DNEC_MISSINGSHADERDATABUFFERS:   return "MISSING_DATA_BUFFERS";
    default:                                return "?";
    }
}

/****************************************************************************/
/* Triangle vertex data                                                     */
/*                                                                          */
/* Each vertex: x, y (position) + r, g, b (colour) = 5 floats              */
/* Layout: interleaved, 20 bytes per vertex                                 */
/*                                                                          */
/* Triangle centered in clip space (-1..1):                                 */
/*   Top:          ( 0.0, -0.5) red                                         */
/*   Bottom-left:  (-0.5,  0.5) green                                       */
/*   Bottom-right: ( 0.5,  0.5) blue                                        */
/****************************************************************************/

static const float triangle_vertices[] = {
    /* x      y      r     g     b   */
     0.0f, -0.5f,  1.0f, 0.0f, 0.0f,   /* top - red */
    -0.5f,  0.5f,  0.0f, 1.0f, 0.0f,   /* bottom-left - green */
     0.5f,  0.5f,  0.0f, 0.0f, 1.0f,   /* bottom-right - blue */
};

#define VERTEX_COUNT 3
#define FLOATS_PER_VERTEX 5
#define VERTEX_STRIDE (FLOATS_PER_VERTEX * sizeof(float))  /* 20 bytes */
#define VBO_SIZE (VERTEX_COUNT * VERTEX_STRIDE)             /* 60 bytes */

/****************************************************************************/
/* Main                                                                     */
/****************************************************************************/

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct Library        *W3DNovaBase = NULL;
    struct IW3DNovaIFace  *iw3d        = NULL;
    struct Screen         *screen      = NULL;
    struct Window         *window      = NULL;
    W3DN_Context          *ctx         = NULL;
    W3DN_FrameBuffer      *fb          = NULL;
    W3DN_VertexBuffer     *vbo         = NULL;
    W3DN_Shader           *vertShader  = NULL;
    W3DN_Shader           *fragShader  = NULL;
    W3DN_ShaderPipeline   *pipeline    = NULL;
    W3DN_RenderState      *rso         = NULL;
    struct BitMap         *backBM      = NULL;
    W3DN_ErrorCode         err;
    int                    exitCode    = 0;

    printf("=== W3D Nova Triangle Test ===\n");

    /*----------------------------------------------------------------------
    ** 1. Open libraries
    **--------------------------------------------------------------------*/
    printf("Opening libraries... ");

    W3DNovaBase = OpenLibrary("Warp3DNova.library", 0);
    if (!W3DNovaBase)
    {
        printf("FAILED\n");
        printf("ERROR: Warp3DNova.library not found.\n");
        return 1;
    }

    iw3d = (struct IW3DNovaIFace *)GetInterface(W3DNovaBase, "main", 1, NULL);
    if (!iw3d)
    {
        printf("FAILED (no W3DNova interface)\n");
        CloseLibrary(W3DNovaBase);
        return 1;
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 2. Create window on the default public screen
    **--------------------------------------------------------------------*/
    printf("Creating window... ");

    screen = LockPubScreen(NULL);
    if (!screen)
    {
        printf("FAILED (no screen)\n");
        exitCode = 1;
        goto cleanup;
    }

    window = OpenWindowTags(NULL,
        WA_Title,       (Tag)"W3D Nova Triangle Test",
        WA_Width,       WIN_WIDTH,
        WA_Height,      WIN_HEIGHT,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget,  FALSE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW,
        WA_PubScreen,   (Tag)screen,
        WA_SmartRefresh, TRUE,
        TAG_DONE);

    if (!window)
    {
        printf("FAILED\n");
        exitCode = 1;
        goto cleanup;
    }

    /* Compute inner window dimensions (minus borders) */
    uint32 innerW = window->Width - window->BorderLeft - window->BorderRight;
    uint32 innerH = window->Height - window->BorderTop - window->BorderBottom;
    printf("OK (%lux%lu inner)\n", (unsigned long)innerW, (unsigned long)innerH);

    /*----------------------------------------------------------------------
    ** 3. Create W3D Nova context
    **--------------------------------------------------------------------*/
    printf("Creating W3D Nova context... ");
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
    printf("OK (%s, board %lu)\n", ctx->drvName, (unsigned long)ctx->boardNum);

    /*----------------------------------------------------------------------
    ** 4. Create framebuffer and bind the window's bitmap
    **--------------------------------------------------------------------*/
    printf("Creating framebuffer... ");

    err = W3DNEC_SUCCESS;
    fb = ctx->CreateFrameBuffer(&err);
    if (!fb || err != W3DNEC_SUCCESS)
    {
        printf("FAILED (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Allocate a back-buffer bitmap matching the inner window */
    backBM = AllocBitMapTags(
        innerW, innerH, 32,
        BMATags_Friend,        (Tag)window->RPort->BitMap,
        BMATags_Displayable,   TRUE,
        BMATags_PixelFormat,   PIXF_A8R8G8B8,
        TAG_DONE);

    if (!backBM)
    {
        printf("FAILED (no bitmap)\n");
        exitCode = 1;
        goto cleanup;
    }

    /* Bind the bitmap to colour attachment 0 */
    {
        struct TagItem fbTags[] = {
            { W3DNTag_BitMap, (Tag)backBM },
            { TAG_DONE,       0 }
        };

        err = ctx->FBBindBuffer(fb, W3DN_FB_COLOR_BUFFER_0, fbTags);
        if (err != W3DNEC_SUCCESS)
        {
            printf("FAILED binding colour buffer (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }

    /* Allocate and bind a depth buffer */
    {
        struct TagItem depthTags[] = {
            { W3DNTag_AllocDepthStencil, W3DNPF_DEPTH },
            { TAG_DONE,                  0 }
        };

        err = ctx->FBBindBuffer(fb, W3DN_FB_DEPTH_STENCIL, depthTags);
        if (err != W3DNEC_SUCCESS)
        {
            printf("FAILED binding depth buffer (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }

    /* Check framebuffer status */
    err = ctx->FBGetStatus(fb);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED (framebuffer incomplete: %s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }
    printf("OK (%lux%lu)\n", (unsigned long)innerW, (unsigned long)innerH);

    /*----------------------------------------------------------------------
    ** 5. Create VBO with triangle vertex data
    **--------------------------------------------------------------------*/
    printf("Creating VBO (%d vertices, %d floats each)... ",
        VERTEX_COUNT, FLOATS_PER_VERTEX);

    err = W3DNEC_SUCCESS;
    vbo = ctx->CreateVertexBufferObject(&err,
        VBO_SIZE, W3DN_STATIC_DRAW, 2, NULL);
    if (!vbo || err != W3DNEC_SUCCESS)
    {
        printf("FAILED (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Define array 0: position (vec2, offset 0) */
    err = ctx->VBOSetArray(vbo,
        0,                      /* arrayIdx */
        W3DNEF_FLOAT,           /* elementType */
        FALSE,                  /* normalized */
        2,                      /* numElements (vec2) */
        VERTEX_STRIDE,          /* stride */
        0,                      /* offset */
        VERTEX_COUNT);          /* count */
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED setting position array (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Define array 1: colour (vec3, offset 8 bytes = 2 floats) */
    err = ctx->VBOSetArray(vbo,
        1,                      /* arrayIdx */
        W3DNEF_FLOAT,           /* elementType */
        FALSE,                  /* normalized */
        3,                      /* numElements (vec3) */
        VERTEX_STRIDE,          /* stride */
        2 * sizeof(float),      /* offset (after position) */
        VERTEX_COUNT);          /* count */
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED setting colour array (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Lock VBO and upload vertex data */
    {
        W3DN_BufferLock *lock;

        err = W3DNEC_SUCCESS;
        lock = ctx->VBOLock(&err, vbo, 0, 0);
        if (!lock || err != W3DNEC_SUCCESS)
        {
            printf("FAILED locking VBO (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }

        memcpy(lock->buffer, triangle_vertices, VBO_SIZE);

        err = ctx->BufferUnlock(lock, 0, VBO_SIZE);
        if (err != W3DNEC_SUCCESS)
        {
            printf("FAILED unlocking VBO (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 6. Compile shaders from SPIR-V
    **--------------------------------------------------------------------*/
    printf("Compiling vertex shader (SPIR-V, %u bytes)... ",
        simple_vert_spv_len);
    {
        const char *log = NULL;
        struct TagItem shaderTags[] = {
            { W3DNTag_DataBuffer, (Tag)simple_vert_spv },
            { W3DNTag_DataSize,   simple_vert_spv_len },
            { W3DNTag_Log,        (Tag)&log },
            { W3DNTag_LogLevel,   W3DNLL_WARNING },
            { TAG_DONE,           0 }
        };

        err = W3DNEC_SUCCESS;
        vertShader = ctx->CompileShader(&err, shaderTags);
        if (!vertShader || err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            if (log)
            {
                printf("  Shader log: %s\n", log);
                ctx->DestroyShaderLog(log);
            }
            exitCode = 1;
            goto cleanup;
        }
        if (log)
        {
            if (strlen(log) > 0)
                printf("(warnings: %s) ", log);
            ctx->DestroyShaderLog(log);
        }
    }
    printf("OK\n");

    printf("Compiling fragment shader (SPIR-V, %u bytes)... ",
        simple_frag_spv_len);
    {
        const char *log = NULL;
        struct TagItem shaderTags[] = {
            { W3DNTag_DataBuffer, (Tag)simple_frag_spv },
            { W3DNTag_DataSize,   simple_frag_spv_len },
            { W3DNTag_Log,        (Tag)&log },
            { W3DNTag_LogLevel,   W3DNLL_WARNING },
            { TAG_DONE,           0 }
        };

        err = W3DNEC_SUCCESS;
        fragShader = ctx->CompileShader(&err, shaderTags);
        if (!fragShader || err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            if (log)
            {
                printf("  Shader log: %s\n", log);
                ctx->DestroyShaderLog(log);
            }
            exitCode = 1;
            goto cleanup;
        }
        if (log)
        {
            if (strlen(log) > 0)
                printf("(warnings: %s) ", log);
            ctx->DestroyShaderLog(log);
        }
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 7. Create shader pipeline
    **--------------------------------------------------------------------*/
    printf("Creating shader pipeline... ");
    {
        struct TagItem pipeTags[] = {
            { W3DNTag_Shader, (Tag)vertShader },
            { W3DNTag_Shader, (Tag)fragShader },
            { TAG_DONE,       0 }
        };

        err = W3DNEC_SUCCESS;
        pipeline = ctx->CreateShaderPipeline(&err, pipeTags);
        if (!pipeline || err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 8. Create render state object
    **--------------------------------------------------------------------*/
    printf("Creating render state... ");

    err = W3DNEC_SUCCESS;
    rso = ctx->CreateRenderStateObject(&err);
    if (!rso || err != W3DNEC_SUCCESS)
    {
        printf("FAILED (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 9. Set viewport and render target
    **--------------------------------------------------------------------*/
    printf("Setting viewport and render target... ");

    err = ctx->SetViewport(rso,
        0.0, 0.0,
        (double)innerW, (double)innerH,
        0.0, 1.0);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED setting viewport (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    err = ctx->SetRenderTarget(rso, fb);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED setting render target (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 10. Bind VBO and shader pipeline
    **--------------------------------------------------------------------*/
    printf("Binding VBO and shader pipeline... ");

    /* Bind VBO array 0 to vertex attribute 0 (position - layout(location=0)) */
    err = ctx->BindVertexAttribArray(rso, 0, vbo, 0);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED binding position (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Bind VBO array 1 to vertex attribute 1 (colour - layout(location=1)) */
    err = ctx->BindVertexAttribArray(rso, 1, vbo, 1);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED binding colour (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Set shader pipeline */
    err = ctx->SetShaderPipeline(rso, pipeline);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED setting pipeline (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }

    /* Disable depth testing for this simple test */
    ctx->SetState(rso, W3DN_DEPTHTEST, W3DN_DISABLE);
    ctx->SetState(rso, W3DN_DEPTHWRITE, W3DN_DISABLE);

    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 11. Clear to dark blue
    **--------------------------------------------------------------------*/
    printf("Clearing to dark blue... ");
    {
        /* RGBA clear colour */
        float clearColour[4] = { 0.0f, 0.0f, 0.2f, 1.0f };
        double clearDepth = 1.0;

        err = ctx->Clear(rso, clearColour, &clearDepth, NULL);
        if (err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 12. Draw triangle
    **--------------------------------------------------------------------*/
    printf("Drawing triangle (%d vertices)... ", VERTEX_COUNT);

    err = ctx->DrawArrays(rso, W3DN_PRIM_TRIANGLES, 0, VERTEX_COUNT);
    if (err != W3DNEC_SUCCESS)
    {
        printf("FAILED (%s)\n", w3dn_errstr(err));
        exitCode = 1;
        goto cleanup;
    }
    printf("OK\n");

    /*----------------------------------------------------------------------
    ** 13. Submit and wait for GPU
    **--------------------------------------------------------------------*/
    printf("Submitting... ");
    {
        err = W3DNEC_SUCCESS;
        uint32 submitID = ctx->Submit(&err);
        if (err != W3DNEC_SUCCESS)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
        printf("OK (submitID=%lu)\n", (unsigned long)submitID);

        printf("Waiting for GPU... ");
        err = ctx->WaitDone(submitID, 5000);
        if (err != W3DNEC_SUCCESS && err != W3DNEC_QUEUEEMPTY)
        {
            printf("FAILED (%s)\n", w3dn_errstr(err));
            exitCode = 1;
            goto cleanup;
        }
        printf("OK\n");
    }

    /*----------------------------------------------------------------------
    ** 14. Blit the back-buffer bitmap to the window
    **--------------------------------------------------------------------*/
    printf("Presenting... ");
    BltBitMapRastPort(
        backBM, 0, 0,
        window->RPort,
        window->BorderLeft, window->BorderTop,
        innerW, innerH,
        0xC0);  /* SRCCOPY */
    printf("OK\n");

    printf("Triangle rendered! Close window to exit.\n");

    /*----------------------------------------------------------------------
    ** 15. Wait for close gadget
    **--------------------------------------------------------------------*/
    {
        struct IntuiMessage *msg;
        BOOL running = TRUE;

        while (running)
        {
            WaitPort(window->UserPort);
            while ((msg = (struct IntuiMessage *)
                    GetMsg(window->UserPort)) != NULL)
            {
                if (msg->Class == IDCMP_CLOSEWINDOW)
                    running = FALSE;
                ReplyMsg((struct Message *)msg);
            }
        }
    }

    /*----------------------------------------------------------------------
    ** 16. Cleanup
    **--------------------------------------------------------------------*/
cleanup:
    printf("Cleaning up... ");

    if (rso)
        ctx->DestroyRenderStateObject(rso);
    if (pipeline)
        ctx->DestroyShaderPipeline(pipeline);
    if (fragShader)
        ctx->DestroyShader(fragShader);
    if (vertShader)
        ctx->DestroyShader(vertShader);
    if (vbo)
        ctx->DestroyVertexBufferObject(vbo);
    if (fb)
        ctx->DestroyFrameBuffer(fb);
    if (backBM)
        FreeBitMap(backBM);
    if (ctx)
        ctx->Destroy();
    if (window)
        CloseWindow(window);
    if (screen)
        UnlockPubScreen(NULL, screen);
    if (iw3d)
        DropInterface((struct Interface *)iw3d);
    if (W3DNovaBase)
        CloseLibrary(W3DNovaBase);

    printf("OK\n");

    if (exitCode == 0)
        printf("=== Test PASSED ===\n");
    else
        printf("=== Test FAILED ===\n");

    return exitCode;
}
