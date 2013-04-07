/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "log.h"
#include "sd-bus-common.h"
#include "util.h"
#include "missing.h"
#include "def.h"
#include "strv.h"

int bus_parse_unit_info(sd_bus_message *m, struct unit_info *u) {
	int r;
        assert(m);
        assert(u);

        r = sd_bus_message_read(m, "(ssssssouso)",
			&u->id,
			&u->description,
			&u->load_state,
			&u->active_state,
			&u->sub_state,
			&u->following,
			&u->unit_path,
			&u->job_id,
			&u->job_type,
			&u->job_path);

        if (r < 0)
		log_error("Failed to parse reply.");

        return r;
}

int bus_get_property(sd_bus *bus, const char *destination, const char *path, const char *interface, const char *property, const char type, void *val) {
        _cleanup_sd_bus_message_unref_ sd_bus_message *reply = NULL;
        int r;
        char typestring[2] = {type, 0};

        r = sd_bus_call_method(
                        bus,
                        destination,
                        path,
                        "org.freedesktop.DBus.Properties",
                        "Get",
                        NULL,
                        &reply,
                        "ss",
                        interface,
                        property);
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, typestring);
        if (r < 0)  {
                log_error("Failed to parse reply.");
                return r;
        }

        return sd_bus_message_read_basic(reply, type, val);

}

int bus_exit_container_force(sd_bus_message *m, char container_type) {
        char type;
        const char *contents;
        int r;

        uint64_t tmp;

        for (r = sd_bus_message_peek_type(m, &type, &contents);
             r > 0;
             r = sd_bus_message_peek_type(m, &type, &contents)) {
                if (type == SD_BUS_TYPE_ARRAY ||
                    type == SD_BUS_TYPE_VARIANT ||
                    type == SD_BUS_TYPE_STRUCT ||
                    type == SD_BUS_TYPE_DICT_ENTRY) {
                        r = sd_bus_message_enter_container(m, type, contents);
                        if (r < 0)
                                return r;

                        r = bus_exit_container_force(m, 0);
                        if (r < 0)
                                return r;
                } else {
                        r = sd_bus_message_read_basic(m, type, &tmp);
                        if (r < 0)
                                return r;
                }
        }
        if (r < 0)
                return r;

        r = sd_bus_message_exit_container(m);
        if (r < 0)
                return r;

        if (container_type) {
                if (!type)
                        return -ENXIO;
                if (type != container_type)
                        return bus_exit_container_force(m, container_type);
        }
        return 0;
}

void bus_message_unrefp(sd_bus_message **reply) {
        if (!reply)
                return;

        if (!*reply)
                return;

        sd_bus_message_unref(*reply);
}
