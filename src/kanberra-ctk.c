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

#include <ctk/ctk.h>
#include <cdk/cdk.h>
#include <cdk/cdkx.h>
#include <X11/Xatom.h>

#include "kanberra.h"
#include "kanberra-ctk.h"
#include "common.h"
#include "malloc.h"
#include "proplist.h"
#include "fork-detect.h"

/**
 * SECTION:kanberra-ctk
 * @short_description: Ctk+ libkanberra Bindings
 *
 * libkanberra-ctk provides a few functions that simplify libkanberra
 * usage from Ctk+ programs. It maintains a single ka_context object
 * per #CdkScreen that is made accessible via
 * ka_ctk_context_get_for_screen(), with a shortcut ka_ctk_context_get()
 * to get the context for the default screen. More importantly, it provides
 * a few functions
 * to compile event sound property lists based on CtkWidget objects or
 * CdkEvent events.
 */

static void read_sound_theme_name(ka_context *c, CtkSettings *s) {
        gchar *theme_name = NULL;

        g_object_get(G_OBJECT(s), "ctk-sound-theme-name", &theme_name, NULL);

        if (theme_name) {
                ka_context_change_props(c, CA_PROP_KANBERRA_XDG_THEME_NAME, theme_name, NULL);
                g_free(theme_name);
        }
}

static void read_enable_event_sounds(ka_context *c, CtkSettings *s) {
        gboolean enable_event_sounds = TRUE;

        if (!g_getenv("KANBERRA_FORCE_EVENT_SOUNDS"))
                g_object_get(G_OBJECT(s), "ctk-enable-event-sounds", &enable_event_sounds, NULL);

        ka_context_change_props(c, CA_PROP_KANBERRA_ENABLE, enable_event_sounds ? "1" : "0", NULL);
}

static void sound_theme_name_changed(CtkSettings *s, GParamSpec *arg1, ka_context *c) {
        read_sound_theme_name(c, s);
}

static void enable_event_sounds_changed(CtkSettings *s, GParamSpec *arg1, ka_context *c) {
        read_enable_event_sounds(c, s);
}

/**
 * ka_ctk_context_get:
 *
 * Gets the single ka_context object for the default screen. See
 * ka_ctk_context_get_for_screen().
 *
 * Returns: a ka_context object. The object is owned by libkanberra-ctk
 *   and must not be destroyed
 */
ka_context *ka_ctk_context_get(void) {
        return ka_ctk_context_get_for_screen(NULL);
}

/**
 * ka_ctk_context_get_for_screen:
 * @screen: the #CdkScreen to get the context for, or %NULL to use
 *   the default screen
 *
 * libkanberra-ctk maintains a single ka_context object for each
 * #CdkScreen. Use this function to access it. The
 * %CA_PROP_KANBERRA_XDG_THEME_NAME of this context property is
 * dynamically bound to the XSETTINGS setting for the XDG theme
 * name. CA_PROP_APPLICATION_NAME is bound to
 * g_get_application_name().
 *
 * Returns: a ka_context object. The object is owned by libkanberra-ctk
 *   and must not be destroyed
 *
 * Since: 0.13
 */
ka_context *ka_ctk_context_get_for_screen(CdkScreen *screen) {
        ka_context *c = NULL;
        ka_proplist *p = NULL;
        const char *name;
        CtkSettings *s;

        if (!screen)
                screen = cdk_screen_get_default();

        if ((c = g_object_get_data(G_OBJECT(screen), "kanberra::ctk::context")))
                return c;

        if (ka_context_create(&c) != CA_SUCCESS)
                return NULL;

        if (ka_proplist_create(&p) != CA_SUCCESS) {
                ka_context_destroy(c);
                return NULL;
        }

        if ((name = g_get_application_name()))
                ka_proplist_sets(p, CA_PROP_APPLICATION_NAME, name);
        else {
                ka_proplist_sets(p, CA_PROP_APPLICATION_NAME, "libkanberra-ctk");
                ka_proplist_sets(p, CA_PROP_APPLICATION_VERSION, PACKAGE_VERSION);
                ka_proplist_sets(p, CA_PROP_APPLICATION_ID, "org.freedesktop.libkanberra.ctk");
        }

        if ((name = ctk_window_get_default_icon_name()))
                ka_proplist_sets(p, CA_PROP_APPLICATION_ICON_NAME, name);

        if ((name = cdk_display_get_name(cdk_screen_get_display(screen))))
                ka_proplist_sets(p, CA_PROP_WINDOW_X11_DISPLAY, name);

        ka_proplist_setf(p, CA_PROP_WINDOW_X11_SCREEN, "%i", cdk_screen_get_number(screen));

        ka_context_change_props_full(c, p);
        ka_proplist_destroy(p);

        if ((s = ctk_settings_get_for_screen(screen))) {

                if (g_object_class_find_property(G_OBJECT_GET_CLASS(s), "ctk-sound-theme-name")) {
                        g_signal_connect(G_OBJECT(s), "notify::ctk-sound-theme-name", G_CALLBACK(sound_theme_name_changed), c);
                        read_sound_theme_name(c, s);
                } else
                        g_debug("This Ctk+ version doesn't have the CtkSettings::ctk-sound-theme-name property.");

                if (g_object_class_find_property(G_OBJECT_GET_CLASS(s), "ctk-enable-event-sounds")) {
                        g_signal_connect(G_OBJECT(s), "notify::ctk-enable-event-sounds", G_CALLBACK(enable_event_sounds_changed), c);
                        read_enable_event_sounds(c, s);
                } else
                        g_debug("This Ctk+ version doesn't have the CtkSettings::ctk-enable-event-sounds property.");
        }

        g_object_set_data_full(G_OBJECT(screen), "kanberra::ctk::context", c, (GDestroyNotify) ka_context_destroy);

        return c;
}

static CtkWindow* get_toplevel(CtkWidget *w) {
        if (!(w = ctk_widget_get_toplevel(w)))
                return NULL;

        if (!CTK_IS_WINDOW(w))
                return NULL;

        return CTK_WINDOW(w);
}

static gint window_get_desktop(CdkDisplay *d, CdkWindow *w) {
        Atom type_return;
        gint format_return;
        gulong nitems_return;
        gulong bytes_after_return;
        guchar *data = NULL;
        gint ret = -1;

#ifdef CDK_IS_X11_DISPLAY
        if (!CDK_IS_X11_DISPLAY(d))
                return 0;
#endif

        if (XGetWindowProperty(CDK_DISPLAY_XDISPLAY(d), CDK_WINDOW_XID(w),
                               cdk_x11_get_xatom_by_name_for_display(d, "_NET_WM_DESKTOP"),
                               0, G_MAXLONG, False, XA_CARDINAL, &type_return,
                               &format_return, &nitems_return, &bytes_after_return,
                               &data) != Success)
                return -1;

        if (type_return == XA_CARDINAL && format_return == 32 && data) {
                guint32 desktop = *(guint32*) data;

                if (desktop != 0xFFFFFFFF)
                        ret = (gint) desktop;
        }

        if (type_return != None && data != NULL)
                XFree(data);

        return ret;
}

/**
 * ka_ctk_proplist_set_for_widget:
 * @p: The proplist to store these sound event properties in
 * @w: The Ctk widget to base these sound event properties on
 *
 * Fill in a ka_proplist object for a sound event that shall originate
 * from the specified Ctk Widget. This will fill in properties like
 * %CA_PROP_WINDOW_NAME or %CA_PROP_WINDOW_X11_DISPLAY for you.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_ctk_proplist_set_for_widget(ka_proplist *p, CtkWidget *widget) {
        CtkWindow *w;
        int ret;
        const char *t, *role;

        ka_return_val_if_fail(p, CA_ERROR_INVALID);
        ka_return_val_if_fail(widget, CA_ERROR_INVALID);
        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);

        if (!(w = get_toplevel(widget)))
                return CA_ERROR_INVALID;

        if ((t = ctk_window_get_title(w)))
                if ((ret = ka_proplist_sets(p, CA_PROP_WINDOW_NAME, t)) < 0)
                        return ret;

        if ((role = ctk_window_get_role(w))) {
                if (role && t) {
                        char *id = ka_sprintf_malloc("%s#%s", t, role);

                        if ((ret = ka_proplist_sets(p, CA_PROP_WINDOW_ID, id)) < 0) {
                                ka_free(id);
                                return ret;
                        }

                        ka_free(id);
                }
        } else if (t)
                if ((ret = ka_proplist_sets(p, CA_PROP_WINDOW_ID, t)) < 0)
                        return ret;

        if ((t = ctk_window_get_icon_name(w)))
                if ((ret = ka_proplist_sets(p, CA_PROP_WINDOW_ICON_NAME, t)) < 0)
                        return ret;

        if (ctk_widget_get_realized(CTK_WIDGET(w))) {
                CdkWindow *dw = NULL;
                CdkScreen *screen = NULL;
                CdkDisplay *display = NULL;
                gint x = -1, y = -1, width = -1, height = -1, screen_width = -1, screen_height = -1;

                if ((dw = ctk_widget_get_window(CTK_WIDGET(w))))
                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_X11_XID, "%lu", (unsigned long) CDK_WINDOW_XID(dw))) < 0)
                                return ret;

                if ((display = ctk_widget_get_display(CTK_WIDGET(w)))) {
                        if ((t = cdk_display_get_name(display)))
                                if ((ret = ka_proplist_sets(p, CA_PROP_WINDOW_X11_DISPLAY, t)) < 0)
                                        return ret;

                        if (dw)  {
                                gint desktop = window_get_desktop(display, dw);

                                if (desktop >= 0)
                                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_DESKTOP, "%i", desktop)) < 0)
                                                return ret;
                        }
                }

                if ((screen = ctk_widget_get_screen(CTK_WIDGET(w)))) {

                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_X11_SCREEN, "%i", cdk_screen_get_number(screen))) < 0)
                                return ret;

                        if (dw)
                                if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_X11_MONITOR, "%i", cdk_screen_get_monitor_at_window(screen, dw))) < 0)
                                        return ret;
                }

                /* FIXME, this might cause a round trip */

                if (dw) {
                        cdk_window_get_origin(dw, &x, &y);

                        if (x >= 0)
                                if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_X, "%i", x)) < 0)
                                        return ret;
                        if (y >= 0)
                                if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_Y, "%i", y)) < 0)
                                        return ret;
                }

                ctk_window_get_size(w, &width, &height);

                if (width > 0)
                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_WIDTH, "%i", width)) < 0)
                                return ret;
                if (height > 0)
                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_HEIGHT, "%i", height)) < 0)
                                return ret;

                if (x >= 0 && width > 0) {
                        screen_width = cdk_screen_get_width(ctk_widget_get_screen(CTK_WIDGET(w)));

                        x += width/2;
                        x = CA_CLAMP(x, 0, screen_width-1);

                        /* We use these strange format strings here to avoid that libc
                         * applies locale information on the formatting of floating
                         * numbers. */

                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_HPOS, "%i.%03i",
                                                    (int) (x/(screen_width-1)), (int) (1000.0*x/(screen_width-1)) % 1000)) < 0)
                                return ret;
                }

                if (y >= 0 && height > 0) {
                        screen_height = cdk_screen_get_height(ctk_widget_get_screen(CTK_WIDGET(w)));

                        y += height/2;
                        y = CA_CLAMP(y, 0, screen_height-1);

                        if ((ret = ka_proplist_setf(p, CA_PROP_WINDOW_VPOS, "%i.%03i",
                                                    (int) (y/(screen_height-1)), (int) (1000.0*y/(screen_height-1)) % 1000)) < 0)
                                return ret;
                }
        }

        return CA_SUCCESS;
}

/**
 * ka_ctk_proplist_set_for_event:
 * @p: The proplist to store these sound event properties in
 * @e: The Cdk event to base these sound event properties on
 *
 * Fill in a ka_proplist object for a sound event that is being
 * triggered by the specified Cdk Event. This will fill in properties
 * like %CA_PROP_EVENT_MOUSE_X or %CA_PROP_EVENT_MOUSE_BUTTON for
 * you. This will internally also cal ka_ctk_proplist_set_for_widget()
 * on the widget this event belongs to.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_ctk_proplist_set_for_event(ka_proplist *p, CdkEvent *e) {
        gdouble x, y;
        CdkWindow *gw;
        CtkWidget *w = NULL;
        int ret;

        ka_return_val_if_fail(p, CA_ERROR_INVALID);
        ka_return_val_if_fail(e, CA_ERROR_INVALID);
        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);

        if ((gw = e->any.window)) {
                cdk_window_get_user_data(gw, (gpointer*) &w);

                if (w)
                        if ((ret = ka_ctk_proplist_set_for_widget(p, w)) < 0)
                                return ret;
        }

        if (cdk_event_get_root_coords(e, &x, &y)) {

                if ((ret = ka_proplist_setf(p, CA_PROP_EVENT_MOUSE_X, "%0.0f", x)) < 0)
                        return ret;

                if ((ret = ka_proplist_setf(p, CA_PROP_EVENT_MOUSE_Y, "%0.0f", y)) < 0)
                        return ret;

                if (w)  {
                        int width, height;

                        width = cdk_screen_get_width(ctk_widget_get_screen(w));
                        height = cdk_screen_get_height(ctk_widget_get_screen(w));

                        /* We use these strange format strings here to avoid that
                         * libc applies locale information on the formatting of
                         * floating numbers. */

                        if ((ret = ka_proplist_setf(p, CA_PROP_EVENT_MOUSE_HPOS, "%i.%03i",
                                                    (int) (x/(width-1)), (int) (1000.0*x/(width-1)) % 1000)) < 0)
                                return ret;

                        if ((ret = ka_proplist_setf(p, CA_PROP_EVENT_MOUSE_VPOS, "%i.%03i",
                                                    (int) (y/(height-1)), (int) (1000.0*y/(height-1)) % 1000)) < 0)
                                return ret;
                }
        }

        if (e->type == CDK_BUTTON_PRESS ||
            e->type == CDK_2BUTTON_PRESS ||
            e->type == CDK_3BUTTON_PRESS ||
            e->type == CDK_BUTTON_RELEASE) {

                if ((ret = ka_proplist_setf(p, CA_PROP_EVENT_MOUSE_BUTTON, "%u", e->button.button)) < 0)
                        return ret;
        }

        return CA_SUCCESS;
}

/**
 * ka_ctk_play_for_widget:
 * @w: The Ctk widget to base these sound event properties on
 * @id: The event id that can later be used to cancel this event sound
 * using ka_context_cancel(). This can be any integer and shall be
 * chosen be the client program. It is a good idea to pass 0 here if
 * cancelling the sound later is not needed. If the same id is passed
 * to multiple sounds they can be canceled with a single
 * ka_context_cancel() call.
 * @...: additional event properties as pairs of strings, terminated by NULL.
 *
 * Play a sound event for the specified widget. This will internally
 * call ka_ctk_proplist_set_for_widget() and then merge them with the
 * properties passed in via the NULL terminated argument
 * list. Finally, it will call ka_context_play_full() to actually play
 * the event sound.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_ctk_play_for_widget(CtkWidget *w, uint32_t id, ...) {
        va_list ap;
        int ret;
        ka_proplist *p;
        CdkScreen *s;

        ka_return_val_if_fail(w, CA_ERROR_INVALID);
        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);

        if ((ret = ka_proplist_create(&p)) < 0)
                return ret;

        if ((ret = ka_ctk_proplist_set_for_widget(p, w)) < 0)
                goto fail;

        va_start(ap, id);
        ret = ka_proplist_merge_ap(p, ap);
        va_end(ap);

        if (ret < 0)
                goto fail;

        s = ctk_widget_get_screen(w);
        ret = ka_context_play_full(ka_ctk_context_get_for_screen(s), id, p, NULL, NULL);

fail:

        ka_assert_se(ka_proplist_destroy(p) == 0);

        return ret;
}

/**
 * ka_ctk_play_for_event:
 * @e: The Cdk event to base these sound event properties on
 * @id: The event id that can later be used to cancel this event sound
 * using ka_context_cancel(). This can be any integer and shall be
 * chosen be the client program. It is a good idea to pass 0 here if
 * cancelling the sound later is not needed. If the same id is passed
 * to multiple sounds they can be canceled with a single
 * ka_context_cancel() call.
 * @...: additional event properties as pairs of strings, terminated by NULL.
 *
 * Play a sound event for the specified event. This will internally
 * call ka_ctk_proplist_set_for_event() and then merge them with the
 * properties passed in via the NULL terminated argument
 * list. Finally, it will call ka_context_play_full() to actually play
 * the event sound.
 *
 * Returns: 0 on success, negative error code on error.
 */

int ka_ctk_play_for_event(CdkEvent *e, uint32_t id, ...) {
        va_list ap;
        int ret;
        ka_proplist *p;
        CdkScreen *s;

        ka_return_val_if_fail(e, CA_ERROR_INVALID);
        ka_return_val_if_fail(!ka_detect_fork(), CA_ERROR_FORKED);

        if ((ret = ka_proplist_create(&p)) < 0)
                return ret;

        if ((ret = ka_ctk_proplist_set_for_event(p, e)) < 0)
                goto fail;

        va_start(ap, id);
        ret = ka_proplist_merge_ap(p, ap);
        va_end(ap);

        if (ret < 0)
                goto fail;

        if (e->any.window)
#if CTK_CHECK_VERSION (2, 90, 7)
                s = cdk_window_get_screen(e->any.window);
#else
                s = cdk_drawable_get_screen(CDK_DRAWABLE(e->any.window));
#endif
        else
                s = cdk_screen_get_default();

        ret = ka_context_play_full(ka_ctk_context_get_for_screen(s), id, p, NULL, NULL);

fail:

        ka_assert_se(ka_proplist_destroy(p) == 0);

        return ret;
}

/**
 * ka_ctk_widget_disable_sounds:
 * @w: The Ctk widget to disable automatic event sounds for.
 * @enable: Boolean specifying whether sound events shall be enabled or disabled for this widget.
 *
 * By default sound events are automatically generated for all kinds
 * of input events. Use this function to disable this. This is
 * intended to be used for widgets which directly generate sound
 * events.
 */

void ka_ctk_widget_disable_sounds(CtkWidget *w, gboolean enable) {
        static GQuark disable_sound_quark = 0;

        /* This is the same quark used by libgnomeui! */
        if (!disable_sound_quark)
                disable_sound_quark = g_quark_from_static_string("gnome_disable_sound_events");

        g_object_set_qdata(G_OBJECT(w), disable_sound_quark, GINT_TO_POINTER(!!enable));
}
