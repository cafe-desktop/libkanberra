/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberraproplisthfoo
#define fookanberraproplisthfoo

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

#include <stdarg.h>

#include "kanberra.h"
#include "mutex.h"

#define N_HASHTABLE 31

typedef struct ka_prop {
        char *key;
        size_t nbytes;
        struct ka_prop *next_in_slot, *next_item, *prev_item;
} ka_prop;

#define CA_PROP_DATA(p) ((void*) ((char*) (p) + CA_ALIGN(sizeof(ka_prop))))

struct ka_proplist {
        ka_mutex *mutex;

        ka_prop *prop_hashtable[N_HASHTABLE];
        ka_prop *first_item;
};

int ka_proplist_merge(ka_proplist **_a, ka_proplist *b, ka_proplist *c);
ka_bool_t ka_proplist_contains(ka_proplist *p, const char *key);

/* Both of the following two functions are not locked! Need manual locking! */
ka_prop* ka_proplist_get_unlocked(ka_proplist *p, const char *key);
const char* ka_proplist_gets_unlocked(ka_proplist *p, const char *key);

int ka_proplist_merge_ap(ka_proplist *p, va_list ap);
int ka_proplist_from_ap(ka_proplist **_p, va_list ap);

#endif
