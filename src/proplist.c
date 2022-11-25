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

#include <stdarg.h>

#include "kanberra.h"
#include "proplist.h"
#include "macro.h"
#include "malloc.h"

static unsigned calc_hash(const char *c) {
        unsigned hash = 0;

        for (; *c; c++)
                hash = 31 * hash + (unsigned) *c;

        return hash;
}

/**
 * ka_proplist_create:
 * @p: A pointer where to fill in a pointer for the new property list.
 *
 * Allocate a new empty property list.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_proplist_create(ka_proplist **_p) {
        ka_proplist *p;
        ka_return_val_if_fail(_p, KA_ERROR_INVALID);

        if (!(p = ka_new0(ka_proplist, 1)))
                return KA_ERROR_OOM;

        if (!(p->mutex = ka_mutex_new())) {
                ka_free(p);
                return KA_ERROR_OOM;
        }

        *_p = p;

        return KA_SUCCESS;
}

static int _unset(ka_proplist *p, const char *key) {
        ka_prop *prop, *nprop;
        unsigned i;

        ka_return_val_if_fail(p, KA_ERROR_INVALID);
        ka_return_val_if_fail(key, KA_ERROR_INVALID);

        i = calc_hash(key) % N_HASHTABLE;

        nprop = NULL;
        for (prop = p->prop_hashtable[i]; prop; nprop = prop, prop = prop->next_in_slot)
                if (strcmp(prop->key, key) == 0)
                        break;

        if (prop) {
                if (nprop)
                        nprop->next_in_slot = prop->next_in_slot;
                else
                        p->prop_hashtable[i] = prop->next_in_slot;

                if (prop->prev_item)
                        prop->prev_item->next_item = prop->next_item;
                else
                        p->first_item = prop->next_item;

                if (prop->next_item)
                        prop->next_item->prev_item = prop->prev_item;

                ka_free(prop->key);
                ka_free(prop);
        }

        return KA_SUCCESS;
}

/**
 * ka_proplist_sets:
 * @p: The property list to add this key/value pair to
 * @key: The key for this key/value pair
 * @value: The value for this key/value pair
 *
 * Add a new string key/value pair to the property list.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_proplist_sets(ka_proplist *p, const char *key, const char *value) {
        ka_return_val_if_fail(p, KA_ERROR_INVALID);
        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(value, KA_ERROR_INVALID);

        return ka_proplist_set(p, key, value, strlen(value)+1);
}

/**
 * ka_proplist_setf:
 * @p: The property list to add this key/value pair to
 * @key: The key for this key/value pair
 * @format: The format string for the value for this key/value pair
 * @...: The parameters for the format string
 *
 * Much like ka_proplist_sets(): add a new string key/value pair to
 * the property list. Takes a standard C format string plus arguments
 * and formats a string of it.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_proplist_setf(ka_proplist *p, const char *key, const char *format, ...) {
        int ret;
        char *k;
        ka_prop *prop;
        size_t size = 100;
        unsigned h;

        ka_return_val_if_fail(p, KA_ERROR_INVALID);
        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(format, KA_ERROR_INVALID);

        if (!(k = ka_strdup(key)))
                return KA_ERROR_OOM;

        for (;;) {
                va_list ap;
                int r;

                if (!(prop = ka_malloc(KA_ALIGN(sizeof(ka_prop)) + size))) {
                        ka_free(k);
                        return KA_ERROR_OOM;
                }


                va_start(ap, format);
                r = vsnprintf(KA_PROP_DATA(prop), size, format, ap);
                va_end(ap);

                ((char*) KA_PROP_DATA(prop))[size-1] = 0;

                if (r > -1 && (size_t) r < size) {
                        prop->nbytes = (size_t) r+1;
                        break;
                }

                if (r > -1)    /* glibc 2.1 */
                        size = (size_t) r+1;
                else           /* glibc 2.0 */
                        size *= 2;

                ka_free(prop);
        }

        prop->key = k;

        ka_mutex_lock(p->mutex);

        if ((ret = _unset(p, key)) < 0) {
                ka_free(prop);
                ka_free(k);
                goto finish;
        }

        h = calc_hash(key) % N_HASHTABLE;

        prop->next_in_slot = p->prop_hashtable[h];
        p->prop_hashtable[h] = prop;

        prop->prev_item = NULL;
        if ((prop->next_item = p->first_item))
                prop->next_item->prev_item = prop;
        p->first_item = prop;

finish:

        ka_mutex_unlock(p->mutex);

        return ret;
}

/**
 * ka_proplist_set:
 * @p: The property list to add this key/value pair to
 * @key: The key for this key/value pair
 * @data: The binary value for this key value pair
 * @nbytes: The size of thebinary value for this key value pair.
 *
 * Add a new binary key/value pair to the property list.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_proplist_set(ka_proplist *p, const char *key, const void *data, size_t nbytes) {
        int ret;
        char *k;
        ka_prop *prop;
        unsigned h;

        ka_return_val_if_fail(p, KA_ERROR_INVALID);
        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(!nbytes || data, KA_ERROR_INVALID);

        if (!(k = ka_strdup(key)))
                return KA_ERROR_OOM;

        if (!(prop = ka_malloc(KA_ALIGN(sizeof(ka_prop)) + nbytes))) {
                ka_free(k);
                return KA_ERROR_OOM;
        }

        prop->key = k;
        prop->nbytes = nbytes;
        memcpy(KA_PROP_DATA(prop), data, nbytes);

        ka_mutex_lock(p->mutex);

        if ((ret = _unset(p, key)) < 0) {
                ka_free(prop);
                ka_free(k);
                goto finish;
        }

        h = calc_hash(key) % N_HASHTABLE;

        prop->next_in_slot = p->prop_hashtable[h];
        p->prop_hashtable[h] = prop;

        prop->prev_item = NULL;
        if ((prop->next_item = p->first_item))
                prop->next_item->prev_item = prop;
        p->first_item = prop;

finish:

        ka_mutex_unlock(p->mutex);

        return ret;
}

/* Not exported, not self-locking */
ka_prop* ka_proplist_get_unlocked(ka_proplist *p, const char *key) {
        ka_prop *prop;
        unsigned i;

        ka_return_val_if_fail(p, NULL);
        ka_return_val_if_fail(key, NULL);

        i = calc_hash(key) % N_HASHTABLE;

        for (prop = p->prop_hashtable[i]; prop; prop = prop->next_in_slot)
                if (strcmp(prop->key, key) == 0)
                        return prop;

        return NULL;
}

/* Not exported, not self-locking */
const char* ka_proplist_gets_unlocked(ka_proplist *p, const char *key) {
        ka_prop *prop;

        ka_return_val_if_fail(p, NULL);
        ka_return_val_if_fail(key, NULL);

        if (!(prop = ka_proplist_get_unlocked(p, key)))
                return NULL;

        if (!memchr(KA_PROP_DATA(prop), 0, prop->nbytes))
                return NULL;

        return KA_PROP_DATA(prop);
}

/**
 * ka_proplist_destroy:
 * @p: The property list to destroy
 *
 * Destroys a property list that was created with ka_proplist_create() earlier.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_proplist_destroy(ka_proplist *p) {
        ka_prop *prop, *nprop;

        ka_return_val_if_fail(p, KA_ERROR_INVALID);

        for (prop = p->first_item; prop; prop = nprop) {
                nprop = prop->next_item;
                ka_free(prop->key);
                ka_free(prop);
        }

        ka_mutex_free(p->mutex);

        ka_free(p);

        return KA_SUCCESS;
}

static int merge_into(ka_proplist *a, ka_proplist *b) {
        int ret = KA_SUCCESS;
        ka_prop *prop;

        ka_return_val_if_fail(a, KA_ERROR_INVALID);
        ka_return_val_if_fail(b, KA_ERROR_INVALID);

        ka_mutex_lock(b->mutex);

        for (prop = b->first_item; prop; prop = prop->next_item)
                if ((ret = ka_proplist_set(a, prop->key, KA_PROP_DATA(prop), prop->nbytes)) < 0)
                        break;

        ka_mutex_unlock(b->mutex);

        return ret;
}

int ka_proplist_merge(ka_proplist **_a, ka_proplist *b, ka_proplist *c) {
        ka_proplist *a;
        int ret;

        ka_return_val_if_fail(_a, KA_ERROR_INVALID);
        ka_return_val_if_fail(b, KA_ERROR_INVALID);
        ka_return_val_if_fail(c, KA_ERROR_INVALID);

        if ((ret = ka_proplist_create(&a)) < 0)
                return ret;

        if ((ret = merge_into(a, b)) < 0 ||
            (ret = merge_into(a, c)) < 0) {
                ka_proplist_destroy(a);
                return ret;
        }

        *_a = a;
        return KA_SUCCESS;
}

ka_bool_t ka_proplist_contains(ka_proplist *p, const char *key) {
        ka_bool_t b;

        ka_return_val_if_fail(p, FALSE);
        ka_return_val_if_fail(key, FALSE);

        ka_mutex_lock(p->mutex);
        b = !!ka_proplist_get_unlocked(p, key);
        ka_mutex_unlock(p->mutex);

        return b;
}

int ka_proplist_merge_ap(ka_proplist *p, va_list ap) {
        int ret;

        ka_return_val_if_fail(p, KA_ERROR_INVALID);

        for (;;) {
                const char *key, *value;

                if (!(key = va_arg(ap, const char*)))
                        break;

                if (!(value = va_arg(ap, const char*)))
                        return KA_ERROR_INVALID;

                if ((ret = ka_proplist_sets(p, key, value)) < 0)
                        return ret;
        }

        return KA_SUCCESS;
}

int ka_proplist_from_ap(ka_proplist **_p, va_list ap) {
        int ret;
        ka_proplist *p;

        ka_return_val_if_fail(_p, KA_ERROR_INVALID);

        if ((ret = ka_proplist_create(&p)) < 0)
                return ret;

        if ((ret = ka_proplist_merge_ap(p, ap)) < 0)
                goto fail;

        *_p = p;

        return KA_SUCCESS;

fail:
        ka_assert_se(ka_proplist_destroy(p) == KA_SUCCESS);

        return ret;
}
