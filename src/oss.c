/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
  This file is part of libkanberra.

  Copyright 2008 Lennart Poettering
                 Joe Marcus Clarke

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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef HAVE_MACHINE_SOUNDCARD_H
#  include <machine/soundcard.h>
#else
#  ifdef HAVE_SOUNDCARD_H
#    include <soundcard.h>
#  else
#    include <sys/soundcard.h>
#  endif
#endif

#include "kanberra.h"
#include "common.h"
#include "driver.h"
#include "llist.h"
#include "read-sound-file.h"
#include "sound-theme-spec.h"
#include "malloc.h"

struct private;

struct outstanding {
        KA_LLIST_FIELDS(struct outstanding);
        ka_bool_t dead;
        uint32_t id;
        ka_finish_callback_t callback;
        void *userdata;
        ka_sound_file *file;
        int pcm;
        int pipe_fd[2];
        ka_context *context;
};

struct private {
        ka_theme_data *theme;
        ka_mutex *outstanding_mutex;
        ka_bool_t signal_semaphore;
        sem_t semaphore;
        ka_bool_t semaphore_allocated;
        KA_LLIST_HEAD(struct outstanding, outstanding);
};

#define PRIVATE(c) ((struct private *) ((c)->private))

static void outstanding_free(struct outstanding *o) {
        ka_assert(o);

        if (o->pipe_fd[1] >= 0)
                close(o->pipe_fd[1]);

        if (o->pipe_fd[0] >= 0)
                close(o->pipe_fd[0]);

        if (o->file)
                ka_sound_file_close(o->file);

        if (o->pcm >= 0) {
                close(o->pcm);
                o->pcm = -1;
        }

        ka_free(o);
}

int driver_open(ka_context *c) {
        struct private *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(!c->driver || ka_streq(c->driver, "oss"), KA_ERROR_NODRIVER);
        ka_return_val_if_fail(!PRIVATE(c), KA_ERROR_STATE);

        if (!(c->private = p = ka_new0(struct private, 1)))
                return KA_ERROR_OOM;

        if (!(p->outstanding_mutex = ka_mutex_new())) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        if (sem_init(&p->semaphore, 0, 0) < 0) {
                driver_destroy(c);
                return KA_ERROR_OOM;
        }

        p->semaphore_allocated = TRUE;

        return KA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        if (p->outstanding_mutex) {
                ka_mutex_lock(p->outstanding_mutex);

                /* Tell all player threads to terminate */
                for (out = p->outstanding; out; out = out->next) {

                        if (out->dead)
                                continue;

                        out->dead = TRUE;

                        if (out->callback)
                                out->callback(c, out->id, KA_ERROR_DESTROYED, out->userdata);

                        /* This will cause the thread to wakeup and terminate */
                        if (out->pipe_fd[1] >= 0) {
                                close(out->pipe_fd[1]);
                                out->pipe_fd[1] = -1;
                        }
                }

                if (p->semaphore_allocated) {
                        /* Now wait until all players are destroyed */
                        p->signal_semaphore = TRUE;
                        while (p->outstanding) {
                                ka_mutex_unlock(p->outstanding_mutex);
                                sem_wait(&p->semaphore);
                                ka_mutex_lock(p->outstanding_mutex);
                        }
                }

                ka_mutex_unlock(p->outstanding_mutex);
                ka_mutex_free(p->outstanding_mutex);
        }

        if (p->theme)
                ka_theme_data_free(p->theme);

        if (p->semaphore_allocated)
                sem_destroy(&p->semaphore);

        ka_free(p);

        c->private = NULL;

        return KA_SUCCESS;
}

int driver_change_device (ka_context *c,
			  const char *device GNUC_UNUSED)
{
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        return KA_SUCCESS;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(changed, KA_ERROR_INVALID);
        ka_return_val_if_fail(merged, KA_ERROR_INVALID);

        return KA_SUCCESS;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);

        return KA_ERROR_NOTSUPPORTED;
}

static int translate_error(int error) {

        switch (error) {
        case ENODEV:
        case ENOENT:
                return KA_ERROR_NOTFOUND;
        case EACCES:
        case EPERM:
                return KA_ERROR_ACCESS;
        case ENOMEM:
                return KA_ERROR_OOM;
        case EBUSY:
                return KA_ERROR_NOTAVAILABLE;
        case EINVAL:
                return KA_ERROR_INVALID;
        case ENOSYS:
                return KA_ERROR_NOTSUPPORTED;
        default:
                if (ka_debug())
                        fprintf(stderr, "Got unhandled error from OSS: %s\n", strerror(error));
                return KA_ERROR_IO;
        }
}

static int open_oss(ka_context *c, struct outstanding *out) {
        int mode, val, test, ret;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);
        ka_return_val_if_fail(out, KA_ERROR_INVALID);

        /* In OSS we have no way to configure a channel mapping for
         * multichannel streams. We cannot support those files hence */
        ka_return_val_if_fail(ka_sound_file_get_nchannels(out->file) <= 2, KA_ERROR_NOTSUPPORTED);

        if ((out->pcm = open(c->device ? c->device : "/dev/dsp", O_WRONLY | O_NONBLOCK, 0)) < 0)
                goto finish_errno;

        if ((mode = fcntl(out->pcm, F_GETFL)) < 0)
                goto finish_errno;

        mode &= ~O_NONBLOCK;

        if (fcntl(out->pcm, F_SETFL, mode) < 0)
                goto finish_errno;

        switch (ka_sound_file_get_sample_type(out->file)) {
        case KA_SAMPLE_U8:
                val = AFMT_U8;
                break;
        case KA_SAMPLE_S16NE:
                val = AFMT_S16_NE;
                break;
        case KA_SAMPLE_S16RE:
#if __BYTE_ORDER == __LITTLE_ENDIAN
                val = AFMT_S16_BE;
#else
                val = AFMT_S16_LE;
#endif
                break;
        }

        test = val;
        if (ioctl(out->pcm, SNDCTL_DSP_SETFMT, &val) < 0)
                goto finish_errno;

        if (val != test) {
                ret = KA_ERROR_NOTSUPPORTED;
                goto finish_ret;
        }

        test = val = (int) ka_sound_file_get_nchannels(out->file);
        if (ioctl(out->pcm, SNDCTL_DSP_CHANNELS, &val) < 0)
                goto finish_errno;

        if (val != test) {
                ret = KA_ERROR_NOTSUPPORTED;
                goto finish_ret;
        }

        test = val = (int) ka_sound_file_get_rate(out->file);
        if (ioctl(out->pcm, SNDCTL_DSP_SPEED, &val) < 0)
                goto finish_errno;

        /* Check to make sure the configured rate is close enough to the
         * requested rate. */
        if (fabs((double) (val - test)) > test * 0.05) {
                ret = KA_ERROR_NOTSUPPORTED;
                goto finish_ret;
        }

        return KA_SUCCESS;

finish_errno:
        return translate_error(errno);

finish_ret:
        return ret;
}

#define BUFSIZE (4*1024)

static void* thread_func(void *userdata) {
        struct outstanding *out = userdata;
        int ret;
        void *data, *d = NULL;
        size_t fs, data_size;
        size_t nbytes = 0;
        struct pollfd pfd[2];
        nfds_t n_pfd = 2;
        struct private *p;

        p = PRIVATE(out->context);

        pthread_detach(pthread_self());

        fs = ka_sound_file_frame_size(out->file);
        data_size = (BUFSIZE/fs)*fs;

        if (!(data = ka_malloc(data_size))) {
                ret = KA_ERROR_OOM;
                goto finish;
        }

        pfd[0].fd = out->pipe_fd[0];
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        pfd[1].fd = out->pcm;
        pfd[1].events = POLLOUT;
        pfd[1].revents = 0;

        for (;;) {
                ssize_t bytes_written;

                if (out->dead)
                        break;

                if (poll(pfd, n_pfd, -1) < 0) {
                        ret = KA_ERROR_SYSTEM;
                        goto finish;
                }

                /* We have been asked to shut down */
                if (pfd[0].revents)
                        break;

                if (pfd[1].revents != POLLOUT) {
                        ret = KA_ERROR_IO;
                        goto finish;
                }

                if (nbytes <= 0) {
                        nbytes = data_size;

                        if ((ret = ka_sound_file_read_arbitrary(out->file, data, &nbytes)) < 0)
                                goto finish;

                        d = data;
                }

                if (nbytes <= 0)
                        break;

                if ((bytes_written = write(out->pcm, d, nbytes)) <= 0) {
                        ret = translate_error(errno);
                        goto finish;
                }

                nbytes -= (size_t) bytes_written;
                d = (uint8_t*) d + (size_t) bytes_written;
        }

        ret = KA_SUCCESS;

finish:

        ka_free(data);

        if (!out->dead)
                if (out->callback)
                        out->callback(out->context, out->id, ret, out->userdata);

        ka_mutex_lock(p->outstanding_mutex);

        KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);

        if (!p->outstanding && p->signal_semaphore)
                sem_post(&p->semaphore);

        outstanding_free(out);

        ka_mutex_unlock(p->outstanding_mutex);

        return NULL;
}

int driver_play(ka_context *c, uint32_t id, ka_proplist *proplist, ka_finish_callback_t cb, void *userdata) {
        struct private *p;
        struct outstanding *out = NULL;
        int ret;
        pthread_t thread;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        if (!(out = ka_new0(struct outstanding, 1))) {
                ret = KA_ERROR_OOM;
                goto finish;
        }

        out->context = c;
        out->id = id;
        out->callback = cb;
        out->userdata = userdata;
        out->pipe_fd[0] = out->pipe_fd[1] = -1;
        out->pcm = -1;

        if (pipe(out->pipe_fd) < 0) {
                ret = KA_ERROR_SYSTEM;
                goto finish;
        }

        if ((ret = ka_lookup_sound(&out->file, NULL, &p->theme, c->props, proplist)) < 0)
                goto finish;

        if ((ret = open_oss(c, out)) < 0)
                goto finish;

        /* OK, we're ready to go, so let's add this to our list */
        ka_mutex_lock(p->outstanding_mutex);
        KA_LLIST_PREPEND(struct outstanding, p->outstanding, out);
        ka_mutex_unlock(p->outstanding_mutex);

        if (pthread_create(&thread, NULL, thread_func, out) < 0) {
                ret = KA_ERROR_OOM;

                ka_mutex_lock(p->outstanding_mutex);
                KA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                ka_mutex_unlock(p->outstanding_mutex);

                goto finish;
        }

        ret = KA_SUCCESS;

finish:

        /* We keep the outstanding struct around if we need clean up later to */
        if (ret != KA_SUCCESS)
                outstanding_free(out);

        return ret;
}

int driver_cancel(ka_context *c, uint32_t id) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        ka_mutex_lock(p->outstanding_mutex);

        for (out = p->outstanding; out; out = out->next) {

                if (out->id != id)
                        continue;

                if (out->dead)
                        continue;

                out->dead = TRUE;

                if (out->callback)
                        out->callback(c, out->id, KA_ERROR_CANCELED, out->userdata);

                /* This will cause the thread to wakeup and terminate */
                if (out->pipe_fd[1] >= 0) {
                        close(out->pipe_fd[1]);
                        out->pipe_fd[1] = -1;
                }
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return KA_SUCCESS;
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

                if (out->dead ||
                    out->id != id)
                        continue;

                *playing = 1;
                break;
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return KA_SUCCESS;
}
