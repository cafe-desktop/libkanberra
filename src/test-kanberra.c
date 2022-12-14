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

#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <stdio.h>

#include "kanberra.h"

static void callback(ka_context *c, uint32_t id, int error, void *userdata) {
        fprintf(stderr, "callback called for id %u, error '%s', userdata=%p\n", id, ka_strerror(error), userdata);
}

int main(int argc, char *argv[]) {
        ka_context *c;
        ka_proplist *p;
        int ret;

        setlocale(LC_ALL, "");

        ret = ka_context_create(&c);
        fprintf(stderr, "create: %s\n", ka_strerror(ret));

        /* Initialize a few meta variables for the following play()
         * calls. They stay valid until they are overwritten with
         * ka_context_change_props() again. */
        ret = ka_context_change_props(c,
                                      KA_PROP_APPLICATION_NAME, "An example",
                                      KA_PROP_APPLICATION_ID, "org.freedesktop.libkanberra.Test",
                                      KA_PROP_WINDOW_X11_SCREEN, getenv("DISPLAY"),
                                      NULL);
        fprintf(stderr, "change_props: %s\n", ka_strerror(ret));

        ret = ka_context_open(c);
        fprintf(stderr, "open: %s\n", ka_strerror(ret));

        /* Now trigger a sound event, the quick version */
        ret = ka_context_play(c, 0,
                              KA_PROP_EVENT_ID, "desktop-login",
                              KA_PROP_MEDIA_FILENAME, "/usr/share/sounds/bar.wav",
                              KA_PROP_MEDIA_NAME, "User has logged off from session",
                              KA_PROP_MEDIA_LANGUAGE, "en_EN",
                              KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                              NULL);
        fprintf(stderr, "play: %s\n", ka_strerror(ret));

        /* Now trigger a sound event, the complex version */
        ka_proplist_create(&p);
        ka_proplist_sets(p, KA_PROP_EVENT_ID, "desktop-logout");
        ka_proplist_sets(p, KA_PROP_MEDIA_FILENAME, "/usr/share/sounds/uxknkurz.wav");
        ka_proplist_sets(p, KA_PROP_MEDIA_NAME, "New email received");
        ka_proplist_setf(p, "test.foo", "%u", 4711);
        ret = ka_context_play_full(c, 1, p, callback, (void*) 0x4711);
        ka_proplist_destroy(p);
        fprintf(stderr, "play_full: %s\n", ka_strerror(ret));

        /* Now trigger a sound event, by filename */
        ret = ka_context_play(c, 2,
                              KA_PROP_MEDIA_FILENAME, "/usr/share/sounds/freedesktop/stereo/audio-channel-front-left.ogg",
                              KA_PROP_MEDIA_NAME, "Front Left",
                              KA_PROP_MEDIA_LANGUAGE, "en_EN",
                              NULL);
        fprintf(stderr, "play (by filename): %s\n", ka_strerror(ret));

        fprintf(stderr, "Sleep half a second ...\n");
        usleep(500000);

        /* Stop one sound */
/*     ret = ka_context_cancel(c, 0); */
/*     fprintf(stderr, "cancel: %s\n", ka_strerror(ret)); */

        fprintf(stderr, "Sleep 2s ...\n");
        sleep(2);

        /* .. */

        ret = ka_context_destroy(c);
        fprintf(stderr, "destroy: %s\n", ka_strerror(ret));

        return 0;
}
