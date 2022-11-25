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

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include <alsa/asoundlib.h>

#include "kanberra.h"
#include "common.h"
#include "driver.h"
#include "llist.h"
#include "read-sound-file.h"
#include "sound-theme-spec.h"
#include "malloc.h"

struct private;

struct outstanding {
        CA_LLIST_FIELDS(struct outstanding);
        ka_bool_t dead;
        uint32_t id;
        ka_finish_callback_t callback;
        void *userdata;
        ka_sound_file *file;
        snd_pcm_t *pcm;
        int pipe_fd[2];
        ka_context *context;
};

struct private {
        ka_theme_data *theme;
        ka_mutex *outstanding_mutex;
        ka_bool_t signal_semaphore;
        sem_t semaphore;
        ka_bool_t semaphore_allocated;
        CA_LLIST_HEAD(struct outstanding, outstanding);
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

        if (o->pcm)
                snd_pcm_close(o->pcm);

        ka_free(o);
}

int driver_open(ka_context *c) {
        struct private *p;

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(!c->driver || ka_streq(c->driver, "alsa"), CA_ERROR_NODRIVER);
        ka_return_val_if_fail(!PRIVATE(c), CA_ERROR_STATE);

        if (!(c->private = p = ka_new0(struct private, 1)))
                return CA_ERROR_OOM;

        if (!(p->outstanding_mutex = ka_mutex_new())) {
                driver_destroy(c);
                return CA_ERROR_OOM;
        }

        if (sem_init(&p->semaphore, 0, 0) < 0) {
                driver_destroy(c);
                return CA_ERROR_OOM;
        }

        p->semaphore_allocated = TRUE;

        return CA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);

        p = PRIVATE(c);

        if (p->outstanding_mutex) {
                ka_mutex_lock(p->outstanding_mutex);

                /* Tell all player threads to terminate */
                for (out = p->outstanding; out; out = out->next) {

                        if (out->dead)
                                continue;

                        out->dead = TRUE;

                        if (out->callback)
                                out->callback(c, out->id, CA_ERROR_DESTROYED, out->userdata);

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

        snd_config_update_free_global();

        return CA_SUCCESS;
}

int driver_change_device(ka_context *c, const char *device) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);

        return CA_SUCCESS;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(changed, CA_ERROR_INVALID);
        ka_return_val_if_fail(merged, CA_ERROR_INVALID);

        return CA_SUCCESS;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, CA_ERROR_INVALID);

        return CA_ERROR_NOTSUPPORTED;
}

static int translate_error(int error) {

        switch (error) {
        case -ENODEV:
        case -ENOENT:
                return CA_ERROR_NOTFOUND;
        case -EACCES:
        case -EPERM:
                return CA_ERROR_ACCESS;
        case -ENOMEM:
                return CA_ERROR_OOM;
        case -EBUSY:
                return CA_ERROR_NOTAVAILABLE;
        case -EINVAL:
                return CA_ERROR_INVALID;
        case -ENOSYS:
                return CA_ERROR_NOTSUPPORTED;
        default:
                if (ka_debug())
                        fprintf(stderr, "Got unhandled error from ALSA: %s\n", snd_strerror(error));
                return CA_ERROR_IO;
        }
}

static const snd_pcm_format_t sample_type_table[] = {
#ifdef WORDS_BIGENDIAN
        [CA_SAMPLE_S16NE] = SND_PCM_FORMAT_S16_BE,
        [CA_SAMPLE_S16RE] = SND_PCM_FORMAT_S16_LE,
#else
        [CA_SAMPLE_S16NE] = SND_PCM_FORMAT_S16_LE,
        [CA_SAMPLE_S16RE] = SND_PCM_FORMAT_S16_BE,
#endif
        [CA_SAMPLE_U8] = SND_PCM_FORMAT_U8
};

static int open_alsa(ka_context *c, struct outstanding *out) {
        int ret;
        snd_pcm_hw_params_t *hwparams;
        unsigned rate;

        snd_pcm_hw_params_alloca(&hwparams);

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);
        ka_return_val_if_fail(out, CA_ERROR_INVALID);

        /* In ALSA we need to open different devices for doing
         * multichannel audio. This cnnot be done in a backend-independant
         * wa, hence we limit ourselves to mono/stereo only. */
        ka_return_val_if_fail(ka_sound_file_get_nchannels(out->file) <= 2, CA_ERROR_NOTSUPPORTED);

        if ((ret = snd_pcm_open(&out->pcm, c->device ? c->device : "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
                goto finish;

        if ((ret = snd_pcm_hw_params_any(out->pcm, hwparams)) < 0)
                goto finish;

        if ((ret = snd_pcm_hw_params_set_access(out->pcm, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
                goto finish;

        if ((ret = snd_pcm_hw_params_set_format(out->pcm, hwparams, sample_type_table[ka_sound_file_get_sample_type(out->file)])) < 0)
                goto finish;

        rate = ka_sound_file_get_rate(out->file);
        if ((ret = snd_pcm_hw_params_set_rate_near(out->pcm, hwparams, &rate, 0)) < 0)
                goto finish;

        if ((ret = snd_pcm_hw_params_set_channels(out->pcm, hwparams, ka_sound_file_get_nchannels(out->file))) < 0)
                goto finish;

        if ((ret = snd_pcm_hw_params(out->pcm, hwparams)) < 0)
                goto finish;

        if ((ret = snd_pcm_prepare(out->pcm)) < 0)
                goto finish;

        return CA_SUCCESS;

finish:

        return translate_error(ret);
}

#define BUFSIZE (16*1024)

static void* thread_func(void *userdata) {
        struct outstanding *out = userdata;
        int ret;
        void *data, *d = NULL;
        size_t fs, data_size;
        size_t nbytes = 0;
        struct pollfd *pfd = NULL;
        nfds_t n_pfd;
        struct private *p;

        p = PRIVATE(out->context);

        pthread_detach(pthread_self());

        fs = ka_sound_file_frame_size(out->file);
        data_size = (BUFSIZE/fs)*fs;

        if (!(data = ka_malloc(data_size))) {
                ret = CA_ERROR_OOM;
                goto finish;
        }

        if ((ret = snd_pcm_poll_descriptors_count(out->pcm)) < 0) {
                ret = translate_error(ret);
                goto finish;
        }

        n_pfd = (nfds_t) ret + 1;
        if (!(pfd = ka_new(struct pollfd, n_pfd))) {
                ret = CA_ERROR_OOM;
                goto finish;
        }

        if ((ret = snd_pcm_poll_descriptors(out->pcm, pfd+1, (unsigned) n_pfd-1)) < 0) {
                ret = translate_error(ret);
                goto finish;
        }

        pfd[0].fd = out->pipe_fd[0];
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;

        for (;;) {
                unsigned short revents;
                snd_pcm_sframes_t sframes;

                if (out->dead)
                        break;

                if (poll(pfd, n_pfd, -1) < 0) {
                        ret = CA_ERROR_SYSTEM;
                        goto finish;
                }

                /* We have been asked to shut down */
                if (pfd[0].revents)
                        break;

                if ((ret = snd_pcm_poll_descriptors_revents(out->pcm, pfd+1, (unsigned) n_pfd-1, &revents)) < 0) {
                        ret = translate_error(ret);
                        goto finish;
                }

                if (revents != POLLOUT) {

                        switch (snd_pcm_state(out->pcm)) {

                        case SND_PCM_STATE_XRUN:

                                if ((ret = snd_pcm_recover(out->pcm, -EPIPE, 1)) != 0) {
                                        ret = translate_error(ret);
                                        goto finish;
                                }
                                break;

                        case SND_PCM_STATE_SUSPENDED:

                                if ((ret = snd_pcm_recover(out->pcm, -ESTRPIPE, 1)) != 0) {
                                        ret = translate_error(ret);
                                        goto finish;
                                }
                                break;

                        default:

                                snd_pcm_drop(out->pcm);

                                if ((ret = snd_pcm_prepare(out->pcm)) < 0) {
                                        ret = translate_error(ret);
                                        goto finish;
                                }
                                break;
                        }

                        continue;
                }

                if (nbytes <= 0) {

                        nbytes = data_size;

                        if ((ret = ka_sound_file_read_arbitrary(out->file, data, &nbytes)) < 0)
                                goto finish;

                        d = data;
                }

                if (nbytes <= 0) {
                        snd_pcm_drain(out->pcm);
                        break;
                }

                if ((sframes = snd_pcm_writei(out->pcm, d, nbytes/fs)) < 0) {

                        if ((ret = snd_pcm_recover(out->pcm, (int) sframes, 1)) < 0) {
                                ret = translate_error(ret);
                                goto finish;
                        }

                        continue;
                }

                nbytes -= (size_t) sframes*fs;
                d = (uint8_t*) d + (size_t) sframes*fs;
        }

        ret = CA_SUCCESS;

finish:

        ka_free(data);
        ka_free(pfd);

        if (!out->dead)
                if (out->callback)
                        out->callback(out->context, out->id, ret, out->userdata);

        ka_mutex_lock(p->outstanding_mutex);

        CA_LLIST_REMOVE(struct outstanding, p->outstanding, out);

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

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, CA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);

        p = PRIVATE(c);

        if (!(out = ka_new0(struct outstanding, 1))) {
                ret = CA_ERROR_OOM;
                goto finish;
        }

        out->context = c;
        out->id = id;
        out->callback = cb;
        out->userdata = userdata;
        out->pipe_fd[0] = out->pipe_fd[1] = -1;

        if (pipe(out->pipe_fd) < 0) {
                ret = CA_ERROR_SYSTEM;
                goto finish;
        }

        if ((ret = ka_lookup_sound(&out->file, NULL, &p->theme, c->props, proplist)) < 0)
                goto finish;

        if ((ret = open_alsa(c, out)) < 0)
                goto finish;

        /* OK, we're ready to go, so let's add this to our list */
        ka_mutex_lock(p->outstanding_mutex);
        CA_LLIST_PREPEND(struct outstanding, p->outstanding, out);
        ka_mutex_unlock(p->outstanding_mutex);

        if (pthread_create(&thread, NULL, thread_func, out) < 0) {
                ret = CA_ERROR_OOM;

                ka_mutex_lock(p->outstanding_mutex);
                CA_LLIST_REMOVE(struct outstanding, p->outstanding, out);
                ka_mutex_unlock(p->outstanding_mutex);

                goto finish;
        }

        ret = CA_SUCCESS;

finish:

        /* We keep the outstanding struct around if we need clean up later to */
        if (ret != CA_SUCCESS)
                outstanding_free(out);

        return ret;
}

int driver_cancel(ka_context *c, uint32_t id) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);

        p = PRIVATE(c);

        ka_mutex_lock(p->outstanding_mutex);

        for (out = p->outstanding; out; out = out->next) {

                if (out->id != id)
                        continue;

                if (out->dead)
                        continue;

                out->dead = TRUE;

                if (out->callback)
                        out->callback(c, out->id, CA_ERROR_CANCELED, out->userdata);

                /* This will cause the thread to wakeup and terminate */
                if (out->pipe_fd[1] >= 0) {
                        close(out->pipe_fd[1]);
                        out->pipe_fd[1] = -1;
                }
        }

        ka_mutex_unlock(p->outstanding_mutex);

        return CA_SUCCESS;
}

int driver_playing(ka_context *c, uint32_t id, int *playing) {
        struct private *p;
        struct outstanding *out;

        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, CA_ERROR_STATE);
        ka_return_val_if_fail(playing, CA_ERROR_INVALID);

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

        return CA_SUCCESS;
}
