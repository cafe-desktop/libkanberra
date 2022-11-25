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

#include <pthread.h>
#include <errno.h>

#include "mutex.h"
#include "malloc.h"

struct ka_mutex {
        pthread_mutex_t mutex;
};

ka_mutex* ka_mutex_new(void) {
        ka_mutex *m;

        if (!(m = ka_new(ka_mutex, 1)))
                return NULL;

        if (pthread_mutex_init(&m->mutex, NULL) < 0) {
                ka_free(m);
                return NULL;
        }

        return m;
}

void ka_mutex_free(ka_mutex *m) {
        ka_assert(m);

        ka_assert_se(pthread_mutex_destroy(&m->mutex) == 0);
        ka_free(m);
}

void ka_mutex_lock(ka_mutex *m) {
        ka_assert(m);

        ka_assert_se(pthread_mutex_lock(&m->mutex) == 0);
}

ka_bool_t ka_mutex_try_lock(ka_mutex *m) {
        int r;
        ka_assert(m);

        if ((r = pthread_mutex_trylock(&m->mutex)) != 0) {
                ka_assert(r == EBUSY);
                return FALSE;
        }

        return TRUE;
}

void ka_mutex_unlock(ka_mutex *m) {
        ka_assert(m);

        ka_assert_se(pthread_mutex_unlock(&m->mutex) == 0);
}
