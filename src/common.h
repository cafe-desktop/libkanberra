/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberracommonh
#define fookanberracommonh

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
#include "macro.h"
#include "mutex.h"

struct ka_context {
        ka_bool_t opened;
        ka_mutex *mutex;

        ka_proplist *props;

        char *driver;
        char *device;

        void *private;
#ifdef HAVE_DSO
        void *private_dso;
#endif
};

typedef enum ka_cache_control {
        CA_CACHE_CONTROL_NEVER,
        CA_CACHE_CONTROL_PERMANENT,
        CA_CACHE_CONTROL_VOLATILE
} ka_cache_control_t;

int ka_parse_cache_control(ka_cache_control_t *control, const char *c);

#endif
