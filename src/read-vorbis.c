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

#include <vorbis/vorbisfile.h>
#include <vorbis/codec.h>

#include "kanberra.h"
#include "read-vorbis.h"
#include "macro.h"
#include "malloc.h"

#define FILE_SIZE_MAX ((off_t) (64U*1024U*1024U))

struct ka_vorbis {
        OggVorbis_File ovf;
        off_t size;
        ka_channel_position_t channel_map[8];
};

static int convert_error(int or) {
        switch (or) {
        case OV_ENOSEEK:
        case OV_EBADPACKET:
        case OV_EBADLINK:
        case OV_EFAULT:
        case OV_EREAD:
        case OV_HOLE:
                return KA_ERROR_IO;

        case OV_EIMPL:
        case OV_EVERSION:
        case OV_ENOTAUDIO:
                return KA_ERROR_NOTSUPPORTED;

        case OV_ENOTVORBIS:
        case OV_EBADHEADER:
        case OV_EOF:
                return KA_ERROR_CORRUPT;

        case OV_EINVAL:
                return KA_ERROR_INVALID;

        default:
                return KA_ERROR_IO;
        }
}

int ka_vorbis_open(ka_vorbis **_v, FILE *f)  {
        int ret, or;
        ka_vorbis *v;
        int64_t n;

        ka_return_val_if_fail(_v, KA_ERROR_INVALID);
        ka_return_val_if_fail(f, KA_ERROR_INVALID);

        if (!(v = ka_new0(ka_vorbis, 1)))
                return KA_ERROR_OOM;

        if ((or = ov_open(f, &v->ovf, NULL, 0)) < 0) {
                ret = convert_error(or);
                goto fail;
        }

        if ((n = ov_pcm_total(&v->ovf, -1)) < 0) {
                ret = convert_error(or);
                ov_clear(&v->ovf);
                goto fail;
        }

        if (((off_t) n * (off_t) sizeof(int16_t)) > FILE_SIZE_MAX) {
                ret = KA_ERROR_TOOBIG;
                ov_clear(&v->ovf);
                goto fail;
        }

        v->size = (off_t) n * (off_t) sizeof(int16_t) * ka_vorbis_get_nchannels(v);

        *_v = v;

        return KA_SUCCESS;

fail:

        ka_free(v);
        return ret;
}

void ka_vorbis_close(ka_vorbis *v) {
        ka_assert(v);

        ov_clear(&v->ovf);
        ka_free(v);
}

unsigned ka_vorbis_get_nchannels(ka_vorbis *v) {
        const vorbis_info *vi;
        ka_assert(v);

        ka_assert_se(vi = ov_info(&v->ovf, -1));

        return (unsigned) vi->channels;
}

unsigned ka_vorbis_get_rate(ka_vorbis *v) {
        const vorbis_info *vi;
        ka_assert(v);

        ka_assert_se(vi = ov_info(&v->ovf, -1));

        return (unsigned) vi->rate;
}

const ka_channel_position_t* ka_vorbis_get_channel_map(ka_vorbis *v) {

        /* See http://www.xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-800004.3.9 */

        switch (ka_vorbis_get_nchannels(v)) {
        case 8:
                v->channel_map[0] = KA_CHANNEL_FRONT_LEFT;
                v->channel_map[1] = KA_CHANNEL_FRONT_CENTER;
                v->channel_map[2] = KA_CHANNEL_FRONT_RIGHT;
                v->channel_map[3] = KA_CHANNEL_SIDE_LEFT;
                v->channel_map[4] = KA_CHANNEL_SIDE_RIGHT;
                v->channel_map[5] = KA_CHANNEL_REAR_LEFT;
                v->channel_map[6] = KA_CHANNEL_REAR_RIGHT;
                v->channel_map[7] = KA_CHANNEL_LFE;
                return v->channel_map;

        case 7:
                v->channel_map[0] = KA_CHANNEL_FRONT_LEFT;
                v->channel_map[1] = KA_CHANNEL_FRONT_CENTER;
                v->channel_map[2] = KA_CHANNEL_FRONT_RIGHT;
                v->channel_map[3] = KA_CHANNEL_SIDE_LEFT;
                v->channel_map[4] = KA_CHANNEL_SIDE_RIGHT;
                v->channel_map[5] = KA_CHANNEL_REAR_CENTER;
                v->channel_map[6] = KA_CHANNEL_LFE;
                return v->channel_map;

        case 6:
                v->channel_map[5] = KA_CHANNEL_LFE;
                /* fall through */

        case 5:
                v->channel_map[3] = KA_CHANNEL_REAR_LEFT;
                v->channel_map[4] = KA_CHANNEL_REAR_RIGHT;
                /* fall through */

        case 3:
                v->channel_map[0] = KA_CHANNEL_FRONT_LEFT;
                v->channel_map[1] = KA_CHANNEL_FRONT_CENTER;
                v->channel_map[2] = KA_CHANNEL_FRONT_RIGHT;
                return v->channel_map;

        case 4:
                v->channel_map[2] = KA_CHANNEL_REAR_LEFT;
                v->channel_map[3] = KA_CHANNEL_REAR_RIGHT;
                /* fall through */

        case 2:
                v->channel_map[0] = KA_CHANNEL_FRONT_LEFT;
                v->channel_map[1] = KA_CHANNEL_FRONT_RIGHT;
                return v->channel_map;

        case 1:
                v->channel_map[0] = KA_CHANNEL_MONO;
                return v->channel_map;
        }

        return NULL;
}

int ka_vorbis_read_s16ne(ka_vorbis *v, int16_t *d, size_t *n){
        long r;
        int section;
        int length;
        size_t n_read = 0;

        ka_return_val_if_fail(v, KA_ERROR_INVALID);
        ka_return_val_if_fail(d, KA_ERROR_INVALID);
        ka_return_val_if_fail(n, KA_ERROR_INVALID);
        ka_return_val_if_fail(*n > 0, KA_ERROR_INVALID);

        length = (int) (*n * sizeof(int16_t));

        do {

                r = ov_read(&v->ovf, (char*) d, length,
#ifdef WORDS_BIGENDIAN
                            1,
#else
                            0,
#endif
                            2, 1, &section);

                if (r < 0)
                        return convert_error((int) r);

                if (r == 0)
                        break;

                /* We only read the first section */
                if (section != 0)
                        break;

                length -= (int) r;
                d += r/sizeof(int16_t);
                n_read += (size_t) r;

        } while (length >= 4096);

        ka_assert(v->size >= (off_t) n_read);
        v->size -= (off_t) n_read;

        *n = n_read/sizeof(int16_t);

        return KA_SUCCESS;
}

off_t ka_vorbis_get_size(ka_vorbis *v) {
        ka_return_val_if_fail(v, (off_t) -1);

        return v->size;
}
