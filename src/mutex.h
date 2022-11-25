/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberramutexhfoo
#define fookanberramutexhfoo

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

#include "macro.h"

typedef struct ka_mutex ka_mutex;

ka_mutex* ka_mutex_new(void);
void ka_mutex_free(ka_mutex *m);

void ka_mutex_lock(ka_mutex *m);
ka_bool_t ka_mutex_try_lock(ka_mutex *m);
void ka_mutex_unlock(ka_mutex *m);

#endif
