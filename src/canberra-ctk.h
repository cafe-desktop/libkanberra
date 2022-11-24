/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef foocanberractkhfoo
#define foocanberractkhfoo

/***
  This file is part of libcanberra.

  Copyright 2008 Lennart Poettering

  libcanberra is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 2.1 of the
  License, or (at your option) any later version.

  libcanberra is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with libcanberra. If not, see
  <http://www.gnu.org/licenses/>.
***/

#include <canberra.h>
#include <cdk/cdk.h>
#include <ctk/ctk.h>

G_BEGIN_DECLS

#ifndef GDK_MULTIHEAD_SAFE
ca_context *ca_ctk_context_get(void);
#endif

ca_context *ca_ctk_context_get_for_screen(GdkScreen *screen);

int ca_ctk_proplist_set_for_widget(ca_proplist *p, CtkWidget *w);

int ca_ctk_play_for_widget(CtkWidget *w, uint32_t id, ...) G_GNUC_NULL_TERMINATED;

int ca_ctk_proplist_set_for_event(ca_proplist *p, GdkEvent *e);

int ca_ctk_play_for_event(GdkEvent *e, uint32_t id, ...) G_GNUC_NULL_TERMINATED;

void ca_ctk_widget_disable_sounds(CtkWidget *w, gboolean enable);

G_END_DECLS

#endif
