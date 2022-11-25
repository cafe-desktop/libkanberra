/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberradriverhfoo
#define fookanberradriverhfoo

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

#include "kanberra.h"

int driver_open(ka_context *c);
int driver_destroy(ka_context *c);

int driver_change_device(ka_context *c, const char *device);
int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged);

int driver_play(ka_context *c, uint32_t id, ka_proplist *p, ka_finish_callback_t cb, void *userdata);
int driver_cancel(ka_context *c, uint32_t id);
int driver_cache(ka_context *c, ka_proplist *p);

int driver_playing(ka_context *c, uint32_t id, int *playing);

#endif
