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

#include "kanberra.h"
#include "read-wav.h"
#include "macro.h"
#include "malloc.h"

#define FILE_SIZE_MAX (64U*1024U*1024U)

/* Stores the bit indexes in dwChannelMask */
enum {
        BIT_FRONT_LEFT,
        BIT_FRONT_RIGHT,
        BIT_FRONT_CENTER,
        BIT_LOW_FREQUENCY,
        BIT_BACK_LEFT,
        BIT_BACK_RIGHT,
        BIT_FRONT_LEFT_OF_CENTER,
        BIT_FRONT_RIGHT_OF_CENTER,
        BIT_BACK_CENTER,
        BIT_SIDE_LEFT,
        BIT_SIDE_RIGHT,
        BIT_TOP_CENTER,
        BIT_TOP_FRONT_LEFT,
        BIT_TOP_FRONT_CENTER,
        BIT_TOP_FRONT_RIGHT,
        BIT_TOP_BACK_LEFT,
        BIT_TOP_BACK_CENTER,
        BIT_TOP_BACK_RIGHT,
        _BIT_MAX
};

static const ka_channel_position_t channel_table[_BIT_MAX] = {
        [BIT_FRONT_LEFT] = CA_CHANNEL_FRONT_LEFT,
        [BIT_FRONT_RIGHT] = CA_CHANNEL_FRONT_RIGHT,
        [BIT_FRONT_CENTER] = CA_CHANNEL_FRONT_CENTER,
        [BIT_LOW_FREQUENCY] = CA_CHANNEL_LFE,
        [BIT_BACK_LEFT] = CA_CHANNEL_REAR_LEFT,
        [BIT_BACK_RIGHT] = CA_CHANNEL_REAR_RIGHT,
        [BIT_FRONT_LEFT_OF_CENTER] = CA_CHANNEL_FRONT_LEFT_OF_CENTER,
        [BIT_FRONT_RIGHT_OF_CENTER] = CA_CHANNEL_FRONT_RIGHT_OF_CENTER,
        [BIT_BACK_CENTER] = CA_CHANNEL_REAR_CENTER,
        [BIT_SIDE_LEFT] = CA_CHANNEL_SIDE_LEFT,
        [BIT_SIDE_RIGHT] = CA_CHANNEL_SIDE_RIGHT,
        [BIT_TOP_CENTER] = CA_CHANNEL_TOP_CENTER,
        [BIT_TOP_FRONT_LEFT] = CA_CHANNEL_TOP_FRONT_LEFT,
        [BIT_TOP_FRONT_CENTER] = CA_CHANNEL_TOP_FRONT_CENTER,
        [BIT_TOP_FRONT_RIGHT] = CA_CHANNEL_TOP_FRONT_RIGHT,
        [BIT_TOP_BACK_LEFT] = CA_CHANNEL_TOP_REAR_LEFT,
        [BIT_TOP_BACK_CENTER] = CA_CHANNEL_TOP_REAR_CENTER,
        [BIT_TOP_BACK_RIGHT] = CA_CHANNEL_TOP_REAR_RIGHT
};

struct ka_wav {
        FILE *file;

        off_t data_size;
        unsigned nchannels;
        unsigned rate;
        unsigned depth;
        uint32_t channel_mask;

        ka_channel_position_t channel_map[_BIT_MAX];
};

#define CHUNK_ID_DATA 0x61746164U
#define CHUNK_ID_FMT 0x20746d66U

static const uint8_t pcm_guid[16] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static int skip_to_chunk(ka_wav *w, uint32_t id, uint32_t *size) {

        ka_return_val_if_fail(w, CA_ERROR_INVALID);
        ka_return_val_if_fail(size, CA_ERROR_INVALID);

        for (;;) {
                uint32_t chunk[2];
                uint32_t s;

                if (fread(chunk, sizeof(uint32_t), CA_ELEMENTSOF(chunk), w->file) != CA_ELEMENTSOF(chunk))
                        goto fail_io;

                s = CA_UINT32_FROM_LE(chunk[1]);

                if (s <= 0 || s >= FILE_SIZE_MAX)
                        return CA_ERROR_TOOBIG;

                if (CA_UINT32_FROM_LE(chunk[0]) == id) {
                        *size = s;
                        break;
                }

                if (fseek(w->file, (long) s, SEEK_CUR) < 0)
                        return CA_ERROR_SYSTEM;
        }

        return CA_SUCCESS;

fail_io:

        if (feof(w->file))
                return CA_ERROR_CORRUPT;
        else if (ferror(w->file))
                return CA_ERROR_SYSTEM;

        ka_assert_not_reached();
}

int ka_wav_open(ka_wav **_w, FILE *f)  {
        uint32_t header[3], fmt_chunk[10];
        int ret;
        ka_wav *w;
        uint32_t file_size, fmt_size, data_size;
        ka_bool_t extensible;
        uint32_t format;

        ka_return_val_if_fail(_w, CA_ERROR_INVALID);
        ka_return_val_if_fail(f, CA_ERROR_INVALID);

        if (!(w = ka_new(ka_wav, 1)))
                return CA_ERROR_OOM;

        w->file = f;

        if (fread(header, sizeof(uint32_t), CA_ELEMENTSOF(header), f) != CA_ELEMENTSOF(header))
                goto fail_io;

        if (CA_UINT32_FROM_LE(header[0]) != 0x46464952U ||
            CA_UINT32_FROM_LE(header[2]) != 0x45564157U) {
                ret = CA_ERROR_CORRUPT;
                goto fail;
        }

        file_size = CA_UINT32_FROM_LE(header[1]);

        if (file_size <= 0 || file_size >= FILE_SIZE_MAX) {
                ret = CA_ERROR_TOOBIG;
                goto fail;
        }

        /* Skip to the fmt chunk */
        if ((ret = skip_to_chunk(w, CHUNK_ID_FMT, &fmt_size)) < 0)
                goto fail;

        switch (fmt_size) {

        case 14: /* WAVEFORMAT */
        case 16:
        case 18: /* WAVEFORMATEX */
                extensible = FALSE;
                break;

        case 40: /* WAVEFORMATEXTENSIBLE */
                extensible = TRUE;
                break;

        default:
                ret = CA_ERROR_NOTSUPPORTED;
                goto fail;
        }

        if (fread(fmt_chunk, 1, fmt_size, f) != fmt_size)
                goto fail_io;

        /* PCM? or WAVEX? */
        format = (CA_UINT32_FROM_LE(fmt_chunk[0]) & 0xFFFF);
        if ((!extensible && format != 0x0001) ||
            (extensible && format != 0xFFFE)) {
                ret = CA_ERROR_NOTSUPPORTED;
                goto fail;
        }

        if (extensible) {
                if (memcmp(fmt_chunk + 6, pcm_guid, 16) != 0) {
                        ret = CA_ERROR_NOTSUPPORTED;
                        goto fail;
                }

                w->channel_mask = CA_UINT32_FROM_LE(fmt_chunk[5]);
        } else
                w->channel_mask = 0;

        w->nchannels = CA_UINT32_FROM_LE(fmt_chunk[0]) >> 16;
        w->rate = CA_UINT32_FROM_LE(fmt_chunk[1]);
        w->depth = CA_UINT32_FROM_LE(fmt_chunk[3]) >> 16;

        if (w->nchannels <= 0 || w->rate <= 0) {
                ret = CA_ERROR_CORRUPT;
                goto fail;
        }

        if (w->depth != 16 && w->depth != 8) {
                ret = CA_ERROR_NOTSUPPORTED;
                goto fail;
        }

        /* Skip to the data chunk */
        if ((ret = skip_to_chunk(w, CHUNK_ID_DATA, &data_size)) < 0)
                goto fail;
        w->data_size = (off_t) data_size;

        if ((w->data_size % (w->depth/8)) != 0) {
                ret = CA_ERROR_CORRUPT;
                goto fail;
        }

        *_w = w;

        return CA_SUCCESS;

fail_io:

        if (feof(f))
                ret = CA_ERROR_CORRUPT;
        else if (ferror(f))
                ret = CA_ERROR_SYSTEM;
        else
                ka_assert_not_reached();

fail:

        ka_free(w);

        return ret;
}

void ka_wav_close(ka_wav *w) {
        ka_assert(w);

        fclose(w->file);
        ka_free(w);
}

unsigned ka_wav_get_nchannels(ka_wav *w) {
        ka_assert(w);

        return w->nchannels;
}

unsigned ka_wav_get_rate(ka_wav *w) {
        ka_assert(w);

        return w->rate;
}

const ka_channel_position_t* ka_wav_get_channel_map(ka_wav *w) {
        unsigned c;
        ka_channel_position_t *p;

        ka_assert(w);

        if (!w->channel_mask)
                return NULL;

        p = w->channel_map;

        for (c = 0; c < _BIT_MAX; c++)
                if ((w->channel_mask & (1 << c)))
                        *(p++) = channel_table[c];

        ka_assert(p <= w->channel_map + _BIT_MAX);

        if (p != w->channel_map + w->nchannels)
                return NULL;

        return w->channel_map;
}

ka_sample_type_t ka_wav_get_sample_type(ka_wav *w) {
        ka_assert(w);

        return w->depth == 16 ?
#ifdef WORDS_BIGENDIAN
                CA_SAMPLE_S16RE
#else
                CA_SAMPLE_S16NE
#endif
                : CA_SAMPLE_U8;
}

int ka_wav_read_s16le(ka_wav *w, int16_t *d, size_t *n) {
        off_t remaining;

        ka_return_val_if_fail(w, CA_ERROR_INVALID);
        ka_return_val_if_fail(w->depth == 16, CA_ERROR_INVALID);
        ka_return_val_if_fail(d, CA_ERROR_INVALID);
        ka_return_val_if_fail(n, CA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, CA_ERROR_INVALID);

        remaining = w->data_size / (off_t) sizeof(int16_t);

        if ((off_t) *n > remaining)
                *n = (size_t) remaining;

        if (*n > 0) {
                *n = fread(d, sizeof(int16_t), *n, w->file);

                if (*n <= 0 && ferror(w->file))
                        return CA_ERROR_SYSTEM;

                ka_assert(w->data_size >= (off_t) *n * (off_t) sizeof(int16_t));
                w->data_size -= (off_t) *n * (off_t) sizeof(int16_t);
        }

        return CA_SUCCESS;
}

int ka_wav_read_u8(ka_wav *w, uint8_t *d, size_t *n) {
        off_t remaining;

        ka_return_val_if_fail(w, CA_ERROR_INVALID);
        ka_return_val_if_fail(w->depth == 8, CA_ERROR_INVALID);
        ka_return_val_if_fail(d, CA_ERROR_INVALID);
        ka_return_val_if_fail(n, CA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, CA_ERROR_INVALID);

        remaining = w->data_size / (off_t) sizeof(uint8_t);

        if ((off_t) *n > remaining)
                *n = (size_t) remaining;

        if (*n > 0) {
                *n = fread(d, sizeof(uint8_t), *n, w->file);

                if (*n <= 0 && ferror(w->file))
                        return CA_ERROR_SYSTEM;

                ka_assert(w->data_size >= (off_t) *n * (off_t) sizeof(uint8_t));
                w->data_size -= (off_t) *n * (off_t) sizeof(uint8_t);
        }

        return CA_SUCCESS;
}

off_t ka_wav_get_size(ka_wav *v) {
        ka_return_val_if_fail(v, (off_t) -1);

        return v->data_size;
}
