/*
** vkex_loop.h -- duration-bounded run-loop helpers for VulkanOS4 examples.
**
** Adds optional `-d N` / `--duration N` CLI flag support to every
** example program. With no flag, examples keep their original
** infinite-wait-until-close-gadget behaviour, so manual / Workbench
** launches are unaffected. With `-d N`, the example exits after N
** seconds, suitable for automated test harnesses.
**
** Two helpers are provided to match the two run-loop shapes used in
** the example tree:
**
**   1) Animated examples (busy-loop render):
**
**          int    duration = vkex_duration_secs(argc, argv);
**          time_t vkex_t0  = time(NULL);
**
**          while (running) {
**              if (vkex_expired(duration, vkex_t0))  running = FALSE;
**              if (CheckSignal(SIGBREAKF_CTRL_C))    running = FALSE;
**              // existing IDCMP drain + render
**          }
**
**   2) Render-once-then-wait examples (WaitPort):
**
**          int duration = vkex_duration_secs(argc, argv);
**          // ...render single frame...
**          vkex_wait_close_or_timer(window, duration);
**
** No external library, no per-example state, no Makefile changes:
** every example just adds `#include "../common/vkex_loop.h"`.
**
** Also handles SIGBREAKF_CTRL_C from a Shell `Break <CLI>` -- the
** animated helper checks it explicitly; the WaitPort helper waits on
** the signal as well as the timer + window port.
*/

#ifndef VKEX_LOOP_H
#define VKEX_LOOP_H

#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*--------------------------------------------------------------------*/
/* CLI parsing                                                        */
/*--------------------------------------------------------------------*/

/* Parse `-d N` or `--duration N` from argv. Returns N (positive) when
** the flag is present and valid; returns 0 otherwise, meaning "no
** duration limit -- keep the example's original behaviour". */
static int vkex_duration_secs(int argc, char **argv) __attribute__((unused));
static int vkex_duration_secs(int argc, char **argv)
{
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--duration") == 0)
        {
            int v = atoi(argv[i + 1]);
            return v > 0 ? v : 0;
        }
    }
    return 0;
}

/*--------------------------------------------------------------------*/
/* Time helpers                                                       */
/*--------------------------------------------------------------------*/

/* Return 1 if `duration_s` wall-clock seconds have elapsed since
** `t0` (from time(NULL)). Returns 0 if `duration_s` is 0 or
** negative -- "infinite, never expires". */
static int vkex_expired(int duration_s, time_t t0) __attribute__((unused));
static int vkex_expired(int duration_s, time_t t0)
{
    if (duration_s <= 0) return 0;
    return (time(NULL) - t0) >= duration_s;
}

/*--------------------------------------------------------------------*/
/* WaitPort replacement: wait for close gadget OR duration timeout    */
/* OR Shell CTRL-C, whichever comes first.                            */
/*                                                                    */
/* Behaviour:                                                         */
/*   duration_s == 0  -- block on window port forever (no timer)      */
/*   duration_s  > 0  -- block on (window port | timer | CTRL-C),     */
/*                       wake on any of them and exit                 */
/*                                                                    */
/* The function drains IDCMP messages it consumes; the caller doesn't */
/* need to do anything before/after.                                  */
/*--------------------------------------------------------------------*/
static void vkex_wait_close_or_timer(struct Window *win, int duration_s) __attribute__((unused));
static void vkex_wait_close_or_timer(struct Window *win, int duration_s)
{
    if (!win) return;

    /* No-duration path: original WaitPort + drain loop. */
    if (duration_s <= 0) {
        BOOL done = FALSE;
        while (!done) {
            WaitPort(win->UserPort);
            struct IntuiMessage *msg;
            while ((msg = (struct IntuiMessage *)
                    GetMsg(win->UserPort)) != NULL) {
                if (msg->Class == IDCMP_CLOSEWINDOW) done = TRUE;
                ReplyMsg((struct Message *)msg);
            }
        }
        return;
    }

    /* Duration path: open timer.device and Wait on multiple signals. */
    struct MsgPort *timerPort = (struct MsgPort *)
        AllocSysObjectTags(ASOT_PORT, TAG_END);
    struct TimeRequest *timerReq = NULL;
    if (timerPort) {
        timerReq = (struct TimeRequest *)AllocSysObjectTags(
            ASOT_IOREQUEST,
            ASOIOR_ReplyPort, (uint32)(uintptr_t)timerPort,
            ASOIOR_Size,      sizeof(struct TimeRequest),
            TAG_END);
    }
    BOOL timerOk = FALSE;
    if (timerReq) {
        if (OpenDevice((CONST_STRPTR)"timer.device",
                       UNIT_VBLANK,
                       (struct IORequest *)timerReq, 0) == 0) {
            timerOk = TRUE;
        } else {
            FreeSysObject(ASOT_IOREQUEST, timerReq);
            timerReq = NULL;
        }
    }

    if (!timerOk) {
        /* Last-resort fallback: just Delay() and exit. Loses the
        ** "exit early on CLOSEWINDOW" property but the example still
        ** terminates within `duration_s` seconds. */
        if (timerPort) FreeSysObject(ASOT_PORT, timerPort);
        Delay((uint32)duration_s * 50);
        return;
    }

    /* Fire one-shot timer request. */
    timerReq->Request.io_Command = TR_ADDREQUEST;
    timerReq->Time.Seconds       = (uint32)duration_s;
    timerReq->Time.Microseconds  = 0;
    SendIO((struct IORequest *)timerReq);

    /* Multi-signal Wait. */
    uint32 winSig   = 1UL << win->UserPort->mp_SigBit;
    uint32 timerSig = 1UL << timerPort->mp_SigBit;
    BOOL done = FALSE;
    while (!done) {
        uint32 sigs = Wait(winSig | timerSig | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = TRUE;
        if (sigs & timerSig)         done = TRUE;
        if (sigs & winSig) {
            struct IntuiMessage *msg;
            while ((msg = (struct IntuiMessage *)
                    GetMsg(win->UserPort)) != NULL) {
                if (msg->Class == IDCMP_CLOSEWINDOW) done = TRUE;
                ReplyMsg((struct Message *)msg);
            }
        }
    }

    /* Cancel any still-pending timer request, drain, close, free. */
    if (!CheckIO((struct IORequest *)timerReq)) {
        AbortIO((struct IORequest *)timerReq);
        WaitIO((struct IORequest *)timerReq);
    } else {
        WaitIO((struct IORequest *)timerReq);
    }
    CloseDevice((struct IORequest *)timerReq);
    FreeSysObject(ASOT_IOREQUEST, timerReq);
    FreeSysObject(ASOT_PORT, timerPort);
}

#endif /* VKEX_LOOP_H */
