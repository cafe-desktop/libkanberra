/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberramallochfoo
#define fookanberramallochfoo

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

#include <stdlib.h>
#include <string.h>

#include "kanberra.h"
#include "macro.h"

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#define ka_malloc malloc
#define ka_free free
#define ka_malloc0(size) calloc(1, (size))
#define ka_strdup strdup
#ifdef HAVE_STRNDUP
#define ka_strndup strndup
#else
char *ka_strndup(const char *s, size_t n);
#endif

void* ka_memdup(const void* p, size_t size);

#define ka_new(t, n) ((t*) ka_malloc(sizeof(t)*(n)))
#define ka_new0(t, n) ((t*) ka_malloc0(sizeof(t)*(n)))
#define ka_newdup(t, p, n) ((t*) ka_memdup(p, sizeof(t)*(n)))

char *ka_sprintf_malloc(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
