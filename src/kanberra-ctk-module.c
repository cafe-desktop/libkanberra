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
#include <cdk/cdkx.h>
#include <X11/Xatom.h>

#include "kanberra-ctk.h"

typedef struct {
        guint signal_id;
        gboolean arg1_is_set;
        GObject *object;
        GValue arg1;
        CdkEvent *event;
} SoundEventData;

/*
   We generate these sounds:

   dialog-error
   dialog-warning
   dialog-information
   dialog-question
   window-new
   window-close
   window-minimized
   window-unminimized
   window-maximized
   window-unmaximized
   notebook-tab-changed
   dialog-ok
   dialog-cancel
   item-selected
   link-pressed
   link-released
   button-pressed
   button-released
   menu-click
   button-toggle-on
   button-toggle-off
   menu-popup
   menu-popdown
   menu-replace
   tooltip-popup
   tooltip-popdown
   drag-start
   drag-accept
   drag-fail
   expander-toggle-on
   expander-toggle-off

   TODO:
   scroll-xxx
   window-switch
   window-resize-xxx
   window-move-xxx

*/

static gboolean disabled = FALSE;

static GQueue sound_event_queue = G_QUEUE_INIT;

static guint idle_id = 0;

static guint
        signal_id_dialog_response,
        signal_id_widget_show,
        signal_id_widget_hide,
        signal_id_check_menu_item_toggled,
        signal_id_menu_item_activate,
        signal_id_toggle_button_toggled,
        signal_id_button_pressed,
        signal_id_button_released,
        signal_id_widget_window_state_event,
        signal_id_notebook_switch_page,
        signal_id_tree_view_cursor_changed,
        signal_id_icon_view_selection_changed,
        signal_id_widget_drag_begin,
        signal_id_widget_drag_failed,
        signal_id_widget_drag_drop,
        signal_id_expander_activate;

static GQuark
        disable_sound_quark,
        was_iconized_quark,
        is_xembed_quark;

/* Make sure GCC doesn't warn us about a missing prototype for this
 * exported function */
void ctk_module_init(gint *argc, gchar ***argv[]);

static const char *translate_message_tye(CtkMessageType mt) {
        static const char *const message_type_table[] = {
                [CTK_MESSAGE_INFO] = "dialog-information",
                [CTK_MESSAGE_WARNING] = "dialog-warning",
                [CTK_MESSAGE_QUESTION] = "dialog-question",
                [CTK_MESSAGE_ERROR] = "dialog-error",
                [CTK_MESSAGE_OTHER] = NULL
        };

        if (mt >= G_N_ELEMENTS(message_type_table))
                return NULL;

        return message_type_table[mt];
}

static const char *translate_response(int response) {
        static const char *const response_table[] = {
                [-CTK_RESPONSE_NONE] = NULL,
                [-CTK_RESPONSE_REJECT] = "dialog-cancel",
                [-CTK_RESPONSE_DELETE_EVENT] = "dialog-cancel",
                [-CTK_RESPONSE_ACCEPT] = "dialog-ok",
                [-CTK_RESPONSE_OK] = "dialog-ok",
                [-CTK_RESPONSE_CANCEL] = "dialog-cancel",
                [-CTK_RESPONSE_CLOSE] = "dialog-ok",
                [-CTK_RESPONSE_YES] = "dialog-ok",
                [-CTK_RESPONSE_NO] = "dialog-cancel",
                [-CTK_RESPONSE_APPLY] = "dialog-ok",
                [-CTK_RESPONSE_HELP] = NULL,
        };

        if (response >= 0)
                return NULL;

        if ((unsigned) -response >= G_N_ELEMENTS(response_table))
                return NULL;

        return response_table[-response];
}

static gboolean is_child_of_combo_box(CtkWidget *w) {

        while (w) {

                if (CTK_IS_COMBO_BOX(w))
                        return TRUE;

                w = ctk_widget_get_parent(w);
        }

        return FALSE;
}

static CtkDialog* find_parent_dialog(CtkWidget *w) {

        while (w) {

                if (CTK_IS_DIALOG(w))
                        return CTK_DIALOG(w);

                w = ctk_widget_get_parent(w);
        }

        return NULL;
}

static void free_sound_event(SoundEventData *d) {

        g_object_unref(d->object);

        if (d->arg1_is_set)
                g_value_unset(&d->arg1);

        if (d->event)
                cdk_event_free(d->event);

        g_slice_free(SoundEventData, d);
}

static gboolean is_menu_hint(CdkWindowTypeHint hint) {
        return
                hint == CDK_WINDOW_TYPE_HINT_POPUP_MENU ||
                hint == CDK_WINDOW_TYPE_HINT_DROPDOWN_MENU ||
                hint == CDK_WINDOW_TYPE_HINT_MENU;
}

static SoundEventData* filter_sound_event(SoundEventData *d) {
        GList *i, *n;

        do {

                for (i = sound_event_queue.head; i; i = n) {
                        SoundEventData *j;

                        j = i->data;
                        n = i->next;

                        if (d->object == j->object) {

                                /* Let's drop a show event immediately followed by a
                                 * hide event */

                                if (d->signal_id == signal_id_widget_show &&
                                    j->signal_id == signal_id_widget_hide) {

                                        free_sound_event(d);
                                        free_sound_event(j);
                                        g_queue_delete_link(&sound_event_queue, i);

                                        return NULL;
                                }

                                /* Let's drop widget hide events in favour of dialog
                                 * response.
                                 *
                                 * Let's drop widget window state events in favour of
                                 * widget hide/show.
                                 *
                                 * Let's drop double events */

                                if ((d->signal_id == signal_id_widget_hide &&
                                     j->signal_id == signal_id_dialog_response) ||

                                    (d->signal_id == signal_id_widget_window_state_event &&
                                     j->signal_id == signal_id_widget_hide) ||

                                    (d->signal_id == signal_id_widget_window_state_event &&
                                     j->signal_id == signal_id_widget_show)) {

                                        free_sound_event(d);
                                        d = j;
                                        g_queue_delete_link(&sound_event_queue, i);
                                        break;
                                }

                                if ((d->signal_id == signal_id_dialog_response &&
                                     j->signal_id == signal_id_widget_hide) ||

                                    (d->signal_id == signal_id_widget_show &&
                                     j->signal_id == signal_id_widget_window_state_event) ||

                                    (d->signal_id == signal_id_widget_hide &&
                                     j->signal_id == signal_id_widget_window_state_event) ||

                                    (d->signal_id == j->signal_id)) {

                                        free_sound_event(j);
                                        g_queue_delete_link(&sound_event_queue, i);
                                }

                        } else if (CTK_IS_WINDOW(d->object) && CTK_IS_WINDOW(j->object)) {

                                CdkWindowTypeHint dhint, jhint;

                                dhint = ctk_window_get_type_hint(CTK_WINDOW(d->object));
                                jhint = ctk_window_get_type_hint(CTK_WINDOW(j->object));

                                if (is_menu_hint(dhint) && is_menu_hint(jhint)) {

                                        if (d->signal_id == signal_id_widget_hide &&
                                            j->signal_id == signal_id_widget_show) {
                                                free_sound_event(d);
                                                d = j;
                                                g_queue_delete_link(&sound_event_queue, i);
                                                break;
                                        }

                                        if (d->signal_id == signal_id_widget_show &&
                                            j->signal_id == signal_id_widget_hide) {

                                                free_sound_event(j);
                                                g_queue_delete_link(&sound_event_queue, i);
                                        }
                                }
                        }
                }

                /* If we exited the iteration early, let's retry. */

        } while (i);

        /* FIXME: Filter menu hide on menu show */

        return d;
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

static gint display_get_desktop(CdkDisplay *d) {
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

        if (XGetWindowProperty(CDK_DISPLAY_XDISPLAY(d), DefaultRootWindow(CDK_DISPLAY_XDISPLAY(d)),
                               cdk_x11_get_xatom_by_name_for_display(d, "_NET_CURRENT_DESKTOP"),
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

static gboolean window_is_xembed(CdkDisplay *d, CdkWindow *w) {
        Atom type_return;
        gint format_return;
        gulong nitems_return;
        gulong bytes_after_return;
        guchar *data = NULL;
        gboolean ret = FALSE;
        Atom xembed;

#ifdef CDK_IS_X11_DISPLAY
        if (!CDK_IS_X11_DISPLAY(d))
                return FALSE;
#endif

        /* Gnome Panel applets are XEMBED windows. We need to make sure we
         * ignore them */

        xembed = cdk_x11_get_xatom_by_name_for_display(d, "_XEMBED_INFO");

        /* be robust against not existing XIDs (LP: #834403) */
        cdk_error_trap_push();
        if (XGetWindowProperty(CDK_DISPLAY_XDISPLAY(d), CDK_WINDOW_XID(w),
                               xembed,
                               0, 2, False, xembed, &type_return,
                               &format_return, &nitems_return, &bytes_after_return,
                               &data) != Success) {
                return FALSE;
        }

#if CTK_CHECK_VERSION(3,0,0)
        cdk_error_trap_pop_ignored();
#else
        cdk_flush();
        cdk_error_trap_pop();
#endif

        if (type_return == xembed && format_return == 32 && data)
                ret = TRUE;

        if (type_return != None && data != NULL)
                XFree(data);

        return ret;
}

static void dispatch_sound_event(SoundEventData *d) {
        int ret = KA_SUCCESS;
        static gboolean menu_is_popped_up = FALSE;

        if (g_object_get_qdata(d->object, disable_sound_quark))
                return;

        /* The CdkWindow of the the widget might have changed while this
         * event was queued for us. Make sure to update it from the
         * current one if necessary. */
        if (d->event && d->event->any.window) {
                CdkWindow *window;

                g_object_unref(G_OBJECT(d->event->any.window));

                if ((window = ctk_widget_get_window(CTK_WIDGET(d->object))))
                        d->event->any.window = CDK_WINDOW(g_object_ref(G_OBJECT(window)));
                else
                        d->event->any.window = NULL;
        }

        if (d->signal_id == signal_id_widget_show) {
                CdkWindowTypeHint hint;

                /* Show/hide signals for non-windows have already been filtered out
                 * by the emission hook! */

                hint = ctk_window_get_type_hint(CTK_WINDOW(d->object));

                if (is_menu_hint(hint)) {

                        if (!menu_is_popped_up) {

                                ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                             KA_PROP_EVENT_ID, "menu-popup",
                                                             KA_PROP_EVENT_DESCRIPTION, "Menu popped up",
                                                             KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                             NULL);
                        } else {
                                ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                             KA_PROP_EVENT_ID, "menu-replace",
                                                             KA_PROP_EVENT_DESCRIPTION, "Menu replaced",
                                                             KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                             NULL);
                        }

                        menu_is_popped_up = TRUE;

                } else if (hint == CDK_WINDOW_TYPE_HINT_TOOLTIP) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "tooltip-popup",
                                                     KA_PROP_EVENT_DESCRIPTION, "Tooltip popped up",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);

                } else if (hint == CDK_WINDOW_TYPE_HINT_NORMAL ||
                           hint == CDK_WINDOW_TYPE_HINT_DIALOG) {

                        gboolean played_sound = FALSE;
                        gboolean is_xembed;

                        is_xembed =
                                ctk_widget_get_realized(CTK_WIDGET(d->object)) &&
                                window_is_xembed(
                                                ctk_widget_get_display(CTK_WIDGET(d->object)),
                                                ctk_widget_get_window(CTK_WIDGET(d->object)));

                        g_object_set_qdata(d->object, is_xembed_quark, GINT_TO_POINTER(is_xembed));

                        if (CTK_IS_MESSAGE_DIALOG(d->object)) {
                                CtkMessageType mt;
                                const char *id;

                                g_object_get(d->object, "message_type", &mt, NULL);

                                if ((id = translate_message_tye(mt))) {

                                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                                     KA_PROP_EVENT_ID, id,
                                                                     KA_PROP_EVENT_DESCRIPTION, "Message dialog shown",
                                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                                     NULL);
                                        played_sound = TRUE;
                                }

                        }

                        if (!played_sound &&
                            !is_xembed &&
                            ctk_window_get_decorated(CTK_WINDOW(d->object))) {

                                ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                             KA_PROP_EVENT_ID, "window-new",
                                                             KA_PROP_EVENT_DESCRIPTION, "Window shown",
                                                             KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                             NULL);

                        }
                }
        }

        if (CTK_IS_DIALOG(d->object) && d->signal_id == signal_id_dialog_response) {

                int response;
                const char *id;

                response = g_value_get_int(&d->arg1);

                if ((id = translate_response(response))) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, id,
                                                     KA_PROP_EVENT_DESCRIPTION, "Dialog closed",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);
                } else {
                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "window-close",
                                                     KA_PROP_EVENT_DESCRIPTION, "Window closed",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);
                }

        } else if (d->signal_id == signal_id_widget_hide) {
                CdkWindowTypeHint hint;

                hint = ctk_window_get_type_hint(CTK_WINDOW(d->object));

                if (is_menu_hint(hint)) {

                        if (CTK_IS_MENU(ctk_bin_get_child(CTK_BIN(d->object)))) {

                                ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                             KA_PROP_EVENT_ID, "menu-popdown",
                                                             KA_PROP_EVENT_DESCRIPTION, "Menu popped down",
                                                             KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                             NULL);
                        }

                        menu_is_popped_up = FALSE;

                } else if (hint == CDK_WINDOW_TYPE_HINT_TOOLTIP) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "tooltip-popdown",
                                                     KA_PROP_EVENT_DESCRIPTION, "Tooltip popped down",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);

                } else if ((hint == CDK_WINDOW_TYPE_HINT_NORMAL ||
                            hint == CDK_WINDOW_TYPE_HINT_DIALOG)) {

                        gboolean is_xembed;

                        is_xembed = !!g_object_get_qdata(d->object, is_xembed_quark);

                        if (!is_xembed &&
                            ctk_window_get_decorated(CTK_WINDOW(d->object)))
                                ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                             KA_PROP_EVENT_ID, "window-close",
                                                             KA_PROP_EVENT_DESCRIPTION, "Window closed",
                                                             KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                             NULL);
                }
        }

        if (CTK_IS_WINDOW(d->object) && d->signal_id == signal_id_widget_window_state_event) {
                CdkEventWindowState *e;
                gint w_desktop = -1, c_desktop = -1;

                e = (CdkEventWindowState*) d->event;

                /* Unfortunately CDK_WINDOW_STATE_ICONIFIED is used both for
                 * proper minimizing and when a window becomes invisible
                 * because the desktop was switched. To handle this we check
                 * if the window becoming invisible is actually on the current
                 * desktop, and only if that's the case we assume it is being
                 * minimized. We then store this information, so that we know
                 * later on when the window is unminimized again. */

                if (ctk_widget_get_realized(CTK_WIDGET(d->object))) {
                        CdkDisplay *display;

                        display = ctk_widget_get_display(CTK_WIDGET(d->object));
                        w_desktop = window_get_desktop(display, ctk_widget_get_window(CTK_WIDGET(d->object)));
                        c_desktop = display_get_desktop(display);
                }

                if ((e->changed_mask & CDK_WINDOW_STATE_ICONIFIED) &&
                    (e->new_window_state & CDK_WINDOW_STATE_ICONIFIED) &&
                    (w_desktop == c_desktop || w_desktop < 0)) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "window-minimized",
                                                     KA_PROP_EVENT_DESCRIPTION, "Window minimized",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);

                        g_object_set_qdata(d->object, was_iconized_quark, GINT_TO_POINTER(1));

                } else if ((e->changed_mask & (CDK_WINDOW_STATE_MAXIMIZED|CDK_WINDOW_STATE_FULLSCREEN)) &&
                           (e->new_window_state & (CDK_WINDOW_STATE_MAXIMIZED|CDK_WINDOW_STATE_FULLSCREEN))) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "window-maximized",
                                                     KA_PROP_EVENT_DESCRIPTION, "Window maximized",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);

                        g_object_set_qdata(d->object, was_iconized_quark, GINT_TO_POINTER(0));

                } else if ((e->changed_mask & CDK_WINDOW_STATE_ICONIFIED) &&
                           !(e->new_window_state & CDK_WINDOW_STATE_ICONIFIED) &&
                           g_object_get_qdata(d->object, was_iconized_quark)) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "window-unminimized",
                                                     KA_PROP_EVENT_DESCRIPTION, "Window unminimized",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);

                        g_object_set_qdata(d->object, was_iconized_quark, GINT_TO_POINTER(0));

                } else if ((e->changed_mask & (CDK_WINDOW_STATE_MAXIMIZED|CDK_WINDOW_STATE_FULLSCREEN)) &&
                           !(e->new_window_state & (CDK_WINDOW_STATE_MAXIMIZED|CDK_WINDOW_STATE_FULLSCREEN))) {

                        ret = ka_ctk_play_for_widget(CTK_WIDGET(d->object), 0,
                                                     KA_PROP_EVENT_ID, "window-unmaximized",
                                                     KA_PROP_EVENT_DESCRIPTION, "Window unmaximized",
                                                     KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                     NULL);
                }
        }

        if (CTK_IS_CHECK_MENU_ITEM(d->object) && d->signal_id == signal_id_check_menu_item_toggled) {

                if (ctk_check_menu_item_get_active(CTK_CHECK_MENU_ITEM(d->object)))
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "button-toggle-on",
                                                    KA_PROP_EVENT_DESCRIPTION, "Check menu item checked",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                else
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "button-toggle-off",
                                                    KA_PROP_EVENT_DESCRIPTION, "Check menu item unchecked",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);

        } else if (CTK_IS_MENU_ITEM(d->object) && d->signal_id == signal_id_menu_item_activate) {

                if (!ctk_menu_item_get_submenu(CTK_MENU_ITEM(d->object)))
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "menu-click",
                                                    KA_PROP_EVENT_DESCRIPTION, "Menu item clicked",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
        }

        if (CTK_IS_TOGGLE_BUTTON(d->object)) {

                if (d->signal_id == signal_id_toggle_button_toggled) {

                        if (!is_child_of_combo_box(CTK_WIDGET(d->object))) {

                                /* We don't want to play this sound if this is a toggle
                                 * button belonging to combo box. */

                                if (ctk_toggle_button_get_active(CTK_TOGGLE_BUTTON(d->object)))
                                        ret = ka_ctk_play_for_event(d->event, 0,
                                                                    KA_PROP_EVENT_ID, "button-toggle-on",
                                                                    KA_PROP_EVENT_DESCRIPTION, "Toggle button checked",
                                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                                    NULL);
                                else
                                        ret = ka_ctk_play_for_event(d->event, 0,
                                                                    KA_PROP_EVENT_ID, "button-toggle-off",
                                                                    KA_PROP_EVENT_DESCRIPTION, "Toggle button unchecked",
                                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                                    NULL);
                        }
                }

        } else if (CTK_IS_LINK_BUTTON(d->object)) {

                if (d->signal_id == signal_id_button_pressed) {
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "link-pressed",
                                                    KA_PROP_EVENT_DESCRIPTION, "Link pressed",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);

                } else if (d->signal_id == signal_id_button_released) {

                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "link-released",
                                                    KA_PROP_EVENT_DESCRIPTION, "Link released",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                }

        } else if (CTK_IS_BUTTON(d->object) && !CTK_IS_TOGGLE_BUTTON(d->object)) {

                if (d->signal_id == signal_id_button_pressed) {
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "button-pressed",
                                                    KA_PROP_EVENT_DESCRIPTION, "Button pressed",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);

                } else if (d->signal_id == signal_id_button_released) {
                        CtkDialog *dialog;
                        gboolean dont_play = FALSE;

                        if ((dialog = find_parent_dialog(CTK_WIDGET(d->object)))) {
                                int response;

                                /* Don't play the click sound if this is a response widget
                                 * we will generate a dialog-xxx event sound anyway. */

                                response = ctk_dialog_get_response_for_widget(dialog, CTK_WIDGET(d->object));
                                dont_play = !!translate_response(response);
                        }

                        if (!dont_play)
                                ret = ka_ctk_play_for_event(d->event, 0,
                                                            KA_PROP_EVENT_ID, "button-released",
                                                            KA_PROP_EVENT_DESCRIPTION, "Button released",
                                                            KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                            NULL);
                }
        }

        if (CTK_IS_NOTEBOOK(d->object) && d->signal_id == signal_id_notebook_switch_page) {
                ret = ka_ctk_play_for_event(d->event, 0,
                                            KA_PROP_EVENT_ID, "notebook-tab-changed",
                                            KA_PROP_EVENT_DESCRIPTION, "Tab changed",
                                            KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                            NULL);
                goto finish;
        }

        if (CTK_IS_TREE_VIEW(d->object) && d->signal_id == signal_id_tree_view_cursor_changed) {
                ret = ka_ctk_play_for_event(d->event, 0,
                                            KA_PROP_EVENT_ID, "item-selected",
                                            KA_PROP_EVENT_DESCRIPTION, "Item selected",
                                            KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                            NULL);
                goto finish;
        }

        if (CTK_IS_ICON_VIEW(d->object) && d->signal_id == signal_id_icon_view_selection_changed) {
                ret = ka_ctk_play_for_event(d->event, 0,
                                            KA_PROP_EVENT_ID, "item-selected",
                                            KA_PROP_EVENT_DESCRIPTION, "Item selected",
                                            KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                            NULL);
                goto finish;
        }

        if (CTK_IS_EXPANDER(d->object) && d->signal_id == signal_id_expander_activate) {

                if (ctk_expander_get_expanded(CTK_EXPANDER(d->object)))
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "expander-toggle-on",
                                                    KA_PROP_EVENT_DESCRIPTION, "Expander expanded",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                else
                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "expander-toggle-off",
                                                    KA_PROP_EVENT_DESCRIPTION, "Expander unexpanded",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);

                goto finish;
        }

        if (CTK_IS_WIDGET(d->object)) {

                if (d->signal_id == signal_id_widget_drag_begin) {

                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "drag-start",
                                                    KA_PROP_EVENT_DESCRIPTION, "Drag started",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                        goto finish;

                } else if (d->signal_id == signal_id_widget_drag_drop) {

                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "drag-accept",
                                                    KA_PROP_EVENT_DESCRIPTION, "Drag accepted",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                        goto finish;

                } else if (d->signal_id == signal_id_widget_drag_failed) {

                        ret = ka_ctk_play_for_event(d->event, 0,
                                                    KA_PROP_EVENT_ID, "drag-fail",
                                                    KA_PROP_EVENT_DESCRIPTION, "Drag failed",
                                                    KA_PROP_KANBERRA_CACHE_CONTROL, "permanent",
                                                    NULL);
                        goto finish;
                }
        }

finish:

        ;
        /* if (ret != KA_SUCCESS) */
        /*     g_warning("Failed to play event sound: %s", ka_strerror(ret)); */
}

static void dispatch_queue(void) {
        SoundEventData *d;

        while ((d = g_queue_pop_head(&sound_event_queue))) {

                if (!(d = filter_sound_event(d)))
                        continue;

                dispatch_sound_event(d);
                free_sound_event(d);
        }
}

static gboolean idle_cb(void *userdata) {
        idle_id = 0;

        dispatch_queue();

        return FALSE;
}

static void connect_settings(void);

static gboolean emission_hook_cb(GSignalInvocationHint *hint, guint n_param_values, const GValue *param_values, gpointer data) {
        static SoundEventData *d = NULL;
        CdkEvent *e;
        GObject *object;

        connect_settings();

        if (disabled)
                return TRUE;

        object = g_value_get_object(&param_values[0]);

        /* g_message("signal '%s' on object of type '%s' with name '%s'", */
        /*           g_signal_name(hint->signal_id), */
        /*           G_OBJECT_TYPE_NAME(object), */
        /*           ctk_widget_get_name(CTK_WIDGET(object))); */

        /* if (CTK_IS_WINDOW(object)) */
        /*     g_message("window role='%s' title='%s' type='%u'", */
        /*               ctk_window_get_role(CTK_WINDOW(object)), */
        /*               ctk_window_get_title(CTK_WINDOW(object)), */
        /*               ctk_window_get_type_hint(CTK_WINDOW(object))); */

        /* Filter a few very often occuring signals as quickly as possible */
        if ((hint->signal_id == signal_id_widget_hide ||
             hint->signal_id == signal_id_widget_show ||
             hint->signal_id == signal_id_widget_window_state_event) &&
            !CTK_IS_WINDOW(object))
                return TRUE;

        if (hint->signal_id != signal_id_widget_hide &&
            hint->signal_id != signal_id_dialog_response &&
            !ctk_widget_is_drawable(CTK_WIDGET (object)))
                return TRUE;

        d = g_slice_new0(SoundEventData);

        d->object = g_object_ref(object);

        d->signal_id = hint->signal_id;

        if (d->signal_id == signal_id_widget_window_state_event) {
                d->event = cdk_event_copy(g_value_peek_pointer(&param_values[1]));
        } else if ((e = ctk_get_current_event()))
                d->event = cdk_event_copy(e);

        if (n_param_values > 1) {
                g_value_init(&d->arg1, G_VALUE_TYPE(&param_values[1]));
                g_value_copy(&param_values[1], &d->arg1);
                d->arg1_is_set = TRUE;
        }

        g_queue_push_tail(&sound_event_queue, d);

        if (idle_id == 0)
                idle_id = cdk_threads_add_idle_full(CDK_PRIORITY_REDRAW-1, (GSourceFunc) idle_cb, NULL, NULL);

        return TRUE;
}

static void install_hook(GType type, const char *sig, guint *sn) {
        GTypeClass *type_class;

        type_class = g_type_class_ref(type);

        *sn = g_signal_lookup(sig, type);
        g_signal_add_emission_hook(*sn, 0, emission_hook_cb, NULL, NULL);

        g_type_class_unref(type_class);
}

static void read_enable_input_feedback_sounds(CtkSettings *s) {
        gboolean enabled = !disabled;

        if (g_getenv("KANBERRA_FORCE_INPUT_FEEDBACK_SOUNDS"))
                disabled = FALSE;
        else {
                g_object_get(G_OBJECT(s), "ctk-enable-input-feedback-sounds", &enabled, NULL);
                disabled = !enabled;
        }
}

static void enable_input_feedback_sounds_changed(CtkSettings *s, GParamSpec *arg1, gpointer userdata) {
        read_enable_input_feedback_sounds(s);
}

static void connect_settings(void) {
        CtkSettings *s;
        static gboolean connected = FALSE;

        if (connected)
                return;

        if (!(s = ctk_settings_get_default()))
                return;

        if (g_object_class_find_property(G_OBJECT_GET_CLASS(s), "ctk-enable-input-feedback-sounds")) {
                g_signal_connect(G_OBJECT(s), "notify::ctk-enable-input-feedback-sounds", G_CALLBACK(enable_input_feedback_sounds_changed), NULL);
                read_enable_input_feedback_sounds(s);
        } else
                g_debug("This Ctk+ version doesn't have the CtkSettings::ctk-enable-input-feedback-sounds property.");

        connected = TRUE;
}

#if CTK_CHECK_VERSION(3,0,0)
#warning "We really need a quit handler in Ctk 3.0, https://bugzilla.gnome.org/show_bug.cgi?id=639770"
#else
static gboolean quit_handler(gpointer data) {
        dispatch_queue();
        return FALSE;
}
#endif

G_MODULE_EXPORT void ctk_module_init(gint *argc, gchar ***argv[]) {

        /* This is the same quark libgnomeui uses! */
        disable_sound_quark = g_quark_from_string("gnome_disable_sound_events");
        was_iconized_quark = g_quark_from_string("kanberra_was_iconized");
        is_xembed_quark = g_quark_from_string("kanberra_is_xembed");

        /* Hook up the ctk setting */
        connect_settings();

        install_hook(CTK_TYPE_WINDOW, "show", &signal_id_widget_show);
        install_hook(CTK_TYPE_WINDOW, "hide", &signal_id_widget_hide);
        install_hook(CTK_TYPE_DIALOG, "response", &signal_id_dialog_response);
        install_hook(CTK_TYPE_MENU_ITEM, "activate", &signal_id_menu_item_activate);
        install_hook(CTK_TYPE_CHECK_MENU_ITEM, "toggled", &signal_id_check_menu_item_toggled);
        install_hook(CTK_TYPE_TOGGLE_BUTTON, "toggled", &signal_id_toggle_button_toggled);
        install_hook(CTK_TYPE_BUTTON, "pressed", &signal_id_button_pressed);
        install_hook(CTK_TYPE_BUTTON, "released", &signal_id_button_released);
        install_hook(CTK_TYPE_WIDGET, "window-state-event", &signal_id_widget_window_state_event);
        install_hook(CTK_TYPE_NOTEBOOK, "switch-page", &signal_id_notebook_switch_page);
        install_hook(CTK_TYPE_TREE_VIEW, "cursor-changed", &signal_id_tree_view_cursor_changed);
        install_hook(CTK_TYPE_ICON_VIEW, "selection-changed", &signal_id_icon_view_selection_changed);
        install_hook(CTK_TYPE_WIDGET, "drag-begin", &signal_id_widget_drag_begin);
        install_hook(CTK_TYPE_WIDGET, "drag-drop", &signal_id_widget_drag_drop);
        install_hook(CTK_TYPE_WIDGET, "drag-failed", &signal_id_widget_drag_failed);
        install_hook(CTK_TYPE_EXPANDER, "activate", &signal_id_expander_activate);

#if !CTK_CHECK_VERSION(3,0,0)
        ctk_quit_add(1, quit_handler, NULL);
#endif
}

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module);

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module) {
        g_module_make_resident(module);
        return NULL;
}
