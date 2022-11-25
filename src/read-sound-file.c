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

#include "read-sound-file.h"
#include "read-wav.h"
#include "read-vorbis.h"
#include "macro.h"
#include "malloc.h"
#include "kanberra.h"

struct ka_sound_file {
        ka_wav *wav;
        ka_vorbis *vorbis;
        char *filename;

        unsigned nchannels;
        unsigned rate;
        ka_sample_type_t type;
};

int ka_sound_file_open(ka_sound_file **_f, const char *fn) {
        FILE *file;
        ka_sound_file *f;
        int ret;

        ka_return_val_if_fail(_f, CA_ERROR_INVALID);
        ka_return_val_if_fail(fn, CA_ERROR_INVALID);

        if (!(f = ka_new0(ka_sound_file, 1)))
                return CA_ERROR_OOM;

        if (!(f->filename = ka_strdup(fn))) {
                ret = CA_ERROR_OOM;
                goto fail;
        }

        if (!(file = fopen(fn, "r"))) {
                ret = errno == ENOENT ? CA_ERROR_NOTFOUND : CA_ERROR_SYSTEM;
                goto fail;
        }

        if ((ret = ka_wav_open(&f->wav, file)) == CA_SUCCESS) {
                f->nchannels = ka_wav_get_nchannels(f->wav);
                f->rate = ka_wav_get_rate(f->wav);
                f->type = ka_wav_get_sample_type(f->wav);
                *_f = f;
                return CA_SUCCESS;
        }

        if (ret == CA_ERROR_CORRUPT) {

                if (fseek(file, 0, SEEK_SET) < 0) {
                        ret = CA_ERROR_SYSTEM;
                        goto fail;
                }

                if ((ret = ka_vorbis_open(&f->vorbis, file)) == CA_SUCCESS)  {
                        f->nchannels = ka_vorbis_get_nchannels(f->vorbis);
                        f->rate = ka_vorbis_get_rate(f->vorbis);
                        f->type = CA_SAMPLE_S16NE;
                        *_f = f;
                        return CA_SUCCESS;
                }
        }

fail:

        ka_free(f->filename);
        ka_free(f);

        return ret;
}

void ka_sound_file_close(ka_sound_file *f) {
        ka_assert(f);

        if (f->wav)
                ka_wav_close(f->wav);
        if (f->vorbis)
                ka_vorbis_close(f->vorbis);

        ka_free(f->filename);
        ka_free(f);
}

unsigned ka_sound_file_get_nchannels(ka_sound_file *f) {
        ka_assert(f);
        return f->nchannels;
}

unsigned ka_sound_file_get_rate(ka_sound_file *f) {
        ka_assert(f);
        return f->rate;
}

ka_sample_type_t ka_sound_file_get_sample_type(ka_sound_file *f) {
        ka_assert(f);
        return f->type;
}

const ka_channel_position_t* ka_sound_file_get_channel_map(ka_sound_file *f) {
        ka_assert(f);

        if (f->wav)
                return ka_wav_get_channel_map(f->wav);
        else
                return ka_vorbis_get_channel_map(f->vorbis);
}

int ka_sound_file_read_int16(ka_sound_file *f, int16_t *d, size_t *n) {
        ka_return_val_if_fail(f, CA_ERROR_INVALID);
        ka_return_val_if_fail(d, CA_ERROR_INVALID);
        ka_return_val_if_fail(n, CA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, CA_ERROR_INVALID);
        ka_return_val_if_fail(f->wav || f->vorbis, CA_ERROR_STATE);
        ka_return_val_if_fail(f->type == CA_SAMPLE_S16NE || f->type == CA_SAMPLE_S16RE, CA_ERROR_STATE);

        if (f->wav)
                return ka_wav_read_s16le(f->wav, d, n);
        else
                return ka_vorbis_read_s16ne(f->vorbis, d, n);
}

int ka_sound_file_read_uint8(ka_sound_file *f, uint8_t *d, size_t *n) {
        ka_return_val_if_fail(f, CA_ERROR_INVALID);
        ka_return_val_if_fail(d, CA_ERROR_INVALID);
        ka_return_val_if_fail(n, CA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, CA_ERROR_INVALID);
        ka_return_val_if_fail(f->wav && !f->vorbis, CA_ERROR_STATE);
        ka_return_val_if_fail(f->type == CA_SAMPLE_U8, CA_ERROR_STATE);

        if (f->wav)
                return ka_wav_read_u8(f->wav, d, n);

        return CA_ERROR_STATE;
}

int ka_sound_file_read_arbitrary(ka_sound_file *f, void *d, size_t *n) {
        int ret;

        ka_return_val_if_fail(f, CA_ERROR_INVALID);
        ka_return_val_if_fail(d, CA_ERROR_INVALID);
        ka_return_val_if_fail(n, CA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, CA_ERROR_INVALID);

        switch (f->type) {
        case CA_SAMPLE_S16NE:
        case CA_SAMPLE_S16RE: {
                size_t k;

                k = *n / sizeof(int16_t);
                if ((ret = ka_sound_file_read_int16(f, d, &k)) == CA_SUCCESS)
                        *n = k * sizeof(int16_t);

                break;
        }

        case CA_SAMPLE_U8: {
                size_t k;

                k = *n;
                if ((ret = ka_sound_file_read_uint8(f, d, &k)) == CA_SUCCESS)
                        *n = k;

                break;
        }

        default:
                ka_assert_not_reached();
        }

        return ret;
}

off_t ka_sound_file_get_size(ka_sound_file *f) {
        ka_return_val_if_fail(f, (off_t) -1);

        if (f->wav)
                return ka_wav_get_size(f->wav);
        else
                return ka_vorbis_get_size(f->vorbis);
}

size_t ka_sound_file_frame_size(ka_sound_file *f) {
        unsigned c;

        ka_assert(f);

        c = ka_sound_file_get_nchannels(f);

        return c * (ka_sound_file_get_sample_type(f) == CA_SAMPLE_U8 ? 1U : 2U);
}
