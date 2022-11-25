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
#include <locale.h>

#include <ctk/ctk.h>
#include <kanberra-ctk.h>

static int ret = 0;
static ka_proplist *proplist = NULL;
static int n_loops = 1;

static void callback(ka_context *c, uint32_t id, int error, void *userdata);

static gboolean idle_quit(gpointer userdata) {
        ctk_main_quit();
        return FALSE;
}

static gboolean idle_play(gpointer userdata) {
        int r;

        g_assert(n_loops > 1);

        n_loops--;

        r = ka_context_play_full(ka_ctk_context_get(), 1, proplist, callback, NULL);

        if (r < 0) {
                g_printerr("Failed to play sound: %s\n", ka_strerror(r));
                ret = 1;
                ctk_main_quit();
        }

        return FALSE;
}

static void callback(ka_context *c, uint32_t id, int error, void *userdata) {

        if (error < 0) {
                g_printerr("Failed to play sound (callback): %s\n", ka_strerror(error));
                ret = 1;

        } else if (n_loops > 1) {
                /* So, why don't we call ka_context_play_full() here directly?
                   -- Because the context this callback is called from is
                   explicitly documented as undefined and no libkanberra function
                   may be called from it. */

                g_idle_add(idle_play, NULL);
                return;
        }

        /* So, why don't we call ctk_main_quit() here directly? -- Because
         * otherwise we might end up with a small race condition: this
         * callback might get called before the main loop actually started
         * running */
        g_idle_add(idle_quit, NULL);
}

static GQuark error_domain(void) {
        return g_quark_from_static_string("kanberra-error-quark");
}

static gboolean property_callback(
                const gchar *option_name,
                const gchar *value,
                gpointer data,
                GError **error) {

        const char *equal;
        char *t;

        if (!(equal = strchr(value, '='))) {
                g_set_error(error, error_domain(), 0, "Property lacks '='.");
                return FALSE;
        }

        t = g_strndup(value, equal - value);

        if (ka_proplist_sets(proplist, t, equal + 1) < 0) {
                g_set_error(error, error_domain(), 0, "Invalid property.");
                g_free(t);
                return FALSE;
        }

        g_free(t);
        return TRUE;
}

int main (int argc, char *argv[]) {
        GOptionContext *oc;
        static gchar *event_id = NULL, *filename = NULL, *event_description = NULL, *cache_control = NULL, *volume = NULL;
        int r;
        static gboolean version = FALSE;
        GError *error = NULL;

        static const GOptionEntry options[] = {
                { "version",       'v', 0, G_OPTION_ARG_NONE,     &version,                  "Display version number and quit", NULL },
                { "id",            'i', 0, G_OPTION_ARG_STRING,   &event_id,                 "Event sound identifier",  "STRING" },
                { "file",          'f', 0, G_OPTION_ARG_STRING,   &filename,                 "Play file",  "PATH" },
                { "description",   'd', 0, G_OPTION_ARG_STRING,   &event_description,        "Event sound description", "STRING" },
                { "cache-control", 'c', 0, G_OPTION_ARG_STRING,   &cache_control,            "Cache control (permanent, volatile, never)", "STRING" },
                { "loop",          'l', 0, G_OPTION_ARG_INT,      &n_loops,                  "Loop how many times (detault: 1)", "INTEGER" },
                { "volume",        'V', 0, G_OPTION_ARG_STRING,   &volume,                   "A floating point dB value for the sample volume (ex: 0.0)", "STRING" },
                { "property",      0,   0, G_OPTION_ARG_CALLBACK, (void*) property_callback, "An arbitrary property", "STRING" },
                { NULL, 0, 0, 0, NULL, NULL, NULL }
        };

        setlocale(LC_ALL, "");

        g_type_init();

        ka_proplist_create(&proplist);

        oc = g_option_context_new("- kanberra-ctk-play");
        g_option_context_add_main_entries(oc, options, NULL);
        g_option_context_add_group(oc, ctk_get_option_group(TRUE));
        g_option_context_set_help_enabled(oc, TRUE);

        if (!(g_option_context_parse(oc, &argc, &argv, &error))) {
                g_print("Option parsing failed: %s\n", error->message);
                return 1;
        }
        g_option_context_free(oc);

        if (version) {
                g_print("kanberra-ctk-play from %s\n", PACKAGE_STRING);
                return 0;
        }

        if (!event_id && !filename) {
                g_printerr("No event id or file specified.\n");
                return 1;
        }

        ka_context_change_props(ka_ctk_context_get(),
                                KA_PROP_APPLICATION_NAME, "kanberra-ctk-play",
                                KA_PROP_APPLICATION_VERSION, PACKAGE_VERSION,
                                KA_PROP_APPLICATION_ID, "org.freedesktop.libkanberra.ctk-play",
                                NULL);

        if (event_id)
                ka_proplist_sets(proplist, KA_PROP_EVENT_ID, event_id);

        if (filename)
                ka_proplist_sets(proplist, KA_PROP_MEDIA_FILENAME, filename);

        if (cache_control)
                ka_proplist_sets(proplist, KA_PROP_KANBERRA_CACHE_CONTROL, cache_control);

        if (event_description)
                ka_proplist_sets(proplist, KA_PROP_EVENT_DESCRIPTION, event_description);

        if (volume)
                ka_proplist_sets(proplist, KA_PROP_KANBERRA_VOLUME, volume);

        r = ka_context_play_full(ka_ctk_context_get(), 1, proplist, callback, NULL);

        if (r < 0) {
                g_printerr("Failed to play sound: %s\n", ka_strerror(r));
                ret = 1;
                goto finish;
        }

        ctk_main();

finish:

        ka_proplist_destroy(proplist);

        return ret;
}
