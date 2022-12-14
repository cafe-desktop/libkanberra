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

#include <ltdl.h>
#include <string.h>
#include <errno.h>

#include "driver.h"
#include "common.h"
#include "malloc.h"
#include "driver-order.h"

struct private_dso {
        lt_dlhandle module;
        ka_bool_t ltdl_initialized;

        int (*driver_open)(ka_context *c);
        int (*driver_destroy)(ka_context *c);
        int (*driver_change_device)(ka_context *c, const char *device);
        int (*driver_change_props)(ka_context *c, ka_proplist *changed, ka_proplist *merged);
        int (*driver_play)(ka_context *c, uint32_t id, ka_proplist *p, ka_finish_callback_t cb, void *userdata);
        int (*driver_cancel)(ka_context *c, uint32_t id);
        int (*driver_cache)(ka_context *c, ka_proplist *p);
        int (*driver_playing)(ka_context *c, uint32_t id, int *playing);
};

#define PRIVATE_DSO(c) ((struct private_dso *) ((c)->private_dso))

static int ka_error_from_lt_error(int code) {

        static const int table[] = {
                [LT_ERROR_UNKNOWN] = KA_ERROR_INTERNAL,
                [LT_ERROR_DLOPEN_NOT_SUPPORTED] = KA_ERROR_NOTSUPPORTED,
                [LT_ERROR_INVALID_LOADER] = KA_ERROR_INTERNAL,
                [LT_ERROR_INIT_LOADER] = KA_ERROR_INTERNAL,
                [LT_ERROR_REMOVE_LOADER] = KA_ERROR_INTERNAL,
                [LT_ERROR_FILE_NOT_FOUND] = KA_ERROR_NOTFOUND,
                [LT_ERROR_DEPLIB_NOT_FOUND] = KA_ERROR_NOTFOUND,
                [LT_ERROR_NO_SYMBOLS] = KA_ERROR_NOTFOUND,
                [LT_ERROR_CANNOT_OPEN] = KA_ERROR_ACCESS,
                [LT_ERROR_CANNOT_CLOSE] = KA_ERROR_INTERNAL,
                [LT_ERROR_SYMBOL_NOT_FOUND] = KA_ERROR_NOTFOUND,
                [LT_ERROR_NO_MEMORY] = KA_ERROR_OOM,
                [LT_ERROR_INVALID_HANDLE] = KA_ERROR_INVALID,
                [LT_ERROR_BUFFER_OVERFLOW] = KA_ERROR_TOOBIG,
                [LT_ERROR_INVALID_ERRORCODE] = KA_ERROR_INVALID,
                [LT_ERROR_SHUTDOWN] = KA_ERROR_INTERNAL,
                [LT_ERROR_CLOSE_RESIDENT_MODULE] = KA_ERROR_INTERNAL,
                [LT_ERROR_INVALID_MUTEX_ARGS] = KA_ERROR_INTERNAL,
                [LT_ERROR_INVALID_POSITION] = KA_ERROR_INTERNAL
#ifdef LT_ERROR_CONFLICTING_FLAGS
                , [LT_ERROR_CONFLICTING_FLAGS] = KA_ERROR_INTERNAL
#endif
        };

        if (code < 0 || code >= (int) KA_ELEMENTSOF(table))
                return KA_ERROR_INTERNAL;

        return table[code];
}

static int lt_error_from_string(const char *t) {

        struct lt_error_code {
                int code;
                const char *text;
        };

        static const struct lt_error_code lt_error_codes[] = {
                /* This is so disgustingly ugly, it makes me vomit. But that's
                 * all ltdl's fault. */
#define LT_ERROR(u, s) { .code = LT_ERROR_ ## u, .text = s },
                lt_dlerror_table
#undef LT_ERROR

                { .code = 0, .text = NULL }
        };

        const struct lt_error_code *c;

        for (c = lt_error_codes; c->text; c++)
                if (ka_streq(t, c->text))
                        return c->code;

        return -1;
}

static int ka_error_from_string(const char *t) {
        int err;

        if ((err = lt_error_from_string(t)) < 0)
                return KA_ERROR_INTERNAL;

        return ka_error_from_lt_error(err);
}

static int try_open(ka_context *c, const char *t) {
        char *mn;
        struct private_dso *p;

        p = PRIVATE_DSO(c);

        if (!(mn = ka_sprintf_malloc(KA_PLUGIN_PATH "/libkanberra-%s", t)))
                return KA_ERROR_OOM;

        errno = 0;
        p->module = lt_dlopenext(mn);
        ka_free(mn);

        if (!p->module) {
                int ret;

                if (errno == ENOENT)
                        ret = KA_ERROR_NOTFOUND;
                else
                        ret = ka_error_from_string(lt_dlerror());

                if (ret == KA_ERROR_NOTFOUND)
                        ret = KA_ERROR_NODRIVER;

                return ret;
        }

        return KA_SUCCESS;
}

static void* real_dlsym(lt_module m, const char *name, const char *symbol) {
        char sn[256];
        char *s;
        void *r;

        ka_return_null_if_fail(m);
        ka_return_null_if_fail(name);
        ka_return_null_if_fail(symbol);

        snprintf(sn, sizeof(sn), "%s_%s", name, symbol);
        sn[sizeof(sn)-1] = 0;

        for (s = sn; *s; s++) {
                if (*s >= 'a' && *s <= 'z')
                        continue;
                if (*s >= 'A' && *s <= 'Z')
                        continue;
                if (*s >= '0' && *s <= '9')
                        continue;

                *s = '_';
        }

        if ((r = lt_dlsym(m, sn)))
                return r;

        return lt_dlsym(m, symbol);
}

#define MAKE_FUNC_PTR(ret, args, x) ((ret (*) args ) (size_t) (x))
#define GET_FUNC_PTR(module, name, symbol, ret, args) MAKE_FUNC_PTR(ret, args, real_dlsym((module), (name), (symbol)))

int driver_open(ka_context *c) {
        int ret;
        struct private_dso *p;
        char *driver;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(!PRIVATE_DSO(c), KA_ERROR_STATE);

        if (!(c->private_dso = p = ka_new0(struct private_dso, 1)))
                return KA_ERROR_OOM;

        if (lt_dlinit() != 0) {
                ret = ka_error_from_string(lt_dlerror());
                driver_destroy(c);
                return ret;
        }

        p->ltdl_initialized = TRUE;

        if (c->driver) {
                char *e;
                size_t n;

                if (!(e = ka_strdup(c->driver))) {
                        driver_destroy(c);
                        return KA_ERROR_OOM;
                }

                n = strcspn(e, ",:");
                e[n] = 0;

                if (n == 0) {
                        driver_destroy(c);
                        ka_free(e);
                        return KA_ERROR_INVALID;
                }

                if ((ret = try_open(c, e)) < 0) {
                        driver_destroy(c);
                        ka_free(e);
                        return ret;
                }

                driver = e;

        } else {
                const char *const * e;

                for (e = ka_driver_order; *e; e++) {

                        if ((ret = try_open(c, *e)) == KA_SUCCESS)
                                break;

                        if (ret != KA_ERROR_NODRIVER &&
                            ret != KA_ERROR_NOTAVAILABLE &&
                            ret != KA_ERROR_NOTFOUND) {

                                driver_destroy(c);
                                return ret;
                        }
                }

                if (!*e) {
                        driver_destroy(c);
                        return KA_ERROR_NODRIVER;
                }

                if (!(driver = ka_strdup(*e))) {
                        driver_destroy(c);
                        return KA_ERROR_OOM;
                }
        }

        ka_assert(p->module);

        if (!(p->driver_open = GET_FUNC_PTR(p->module, driver, "driver_open", int, (ka_context*))) ||
            !(p->driver_destroy = GET_FUNC_PTR(p->module, driver, "driver_destroy", int, (ka_context*))) ||
            !(p->driver_change_device = GET_FUNC_PTR(p->module, driver, "driver_change_device", int, (ka_context*, const char *))) ||
            !(p->driver_change_props = GET_FUNC_PTR(p->module, driver, "driver_change_props", int, (ka_context *, ka_proplist *, ka_proplist *))) ||
            !(p->driver_play = GET_FUNC_PTR(p->module, driver, "driver_play", int, (ka_context*, uint32_t, ka_proplist *, ka_finish_callback_t, void *))) ||
            !(p->driver_cancel = GET_FUNC_PTR(p->module, driver, "driver_cancel", int, (ka_context*, uint32_t))) ||
            !(p->driver_cache = GET_FUNC_PTR(p->module, driver, "driver_cache", int, (ka_context*, ka_proplist *))) ||
            !(p->driver_playing = GET_FUNC_PTR(p->module, driver, "driver_playing", int, (ka_context*, uint32_t, int*)))) {

                ka_free(driver);
                driver_destroy(c);
                return KA_ERROR_CORRUPT;
        }

        ka_free(driver);

        if ((ret = p->driver_open(c)) < 0) {
                p->driver_destroy = NULL;
                driver_destroy(c);
                return ret;
        }

        return KA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        struct private_dso *p;
        int ret = KA_SUCCESS;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);

        if (p->driver_destroy)
                ret = p->driver_destroy(c);

        if (p->module)
                lt_dlclose(p->module);

        if (p->ltdl_initialized) {
                lt_dlexit();
                p->ltdl_initialized = FALSE;
        }

        ka_free(p);

        c->private_dso = NULL;

        return ret;
}

int driver_change_device(ka_context *c, const char *device) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_change_device, KA_ERROR_STATE);

        return p->driver_change_device(c, device);
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_change_props, KA_ERROR_STATE);

        return p->driver_change_props(c, changed, merged);
}

int driver_play(ka_context *c, uint32_t id, ka_proplist *pl, ka_finish_callback_t cb, void *userdata) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_play, KA_ERROR_STATE);

        return p->driver_play(c, id, pl, cb, userdata);
}

int driver_cancel(ka_context *c, uint32_t id) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_cancel, KA_ERROR_STATE);

        return p->driver_cancel(c, id);
}

int driver_cache(ka_context *c, ka_proplist *pl) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_cache, KA_ERROR_STATE);

        return p->driver_cache(c, pl);
}

int driver_playing(ka_context *c, uint32_t id, int *playing) {
        struct private_dso *p;

        ka_return_val_if_fail(c, KA_ERROR_INVALID);
        ka_return_val_if_fail(c->private_dso, KA_ERROR_STATE);
        ka_return_val_if_fail(playing, KA_ERROR_INVALID);

        p = PRIVATE_DSO(c);
        ka_return_val_if_fail(p->driver_playing, KA_ERROR_STATE);

        return p->driver_playing(c, id, playing);
}
