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
#include "common.h"
#include "malloc.h"
#include "driver.h"
#include "proplist.h"
#include "macro.h"
#include "fork-detect.h"

/**
 * SECTION:kanberra
 * @short_description: General libkanberra API
 *
 * libkanberra defines a simple abstract interface for playing event sounds.
 *
 * libkanberra relies on the XDG sound naming specification for
 * identifying event sounds. On Unix/Linux the right sound to play is
 * found via the mechanisms defined in the XDG sound themeing
 * specification. On other systems the XDG sound name is translated to
 * the native sound id for the operating system.
 *
 * An event sound is triggered via libkanberra by calling the
 * ka_context_play() function on a previously created ka_context
 * object. The ka_context_play() takes a list of key-value pairs that
 * describe the event sound to generate as closely as possible. The
 * most important property is %CA_PROP_EVENT_ID which defines the XDG
 * sound name for the sound to play.
 *
 * libkanberra is not a generic event abstraction system. It's only
 * purpose is playing sounds -- however in a very elaborate way. As
 * much information about the context the sound is triggered from
 * shall be supplied to the sound system as possible, so that it can
 * replace the sound with some other kind of feedback for a11y
 * cases. Also this additional information can be used to enhance user
 * experience (e.g. by positioning sounds in space depending on the
 * place on the screen the sound was triggered from, and similar
 * uses).
 *
 * The set of properties defined for event sounds is extensible and
 * shared with other audio systems, such as PulseAudio. Some of
 * the properties that may be set are specific to an application, to a
 * window, to an input event or to the media being played back.
 *
 * The user can attach a set of properties to the context itself,
 * which is than automatically inherited by each sample being played
 * back. (ka_context_change_props()).
 *
 * Some of the properties can be filled in by libkanberra or one of
 * its backends automatically and thus need not be be filled in by the
 * application (such as %CA_PROP_APPLICATION_PROCESS_ID and
 * friends). However the application can always overwrite any of these
 * implicit properties.
 *
 * libkanberra is thread-safe and OOM-safe (as far as the backend
 * allows this). It is not async-signal safe.
 *
 * Most libkanberra functions return an integer that indicates success
 * when 0 (%CA_SUCCESS) or an error when negative. In the latter case
 * ka_strerror() can be used to convert this code into a human
 * readable string.
 *
 * libkanberra property names need to be in 7bit ASCII, string
 * property values UTF8.
 *
 * Optionally a libkanberra backend can support caching of sounds in a
 * sound system. If this functionality is used, the latencies for
 * event sound playback can be much smaller and fewer resources are
 * needed to start playback. If a backend does not support cacheing,
 * the respective functions will return an error code of
 * %CA_ERROR_NOTSUPPORTED.
 *
 * It is highly recommended that the application sets the
 * %CA_PROP_APPLICATION_NAME, %CA_PROP_APPLICATION_ID,
 * %CA_PROP_APPLICATION_ICON_NAME/%CA_PROP_APPLICATION_ICON properties
 * immediately after creating the ka_context, before calling
 * ka_context_open() or ka_context_play().
 *
 * Its is highly recommended to pass at least %CA_PROP_EVENT_ID,
 * %CA_PROP_EVENT_DESCRIPTION to ka_context_play() for each event
 * sound generated. For sound events based on mouse inputs events
 * %CA_PROP_EVENT_MOUSE_X, %CA_PROP_EVENT_MOUSE_Y, %CA_PROP_EVENT_MOUSE_HPOS,
 * %CA_PROP_EVENT_MOUSE_VPOS, %CA_PROP_EVENT_MOUSE_BUTTON should be
 * passed. For sound events attached to a widget on the screen, the
 * %CA_PROP_WINDOW_xxx properties should be set.
 *
 *
 */

/**
 * ka_context_create:
 * @c: A pointer wheere to fill in the newly created context object.
 *
 * Create an (unconnected) context object. This call will not connect
 * to the sound system, calling this function might even suceed if no
 * working driver backend is available. To find out if one is
 * available call ka_context_open().
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_create(ka_context **_c) {
        ka_context *c;
        int ret;
        const char *d;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(_c, CA_ERROR_INVALID);

        if (!(c = ka_new0(ka_context, 1)))
                return CA_ERROR_OOM;

        if (!(c->mutex = ka_mutex_new())) {
                ka_context_destroy(c);
                return CA_ERROR_OOM;
        }

        if ((ret = ka_proplist_create(&c->props)) < 0) {
                ka_context_destroy(c);
                return ret;
        }

        if ((d = getenv("KANBERRA_DRIVER"))) {
                if ((ret = ka_context_set_driver(c, d)) < 0) {
                        ka_context_destroy(c);
                        return ret;
                }
        }

        if ((d = getenv("KANBERRA_DEVICE"))) {
                if ((ret = ka_context_change_device(c, d)) < 0) {
                        ka_context_destroy(c);
                        return ret;
                }
        }

        *_c = c;
        return CA_SUCCESS;
}

/**
 * ka_context_destroy:
 * @c: the context to destroy.
 *
 * Destroy a (connected or unconnected) context object.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_destroy(ka_context *c) {
        int ret = CA_SUCCESS;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        /* There's no locking necessary here, because the application is
         * broken anyway if it destructs this object in one thread and
         * still is calling a method of it in another. */

        if (c->opened)
                ret = driver_destroy(c);

        if (c->props)
                ka_assert_se(ka_proplist_destroy(c->props) == CA_SUCCESS);

        if (c->mutex)
                ka_mutex_free(c->mutex);

        ka_free(c->driver);
        ka_free(c->device);
        ka_free(c);

        return ret;
}

/**
 * ka_context_set_driver:
 * @c: the context to change the backend driver for
 * @driver: the backend driver to use (e.g. "alsa", "pulse", "null", ...)
 *
 * Specify the backend driver used. This function may not be called again after
 * ka_context_open() suceeded. This function might suceed even when
 * the specified driver backend is not available. Use
 * ka_context_open() to find out whether the backend is available.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_set_driver(ka_context *c, const char *driver) {
        char *n;
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_mutex_lock(c->mutex);
        ka_return_val_if_fail_unlock(!c->opened, CA_ERROR_STATE, c->mutex);

        if (!driver)
                n = NULL;
        else if (!(n = ka_strdup(driver))) {
                ret = CA_ERROR_OOM;
                goto fail;
        }

        ka_free(c->driver);
        c->driver = n;

        ret = CA_SUCCESS;

fail:
        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 * ka_context_change_device:
 * @c: the context to change the backend device for
 * @device: the backend device to use, in a format that is specific to the backend.
 *
 * Specify the backend device to use. This function may be called not be called after
 * ka_context_open() suceeded. This function might suceed even when
 * the specified driver backend is not available. Use
 * ka_context_open() to find out whether the backend is available
 *
 * Depending on the backend use this might or might not cause all
 * currently playing event sounds to be moved to the new device..
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_change_device(ka_context *c, const char *device) {
        char *n;
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_mutex_lock(c->mutex);

        if (!device)
                n = NULL;
        else if (!(n = ka_strdup(device))) {
                ret = CA_ERROR_OOM;
                goto fail;
        }

        ret = c->opened ? driver_change_device(c, n) : CA_SUCCESS;

        if (ret == CA_SUCCESS) {
                ka_free(c->device);
                c->device = n;
        } else
                ka_free(n);

fail:
        ka_mutex_unlock(c->mutex);

        return ret;
}

static int context_open_unlocked(ka_context *c) {
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        if (c->opened)
                return CA_SUCCESS;

        if ((ret = driver_open(c)) == CA_SUCCESS)
                c->opened = TRUE;

        return ret;
}

/**
 * ka_context_open:
 * @c: the context to connect.
 *
 * Connect the context to the sound system. This call is implicitly
 * called in ka_context_play() or ka_context_cache() if not called
 * explicitly. It is recommended to initialize application properties
 * with ka_context_change_props() before calling this function.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_open(ka_context *c) {
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_mutex_lock(c->mutex);
        ka_return_val_if_fail_unlock(!c->opened, CA_ERROR_STATE, c->mutex);

        ret = context_open_unlocked(c);

        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 * ka_context_change_props:
 * @c: the context to set the properties on.
 * @...: the list of string pairs for the properties. Needs to be a NULL terminated list.
 *
 * Write one or more string properties to the context object. Requires
 * final NULL sentinel. Properties set like this will be attached to
 * both the client object of the sound server and to all event sounds
 * played or cached. It is recommended to call this function at least
 * once before calling ka_context_open(), so that the initial
 * application properties are set properly before the initial
 * connection to the sound system. This function can be called both
 * before and after the ka_context_open() call. Properties that have
 * already been set before will be overwritten.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_change_props(ka_context *c, ...)  {
        va_list ap;
        int ret;
        ka_proplist *p = NULL;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        va_start(ap, c);
        ret = ka_proplist_from_ap(&p, ap);
        va_end(ap);

        if (ret < 0)
                return ret;

        ret = ka_context_change_props_full(c, p);

        ka_assert_se(ka_proplist_destroy(p) == 0);

        return ret;
}

/**
 * ka_context_change_props_full:
 * @c: the context to set the properties on.
 * @p: the property list to set.
 *
 * Similar to ka_context_change_props(), but takes a ka_proplist
 * instead of a variable list of properties. Can be used to set binary
 * properties such as %CA_PROP_APPLICATION_ICON.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_change_props_full(ka_context *c, ka_proplist *p) {
        int ret;
        ka_proplist *merged;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(p, CA_ERROR_INVALID);

        ka_mutex_lock(c->mutex);

        if ((ret = ka_proplist_merge(&merged, c->props, p)) < 0)
                goto finish;

        ret = c->opened ? driver_change_props(c, p, merged) : CA_SUCCESS;

        if (ret == CA_SUCCESS) {
                ka_assert_se(ka_proplist_destroy(c->props) == CA_SUCCESS);
                c->props = merged;
        } else
                ka_assert_se(ka_proplist_destroy(merged) == CA_SUCCESS);

finish:

        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 * ka_context_play:
 * @c: the context to play the event sound on
 * @id: an integer id this sound can later be identified with when calling ka_context_cancel()
 * @...: additional properties for this sound event.
 *
 * Play one event sound. id can be any numeric value which later can
 * be used to cancel an event sound that is currently being
 * played. You may use the same id twice or more times if you want to
 * cancel multiple event sounds with a single ka_context_cancel() call
 * at once. It is recommended to pass 0 for the id if the event sound
 * shall never be canceled. If the requested sound is not cached in
 * the server yet this call might result in the sample being uploaded
 * temporarily or permanently (this may be controlled with %CA_PROP_KANBERRA_CACHE_CONTROL). This function will start playback
 * in the background. It will not wait until playback
 * completed. Depending on the backend used a sound that is started
 * shortly before your application terminates might or might not continue to
 * play after your application terminated. If you want to make sure
 * that all sounds finish to play you need to wait synchronously for
 * the callback function of ka_context_play_full() to be called before you
 * terminate your application.
 *
 * The sample to play is identified by the %CA_PROP_EVENT_ID
 * property. If it is already cached in the server the cached version
 * is played. The properties passed in this call are merged with the
 * properties supplied when the sample was cached (if applicable)
 * and the context properties as set with ka_context_change_props().
 *
 * If %CA_PROP_EVENT_ID is not defined the sound file passed in the
 * %CA_PROP_MEDIA_FILENAME is played.
 *
 * On Linux/Unix the right sound to play is determined according to
 * %CA_PROP_EVENT_ID,
 * %CA_PROP_APPLICATION_LANGUAGE/%CA_PROP_MEDIA_LANGUAGE, the system
 * locale, %CA_PROP_KANBERRA_XDG_THEME_NAME and
 * %CA_PROP_KANBERRA_XDG_THEME_OUTPUT_PROFILE, following the XDG Sound
 * Theming Specification. On non-Unix systems the native event sound
 * that matches the XDG sound name in %CA_PROP_EVENT_ID is played.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_play(ka_context *c, uint32_t id, ...) {
        int ret;
        va_list ap;
        ka_proplist *p = NULL;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        va_start(ap, id);
        ret = ka_proplist_from_ap(&p, ap);
        va_end(ap);

        if (ret < 0)
                return ret;

        ret = ka_context_play_full(c, id, p, NULL, NULL);

        ka_assert_se(ka_proplist_destroy(p) == 0);

        return ret;
}

/**
 * ka_context_play_full:
 * @c: the context to play the event sound on
 * @id: an integer id this sound can be later be identified with when calling ka_context_cancel() or when the callback is called.
 * @p: A property list of properties for this event sound
 * @cb: A callback to call when this sound event sucessfully finished playing or when an error occured during playback.
 *
 * Play one event sound, and call the specified callback function when
 * completed. See ka_finish_callback_t for the semantics the callback
 * is called in. Also see ka_context_play().
 *
 * It is guaranteed that the callback is called exactly once if
 * ka_context_play_full() returns CA_SUCCESS. You thus may safely pass
 * allocated memory to the callback and assume that it is freed
 * properly.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_play_full(ka_context *c, uint32_t id, ka_proplist *p, ka_finish_callback_t cb, void *userdata) {
        int ret;
        const char *t;
        ka_bool_t enabled = TRUE;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(p, CA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, CA_ERROR_INVALID);

        ka_mutex_lock(c->mutex);

        ka_return_val_if_fail_unlock(ka_proplist_contains(p, CA_PROP_EVENT_ID) ||
                                     ka_proplist_contains(c->props, CA_PROP_EVENT_ID) ||
                                     ka_proplist_contains(p, CA_PROP_MEDIA_FILENAME) ||
                                     ka_proplist_contains(c->props, CA_PROP_MEDIA_FILENAME), CA_ERROR_INVALID, c->mutex);

        ka_mutex_lock(c->props->mutex);
        if ((t = ka_proplist_gets_unlocked(c->props, CA_PROP_KANBERRA_ENABLE)))
                enabled = !ka_streq(t, "0");
        ka_mutex_unlock(c->props->mutex);

        ka_mutex_lock(p->mutex);
        if ((t = ka_proplist_gets_unlocked(p, CA_PROP_KANBERRA_ENABLE)))
                enabled = !ka_streq(t, "0");
        ka_mutex_unlock(p->mutex);

        ka_return_val_if_fail_unlock(enabled, CA_ERROR_DISABLED, c->mutex);

        if ((ret = context_open_unlocked(c)) < 0)
                goto finish;

        ka_assert(c->opened);

        ret = driver_play(c, id, p, cb, userdata);

finish:

        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 *
 * ka_context_cancel:
 * @c: the context to cancel the sounds on
 * @id: the id that identify the sounds to cancel.
 *
 * Cancel one or more event sounds that have been started via
 * ka_context_play(). If the sound was started with
 * ka_context_play_full() and a callback function was passed this
 * might cause this function to be called with %CA_ERROR_CANCELED as
 * error code.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_cancel(ka_context *c, uint32_t id)  {
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_mutex_lock(c->mutex);
        ka_return_val_if_fail_unlock(c->opened, CA_ERROR_STATE, c->mutex);

        ret = driver_cancel(c, id);

        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 * ka_context_cache:
 * @c: The context to use for uploading.
 * @...: The properties for this event sound. Terminated with NULL.
 *
 * Upload the specified sample into the audio server and attach the
 * specified properties to it. This function will only return after
 * the sample upload was finished.
 *
 * The sound to cache is found with the same algorithm that is used to
 * find the sounds for ka_context_play().
 *
 * If the backend doesn't support caching sound samples this function
 * will return %CA_ERROR_NOTSUPPORTED.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_context_cache(ka_context *c, ...) {
        int ret;
        va_list ap;
        ka_proplist *p = NULL;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        va_start(ap, c);
        ret = ka_proplist_from_ap(&p, ap);
        va_end(ap);

        if (ret < 0)
                return ret;

        ret = ka_context_cache_full(c, p);

        ka_assert_se(ka_proplist_destroy(p) == 0);

        return ret;
}

/**
 * ka_context_cache_full:
 * @c: The context to use for uploading.
 * @p: The property list for this event sound.
 *
 * Upload the specified sample into the server and attach the
 * specified properties to it. Similar to ka_context_cache() but takes
 * a ka_proplist instead of a variable number of arguments.
 *
 * If the backend doesn't support caching sound samples this function
 * will return CA_ERROR_NOTSUPPORTED.
 *
 * Returns: 0 on success, negative error code on error.
 */
int ka_context_cache_full(ka_context *c, ka_proplist *p) {
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(p, CA_ERROR_INVALID);

        ka_mutex_lock(c->mutex);

        ka_return_val_if_fail_unlock(ka_proplist_contains(p, CA_PROP_EVENT_ID) ||
                                     ka_proplist_contains(c->props, CA_PROP_EVENT_ID), CA_ERROR_INVALID, c->mutex);

        if ((ret = context_open_unlocked(c)) < 0)
                goto finish;

        ka_assert(c->opened);

        ret = driver_cache(c, p);

finish:

        ka_mutex_unlock(c->mutex);

        return ret;
}

/**
 * ka_strerror:
 * @code: Numerical error code as returned by a libkanberra API function
 *
 * Converts a numerical error code as returned by most libkanberra API functions into a human readable error string.
 *
 * Returns: a human readable error string.
 */
const char *ka_strerror(int code) {

        const char * const error_table[-_CA_ERROR_MAX] = {
                [-CA_SUCCESS] = "Success",
                [-CA_ERROR_NOTSUPPORTED] = "Operation not supported",
                [-CA_ERROR_INVALID] = "Invalid argument",
                [-CA_ERROR_STATE] = "Invalid state",
                [-CA_ERROR_OOM] = "Out of memory",
                [-CA_ERROR_NODRIVER] = "No such driver",
                [-CA_ERROR_SYSTEM] = "System error",
                [-CA_ERROR_CORRUPT] = "File or data corrupt",
                [-CA_ERROR_TOOBIG] = "File or data too large",
                [-CA_ERROR_NOTFOUND] = "File or data not found",
                [-CA_ERROR_DESTROYED] = "Destroyed",
                [-CA_ERROR_CANCELED] = "Canceled",
                [-CA_ERROR_NOTAVAILABLE] = "Not available",
                [-CA_ERROR_ACCESS] = "Access forbidden",
                [-CA_ERROR_IO] = "IO error",
                [-CA_ERROR_INTERNAL] = "Internal error",
                [-CA_ERROR_DISABLED] = "Sound disabled",
                [-CA_ERROR_FORKED] = "Process forked",
                [-CA_ERROR_DISCONNECTED] = "Disconnected"
        };

        ka_return_val_if_fail(code <= 0, NULL);
        ka_return_val_if_fail(code > _CA_ERROR_MAX, NULL);

        return error_table[-code];
}

/* Not exported */
int ka_parse_cache_control(ka_cache_control_t *control, const char *c) {
        ka_return_val_if_fail(control, CA_ERROR_INVALID);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        if (ka_streq(c, "never"))
                *control = CA_CACHE_CONTROL_NEVER;
        else if (ka_streq(c, "permanent"))
                *control = CA_CACHE_CONTROL_PERMANENT;
        else if (ka_streq(c, "volatile"))
                *control = CA_CACHE_CONTROL_VOLATILE;
        else
                return CA_ERROR_INVALID;

        return CA_SUCCESS;
}

/**
 * ka_context_playing:
 * @c: the context to check if sound is still playing
 * @id: the id that identify the sounds to check
 * @playing: a pointer to a boolean that will be updated with the play status
 *
 * Check if at least one sound with the specified id is still
 * playing. Returns 0 in *playing if no sound with this id is playing
 * anymore or non-zero if there is at least one playing.
 *
 * Returns: 0 on success, negative error code on error.
 * Since: 0.16
 */
int ka_context_playing(ka_context *c, uint32_t id, int *playing)  {
        int ret;

        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(playing, CA_ERROR_INVALID);
        ka_mutex_lock(c->mutex);
        ka_return_val_if_fail_unlock(c->opened, CA_ERROR_STATE, c->mutex);

        ret = driver_playing(c, id, playing);

        ka_mutex_unlock(c->mutex);

        return ret;
}
