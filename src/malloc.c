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

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "malloc.h"
#include "macro.h"

void* ka_memdup(const void* p, size_t size) {
        void *r;

        ka_assert(p);

        if (!(r = ka_malloc(size)))
                return NULL;

        memcpy(r, p, size);
        return r;
}

char *ka_sprintf_malloc(const char *format, ...) {
        size_t  size = 100;
        char *c = NULL;

        ka_assert(format);

        for(;;) {
                int r;
                va_list ap;

                ka_free(c);

                if (!(c = ka_new(char, size)))
                        return NULL;

                va_start(ap, format);
                r = vsnprintf(c, size, format, ap);
                va_end(ap);

                c[size-1] = 0;

                if (r > -1 && (size_t) r < size)
                        return c;

                if (r > -1)    /* glibc 2.1 */
                        size = (size_t) r+1;
                else           /* glibc 2.0 */
                        size *= 2;
        }
}

#ifndef HAVE_STRNDUP
char *ka_strndup(const char *s, size_t n) {
        size_t n_avail;
        char *p;

        if (!s)
                return NULL;

        if (memchr(s, '\0', n)) {
                n_avail = strlen(s);
                if (n_avail > n)
                        n_avail = n;
        } else
                n_avail = n;

        if (!(p = ka_new(char, n_avail + 1)))
                return NULL;

        memcpy(p, s, n_avail);
        p[n_avail] = '\0';

        return p;
}
#endif
