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
 * @short Upipe module dropping urefs not matching certain values for
 * int attributes
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_match_attr.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

enum upipe_match_attr_type {
    UPIPE_MATCH_ATTR_NONE,
    UPIPE_MATCH_ATTR_UINT8_T,
    UPIPE_MATCH_ATTR_UINT64_T,
};

/** @internal @This is the private context of a match_attr pipe. */
struct upipe_match_attr {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** match uint8_t */
    bool (*match_uint8_t) (struct uref*, uint8_t, uint8_t);
    /** match uint64_t */
    bool (*match_uint64_t) (struct uref*, uint64_t, uint64_t);
    /** mode */
    enum upipe_match_attr_type mode;
    /** min */
    uint64_t min;
    /** max */
    uint64_t max;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_match_attr, upipe)
UPIPE_HELPER_FLOW(upipe_match_attr, NULL)
UPIPE_HELPER_OUTPUT(upipe_match_attr, output, flow_def, flow_def_sent)

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_match_attr_input(struct upipe *upipe, struct uref *uref,
                                   struct upump *upump)
{
    struct upipe_match_attr *upipe_match_attr = upipe_match_attr_from_upipe(upipe);
    bool forward = true;
    uint64_t min = upipe_match_attr->min;
    uint64_t max = upipe_match_attr->max;

    /* check uref */
    switch (upipe_match_attr->mode) {
        case UPIPE_MATCH_ATTR_UINT8_T: {
            if (likely(upipe_match_attr->match_uint8_t)) {
                forward = upipe_match_attr->match_uint8_t(uref, min, max);
            }
            break;
        }
        case UPIPE_MATCH_ATTR_UINT64_T: {
            if (likely(upipe_match_attr->match_uint64_t)) {
                forward = upipe_match_attr->match_uint64_t(uref, min, max);
            }
            break;
        }
        case UPIPE_MATCH_ATTR_NONE:
        default:
            break;
    }

    if (forward) {
        upipe_match_attr_output(upipe, uref, upump);
    } else {
        uref_free(uref);
    }
}

/** @internal @This processes control commands on a match_attr pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_match_attr_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    struct upipe_match_attr *upipe_match_attr = upipe_match_attr_from_upipe(upipe);
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_match_attr_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_match_attr_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_match_attr_set_output(upipe, output);
        }

        case UPIPE_MATCH_ATTR_SET_UINT8_T: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_MATCH_ATTR_SIGNATURE);
            upipe_match_attr->match_uint8_t = va_arg(args,
                      bool (*)(struct uref*, uint8_t, uint8_t));
            upipe_match_attr->mode = UPIPE_MATCH_ATTR_UINT8_T;
            return true;
        }
        case UPIPE_MATCH_ATTR_SET_UINT64_T: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_MATCH_ATTR_SIGNATURE);
            upipe_match_attr->match_uint64_t = va_arg(args,
                    bool (*)(struct uref*, uint64_t, uint64_t));
            upipe_match_attr->mode = UPIPE_MATCH_ATTR_UINT64_T;
            return true;
        }
        case UPIPE_MATCH_ATTR_SET_BOUNDARIES: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_MATCH_ATTR_SIGNATURE);
            upipe_match_attr->min = va_arg(args, uint64_t);
            upipe_match_attr->max = va_arg(args, uint64_t);
            return true;
        }
        default:
            return false;
    }
}

/** @internal @This allocates a match_attr pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_match_attr_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_match_attr_alloc_flow(mgr, uprobe, signature,
                                                      args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_match_attr *upipe_match_attr =
        upipe_match_attr_from_upipe(upipe);
    upipe_match_attr_init_output(upipe);
    upipe_match_attr->match_uint8_t = NULL;
    upipe_match_attr->match_uint64_t = NULL;
    upipe_match_attr->mode = UPIPE_MATCH_ATTR_NONE;
    upipe_match_attr_store_flow_def(upipe, flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_match_attr_free(struct upipe *upipe)
{
    struct upipe_match_attr *upipe_match_attr = upipe_match_attr_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_match_attr_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_match_attr);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_match_attr_mgr = {
    .signature = UPIPE_MATCH_ATTR_SIGNATURE,

    .upipe_alloc = upipe_match_attr_alloc,
    .upipe_input = upipe_match_attr_input,
    .upipe_control = upipe_match_attr_control,
    .upipe_free = upipe_match_attr_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all match_attr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_match_attr_mgr_alloc(void)
{
    return &upipe_match_attr_mgr;
}