/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberrareadvorbishfoo
#define fookanberrareadvorbishfoo

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

#include <stdio.h>
#include <inttypes.h>

#include "read-sound-file.h"

typedef struct ka_vorbis ka_vorbis;

int ka_vorbis_open(ka_vorbis **v, FILE *f);
void ka_vorbis_close(ka_vorbis *v);

unsigned ka_vorbis_get_nchannels(ka_vorbis *v);
unsigned ka_vorbis_get_rate(ka_vorbis *v);
const ka_channel_position_t* ka_vorbis_get_channel_map(ka_vorbis *v);

int ka_vorbis_read_s16ne(ka_vorbis *v, int16_t *d, size_t *n);

off_t ka_vorbis_get_size(ka_vorbis *f);

#endif
