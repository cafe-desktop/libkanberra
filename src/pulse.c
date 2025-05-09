/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
  This file is part of libkanberra.

  Copyright 2008 Lennart Poettering

  libkanberra is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 2.1 of the
  License, or (at your option) any later version.

  libkanberra is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with libkanberra. If not, see
  <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* The locking order needs to be strictly followed! First take the
 * mainloop mutex, only then take outstanding_mutex if you need both!
 * Not the other way round, beacause we might then enter a
 * deadlock!  */

#include <errno.h>
#include <stdlib.h>

#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/scache.h>
#include <pulse/subscribe.h>
#include <pulse/introspect.h>

#include "kanberra.h"
#include "common.h"
#include "driver.h"
#include "llist.h"
#include "read-sound-file.h"
#include "sound-theme-spec.h"
#include "malloc.h"

enum outstanding_type {
        OUTSTANDING_SAMPLE,
        OUTSTANDING_STREAM,
        OUTSTANDING_UPLOAD
};

struct outstanding {
        KA_LLIST_FIELDS(struct outstanding);
        enum outstanding_type type;
        ka_context *context;
        uint32_t id;
        uint32_t sink_input;
        pa_stream *stream;
        pa_operation *drain_operation;
        ka_finish_callback_t callback;
        void *userdata;
        ka_sound_file *file;
        int error;
        unsigned clean_up:1; /* Handler needs to clean up the outstanding struct */
        unsigned finished:1; /* finished playing */
};

struct private {
        pa_threaded_mainloop *mainloop;
        pa_context *context;
        ka_theme_data *theme;
        ka_bool_t subscribed;
        ka_bool_t reconnect;

        ka_mutex *outstanding_mutex;
        KA_LLIST_HEAD(struct outstanding, outstanding);
};

#define PRIVATE(c) ((struct private *) ((c)->private))

static void context_state_cb(pa_context *pc, void *userdata);
static void context_subscribe_cb(pa_context *pc, pa_subscription_event_type_t t, uint32_t idx, void *userdata);

static void outstanding_disconnect(struct outstanding *o) {
        ka_assert(o);

        if (o->stream) {
                if (o->drain_operation) {
                        pa_operation_cancel(o->drain_operation);
                        pa_operation_unref(o->drain_operation);
                        o->drain_operation = NULL;
                }

                pa_stream_set_write_callback(o->stream, NULL, NULL);
                pa_stream_set_state_callback(o->stream, NULL, NULL);
                pa_stream_disconnect(o->stream);
                pa_stream_unref(o->stream);
                o->stream = NULL;
        }
}

static void outstanding_free(struct outstanding *o) {
        ka_assert(o);

        outstanding_disconnect(o);

        if (o->file)
                ka_sound_file_close(o->file);

        ka_free(o);
}

static int convert_proplist(pa_proplist **_l, ka_proplist *c) {
        pa_proplist *l;
        ka_prop *i;

        ka_return_val_if_fail(_l, KA_ERROR_INVALID);
        ka_return_val_if_fail(c, KA_ERROR_INVALID);

        if (!(l = pa_proplist_new()))
                return KA_ERROR_OOM;

        ka_mutex_lock(c->mutex);

        for (i = c->first_item; i; i = i->next_item)
                if (pa_proplist_set(l, i->key, KA_PROP_DATA(i), i->nbytes) < 0) {
                        ka_mutex_unlock(c->mutex);
                        pa_proplist_free(l);
                        return KA_ERROR_INVALID;
                }

        ka_mutex_unlock(c->mutex);

        *_l = l;

        return KA_SUCCESS;
}

static pa_proplist *strip_prefix(pa_proplist *l, const char *prefix) {
        const char *key;
        void *state = NULL;
        ka_assert(l);

        while ((key = pa_proplist_iterate(l, &state)))
                if (strncmp(key, prefix, strlen(prefix)) == 0)
                        pa_proplist_unset(l, key);

        return l;
}

static void add_common(pa_proplist *l) {
        ka_assert(l);

        if (!pa_proplist_contains(l, KA_PROP_MEDIA_ROLE))
                pa_proplist_sets(l, KA_PROP_MEDIA_ROLE, "event");

        if (!pa_proplist_contains(l, KA_PROP_MEDIA_NAME)) {
                const char *t;

                if ((t = pa_proplist_gets(l, KA_PROP_EVENT_ID)))
                        pa_proplist_sets(l, KA_PROP_MEDIA_NAME, t);
                else if ((t = pa_proplist_gets(l, KA_PROP_MEDIA_FILENAME)))
                        pa_proplist_sets(l, KA_PROP_MEDIA_NAME, t);
                else
                        pa_proplist_sets(l, KA_PROP_MEDIA_NAME, "libkanberra");
        }
}

static int translate_error(int error) {
        static const int table[PA_ERR_MAX] = {
                [PA_OK]                       = KA_SUCCESS,
                [PA_ERR_ACCESS]               = KA_ERROR_ACCESS,
                [PA_ERR_COMMAND]              = KA_ERROR_IO,
                [PA_ERR_INVALID]              = KA_ERROR_INVALID,
                [PA_ERR_EXIST]                = KA_ERROR_IO,
                [PA_ERR_NOENTITY]             = KA_ERROR_NOTFOUND,
                [PA_ERR_CONNECTIONREFUSED]    = KA_ERROR_NOTAVAILABLE,
                [PA_ERR_PROTOCOL]             = KA_ERROR_IO,
                [PA_ERR_TIMEOUT]              = KA_ERROR_IO,
                [PA_ERR_AUTHKEY]              = KA_ERROR_ACCESS,
                [PA_ERR_INTERNAL]             = KA_ERROR_IO,
                [PA_ERR_CONNECTIONTERMINATED] = KA_ERROR_IO,
                [PA_ERR_KILLED]               = KA_ERROR_DESTROYED,
                [PA_ERR_INVALIDSERVER]        = KA_ERROR_INVALID,
                [PA_ERR_MODINITFAILED]        = KA_ERROR_NODRIVER,
                [PA_ERR_BADSTATE]             = KA_ERROR_STATE,
                [PA_ERR_NODATA]               = KA_ERROR_IO,
                [PA_ERR_VERSION]              = KA_ERROR_NOTSUPPORTED,
                [PA_ERR_TOOLARGE]             = KA_ERROR_TOOBIG,
#ifdef PA_ERR_NOTSUPPORTED
                [PA_ERR_NOTSUPPORTED]         = KA_ERROR_NOTSUPPORTED,
#endif
#ifdef PA_ERR_UNKNOWN
                [PA_ERR_UNKNOWN]              = KA_ERROR_IO,
#endif
#ifdef PA_ERR_NOEXTENSION
                [PA_ERR_NOEXTENSION]          = KA_ERROR_NOTSUPPORTED,
#endif
#ifdef PA_ERR_OBSOLETE
                [PA_ERR_OBSOLETE]             = KA_ERROR_NOTSUPPORTED,
#endif
#ifdef PA_ERR_NOTIMPLEMENTED
                [PA_ERR_NOTIMPLEMENTED]       = KA_ERROR_NOTSUPPORTED
#endif
        };

        ka_assert(error >= 0);

        if (error >= PA_ERR_MAX || !table[error])
                return KA_ERROR_IO;

        return table[error];
}

static int context_connect(ka_context *c, ka_bool_t nofail) {
        pa_proplist *l;
        struct private *p;
        int ret;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(p = c->private, KA_ERROR_STATE);
        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);
        ka_return_val_if_fail(!p->context, KA_ERROR_STATE);

        /* If this immediate attempt fails, don't try to reconnect. */
        p->reconnect = FALSE;

        if ((ret = convert_proplist(&l, c->props)) < 0)
                return ret;

        strip_prefix(l, "kanberra.");

        if (!pa_proplist_contains(l, PA_PROP_APPLICATION_NAME)) {
                pa_proplist_sets(l, PA_PROP_APPLICATION_NAME, "libkanberra");
                pa_proplist_sets(l, PA_PROP_APPLICATION_VERSION, PACKAGE_VERSION);

                if (!pa_proplist_contains(l, PA_PROP_APPLICATION_ID))
                        pa_proplist_sets(l, PA_PROP_APPLICATION_ID, "org.freedesktop.libkanberra");

        }

        if (!(p->context = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(p->mainloop), NULL, l))) {
                pa_proplist_free(l);
                return KA_ERROR_OOM;
        }

        pa_proplist_free(l);

        pa_context_set_state_callback(p->context, context_state_cb, c);
        pa_context_set_subscribe_callback(p->context, context_subscribe_cb, c);

        if (pa_context_connect(p->context, NULL, nofail ? PA_CONTEXT_NOFAIL : 0, NULL) < 0) {
                ret = translate_error(p->context ? pa_context_errno(p->context) : PA_ERR_CONNECTIONREFUSED);

                if (p->context) {
                        pa_context_disconnect(p->context);
                        pa_context_unref(p->context);
                        p->context = NULL;
                }

                return ret;
        }

        return KA_SUCCESS;
}

static void context_state_cb(pa_context *pc, void *userdata) {
        ka_context *c = userdata;
        pa_context_state_t state;
        struct private *p;

        ka_assert(pc);
        ka_assert(c);

        p = PRIVATE(c);

        state = pa_context_get_state(pc);
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
                struct outstanding *out;
                int ret;

                if (state == PA_CONTEXT_TERMINATED)
                        ret = KA_ERROR_DESTROYED;
                else
                        ret = translate_error(pa_context_errno(pc));

                ka_mutex_lock(p->outstanding_mutex);

                while ((out = p->outstanding)) {

                        outstanding_disconnect(out);
                        KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);

                        ka_mutex_unlock(p->outstanding_mutex);

                        if (out->callback)
                                out->callback(c, out->id, ret, out->userdata);

                        outstanding_free(out);

                        ka_mutex_lock(p->outstanding_mutex);
                }

                ka_mutex_unlock(p->outstanding_mutex);

                if (state == PA_CONTEXT_FAILED && p->reconnect) {

                        if (p->context) {
                                pa_context_disconnect(p->context);
                                pa_context_unref(p->context);
                                p->context = NULL;
                        }

                        p->subscribed = FALSE;

                        /* If we managed to connect once, then let's try to
                         * reconnect, and pass NOFAIL */
                        context_connect(c, TRUE);
                }

        } else if (state == PA_CONTEXT_READY)
                /* OK, the connection suceeded once, if it dies now try to
                 * reconnect */
                p->reconnect = TRUE;

        pa_threaded_mainloop_signal(p->mainloop, FALSE);
}

static void context_subscribe_cb(pa_context *pc, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
        struct outstanding *out, *n;
        KA_LLIST_HEAD(struct outstanding, l);
        ka_context *c = userdata;
        struct private *p;

        ka_assert(pc);
        ka_assert(c);

        if (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_REMOVE))
                return;

        p = PRIVATE(c);

        KA_LLIST_HEAD_INIT(struct outstanding, l);

        ka_mutex_lock(p->outstanding_mutex);

        for (out = p->outstanding; out; out = n) {
                n = out->next;

                if (!out->clean_up || out->type != OUTSTANDING_SAMPLE || out->sink_input != idx)
                        continue;

                outstanding_disconnect(out);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);

                KA_LLIST_PREPEND(struct outstanding, l, out);
        }

        ka_mutex_unlock(p->outstanding_mutex);

        while (l) {
                out = l;

                KA_LLIST_REMOVE(struct outstanding, l, out);

                if (out->callback)
                        out->callback(c, out->id, KA_SUCCESS, out->userdata);

                outstanding_free(out);
        }
}

int driver_open(ka_context *c) {
        struct private *p;
        int ret;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(!c->driver || ka_streq(c->driver, "pulse"), KA_ERROR_NODRIVER);
        ka_return_val_if_fail(!PRIVATE(c), KA_ERROR_STATE);

        if (!(c->private = p = ka_new0(struct private, 1)))
                return KA_ERROR_OOM;

        if (!(p->outstanding_mutex = ka_mutex_new())) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        if (!(p->mainloop = pa_threaded_mainloop_new())) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        /* The initial connection is without NOFAIL, since we want to have
         * this call fail cleanly if we cannot connect. */
        if ((ret = context_connect(c, FALSE)) != KA_SUCCESS) {
                driver_destroy(c);
                return ret;
        }

        pa_threaded_mainloop_lock(p->mainloop);

        if (pa_threaded_mainloop_start(p->mainloop) < 0) {
                pa_threaded_mainloop_unlock(p->mainloop);
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        for (;;) {
                pa_context_state_t state;

                if (!p->context) {
                        ret = translate_error(PA_ERR_CONNECTIONREFUSED);
                        pa_threaded_mainloop_unlock(p->mainloop);
                        driver_destroy(c);
                        return ret;
                }

                state = pa_context_get_state(p->context);

                if (state == PA_CONTEXT_READY)
                        break;

                if (state == PA_CONTEXT_FAILED) {
                        ret = translate_error(pa_context_errno(p->context));
                        pa_threaded_mainloop_unlock(p->mainloop);
                        driver_destroy(c);
                        return ret;
                }

                ka_assert(state != PA_CONTEXT_TERMINATED);

                pa_threaded_mainloop_wait(p->mainloop);
        }

        pa_threaded_mainloop_unlock(p->mainloop);

        return KA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        struct private *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        if (p->mainloop)
                pa_threaded_mainloop_stop(p->mainloop);

        if (p->context) {
                pa_context_disconnect(p->context);
                pa_context_unref(p->context);
        }

        while (p->outstanding) {
                struct outstanding *out = p->outstanding;
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);

                if (out->callback)
                        out->callback(c, out->id, KA_ERROR_DESTROYED, out->userdata);

                outstanding_free(out);
        }

        if (p->mainloop)
                pa_threaded_mainloop_free(p->mainloop);

        if (p->theme)
                ka_theme_data_free(p->theme);

        if (p->outstanding_mutex)
                ka_mutex_free(p->outstanding_mutex);

        ka_free(p);

        c->private = NULL;

        return KA_SUCCESS;
}

int driver_change_device (ka_context *c,
			  const char *device GNUC_UNUSED)
{
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        /* We're happy with any device change. We might however add code
         * here eventually to move all currently played back event sounds
         * to the new device. */

        return KA_SUCCESS;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        struct private *p;
        pa_operation *o;
        pa_proplist *l;
        int ret = KA_SUCCESS;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(changed, KA_ERROR_INVALID);
        ka_return_val_if_fail(merged, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);

        pa_threaded_mainloop_lock(p->mainloop);

        if (!p->context) {
                pa_threaded_mainloop_unlock(p->mainloop);
                return KA_ERROR_STATE; /* can be silently ignored */
        }

        if ((ret = convert_proplist(&l, changed)) < 0)
                return ret;

        strip_prefix(l, "kanberra.");

        /* We start these asynchronously and don't care about the return
         * value */

        if (!(o = pa_context_proplist_update(p->context, PA_UPDATE_REPLACE, l, NULL, NULL)))
                ret = translate_error(pa_context_errno(p->context));
        else
                pa_operation_unref(o);

        pa_threaded_mainloop_unlock(p->mainloop);

        pa_proplist_free(l);

        return ret;
}

static int subscribe(ka_context *c) {
        struct private *p;
        pa_operation *o;
        int ret = KA_SUCCESS;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);
        p = PRIVATE(c);

        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);

        if (p->subscribed)
                return KA_SUCCESS;

        pa_threaded_mainloop_lock(p->mainloop);

        if (!p->context) {
                pa_threaded_mainloop_unlock(p->mainloop);
                return KA_ERROR_STATE;
        }

        /* We start these asynchronously and don't care about the return
         * value */

        if (!(o = pa_context_subscribe(p->context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL)))
                ret = translate_error(pa_context_errno(p->context));
        else
                pa_operation_unref(o);

        pa_threaded_mainloop_unlock(p->mainloop);

        p->subscribed = TRUE;

        return ret;
}

static void play_sample_cb(pa_context *c, uint32_t idx, void *userdata) {
        struct private *p;
        struct outstanding *out = userdata;

        ka_assert(c);
        ka_assert(out);

        p = PRIVATE(out->context);

        if (idx != PA_INVALID_INDEX) {
                out->error = KA_SUCCESS;
                out->sink_input = idx;
        } else
                out->error = translate_error(pa_context_errno(c));

        pa_threaded_mainloop_signal(p->mainloop, FALSE);
}

static void stream_state_cb(pa_stream *s, void *userdata) {
        struct private *p;
        struct outstanding *out = userdata;
        pa_stream_state_t state;

        ka_assert(s);
        ka_assert(out);

        p = PRIVATE(out->context);

        state = pa_stream_get_state(s);

        switch (state) {
        case PA_STREAM_CREATING:
        case PA_STREAM_UNCONNECTED:
                break;

        case PA_STREAM_READY:
                out->sink_input = pa_stream_get_index(out->stream);
                break;

        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED: {
                int err;

                err = state == PA_STREAM_FAILED ? translate_error(pa_context_errno(pa_stream_get_context(s))) : KA_ERROR_DESTROYED;

                if (out->clean_up) {
                        ka_mutex_lock(p->outstanding_mutex);
                        outstanding_disconnect(out);
                        KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                        ka_mutex_unlock(p->outstanding_mutex);

                        if (out->callback)
                                out->callback(out->context, out->id, out->error, out->userdata);

                        outstanding_free(out);
                } else {
                        out->finished = TRUE;
                        out->error = err;
                }

                break;
        }
        }

        pa_threaded_mainloop_signal(p->mainloop, FALSE);
}

static void stream_drain_cb(pa_stream *s, int success, void *userdata) {
        struct private *p;
        struct outstanding *out = userdata;
        int err;

        ka_assert(s);
        ka_assert(out);
        ka_assert(out->type == OUTSTANDING_STREAM);

        p = PRIVATE(out->context);
        err = success ? KA_SUCCESS : translate_error(pa_context_errno(p->context));

        if (out->clean_up) {
                ka_mutex_lock(p->outstanding_mutex);
                outstanding_disconnect(out);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                ka_mutex_unlock(p->outstanding_mutex);

                if (out->callback)
                        out->callback(out->context, out->id, err, out->userdata);

                outstanding_free(out);

        } else {
                pa_stream_disconnect(s);
                out->error = err;
                out->finished = TRUE;

                if (out->drain_operation) {
                        pa_operation_unref(out->drain_operation);
                        out->drain_operation = NULL;
                }
        }

        pa_threaded_mainloop_signal(p->mainloop, FALSE);
}

static void stream_write_cb(pa_stream *s, size_t bytes, void *userdata) {
        struct outstanding *out = userdata;
        struct private *p;
        void *data;
        int ret;
        ka_bool_t eof = FALSE;

        ka_assert(s);
        ka_assert(bytes > 0);
        ka_assert(out);

        p = PRIVATE(out->context);

        while (bytes > 0) {
                size_t rbytes = bytes;

                if (!(data = ka_malloc(rbytes))) {
                        ret = KA_ERROR_OOM;
                        goto finish;
                }

                if ((ret = ka_sound_file_read_arbitrary(out->file, data, &rbytes)) < 0)
                        goto finish;

                if (rbytes <= 0) {
                        eof = TRUE;
                        break;
                }

                ka_assert(rbytes <= bytes);

                if ((ret = pa_stream_write(s, data, rbytes, ka_free, 0, PA_SEEK_RELATIVE)) < 0) {
                        ret = translate_error(ret);
                        goto finish;
                }

                data = NULL;

                bytes -= rbytes;
        }

        if (eof || ka_sound_file_get_size(out->file) <= 0) {

                /* We reached EOF */

                if (out->type == OUTSTANDING_UPLOAD) {

                        if (pa_stream_finish_upload(s) < 0) {
                                ret = translate_error(pa_context_errno(p->context));
                                goto finish;
                        }

                        /* Let's just signal driver_cache() which has been waiting for us */
                        pa_threaded_mainloop_signal(p->mainloop, FALSE);

                } else {
                        ka_assert(out->type == OUTSTANDING_STREAM);

                        if (out->drain_operation) {
                                pa_operation_cancel(out->drain_operation);
                                pa_operation_unref(out->drain_operation);
                        }

                        if (!(out->drain_operation = pa_stream_drain(s, stream_drain_cb, out))) {
                                ret = translate_error(pa_context_errno(p->context));
                                goto finish;
                        }
                }

                pa_stream_set_write_callback(s, NULL, NULL);
        }

        ka_free(data);

        return;

finish:

        ka_free(data);

        if (out->clean_up) {
                ka_mutex_lock(p->outstanding_mutex);
                outstanding_disconnect(out);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                ka_mutex_unlock(p->outstanding_mutex);

                if (out->callback)
                        out->callback(out->context, out->id, ret, out->userdata);

                outstanding_free(out);

        } else {
                pa_stream_disconnect(s);
                out->error = ret;
                out->finished = TRUE;
        }

        pa_threaded_mainloop_signal(p->mainloop, FALSE);
}

static const pa_sample_format_t sample_type_table[] = {
        [KA_SAMPLE_S16NE] = PA_SAMPLE_S16NE,
        [KA_SAMPLE_S16RE] = PA_SAMPLE_S16RE,
        [KA_SAMPLE_U8] = PA_SAMPLE_U8
};

static const pa_channel_position_t channel_table[_KA_CHANNEL_POSITION_MAX] = {
        [KA_CHANNEL_MONO] = PA_CHANNEL_POSITION_MONO,
        [KA_CHANNEL_FRONT_LEFT] = PA_CHANNEL_POSITION_FRONT_LEFT,
        [KA_CHANNEL_FRONT_RIGHT] = PA_CHANNEL_POSITION_FRONT_RIGHT,
        [KA_CHANNEL_FRONT_CENTER] = PA_CHANNEL_POSITION_FRONT_CENTER,
        [KA_CHANNEL_REAR_LEFT] = PA_CHANNEL_POSITION_REAR_LEFT,
        [KA_CHANNEL_REAR_RIGHT] = PA_CHANNEL_POSITION_REAR_RIGHT,
        [KA_CHANNEL_REAR_CENTER] = PA_CHANNEL_POSITION_REAR_CENTER,
        [KA_CHANNEL_LFE] = PA_CHANNEL_POSITION_LFE,
        [KA_CHANNEL_FRONT_LEFT_OF_CENTER] = PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        [KA_CHANNEL_FRONT_RIGHT_OF_CENTER] = PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        [KA_CHANNEL_SIDE_LEFT] = PA_CHANNEL_POSITION_SIDE_LEFT,
        [KA_CHANNEL_SIDE_RIGHT] = PA_CHANNEL_POSITION_SIDE_RIGHT,
        [KA_CHANNEL_TOP_CENTER] = PA_CHANNEL_POSITION_TOP_CENTER,
        [KA_CHANNEL_TOP_FRONT_LEFT] = PA_CHANNEL_POSITION_FRONT_LEFT,
        [KA_CHANNEL_TOP_FRONT_RIGHT] = PA_CHANNEL_POSITION_FRONT_RIGHT,
        [KA_CHANNEL_TOP_FRONT_CENTER] = PA_CHANNEL_POSITION_FRONT_CENTER,
        [KA_CHANNEL_TOP_REAR_LEFT] = PA_CHANNEL_POSITION_REAR_LEFT,
        [KA_CHANNEL_TOP_REAR_RIGHT] = PA_CHANNEL_POSITION_REAR_RIGHT,
        [KA_CHANNEL_TOP_REAR_CENTER] = PA_CHANNEL_POSITION_TOP_REAR_CENTER
};

static ka_bool_t convert_channel_map(ka_sound_file *f, pa_channel_map *cm) {
        const ka_channel_position_t *positions;
        unsigned c;

        ka_assert(f);
        ka_assert(cm);

        if (!(positions = ka_sound_file_get_channel_map(f)))
                return FALSE;

        cm->channels = ka_sound_file_get_nchannels(f);
        for (c = 0; c < cm->channels; c++)
                cm->map[c] = channel_table[positions[c]];

        return TRUE;
}

int driver_play(ka_context *c, uint32_t id, ka_proplist *proplist, ka_finish_callback_t cb, void *userdata) {
        struct private *p;
        pa_proplist *l = NULL;
        const char *n, *vol, *ct, *channel;
        char *name = NULL;
#if defined(PA_MAJOR) && ((PA_MAJOR > 0) || (PA_MAJOR == 0 && PA_MINOR > 9) || (PA_MAJOR == 0 && PA_MINOR == 9 && PA_MICRO >= 15))
        pa_volume_t v = (pa_volume_t) -1;
#else
        pa_volume_t v = PA_VOLUME_NORM;
#endif
        ka_bool_t volume_set = FALSE;
        pa_cvolume cvol;
        pa_sample_spec ss;
        pa_channel_map cm;
        pa_channel_position_t position = PA_CHANNEL_POSITION_INVALID;
        ka_bool_t cm_good;
        ka_cache_control_t cache_control = KA_CACHE_CONTROL_NEVER;
        struct outstanding *out = NULL;
        int try = 3;
        int ret;
        pa_operation *o;
        char *sp;
        pa_buffer_attr ba;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);

        if (!(out = ka_new0(struct outstanding, 1))) {
                ret = KA_ERROR_OOM;
                goto finish_unlocked;
        }

        out->type = OUTSTANDING_SAMPLE;
        out->context = c;
        out->sink_input = PA_INVALID_INDEX;
        out->id = id;
        out->callback = cb;
        out->userdata = userdata;

        if ((ret = convert_proplist(&l, proplist)) < 0)
                goto finish_unlocked;

        if ((n = pa_proplist_gets(l, KA_PROP_EVENT_ID)))
                if (!(name = ka_strdup(n))) {
                        ret = KA_ERROR_OOM;
                        goto finish_unlocked;
                }

        if ((vol = pa_proplist_gets(l, KA_PROP_KANBERRA_VOLUME))) {
                char *e = NULL;
                double dvol;

                errno = 0;
                dvol = strtod(vol, &e);
                if (errno != 0 || !e || *e) {
                        ret = KA_ERROR_INVALID;
                        goto finish_unlocked;
                }

                v = pa_sw_volume_from_dB(dvol);
                volume_set = TRUE;
        }

        if ((ct = pa_proplist_gets(l, KA_PROP_KANBERRA_CACHE_CONTROL)))
                if ((ret = ka_parse_cache_control(&cache_control, ct)) < 0) {
                        ret = KA_ERROR_INVALID;
                        goto finish_unlocked;
                }

        if ((channel = pa_proplist_gets(l, KA_PROP_KANBERRA_FORCE_CHANNEL))) {
                pa_channel_map t;

                if (!pa_channel_map_parse(&t, channel) ||
                    t.channels != 1) {
                        ret = KA_ERROR_INVALID;
                        goto finish_unlocked;
                }

                position = t.map[0];

                /* We cannot remap cached samples, so let's fail when cacheing
                 * shall be used */
                if (cache_control != KA_CACHE_CONTROL_NEVER) {
                        ret = KA_ERROR_NOTSUPPORTED;
                        goto finish_unlocked;
                }
        }

        strip_prefix(l, "kanberra.");
        add_common(l);

        if ((ret = subscribe(c)) < 0)
                goto finish_unlocked;

        if (name && cache_control != KA_CACHE_CONTROL_NEVER) {

                /* Ok, this sample has an event id, let's try to play it from the cache */

                for (;;) {
                        ka_bool_t canceled;

                        pa_threaded_mainloop_lock(p->mainloop);

                        if (!p->context) {
                                ret = KA_ERROR_STATE;
                                goto finish_locked;
                        }

                        /* Let's try to play the sample */
                        if (!(o = pa_context_play_sample_with_proplist(p->context, name, c->device, v, l, play_sample_cb, out))) {
                                ret = translate_error(pa_context_errno(p->context));
                                goto finish_locked;
                        }

                        for (;;) {
                                pa_operation_state_t state = pa_operation_get_state(o);

                                if (state == PA_OPERATION_DONE) {
                                        canceled = FALSE;
                                        break;
                                } else if (state == PA_OPERATION_CANCELED) {
                                        canceled = TRUE;
                                        break;
                                }

                                pa_threaded_mainloop_wait(p->mainloop);
                        }

                        pa_operation_unref(o);

                        if (!canceled && p->context && out->error == KA_SUCCESS) {
                                ret = KA_SUCCESS;
                                goto finish_locked;
                        }

                        pa_threaded_mainloop_unlock(p->mainloop);

                        /* The operation might have been canceled due to connection termination */
                        if (canceled || !p->context) {
                                ret = KA_ERROR_DISCONNECTED;
                                goto finish_unlocked;
                        }

                        /* Did some other error occur? */
                        if (out->error != KA_ERROR_NOTFOUND) {
                                ret = out->error;
                                goto finish_unlocked;
                        }

                        /* Hmm, we need to play it directly */
                        if (cache_control != KA_CACHE_CONTROL_PERMANENT)
                                break;

                        /* Don't loop forever */
                        if (--try <= 0)
                                break;

                        /* Let's upload the sample and retry playing */
                        if ((ret = driver_cache(c, proplist)) < 0)
                                goto finish_unlocked;
                }
        }

        out->type = OUTSTANDING_STREAM;

        /* Let's stream the sample directly */
        if ((ret = ka_lookup_sound(&out->file, &sp, &p->theme, c->props, proplist)) < 0)
                goto finish_unlocked;

        if (sp)
                if (!pa_proplist_contains(l, KA_PROP_MEDIA_FILENAME))
                        pa_proplist_sets(l, KA_PROP_MEDIA_FILENAME, sp);

        ka_free(sp);

        ss.format = sample_type_table[ka_sound_file_get_sample_type(out->file)];
        ss.channels = (uint8_t) ka_sound_file_get_nchannels(out->file);
        ss.rate = ka_sound_file_get_rate(out->file);

        if (position != PA_CHANNEL_POSITION_INVALID) {
                unsigned u;
                /* Apply kanberra.force_channel */

                cm.channels = ss.channels;
                for (u = 0; u < cm.channels; u++)
                        cm.map[u] = position;

                cm_good = TRUE;
        } else
                cm_good = convert_channel_map(out->file, &cm);

        pa_threaded_mainloop_lock(p->mainloop);

        if (!p->context) {
                ret = KA_ERROR_STATE;
                goto finish_locked;
        }

        if (!(out->stream = pa_stream_new_with_proplist(p->context, NULL, &ss, cm_good ? &cm : NULL, l))) {
                ret = translate_error(pa_context_errno(p->context));
                goto finish_locked;
        }

        pa_stream_set_state_callback(out->stream, stream_state_cb, out);
        pa_stream_set_write_callback(out->stream, stream_write_cb, out);

        if (volume_set)
                pa_cvolume_set(&cvol, ss.channels, v);

        /* Make sure we get the longest latency possible, to minimize CPU
         * consumption */
        ba.maxlength = (uint32_t) -1;
        ba.tlength = (uint32_t) -1;
        ba.prebuf = (uint32_t) -1;
        ba.minreq = (uint32_t) -1;
        ba.fragsize = (uint32_t) -1;

        if (pa_stream_connect_playback(out->stream, c->device, &ba,
#ifdef PA_STREAM_FAIL_ON_SUSPEND
                                       PA_STREAM_FAIL_ON_SUSPEND
#else
                                       0
#endif
                                       | (position != PA_CHANNEL_POSITION_INVALID ? PA_STREAM_NO_REMIX_CHANNELS : 0)
                                       , volume_set ? &cvol : NULL, NULL) < 0) {
                ret = translate_error(pa_context_errno(p->context));
                goto finish_locked;
        }

        for (;;) {
                pa_stream_state_t state;

                if (!p->context || !out->stream) {
                        ret = KA_ERROR_STATE;
                        goto finish_locked;
                }

                state = pa_stream_get_state(out->stream);

                /* Stream sucessfully created */
                if (state == PA_STREAM_READY)
                        break;

                /* Check for failure */
                if (state == PA_STREAM_FAILED) {
                        ret = translate_error(pa_context_errno(p->context));
                        goto finish_locked;
                }

                /* Prematurely ended */
                if (state == PA_STREAM_TERMINATED) {
                        ret = out->error;
                        goto finish_locked;
                }

                pa_threaded_mainloop_wait(p->mainloop);
        }

        ret = KA_SUCCESS;

finish_locked:

        /* We keep the outstanding struct around to clean up later if the sound din't finish yet*/
        if (ret == KA_SUCCESS && !out->finished) {
                out->clean_up = TRUE;

                ka_mutex_lock(p->outstanding_mutex);
                KA_LLIST_PREPEND(struct outstanding, p->outstanding, out);
                ka_mutex_unlock(p->outstanding_mutex);
        } else
                outstanding_free(out);

        out = NULL;

        pa_threaded_mainloop_unlock(p->mainloop);

finish_unlocked:

        if (out)
                outstanding_free(out);

        if (l)
                pa_proplist_free(l);

        ka_free(name);

        return ret;
}

int driver_cancel(ka_context *c, uint32_t id) {
        struct private *p;
        pa_operation *o;
        int ret = KA_SUCCESS;
        struct outstanding *out, *n;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);

        pa_threaded_mainloop_lock(p->mainloop);

        if (!p->context) {
                pa_threaded_mainloop_unlock(p->mainloop);
                return KA_ERROR_STATE;
        }

        ka_mutex_lock(p->outstanding_mutex);

        /* We start these asynchronously and don't care about the return
         * value */

        for (out = p->outstanding; out; out = n) {
                int ret2 = KA_SUCCESS;
                n = out->next;

                if (out->type == OUTSTANDING_UPLOAD ||
                    out->id != id ||
                    out->sink_input == PA_INVALID_INDEX)
                        continue;

                if (!(o = pa_context_kill_sink_input(p->context, out->sink_input, NULL, NULL)))
                        ret2 = translate_error(pa_context_errno(p->context));
                else
                        pa_operation_unref(o);

                /* We make sure here to kill all streams identified by the id
                 * here. However, we will return only the first error we
                 * encounter */

                if (ret2 && ret == KA_SUCCESS)
                        ret = ret2;

                if (out->callback)
                        out->callback(c, out->id, KA_ERROR_CANCELED, out->userdata);

                outstanding_disconnect(out);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                outstanding_free(out);
        }

        ka_mutex_unlock(p->outstanding_mutex);

        pa_threaded_mainloop_unlock(p->mainloop);

        return ret;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        struct private *p;
        pa_proplist *l = NULL;
        const char *n, *ct;
        pa_sample_spec ss;
        pa_channel_map cm;
        ka_bool_t cm_good;
        ka_cache_control_t cache_control = KA_CACHE_CONTROL_PERMANENT;
        struct outstanding *out;
        int ret;
        char *sp;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_return_val_if_fail(p->mainloop, KA_ERROR_STATE);

        if (!(out = ka_new0(struct outstanding, 1))) {
                ret = KA_ERROR_OOM;
                goto finish_unlocked;
        }

        out->type = OUTSTANDING_UPLOAD;
        out->context = c;
        out->sink_input = PA_INVALID_INDEX;

        if ((ret = convert_proplist(&l, proplist)) < 0)
                goto finish_unlocked;

        if (!(n = pa_proplist_gets(l, KA_PROP_EVENT_ID))) {
                ret = KA_ERROR_INVALID;
                goto finish_unlocked;
        }

        if ((ct = pa_proplist_gets(l, KA_PROP_KANBERRA_CACHE_CONTROL)))
                if ((ret = ka_parse_cache_control(&cache_control, ct)) < 0) {
                        ret = KA_ERROR_INVALID;
                        goto finish_unlocked;
                }

        if (cache_control != KA_CACHE_CONTROL_PERMANENT) {
                ret = KA_ERROR_INVALID;
                goto finish_unlocked;
        }

        if ((ct = pa_proplist_gets(l, KA_PROP_KANBERRA_FORCE_CHANNEL))) {
                ret = KA_ERROR_NOTSUPPORTED;
                goto finish_unlocked;
        }

        strip_prefix(l, "kanberra.");
        strip_prefix(l, "event.mouse.");
        strip_prefix(l, "window.");
        add_common(l);

        /* Let's stream the sample directly */
        if ((ret = ka_lookup_sound(&out->file, &sp, &p->theme, c->props, proplist)) < 0)
                goto finish_unlocked;

        if (sp)
                if (!pa_proplist_contains(l, KA_PROP_MEDIA_FILENAME))
                        pa_proplist_sets(l, KA_PROP_MEDIA_FILENAME, sp);

        ka_free(sp);

        ss.format = sample_type_table[ka_sound_file_get_sample_type(out->file)];
        ss.channels = (uint8_t) ka_sound_file_get_nchannels(out->file);
        ss.rate = ka_sound_file_get_rate(out->file);

        cm_good = convert_channel_map(out->file, &cm);

        pa_threaded_mainloop_lock(p->mainloop);

        if (!p->context) {
                ret = KA_ERROR_STATE;
                goto finish_locked;
        }

        if (!(out->stream = pa_stream_new_with_proplist(p->context, NULL, &ss, cm_good ? &cm : NULL, l))) {
                ret = translate_error(pa_context_errno(p->context));
                goto finish_locked;
        }

        pa_stream_set_state_callback(out->stream, stream_state_cb, out);
        pa_stream_set_write_callback(out->stream, stream_write_cb, out);

        if (pa_stream_connect_upload(out->stream, (size_t) ka_sound_file_get_size(out->file)) < 0) {
                ret = translate_error(pa_context_errno(p->context));
                goto finish_locked;
        }

        for (;;) {
                pa_stream_state_t state;

                if (!p->context || !out->stream) {
                        ret = KA_ERROR_STATE;
                        goto finish_locked;
                }

                state = pa_stream_get_state(out->stream);

                /* Stream sucessfully created and uploaded */
                if (state == PA_STREAM_TERMINATED)
                        break;

                /* Check for failure */
                if (state == PA_STREAM_FAILED) {
                        ret = translate_error(pa_context_errno(p->context));
                        goto finish_locked;
                }

                pa_threaded_mainloop_wait(p->mainloop);
        }

        ret = KA_SUCCESS;

finish_locked:
        outstanding_free(out);
        out = NULL;

        pa_threaded_mainloop_unlock(p->mainloop);

finish_unlocked:

        if (out)
                outstanding_free(out);

        if (l)
                pa_proplist_free(l);

        return ret;
}

int driver_playing(ka_context *c, uint32_t id, int *playing) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);
        ka_return_val_if_fail(playing, KA_ERROR_INVALID);

        p = PRIVATE(c);

        *playing = 0;

        ka_mutex_lock(p->outstanding_mutex);

        for (out = p->outstanding; out; out = out->next) {

                if (out->type == OUTSTANDING_UPLOAD ||
                    out->id != id ||
                    out->sink_input == PA_INVALID_INDEX)
                        continue;

                *playing = 1;
                break;
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return KA_SUCCESS;
}
