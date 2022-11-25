/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberracachehfoo
#define fookanberracachehfoo

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

#include "read-sound-file.h"

int ka_cache_lookup_sound(
                ka_sound_file **f,
                ka_sound_file_open_callback_t sfopen,
                char **sound_path,
                const char *theme,
                const char *name,
                const char *locale,
                const char *profile);

int ka_cache_store_sound(
                const char *theme,
                const char *name,
                const char *locale,
                const char *profile,
                const char *fname);

#endif
