/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
  This file is part of libkanberra.

  Copyright 2008 Nokia Corporation and/or its subsidiary(-ies).

  Author: Marc-Andre Lureau <marc-andre.lureau@nokia.com>

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

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <gst/gst.h>

#include "kanberra.h"
#include "common.h"
#include "driver.h"
#include "llist.h"
#include "read-sound-file.h"
#include "sound-theme-spec.h"
#include "malloc.h"

struct outstanding {
        KA_LLIST_FIELDS(struct outstanding);
        ka_bool_t dead;
        uint32_t id;
        int err;
        ka_finish_callback_t callback;
        void *userdata;
        GstElement *pipeline;
        struct ka_context *context;
};

struct private {
        ka_theme_data *theme;
        ka_bool_t signal_semaphore;
        sem_t semaphore;

        GstBus *mgr_bus;

        /* Everything below protected by the outstanding_mutex */
        ka_mutex *outstanding_mutex;
        ka_bool_t mgr_thread_running;
        ka_bool_t semaphore_allocated;
        KA_LLIST_HEAD(struct outstanding, outstanding);
};

#define PRIVATE(c) ((struct private *) ((c)->private))

static void* thread_func(void *userdata);
static void send_eos_msg(struct outstanding *out, int err);
static void send_mgr_exit_msg (struct private *p);

static void outstanding_free(struct outstanding *o) {
        GstBus *bus;

        ka_assert(o);

        if (o->pipeline) {
                bus = gst_pipeline_get_bus(GST_PIPELINE (o->pipeline));
                if (bus != NULL) {
                        gst_bus_set_sync_handler(bus, NULL, NULL, NULL);
                        gst_object_unref(bus);
                }

                gst_object_unref(GST_OBJECT(o->pipeline));
        }

        ka_free(o);
}

int driver_open(ka_context *c) {
        GError *error = NULL;
        struct private *p;
        pthread_t thread;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(!PRIVATE(c), KA_ERROR_INVALID);
        ka_return_val_if_fail(!c->driver || ka_streq(c->driver, "gstreamer"), KA_ERROR_NODRIVER);

        gst_init_check(NULL, NULL, &error);
        if (error != NULL) {
                g_warning("gst_init: %s ", error->message);
                g_error_free(error);
                return KA_ERROR_INVALID;
        }

        if (!(p = ka_new0(struct private, 1)))
                return KA_ERROR_OOM;
        c->private = p;

        if (!(p->outstanding_mutex = ka_mutex_new())) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        if (sem_init(&p->semaphore, 0, 0) < 0) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }
        p->semaphore_allocated = TRUE;

        p->mgr_bus = gst_bus_new();
        if (p->mgr_bus == NULL) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }
        gst_bus_set_flushing(p->mgr_bus, FALSE);

        /* Give a reference to the bus to the mgr thread */
        if (pthread_create(&thread, NULL, thread_func, p) < 0) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }
        p->mgr_thread_running = TRUE;

        return KA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(PRIVATE(c), KA_ERROR_STATE);

        p = PRIVATE(c);

        if (p->outstanding_mutex) {
                ka_mutex_lock(p->outstanding_mutex);

                /* Tell all player threads to terminate */
                out = p->outstanding;
                while (out) {
                        if (!out->dead)
                                send_eos_msg(out, KA_ERROR_DESTROYED);
                        out = out->next;
                }

                /* Now that we've sent EOS for all pending players, append a
                 * message to wait for the mgr thread to exit */
                if (p->mgr_thread_running && p->semaphore_allocated) {
                        send_mgr_exit_msg(p);

                        p->signal_semaphore = TRUE;
                        while (p->mgr_thread_running) {
                                ka_mutex_unlock(p->outstanding_mutex);
                                sem_wait(&p->semaphore);
                                ka_mutex_lock(p->outstanding_mutex);
                        }
                }

                ka_mutex_unlock(p->outstanding_mutex);
                ka_mutex_free(p->outstanding_mutex);
        }

        if (p->mgr_bus)
                g_object_unref(p->mgr_bus);

        if (p->theme)
                ka_theme_data_free(p->theme);

        if (p->semaphore_allocated)
                sem_destroy(&p->semaphore);

        ka_free(p);

        /* no gst_deinit(), see doc */

        return KA_SUCCESS;
}

int driver_change_device(ka_context *c, const char *device) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(PRIVATE(c), KA_ERROR_STATE);

        return KA_SUCCESS;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(changed, KA_ERROR_INVALID);
        ka_return_val_if_fail(merged, KA_ERROR_INVALID);
        ka_return_val_if_fail(PRIVATE(c), KA_ERROR_STATE);

        return KA_SUCCESS;
}

static void
send_eos_msg(struct outstanding *out, int err) {
        struct private *p;
        GstMessage *m;
        GstStructure *s;

        out->dead = TRUE;
        out->err = err;

        p = PRIVATE(out->context);
        s = gst_structure_new("application/eos", "info", G_TYPE_POINTER, out, NULL);
        m = gst_message_new_application (GST_OBJECT (out->pipeline), s);

        gst_bus_post (p->mgr_bus, m);
}

static GstBusSyncReply
bus_cb(GstBus *bus, GstMessage *message, gpointer data) {
        int err;
        struct outstanding *out;
        struct private *p;

        ka_return_val_if_fail(bus, GST_BUS_DROP);
        ka_return_val_if_fail(message, GST_BUS_DROP);
        ka_return_val_if_fail(data, GST_BUS_DROP);

        out = data;
        p = PRIVATE(out->context);

        switch (GST_MESSAGE_TYPE(message)) {
                /* for all elements */
        case GST_MESSAGE_ERROR:
                err = KA_ERROR_SYSTEM;
                break;
        case GST_MESSAGE_EOS:
                /* only respect EOS from the toplevel pipeline */
                if (GST_OBJECT(out->pipeline) != GST_MESSAGE_SRC(message))
                        return GST_BUS_PASS;

                err = KA_SUCCESS;
                break;
        default:
                return GST_BUS_PASS;
        }

        /* Bin finished playback: ask the manager thread to shut it
         * down, since we can't from the sync message handler */
        ka_mutex_lock(p->outstanding_mutex);
        if (!out->dead)
                send_eos_msg(out, err);
        ka_mutex_unlock(p->outstanding_mutex);

        return GST_BUS_PASS;
}

struct ka_sound_file {
        GstElement *fdsrc;
};

static int ka_gst_sound_file_open(ka_sound_file **_f, const char *fn) {
        int fd;
        ka_sound_file *f;

        ka_return_val_if_fail(_f, KA_ERROR_INVALID);
        ka_return_val_if_fail(fn, KA_ERROR_INVALID);

        if ((fd = open(fn, O_RDONLY)) == -1)
                return errno == ENOENT ? KA_ERROR_NOTFOUND : KA_ERROR_SYSTEM;

        if (!(f = ka_new0(ka_sound_file, 1))) {
                close(fd);
                return KA_ERROR_OOM;
        }

        if (!(f->fdsrc = gst_element_factory_make("fdsrc", NULL))) {
                close(fd);
                ka_free(f);
                return KA_ERROR_OOM;
        }

        g_object_set(GST_OBJECT(f->fdsrc), "fd", fd, NULL);
        *_f = f;

        return KA_SUCCESS;
}

static void on_pad_added(GstElement *element, GstPad *pad, gboolean arg1, gpointer data)
{
        GstStructure *structure;
        GstElement *sinkelement;
        GstCaps *caps;
        GstPad *vpad;
        const char *type;

        sinkelement = GST_ELEMENT(data);

        caps = gst_pad_query_caps(pad, NULL);
        if (gst_caps_is_empty(caps) || gst_caps_is_any(caps)) {
                gst_caps_unref(caps);
                return;
        }

        structure = gst_caps_get_structure(caps, 0);
        type = gst_structure_get_name(structure);
        if (g_str_has_prefix(type, "audio/x-raw") == TRUE) {
                vpad = gst_element_get_static_pad(sinkelement, "sink");
                gst_pad_link(pad, vpad);
                gst_object_unref(vpad);
        }
        gst_caps_unref(caps);
}

static void
send_mgr_exit_msg (struct private *p) {
        GstMessage *m;
        GstStructure *s;

        s = gst_structure_new("application/mgr-exit", NULL);
        m = gst_message_new_application (NULL, s);

        gst_bus_post (p->mgr_bus, m);
}

/* Global manager thread that shuts down GStreamer pipelines when ordered */
static void* thread_func(void *userdata) {
        struct private *p = userdata;
        GstBus *bus = g_object_ref(p->mgr_bus);

        pthread_detach(pthread_self());

        /* Pop messages from the manager bus until we see an exit command */
        do {
                GstMessage *m = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
                const GstStructure *s;
                const GValue *v;
                struct outstanding *out;

                if (m == NULL)
                        break;
                if (GST_MESSAGE_TYPE(m) != GST_MESSAGE_APPLICATION) {
                        gst_message_unref (m);
                        break;
                }

                s = gst_message_get_structure(m);
                if (gst_structure_has_name(s, "application/mgr-exit")) {
                        gst_message_unref (m);
                        break;
                }

                /* Otherwise, this must be an EOS message for an outstanding pipe */
                ka_assert(gst_structure_has_name(s, "application/eos"));
                v  = gst_structure_get_value(s, "info");
                ka_assert(v);
                out = g_value_get_pointer(v);
                ka_assert(out);

                /* Set pipeline back to NULL to close things. By the time this
                 * completes, we can be sure bus_cb won't be called */
                if (gst_element_set_state(out->pipeline, GST_STATE_NULL) ==
                    GST_STATE_CHANGE_FAILURE) {
                        gst_message_unref (m);
                        break;
                }
                if (out->callback)
                        out->callback(out->context, out->id, out->err, out->userdata);

                ka_mutex_lock(p->outstanding_mutex);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                outstanding_free(out);
                ka_mutex_unlock(p->outstanding_mutex);

                gst_message_unref(m);
        } while (TRUE);

        /* Signal the semaphore and exit */
        ka_mutex_lock(p->outstanding_mutex);
        if (p->signal_semaphore)
                sem_post(&p->semaphore);
        p->mgr_thread_running = FALSE;
        ka_mutex_unlock(p->outstanding_mutex);

        gst_bus_set_flushing(bus, TRUE);
        g_object_unref (bus);
        return NULL;
}


int driver_play(ka_context *c, uint32_t id, ka_proplist *proplist, ka_finish_callback_t cb, void *userdata) {
        struct private *p;
        struct outstanding *out;
        ka_sound_file *f;
        GstElement *decodebin, *sink, *audioconvert, *audioresample, *abin;
        GstBus *bus;
        GstPad *audiopad;
        int ret;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, KA_ERROR_INVALID);

        out = NULL;
        f = NULL;
        sink = NULL;
        decodebin = NULL;
        audioconvert = NULL;
        audioresample = NULL;
        abin = NULL;
        p = PRIVATE(c);

        if ((ret = ka_lookup_sound_with_callback(&f, ka_gst_sound_file_open, NULL, &p->theme, c->props, proplist)) < 0)
                goto fail;

        if (!(out = ka_new0(struct outstanding, 1)))
                return KA_ERROR_OOM;

        out->id = id;
        out->callback = cb;
        out->userdata = userdata;
        out->context = c;

        if (!(out->pipeline = gst_pipeline_new(NULL))
            || !(decodebin = gst_element_factory_make("decodebin2", NULL))
            || !(audioconvert = gst_element_factory_make("audioconvert", NULL))
            || !(audioresample = gst_element_factory_make("audioresample", NULL))
            || !(sink = gst_element_factory_make("autoaudiosink", NULL))
            || !(abin = gst_bin_new ("audiobin"))) {

                /* At this point, if there is a failure, free each plugin separately. */
                if (out->pipeline != NULL)
                        g_object_unref (out->pipeline);
                if (decodebin != NULL)
                        g_object_unref(decodebin);
                if (audioconvert != NULL)
                        g_object_unref(audioconvert);
                if (audioresample != NULL)
                        g_object_unref(audioresample);
                if (sink != NULL)
                        g_object_unref(sink);
                if (abin != NULL)
                        g_object_unref(abin);

                ka_free(out);

                ret = KA_ERROR_OOM;
                goto fail;
        }

        bus = gst_pipeline_get_bus(GST_PIPELINE (out->pipeline));
        gst_bus_set_sync_handler(bus, bus_cb, out, NULL);
        gst_object_unref(bus);

        g_signal_connect(decodebin, "new-decoded-pad",
                         G_CALLBACK (on_pad_added), abin);
        gst_bin_add_many(GST_BIN (abin), audioconvert, audioresample, sink, NULL);
        gst_element_link_many(audioconvert, audioresample, sink, NULL);

        audiopad = gst_element_get_static_pad(audioconvert, "sink");
        gst_element_add_pad(abin, gst_ghost_pad_new("sink", audiopad));
        gst_object_unref(audiopad);

        gst_bin_add_many(GST_BIN (out->pipeline),
                         f->fdsrc, decodebin, abin, NULL);
        if (!gst_element_link(f->fdsrc, decodebin)) {
                /* Bin now owns the fdsrc... */
                f->fdsrc = NULL;

                outstanding_free(out);
                ret = KA_ERROR_OOM;
                goto fail;
        }
        /* Bin now owns the fdsrc... */
        f->fdsrc = NULL;

        ka_free(f);
        f = NULL;

        ka_mutex_lock(p->outstanding_mutex);
        KA_LLIST_PREPEND(struct outstanding, p->outstanding, out);
        ka_mutex_unlock(p->outstanding_mutex);

        if (gst_element_set_state(out->pipeline,
                                  GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
                ret = KA_ERROR_NOTAVAILABLE;
                goto fail;
        }

        return KA_SUCCESS;

fail:
        if (f && f->fdsrc)
                gst_object_unref(f->fdsrc);

        if (f)
                ka_free(f);

        return ret;
}

int driver_cancel(ka_context *c, uint32_t id) {
        struct private *p;
        struct outstanding *out = NULL;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(PRIVATE(c), KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_mutex_lock(p->outstanding_mutex);

        for (out = p->outstanding; out;/* out = out->next*/) {
                struct outstanding *next;

                if (out->id != id || out->pipeline == NULL || out->dead == TRUE) {
                        out = out->next;
                        continue;
                }

                if (gst_element_set_state(out->pipeline, GST_STATE_NULL) ==
                    GST_STATE_CHANGE_FAILURE)
                        goto error;

                if (out->callback)
                        out->callback(c, out->id, KA_ERROR_CANCELED, out->userdata);
                next = out->next;
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                outstanding_free(out);
                out = next;
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return KA_SUCCESS;

error:
        ka_mutex_unlock(p->outstanding_mutex);
        return KA_ERROR_SYSTEM;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(PRIVATE(c), KA_ERROR_STATE);

        return KA_ERROR_NOTSUPPORTED;
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

                if (out->id != id || out->pipeline == NULL || out->dead == TRUE)
                        continue;

                *playing = 1;
                break;
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return KA_SUCCESS;
}
