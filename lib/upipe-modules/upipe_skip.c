/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module - skip
 * Skip arbitrary length of data in blocks.
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_skip.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#define EXPECTED_FLOW "block."

/** upipe_skip structure */ 
struct upipe_skip {
    /** skip offset */
    size_t offset;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_skip, upipe);
UPIPE_HELPER_OUTPUT(upipe_skip, output, flow_def, flow_def_sent);

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static inline void upipe_skip_input_block(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);

    // skip given length
    uref_block_resize(uref, upipe_skip->offset, -1);

    upipe_skip_output(upipe, uref, upump);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_skip_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
    const char *def;

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW))) {
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        upipe_dbg_va(upipe, "flow definition %s", def);
        if (unlikely(!uref_flow_set_def(uref, "block.")))
            upipe_throw_aerror(upipe);
        upipe_skip_store_flow_def(upipe, uref);
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_skip->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_skip_input_block(upipe, uref, upump);
}

/** @internal @This processes control commands on a skip pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_skip_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_skip_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_skip_set_output(upipe, output);
        }
        case UPIPE_SKIP_SET_OFFSET: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_SKIP_SIGNATURE);
            struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
            upipe_skip->offset = va_arg(args, size_t);
            return true;
        }
        case UPIPE_SKIP_GET_OFFSET: {
            int signature = va_arg(args, int);
            assert(signature == UPIPE_SKIP_SIGNATURE);
            struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
            size_t *offset_p = va_arg(args, size_t *);
            if (unlikely(!offset_p)) {
                return false;
            }
            upipe_skip->offset = *offset_p;
            return true;
        }
        default:
            return false;
    }
}

/** @internal @This allocates a skip pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_skip_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_skip *upipe_skip = malloc(sizeof(struct upipe_skip));
    if (unlikely(upipe_skip == NULL))
        return NULL;
    struct upipe *upipe = upipe_skip_to_upipe(upipe_skip);
    upipe_init(upipe, mgr, uprobe);
    upipe_skip_init_output(upipe);

    upipe_skip->offset = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_skip_free(struct upipe *upipe)
{
    struct upipe_skip *upipe_skip = upipe_skip_from_upipe(upipe);
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    upipe_skip_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_skip);
}

static struct upipe_mgr upipe_skip_mgr = {
    .signature = UPIPE_SKIP_SIGNATURE,

    .upipe_alloc = upipe_skip_alloc,
    .upipe_input = upipe_skip_input,
    .upipe_control = upipe_skip_control,
    .upipe_free = upipe_skip_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for skip pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_skip_mgr_alloc(void)
{
    return &upipe_skip_mgr;
}