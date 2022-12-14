/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberractkhfoo
#define fookanberractkhfoo

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

#include <kanberra.h>
#include <cdk/cdk.h>
#include <ctk/ctk.h>

G_BEGIN_DECLS

#ifndef CDK_MULTIHEAD_SAFE
ka_context *ka_ctk_context_get(void);
#endif

ka_context *ka_ctk_context_get_for_screen(CdkScreen *screen);

int ka_ctk_proplist_set_for_widget(ka_proplist *p, CtkWidget *w);

int ka_ctk_play_for_widget(CtkWidget *w, uint32_t id, ...) G_GNUC_NULL_TERMINATED;

int ka_ctk_proplist_set_for_event(ka_proplist *p, CdkEvent *e);

int ka_ctk_play_for_event(CdkEvent *e, uint32_t id, ...) G_GNUC_NULL_TERMINATED;

void ka_ctk_widget_disable_sounds(CtkWidget *w, gboolean enable);

G_END_DECLS

#endif
