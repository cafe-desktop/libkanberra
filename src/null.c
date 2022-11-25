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

#include "kanberra.h"
#include "common.h"
#include "driver.h"

int driver_open(ka_context *c) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(!c->driver || ka_streq(c->driver, "null"), CA_ERROR_NODRIVER);

        return CA_SUCCESS;
}

int driver_destroy(ka_context *c) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        return CA_SUCCESS;
}

int driver_change_device(ka_context *c, const char *device) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        return CA_SUCCESS;
}

int driver_change_props(ka_context *c, ka_proplist *changed, ka_proplist *merged) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(changed, CA_ERROR_INVALID);
        ka_return_val_if_fail(merged, CA_ERROR_INVALID);

        return CA_SUCCESS;
}

int driver_play(ka_context *c, uint32_t id, ka_proplist *proplist, ka_finish_callback_t cb, void *userdata) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, CA_ERROR_INVALID);
        ka_return_val_if_fail(!userdata || cb, CA_ERROR_INVALID);

        if (cb)
                cb(c, id, CA_SUCCESS, userdata);

        return CA_SUCCESS;
}

int driver_cancel(ka_context *c, uint32_t id) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);

        return CA_SUCCESS;
}

int driver_cache(ka_context *c, ka_proplist *proplist) {
        ka_return_val_if_fail(c, CA_ERROR_INVALID);
        ka_return_val_if_fail(proplist, CA_ERROR_INVALID);

        return CA_ERROR_NOTSUPPORTED;
}
