/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for upipe_worker_sink (using upump_ev)
 */

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_worker_sink.h>
#include <upipe-modules/upipe_transfer.h>
#include <upipe-modules/upipe_null.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define XFER_QUEUE 255
#define XFER_POOL 1
#define WSINK_QUEUE 1024

static struct uprobe *logger;
static bool transferred = false;
static unsigned int nb_packets = 0;

/** helper phony pipe */
struct test_pipe {
    struct urefcount urefcount;
    struct upipe upipe;
};

/** helper phony pipe */
static void test_free(struct urefcount *urefcount)
{
    struct test_pipe *test_pipe =
        container_of(urefcount, struct test_pipe, urefcount);
    upipe_dbg(&test_pipe->upipe, "dead");
    urefcount_clean(&test_pipe->urefcount);
    upipe_clean(&test_pipe->upipe);
    free(test_pipe);
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr,
                                struct uprobe *uprobe, uint32_t signature,
                                va_list args)
{
    struct test_pipe *test_pipe = malloc(sizeof(struct test_pipe));
    assert(test_pipe != NULL);
    upipe_init(&test_pipe->upipe, mgr, uprobe);
    urefcount_init(&test_pipe->urefcount, test_free);
    test_pipe->upipe.refcount = &test_pipe->urefcount;
    return &test_pipe->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    upipe_dbg(upipe, "input");
    uref_free(uref);
    nb_packets--;
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_dbg(upipe, "attached");
            transferred = true;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            upipe_dbg(upipe, "flow_def set");
            return UBASE_ERR_NONE;
        }
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

static void *thread(void *_upipe_xfer_mgr)
{
    struct upipe_mgr *upipe_xfer_mgr = (struct upipe_mgr *)_upipe_xfer_mgr;

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);

    ubase_assert(upipe_xfer_mgr_attach(upipe_xfer_mgr, upump_mgr));
    upipe_mgr_release(upipe_xfer_mgr);

    ev_loop(loop, 0);

    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_VERBOSE);
    assert(logger != NULL);
    logger = uprobe_pthread_upump_mgr_alloc(logger);
    uprobe_pthread_upump_mgr_set(logger, upump_mgr);
    assert(logger != NULL);

    struct upipe *upipe_test = upipe_void_alloc(&test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "test"));
    assert(upipe_test != NULL);

    struct upipe_mgr *upipe_xfer_mgr =
        upipe_xfer_mgr_alloc(XFER_QUEUE, XFER_POOL);
    assert(upipe_xfer_mgr != NULL);

    pthread_t id;
    upipe_mgr_use(upipe_xfer_mgr);
    assert(pthread_create(&id, NULL, thread, upipe_xfer_mgr) == 0);

    struct upipe_mgr *upipe_wsink_mgr = upipe_wsink_mgr_alloc(upipe_xfer_mgr);
    assert(upipe_wsink_mgr != NULL);
    upipe_mgr_release(upipe_xfer_mgr);

    struct upipe *upipe_handle = upipe_wsink_alloc(upipe_wsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "wsink"),
            upipe_test,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_VERBOSE, "wsink_x"),
            WSINK_QUEUE);
    /* from now on upipe_test shouldn't be accessed from this thread */
    assert(upipe_handle != NULL);
    upipe_mgr_release(upipe_wsink_mgr);

    struct uref *uref = uref_alloc(uref_mgr);
    ubase_assert(uref_flow_set_def(uref, "void."));
    ubase_assert(upipe_set_flow_def(upipe_handle, uref));
    uref_flow_delete_def(uref);
    nb_packets++;
    upipe_input(upipe_handle, uref, NULL);
    upipe_release(upipe_handle);

    ev_loop(loop, 0);

    uprobe_err(logger, NULL, "joining");
    assert(!pthread_join(id, NULL));
    uprobe_err(logger, NULL, "joined");
    assert(transferred);
    assert(!nb_packets);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);

    ev_default_destroy();
    return 0;
}
