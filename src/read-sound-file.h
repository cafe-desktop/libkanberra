/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberrareadsoundfilehfoo
#define fookanberrareadsoundfilehfoo

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

#include <sys/types.h>
#include <inttypes.h>

typedef enum ka_sample_type {
        KA_SAMPLE_S16NE,
        KA_SAMPLE_S16RE,
        KA_SAMPLE_U8
} ka_sample_type_t;

typedef enum ka_channel_position {
        KA_CHANNEL_MONO,
        KA_CHANNEL_FRONT_LEFT,
        KA_CHANNEL_FRONT_RIGHT,
        KA_CHANNEL_FRONT_CENTER,
        KA_CHANNEL_REAR_LEFT,
        KA_CHANNEL_REAR_RIGHT,
        KA_CHANNEL_REAR_CENTER,
        KA_CHANNEL_LFE,
        KA_CHANNEL_FRONT_LEFT_OF_CENTER,
        KA_CHANNEL_FRONT_RIGHT_OF_CENTER,
        KA_CHANNEL_SIDE_LEFT,
        KA_CHANNEL_SIDE_RIGHT,
        KA_CHANNEL_TOP_CENTER,
        KA_CHANNEL_TOP_FRONT_LEFT,
        KA_CHANNEL_TOP_FRONT_RIGHT,
        KA_CHANNEL_TOP_FRONT_CENTER,
        KA_CHANNEL_TOP_REAR_LEFT,
        KA_CHANNEL_TOP_REAR_RIGHT,
        KA_CHANNEL_TOP_REAR_CENTER,
        _KA_CHANNEL_POSITION_MAX
} ka_channel_position_t;

typedef struct ka_sound_file ka_sound_file;

int ka_sound_file_open(ka_sound_file **f, const char *fn);
void ka_sound_file_close(ka_sound_file *f);

unsigned ka_sound_file_get_nchannels(ka_sound_file *f);
unsigned ka_sound_file_get_rate(ka_sound_file *f);
ka_sample_type_t ka_sound_file_get_sample_type(ka_sound_file *f);
const ka_channel_position_t* ka_sound_file_get_channel_map(ka_sound_file *f);

off_t ka_sound_file_get_size(ka_sound_file *f);

int ka_sound_file_read_int16(ka_sound_file *f, int16_t *d, size_t *n);
int ka_sound_file_read_uint8(ka_sound_file *f, uint8_t *d, size_t *n);

int ka_sound_file_read_arbitrary(ka_sound_file *f, void *d, size_t *n);

size_t ka_sound_file_frame_size(ka_sound_file *f);

#endif
