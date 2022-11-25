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

#include "driver.h"
#include "llist.h"
#include "malloc.h"
#include "common.h"
#include "driver-order.h"

struct backend {
        KA_LLIST_FIELDS(struct backend);
        ka_context *context;
};

struct private {
        ka_context *context;
        KA_LLIST_HEAD(struct backend, backends);
};

#define PRIVATE(c) ((struct private *) ((c)->private))

static int add_backend(struct private *p, const char *name) {
        struct backend *b, *last;
        int ret;

        ka_assert(p);
        ka_assert(name);

        if (ka_streq(name, "multi"))
                return KA_ERROR_NOTAVAILABLE;

        for (b = p->backends; b; b = b->next)
                if (ka_streq(b->context->driver, name))
                        return KA_ERROR_NOTAVAILABLE;

        if (!(b = ka_new0(struct backend, 1)))
                return KA_ERROR_OOM;

        if ((ret = ka_context_create(&b->context)) < 0)
                goto fail;

        if ((ret = ka_context_change_props_full(b->context, p->context->props)) < 0)
                goto fail;

        if ((ret = ka_context_set_driver(b->context, name)) < 0)
                goto fail;

        if ((ret = ka_context_open(b->context)) < 0)
                goto fail;

        for (last = p->backends; last; last = last->next)
                if (!last->next)
                        break;

        KA_LLIST_INSERT_AFTER(struct backend, p->backends, last, b);

        return KA_SUCCESS;

fail:

        if (b->context)
                ka_context_destroy(b->context);

        ka_free(b);

        return ret;
}

static int remove_backend(struct private *p, struct backend *b) {
        int ret;

        ka_assert(p);
        ka_assert(b);

        ret = ka_context_destroy(b->context);
        KA_LLIST_REMOVE(struct backend, p->backends, b);
        ka_free(b);

        return ret;
}

int driver_open(ka_context *c) {
        struct private *p;
        int ret = KA_SUCCESS;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->driver, KA_ERROR_NODRIVER);
        ka_return_val_if_fail(!strncmp(c->driver, "multi", 5), KA_ERROR_NODRIVER);
        ka_return_val_if_fail(!PRIVATE(c), KA_ERROR_STATE);

        if (!(c->private = p = ka_new0(struct private, 1)))
                return KA_ERROR_OOM;

        p->context = c;

        if (c->driver) {
                char *e, *k;

                if (!(e = ka_strdup(c->driver))) {
                        driver_destroy(c);
                        return KA_ERROR_OOM;
                }

                k = e;
                for (;;)  {
                        size_t n;
                        ka_bool_t last;

                        n = strcspn(k, ",:");
                        last = k[n] == 0;
                        k[n] = 0;

                        if (n > 0) {
                                int r;

                                r = add_backend(p, k);

                                if (ret == KA_SUCCESS)
                                        ret = r;
                        }

                        if (last)
                                break;

                        k += n+1 ;
                }

                ka_free(e);

        } else {

                const char *const *e;

                for (e = ka_driver_order; *e; e++) {
                        int r;

                        r = add_backend(p, *e);

                        /* We return the error code of the first module that fails only */
                        if (ret == KA_SUCCESS)
                                ret = r;
                }
        }

        if (!p->backends) {
                driver_destroy(c);
                return ret == KA_SUCCESS ? KA_ERROR_NODRIVER : ret;
        }

        return KA_SUCCESS;
}


int driver_destroy(ka_context *c) {
        int ret = KA_SUCCESS;
        struct private *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        while (p->backends) {
                int r;

                r = remove_backend(p, p->backends);

                if (ret == KA_SUCCESS)
                        ret = r;
        }

        ka_free(p);

        c->private = NULL;

        return ret;
}

int driver_change_device(ka_context *c, const char *device) {
        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        return KA_ERROR_NOTSUPPORTED;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        int ret = KA_SUCCESS;
        struct private *p;
        struct backend *b;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(changed, KA_ERROR_INVALID);
        ka_return_val_if_fail(merged, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        for (b = p->backends; b; b = b->next) {
                int r;

                r = ka_context_change_props_full(b->context, changed);

                /* We only return the first failure */
                if (ret == KA_SUCCESS)
                        ret = r;
        }

        return ret;
}

struct closure {
        ka_context *context;
        ka_finish_callback_t callback;
        void *userdata;
};

static void call_closure(ka_context *c, uint32_t id, int error_code, void *userdata) {
        struct closure *closure = userdata;

        closure->callback(closure->context, id, error_code, closure->userdata);
        ka_free(closure);
}

int driver_play(ka_context *c, uint32_t id, ka_proplist *proplist, ka_finish_callback_t cb, void *userdata) {
        int ret = KA_SUCCESS;
        struct private *p;
        struct backend *b;
        struct closure *closure;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        if (cb) {
                if (!(closure = ka_new(struct closure, 1)))
                        return KA_ERROR_OOM;

                closure->context = c;
                closure->callback = cb;
                closure->userdata = userdata;
        } else
                closure = NULL;

        /* The first backend that can play this, takes it */
        for (b = p->backends; b; b = b->next) {
                int r;

                if ((r = ka_context_play_full(b->context, id, proplist, closure ? call_closure : NULL, closure)) == KA_SUCCESS)
                        return r;

                /* We only return the first failure */
                if (ret == KA_SUCCESS)
                        ret = r;
        }

        ka_free(closure);

        return ret;
}

int driver_cancel(ka_context *c, uint32_t id) {
        int ret = KA_SUCCESS;
        struct private *p;
        struct backend *b;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        for (b = p->backends; b; b = b->next) {
                int r;

                r = ka_context_cancel(b->context, id);

                /* We only return the first failure */
                if (ret == KA_SUCCESS)
                        ret = r;
        }

        return ret;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        int ret = KA_SUCCESS;
        struct private *p;
        struct backend *b;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        /* The first backend that can cache this, takes it */
        for (b = p->backends; b; b = b->next) {
                int r;

                if ((r = ka_context_cache_full(b->context,  proplist)) == KA_SUCCESS)
                        return r;

                /* We only return the first failure */
                if (ret == KA_SUCCESS)
                        ret = r;
        }

        return ret;
}

int driver_playing(ka_context *c, uint32_t id, int *playing) {
        int ret = KA_SUCCESS;
        struct private *p;
        struct backend *b;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(playing, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private, KA_ERROR_STATE);

        p = PRIVATE(c);

        *playing = 0;

        for (b = p->backends; b; b = b->next) {
                int r, _playing = 0;

                r = ka_context_playing(b->context, id, &_playing);

                /* We only return the first failure */
                if (ret == KA_SUCCESS)
                        ret = r;

                if (_playing)
                        *playing = 1;
        }

        return ret;
}
