/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

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

#include <systemd/sd-bus.h>
#include <inttypes.h>
#include <sys/types.h>

enum {
	SD_BUS_BUS_USER,
	SD_BUS_BUS_SYSTEM,
};

struct unit_info {
        const char *id;
        const char *description;
        const char *load_state;
        const char *active_state;
        const char *sub_state;
        const char *following;
        const char *unit_path;
        uint32_t job_id;
        const char *job_type;
        const char *job_path;
};

int bus_parse_unit_info(sd_bus_message *m, struct unit_info *u);

int bus_get_property(sd_bus *bus, const char *destination, const char *path, const char *interface, const char *property, const char type, void *val);
int bus_exit_container_force(sd_bus_message *m, char container_type);

void bus_message_unrefp(sd_bus_message **m);

#define _cleanup_sd_bus_message_unref_ __attribute__((cleanup(bus_message_unrefp)))
#define _cleanup_sd_bus_error_free_ __attribute__((cleanup(sd_bus_error_free)))
