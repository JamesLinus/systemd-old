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

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>

#include "cgroup-show.h"
#include "cgroup-util.h"
#include "log.h"
#include "path-util.h"
#include "util.h"
#include "pager.h"
#include "build.h"
#include "output-mode.h"
#include "fileio.h"
#include "sd-bus.h"
#include "bus-util.h"
#include "bus-error.h"
#include "unit-name.h"
#include "strv.h"

static bool arg_pager = true;
static bool arg_kernel_threads = false;
static bool arg_all = false;
static int arg_full = -1;
static char* arg_machine = NULL;

static void help(void) {
        printf("%s [OPTIONS...] [CGROUP...]\n\n"
               "Recursively show control group contents.\n\n"
               "  -h --help           Show this help\n"
               "     --version        Show package version\n"
               "     --no-pager       Do not pipe output into a pager\n"
               "  -a --all            Show all groups, including empty\n"
               "  -l --full           Do not ellipsize output\n"
               "  -k                  Include kernel threads in output\n"
               "  -M --machine        Show container\n"
               , program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                { "no-pager",  0 , false, option_set_bool,     &arg_pager,          false },
                { "all",      'a', false, option_set_bool,     &arg_all,            true  },
                { "full",     'l', false, option_set_bool,     &arg_full,           true  },
                { NULL,       'k', false, option_set_bool,     &arg_kernel_threads, true  },
                { "machine",  'M', true,  option_parse_string, &arg_machine               },
                {}
        };
        int r = 0;
        int output_flags;
        _cleanup_free_ char *root = NULL;
        _cleanup_bus_close_unref_ sd_bus *bus = NULL;
        char **args;

        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, &args);
        if (r <= 0)
                goto finish;

        if (arg_pager) {
                r = pager_open(false);
                if (r > 0) {
                        if (arg_full == -1)
                                arg_full = true;
                }
        }

        output_flags =
                arg_all * OUTPUT_SHOW_ALL |
                (arg_full > 0) * OUTPUT_FULL_WIDTH;

        r = sd_bus_default_system(&bus);
        if (r < 0) {
                log_error("Failed to create bus connection: %s", strerror(-r));
                goto finish;
        }

        if (strv_length(argv) > 0) {
                char **a;

                STRV_FOREACH(a, args) {
                        int q;

                        fprintf(stdout, "%s:\n", *a);
                        fflush(stdout);

                        if (arg_machine)
                                root = strjoin("machine/", arg_machine, "/", *a, NULL);
                        else
                                root = strdup(*a);
                        if (!root)
                                return log_oom();

                        q = show_cgroup_by_path(root, NULL, 0,
                                                arg_kernel_threads, output_flags);
                        if (q < 0)
                                r = q;
                }

        } else {
                _cleanup_free_ char *p;

                p = get_current_dir_name();
                if (!p) {
                        log_error("Cannot determine current working directory: %m");
                        goto finish;
                }

                if (path_startswith(p, "/sys/fs/cgroup") && !arg_machine) {
                        printf("Working Directory %s:\n", p);
                        r = show_cgroup_by_path(p, NULL, 0,
                                                arg_kernel_threads, output_flags);
                } else {
                        if (arg_machine) {
                                char *m;
                                const char *cgroup;
                                _cleanup_free_ char *scope = NULL;
                                _cleanup_free_ char *path = NULL;
                                _cleanup_bus_message_unref_ sd_bus_message *reply = NULL;
                                _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;

                                m = strappenda("/run/systemd/machines/", arg_machine);
                                r = parse_env_file(m, NEWLINE, "SCOPE", &scope, NULL);
                                if (r < 0) {
                                        log_error("Failed to get machine path: %s", strerror(-r));
                                        goto finish;
                                }

                                path = unit_dbus_path_from_name(scope);
                                if (!path) {
                                        log_oom();
                                        goto finish;
                                }

                                r = sd_bus_get_property(
                                                bus,
                                                "org.freedesktop.systemd1",
                                                path,
                                                "org.freedesktop.systemd1.Scope",
                                                "ControlGroup",
                                                &error,
                                                &reply,
                                                "s");

                                if (r < 0) {
                                        log_error("Failed to query ControlGroup: %s", bus_error_message(&error, -r));
                                        goto finish;
                                }

                                r = sd_bus_message_read(reply, "s", &cgroup);
                                if (r < 0) {
                                        bus_log_parse_error(r);
                                        goto finish;
                                }

                                root = strdup(cgroup);
                                if (!root) {
                                        log_oom();
                                        goto finish;
                                }

                        } else
                                r = cg_get_root_path(&root);
                        if (r < 0) {
                                log_error("Failed to get %s path: %s",
                                          arg_machine ? "machine" : "root", strerror(-r));
                                goto finish;
                        }

                        r = show_cgroup(SYSTEMD_CGROUP_CONTROLLER, root, NULL, 0,
                                        arg_kernel_threads, output_flags);
                }
        }

        if (r < 0)
                log_error("Failed to list cgroup tree %s: %s", root, strerror(-r));

finish:
        pager_close();

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
