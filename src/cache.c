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

#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include <tdb.h>

#include "malloc.h"
#include "macro.h"
#include "mutex.h"
#include "kanberra.h"
#include "sound-theme-spec.h"
#include "cache.h"

#define FILENAME "event-sound-cache.tdb"
#define UPDATE_INTERVAL 10

/* This part is not portable due to pthread_once usage, should be abstracted
 * when we port this to platforms that do not have POSIX threading */

static ka_mutex *mutex = NULL;
static struct tdb_context *database = NULL;

static void allocate_mutex_once(void) {
        mutex = ka_mutex_new();
}

static int allocate_mutex(void) {
        static pthread_once_t once = PTHREAD_ONCE_INIT;

        if (pthread_once(&once, allocate_mutex_once) != 0)
                return KA_ERROR_OOM;

        if (!mutex)
                return KA_ERROR_OOM;

        return 0;
}

static int get_cache_home(char **e) {
        const char *env, *subdir;
        char *r;

        ka_return_val_if_fail(e, KA_ERROR_INVALID);

        if ((env = getenv("XDG_CACHE_HOME")) && *env == '/')
                subdir = "";
        else if ((env = getenv("HOME")) && *env == '/')
                subdir = "/.cache";
        else {
                *e = NULL;
                return KA_SUCCESS;
        }

        if (!(r = ka_new(char, strlen(env) + strlen(subdir) + 1)))
                return KA_ERROR_OOM;

        sprintf(r, "%s%s", env, subdir);
        *e = r;

        return KA_SUCCESS;
}

static int sensible_gethostbyname(char *n, size_t l) {

        if (gethostname(n, l) < 0)
                return -1;

        n[l-1] = 0;

        if (strlen(n) >= l-1) {
                errno = ENAMETOOLONG;
                return -1;
        }

        if (!n[0]) {
                errno = ENOENT;
                return -1;
        }

        return 0;
}

static int get_machine_id(char **id) {
        FILE *f;
        size_t l;

        ka_return_val_if_fail(id, KA_ERROR_INVALID);

        /* First we try the D-Bus machine id */

        if ((f = fopen(KA_MACHINE_ID, "r"))) {
                char ln[34] = "", *r;

                r = fgets(ln, sizeof(ln)-1, f);
                fclose(f);

                if (r) {
                        ln[strcspn(ln, " \n\r\t")] = 0;

                        if (!(*id = ka_strdup(ln)))
                                return KA_ERROR_OOM;

                        return KA_SUCCESS;
                }
        }

        /* Then we try the host name */

        l = 100;

        for (;;) {
                if (!(*id = ka_new(char, l)))
                        return KA_ERROR_OOM;

                if (sensible_gethostbyname(*id, l) >= 0)
                        return KA_SUCCESS;

                ka_free(*id);

                if (errno != EINVAL && errno != ENAMETOOLONG)
                        break;

                l *= 2;
        }

        /* Then we use the POSIX host id */

        *id = ka_sprintf_malloc("%08lx", (unsigned long) gethostid());
        return KA_SUCCESS;
}

static int db_open(void) {
        int ret;
        char *c, *id, *pn;

        if ((ret = allocate_mutex()) < 0)
                return ret;

        ka_mutex_lock(mutex);

        if (database) {
                ret = KA_SUCCESS;
                goto finish;
        }

        if ((ret = get_cache_home(&c)) < 0)
                goto finish;

        if (!c) {
                ret = KA_ERROR_NOTFOUND;
                goto finish;
        }

        /* Try to create, just in case it doesn't exist yet. We don't do
         * this recursively however. */
        mkdir(c, 0755);

        if ((ret = get_machine_id(&id)) < 0) {
                ka_free(c);
                goto finish;
        }

        /* This data is machine specific, hence we include some kind of
         * stable machine id here in the name. Also, we don't want to care
         * abouth endianess/packing issues, hence we include the compiler
         * target in the name, too. */

        pn = ka_sprintf_malloc("%s/" FILENAME ".%s." CANONICAL_HOST, c, id);
        ka_free(c);
        ka_free(id);

        if (!pn) {
                ret = KA_ERROR_OOM;
                goto finish;
        }

        /* We pass TDB_NOMMAP here as long as rhbz 460851 is not fixed in
         * tdb. */
        database = tdb_open(pn, 0, TDB_NOMMAP, O_RDWR|O_CREAT|O_NOCTTY
#ifdef O_CLOEXEC
                            | O_CLOEXEC
#endif
                            , 0644);
        ka_free(pn);

        if (!database) {
                ret = KA_ERROR_CORRUPT;
                goto finish;
        }

        ret = KA_SUCCESS;

finish:
        ka_mutex_unlock(mutex);

        return ret;
}

#ifdef KA_GCC_DESTRUCTOR

static void db_close(void) KA_GCC_DESTRUCTOR;

static void db_close(void) {
        /* Only here to make this valgrind clean */

        if (!getenv("VALGRIND"))
                return;

        if (mutex) {
                ka_mutex_free(mutex);
                mutex = NULL;
        }

        if (database) {
                tdb_close(database);
                database = NULL;
        }
}

#endif

static int db_lookup(const void *key, size_t klen, void **data, size_t *dlen) {
        int ret;
        TDB_DATA k, d;

        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(klen > 0, KA_ERROR_INVALID);
        ka_return_val_if_fail(data, KA_ERROR_INVALID);
        ka_return_val_if_fail(dlen, KA_ERROR_INVALID);

        if ((ret = db_open()) < 0)
                return ret;

        k.dptr = (void*) key;
        k.dsize = klen;

        ka_mutex_lock(mutex);

        ka_assert(database);
        d = tdb_fetch(database, k);
        if (!d.dptr) {
                ret = KA_ERROR_NOTFOUND;
                goto finish;
        }

        *data = d.dptr;
        *dlen = d.dsize;

finish:
        ka_mutex_unlock(mutex);

        return ret;
}

static int db_store(const void *key, size_t klen, const void *data, size_t dlen) {
        int ret;
        TDB_DATA k, d;

        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(klen > 0, KA_ERROR_INVALID);
        ka_return_val_if_fail(data || dlen == 0, KA_ERROR_INVALID);

        if ((ret = db_open()) < 0)
                return ret;

        k.dptr = (void*) key;
        k.dsize = klen;

        d.dptr = (void*) data;
        d.dsize = dlen;

        ka_mutex_lock(mutex);

        ka_assert(database);
        if (tdb_store(database, k, d, TDB_REPLACE) < 0) {
                ret = KA_ERROR_CORRUPT;
                goto finish;
        }

        ret = KA_SUCCESS;

finish:
        ka_mutex_unlock(mutex);

        return ret;
}

static int db_remove(const void *key, size_t klen) {
        int ret;
        TDB_DATA k;

        ka_return_val_if_fail(key, KA_ERROR_INVALID);
        ka_return_val_if_fail(klen > 0, KA_ERROR_INVALID);

        if ((ret = db_open()) < 0)
                return ret;

        k.dptr = (void*) key;
        k.dsize = klen;

        ka_mutex_lock(mutex);

        ka_assert(database);
        if (tdb_delete(database, k) < 0) {
                ret = KA_ERROR_CORRUPT;
                goto finish;
        }

        ret = KA_SUCCESS;

finish:
        ka_mutex_unlock(mutex);

        return ret;
}

static char *build_key(
                const char *theme,
                const char *name,
                const char *locale,
                const char *profile,
                size_t *klen) {

        char *key, *k;
        size_t tl, nl, ll, pl;

        tl = strlen(theme);
        nl = strlen(name);
        ll = strlen(locale);
        pl = strlen(profile);
        *klen = tl+1+nl+1+ll+1+pl+1;

        if (!(key = ka_new(char, *klen)))
                return NULL;

        k = key;
        strcpy(k, theme);
        k += tl+1;
        strcpy(k, name);
        k += nl+1;
        strcpy(k, locale);
        k += ll+1;
        strcpy(k, profile);

        return key;
}

static int get_last_change(time_t *t) {
        int ret;
        char *e, *k;
        struct stat st;
        static time_t last_check = 0, last_change = 0;
        time_t now;
        const char *g;

        ka_return_val_if_fail(t, KA_ERROR_INVALID);

        if ((ret = allocate_mutex()) < 0)
                return ret;

        ka_mutex_lock(mutex);

        ka_assert_se(time(&now) != (time_t) -1);

        if (now < last_check + UPDATE_INTERVAL) {
                *t = last_change;
                ret = KA_SUCCESS;
                goto finish;
        }

        if ((ret = ka_get_data_home(&e)) < 0)
                goto finish;

        *t = 0;

        if (e) {
                if (!(k = ka_new(char, strlen(e) + sizeof("/sounds")))) {
                        ka_free(e);
                        ret = KA_ERROR_OOM;
                        goto finish;
                }

                sprintf(k, "%s/sounds", e);
                ka_free(e);

                if (stat(k, &st) >= 0)
                        *t = st.st_mtime;

                ka_free(k);
        }

        g = ka_get_data_dirs();

        for (;;) {
                size_t j = strcspn(g, ":");

                if (g[0] == '/' && j > 0) {

                        if (!(k = ka_new(char, j + sizeof("/sounds")))) {
                                ret = KA_ERROR_OOM;
                                goto finish;
                        }

                        memcpy(k, g, j);
                        strcpy(k+j, "/sounds");

                        if (stat(k, &st) >= 0)
                                if (st.st_mtime >= *t)
                                        *t = st.st_mtime;

                        ka_free(k);
                }

                if (g[j] == 0)
                        break;

                g += j+1;
        }

        last_change = *t;
        last_check = now;

        ret = 0;

finish:

        ka_mutex_unlock(mutex);

        return ret;
}

int ka_cache_lookup_sound(
                ka_sound_file **f,
                ka_sound_file_open_callback_t sfopen,
                char **sound_path,
                const char *theme,
                const char *name,
                const char *locale,
                const char *profile) {

        char *key = NULL;
        void *data = NULL;
        size_t klen, dlen;
        int ret;
        uint32_t timestamp;
        time_t last_change, now;
        ka_bool_t remove_entry = FALSE;

        ka_return_val_if_fail(f, KA_ERROR_INVALID);
        ka_return_val_if_fail(sfopen, KA_ERROR_INVALID);
        ka_return_val_if_fail(theme, KA_ERROR_INVALID);
        ka_return_val_if_fail(name && *name, KA_ERROR_INVALID);
        ka_return_val_if_fail(locale, KA_ERROR_INVALID);
        ka_return_val_if_fail(profile, KA_ERROR_INVALID);

        if (sound_path)
                *sound_path = NULL;

        if (!(key = build_key(theme, name, locale, profile, &klen)))
                return KA_ERROR_OOM;

        ret = db_lookup(key, klen, &data, &dlen);

        if (ret < 0)
                goto finish;

        ka_assert(data);

        if (dlen < sizeof(uint32_t) ||
            (dlen > sizeof(uint32_t) && ((char*) data)[dlen-1] != 0)) {

                /* Corrupt entry */
                ret = KA_ERROR_NOTFOUND;
                remove_entry = TRUE;
                goto finish;
        }

        memcpy(&timestamp, data, sizeof(timestamp));

        if ((ret = get_last_change(&last_change)) < 0)
                goto finish;

        ka_assert_se(time(&now) != (time_t) -1);

        /* Hmm, is the entry older than the last change to our sound theme
         * dirs? Also, check for clock skews */
        if ((time_t) timestamp < last_change || ((time_t) timestamp > now)) {
                remove_entry = TRUE;
                ret = KA_ERROR_NOTFOUND;
                goto finish;
        }

        if (dlen <= sizeof(uint32_t)) {
                /* Negative caching entry. */
                *f = NULL;
                ret = KA_SUCCESS;
                goto finish;
        }

        if (sound_path) {
                if (!(*sound_path = ka_strdup((const char*) data + sizeof(uint32_t)))) {
                        ret = KA_ERROR_OOM;
                        goto finish;
                }
        }

        if ((ret = sfopen(f, (const char*) data + sizeof(uint32_t))) < 0)
                remove_entry = TRUE;

finish:

        if (remove_entry)
                db_remove(key, klen);

        if (sound_path && ret < 0)
                ka_free(*sound_path);

        ka_free(key);
        ka_free(data);

        return ret;
}

int ka_cache_store_sound(
                const char *theme,
                const char *name,
                const char *locale,
                const char *profile,
                const char *fname) {

        char *key;
        void *data;
        size_t klen, dlen;
        int ret;
        time_t now;

        ka_return_val_if_fail(theme, KA_ERROR_INVALID);
        ka_return_val_if_fail(name && *name, KA_ERROR_INVALID);
        ka_return_val_if_fail(locale, KA_ERROR_INVALID);
        ka_return_val_if_fail(profile, KA_ERROR_INVALID);

        if (!(key = build_key(theme, name, locale, profile, &klen)))
                return KA_ERROR_OOM;

        dlen = sizeof(uint32_t) + (fname ? strlen(fname) + 1 : 0);

        if (!(data = ka_malloc(dlen))) {
                ka_free(key);
                return KA_ERROR_OOM;
        }

        ka_assert_se(time(&now) != (time_t) -1);
        *(uint32_t*) data = (uint32_t) now;

        if (fname)
                strcpy((char*) data + sizeof(uint32_t), fname);

        ret = db_store(key, klen, data, dlen);

        ka_free(key);
        ka_free(data);

        return ret;
}
