/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberrareadwavhfoo
#define fookanberrareadwavhfoo

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

#include "read-sound-file.h"

typedef struct ka_wav ka_wav;

int ka_wav_open(ka_wav **v, FILE *f);
void ka_wav_close(ka_wav *f);

unsigned ka_wav_get_nchannels(ka_wav *f);
unsigned ka_wav_get_rate(ka_wav *f);
ka_sample_type_t ka_wav_get_sample_type(ka_wav *f);
const ka_channel_position_t* ka_wav_get_channel_map(ka_wav *f);

int ka_wav_read_u8(ka_wav *f, uint8_t *d, size_t *n);
int ka_wav_read_s16le(ka_wav *f, int16_t *d, size_t *n);

off_t ka_wav_get_size(ka_wav *f);

#endif
