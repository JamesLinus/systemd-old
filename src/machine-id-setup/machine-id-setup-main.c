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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "machine-id-setup.h"
#include "log.h"
#include "option.h"

static const char *arg_root = "";

static void help(void) {
        printf("%s [OPTIONS...]\n\n"
               "Initialize /etc/machine-id from a random source.\n\n"
               "  -h --help             Show this help\n"
               "     --version          Show package version\n"
               "     --root=ROOT        Filesystem root\n",
               program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                { "root", 0, true, option_parse_string, &arg_root },
                {}
        };
        int r;

        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, NULL);
        if (r <= 0)
                return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

        return machine_id_setup(arg_root) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
