/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberrasoundthemespechfoo
#define fookanberrasoundthemespechfoo

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
#include "proplist.h"

typedef struct ka_theme_data ka_theme_data;

typedef int (*ka_sound_file_open_callback_t)(ka_sound_file **f, const char *fn);

int ka_lookup_sound(ka_sound_file **f, char **sound_path, ka_theme_data **t, ka_proplist *cp, ka_proplist *sp);
int ka_lookup_sound_with_callback(ka_sound_file **f, ka_sound_file_open_callback_t sfopen, char **sound_path, ka_theme_data **t, ka_proplist *cp, ka_proplist *sp);
void ka_theme_data_free(ka_theme_data *t);

int ka_get_data_home(char **e);
const char *ka_get_data_dirs(void);

#endif
