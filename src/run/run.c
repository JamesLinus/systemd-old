/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

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

#include <stdio.h>
#include <getopt.h>

#include "sd-bus.h"
#include "bus-util.h"
#include "strv.h"
#include "build.h"
#include "unit-name.h"
#include "env-util.h"
#include "path-util.h"
#include "bus-error.h"

static bool arg_scope = false;
static bool arg_remain_after_exit = false;
static const char *arg_unit = NULL;
static const char *arg_description = NULL;
static const char *arg_slice = NULL;
static bool arg_send_sighup = false;
static BusTransport arg_transport = {BUS_TRANSPORT_LOCAL};
static const char *arg_service_type = NULL;
static const char *arg_exec_user = NULL;
static const char *arg_exec_group = NULL;
static int arg_nice = PRIO_MIN - 1;
static char **arg_environment = NULL;
static char **arg_property = NULL;

static void help(void) {
        printf("%s [OPTIONS...] COMMAND [ARGS...]\n\n"
               "Run the specified command in a transient scope or service unit.\n\n"
               "  -h --help                 Show this help\n"
               "     --version              Show package version\n"
               "     --user                 Run as user unit\n"
               "  -H --host=[USER@]HOST     Operate on remote host\n"
               "  -M --machine=CONTAINER    Operate on local container\n"
               "     --scope                Run this as scope rather than service\n"
               "     --unit=UNIT            Run under the specified unit name\n"
               "  -p --property=NAME=VALUE  Set unit property\n"
               "     --description=TEXT     Description for unit\n"
               "     --slice=SLICE          Run in the specified slice\n"
               "  -r --remain-after-exit    Leave service around until explicitly stopped\n"
               "     --send-sighup          Send SIGHUP when terminating\n"
               "     --service-type=TYPE    Service type\n"
               "     --uid=USER             Run as system user\n"
               "     --gid=GROUP            Run as system group\n"
               "     --nice=NICE            Nice level\n"
               "     --setenv=NAME=VALUE    Set environment\n",
               program_invocation_short_name);
}

static int message_start_transient_unit_new(sd_bus *bus, const char *name, sd_bus_message **ret) {
        _cleanup_bus_message_unref_ sd_bus_message *m = NULL;
        char **i;
        int r;

        assert(bus);
        assert(name);
        assert(ret);

        r = sd_bus_message_new_method_call(
                        bus,
                        &m,
                        "org.freedesktop.systemd1",
                        "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager",
                        "StartTransientUnit");
        if (r < 0)
                return r;

        r = sd_bus_message_append(m, "ss", name, "fail");
        if (r < 0)
                return r;

        r = sd_bus_message_open_container(m, 'a', "(sv)");
        if (r < 0)
                return r;

        STRV_FOREACH(i, arg_property) {
                r = sd_bus_message_open_container(m, 'r', "sv");
                if (r < 0)
                        return r;

                r = bus_append_unit_property_assignment(m, *i);
                if (r < 0)
                        return r;

                r = sd_bus_message_close_container(m);
                if (r < 0)
                        return r;
        }

        r = sd_bus_message_append(m, "(sv)", "Description", "s", arg_description);
        if (r < 0)
                return r;

        if (!isempty(arg_slice)) {
                _cleanup_free_ char *slice;

                slice = unit_name_mangle_with_suffix(arg_slice, MANGLE_NOGLOB, ".slice");
                if (!slice)
                        return -ENOMEM;

                r = sd_bus_message_append(m, "(sv)", "Slice", "s", slice);
                if (r < 0)
                        return r;
        }

        if (arg_send_sighup) {
                r = sd_bus_message_append(m, "(sv)", "SendSIGHUP", "b", arg_send_sighup);
                if (r < 0)
                        return r;
        }

        *ret = m;
        m = NULL;

        return 0;
}

static int message_start_transient_unit_send(sd_bus *bus, sd_bus_message *m, sd_bus_error *error, sd_bus_message **reply) {
        int r;

        assert(bus);
        assert(m);

        r = sd_bus_message_close_container(m);
        if (r < 0)
                return r;

        r = sd_bus_message_append(m, "a(sa(sv))", 0);
        if (r < 0)
                return r;

        return sd_bus_call(bus, m, 0, error, reply);
}

static int start_transient_service(
                sd_bus *bus,
                char **argv,
                sd_bus_error *error) {

        _cleanup_bus_message_unref_ sd_bus_message *m = NULL;
        _cleanup_free_ char *name = NULL;
        int r;

        if (arg_unit) {
                name = unit_name_mangle_with_suffix(arg_unit, MANGLE_NOGLOB, ".service");
                if (!name)
                        return log_oom();
        } else if (asprintf(&name, "run-"PID_FMT".service", getpid()) < 0)
                return log_oom();

        r = message_start_transient_unit_new(bus, name, &m);
        if (r < 0)
                return bus_log_create_error(r);

        if (arg_remain_after_exit) {
                r = sd_bus_message_append(m, "(sv)", "RemainAfterExit", "b", arg_remain_after_exit);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        if (arg_service_type) {
                r = sd_bus_message_append(m, "(sv)", "Type", "s", arg_service_type);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        if (arg_exec_user) {
                r = sd_bus_message_append(m, "(sv)", "User", "s", arg_exec_user);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        if (arg_exec_group) {
                r = sd_bus_message_append(m, "(sv)", "Group", "s", arg_exec_group);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        if (arg_nice >= PRIO_MIN) {
                r = sd_bus_message_append(m, "(sv)", "Nice", "i", arg_nice);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        if (!strv_isempty(arg_environment)) {
                r = sd_bus_message_open_container(m, 'r', "sv");
                if (r < 0)
                        return bus_log_create_error(r);

                r = sd_bus_message_append(m, "s", "Environment");
                if (r < 0)
                        return bus_log_create_error(r);

                r = sd_bus_message_open_container(m, 'v', "as");
                if (r < 0)
                        return bus_log_create_error(r);

                r = sd_bus_message_append_strv(m, arg_environment);
                if (r < 0)
                        return bus_log_create_error(r);

                r = sd_bus_message_close_container(m);
                if (r < 0)
                        return bus_log_create_error(r);

                r = sd_bus_message_close_container(m);
                if (r < 0)
                        return bus_log_create_error(r);
        }

        r = sd_bus_message_open_container(m, 'r', "sv");
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_append(m, "s", "ExecStart");
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_open_container(m, 'v', "a(sasb)");
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_open_container(m, 'a', "(sasb)");
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_open_container(m, 'r', "sasb");
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_append(m, "s", argv[0]);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_append_strv(m, argv);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_append(m, "b", false);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_close_container(m);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_close_container(m);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_close_container(m);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_close_container(m);
        if (r < 0)
                return bus_log_create_error(r);

        r = message_start_transient_unit_send(bus, m, error, NULL);
        if (r < 0)
                return bus_log_create_error(r);

        log_info("Running as unit %s.", name);

        return 0;
}

static int start_transient_scope(
                sd_bus *bus,
                char **argv,
                sd_bus_error *error) {

        _cleanup_bus_message_unref_ sd_bus_message *m = NULL;
        _cleanup_free_ char *name = NULL;
        _cleanup_strv_free_ char **env = NULL, **user_env = NULL;
        int r;

        assert(bus);

        if (arg_unit) {
                name = unit_name_mangle_with_suffix(arg_unit, MANGLE_NOGLOB, ".scope");
                if (!name)
                        return log_oom();
        } else if (asprintf(&name, "run-"PID_FMT".scope", getpid()) < 0)
                return log_oom();

        r = message_start_transient_unit_new(bus, name, &m);
        if (r < 0)
                return bus_log_create_error(r);

        r = sd_bus_message_append(m, "(sv)", "PIDs", "au", 1, (uint32_t) getpid());
        if (r < 0)
                return bus_log_create_error(r);

        r = message_start_transient_unit_send(bus, m, error, NULL);
        if (r < 0)
                return bus_log_create_error(r);

        if (arg_nice >= PRIO_MIN) {
                if (setpriority(PRIO_PROCESS, 0, arg_nice) < 0) {
                        log_error("Failed to set nice level: %m");
                        return -errno;
                }
        }

        if (arg_exec_group) {
                gid_t gid;

                r = get_group_creds(&arg_exec_group, &gid);
                if (r < 0) {
                        log_error("Failed to resolve group %s: %s", arg_exec_group, strerror(-r));
                        return r;
                }

                if (setresgid(gid, gid, gid) < 0) {
                        log_error("Failed to change GID to " GID_FMT ": %m", gid);
                        return -errno;
                }
        }

        if (arg_exec_user) {
                const char *home, *shell;
                uid_t uid;
                gid_t gid;

                r = get_user_creds(&arg_exec_user, &uid, &gid, &home, &shell);
                if (r < 0) {
                        log_error("Failed to resolve user %s: %s", arg_exec_user, strerror(-r));
                        return r;
                }

                r = strv_extendf(&user_env, "HOME=%s", home);
                if (r < 0)
                        return log_oom();

                r = strv_extendf(&user_env, "SHELL=%s", shell);
                if (r < 0)
                        return log_oom();

                r = strv_extendf(&user_env, "USER=%s", arg_exec_user);
                if (r < 0)
                        return log_oom();

                r = strv_extendf(&user_env, "LOGNAME=%s", arg_exec_user);
                if (r < 0)
                        return log_oom();

                if (!arg_exec_group) {
                        if (setresgid(gid, gid, gid) < 0) {
                                log_error("Failed to change GID to " GID_FMT ": %m", gid);
                                return -errno;
                        }
                }

                if (setresuid(uid, uid, uid) < 0) {
                        log_error("Failed to change UID to " UID_FMT ": %m", uid);
                        return -errno;
                }
        }

        env = strv_env_merge(3, environ, user_env, arg_environment);
        if (!env)
                return log_oom();

        log_info("Running as unit %s.", name);

        execvpe(argv[0], argv, env);
        log_error("Failed to execute: %m");
        return -errno;
}

static int parse_nice(const struct sd_option *option, char *optarg) {
        int r;
        int *data = (int*) option->userdata;
        r = safe_atoi(optarg, data);
        if (r < 0 || arg_nice < PRIO_MIN || arg_nice >= PRIO_MAX) {
                log_error("Failed to parse nice value");
                return -EINVAL;
        }

        return 1;
}

int main(int argc, char* argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                OPTIONS_TRANSPORT(arg_transport),
                { "scope",               0, false, option_set_bool,     &arg_scope,             true },
                { "unit",                0, true,  option_parse_string, &arg_unit                    },
                { "description",         0, true,  option_parse_string, &arg_description             },
                { "slice",               0, true,  option_parse_string, &arg_slice                   },
                { "remain-after-exit", 'r', false, option_set_bool,     &arg_remain_after_exit, true },
                { "send-sighup",         0, false, option_set_bool,     &arg_send_sighup,       true },
                { "service-type",        0, true,  option_parse_string, &arg_service_type            },
                { "uid",                 0, true,  option_parse_string, &arg_exec_user               },
                { "gid",                 0, true,  option_parse_string, &arg_exec_group              },
                { "nice",                0, true,  parse_nice,          &arg_nice                    },
                { "setenv",              0, true,  option_strv_extend,  &arg_environment             },
                { "property",          'p', true,  option_strv_extend,  &arg_property                },
                {}
        };
        char **args = NULL;
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        _cleanup_bus_close_unref_ sd_bus *bus = NULL;
        _cleanup_free_ char *description = NULL, *command = NULL;
        int r;

        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, &args);
        if (r <= 0)
                goto finish;

        if (arg_transport.user && arg_transport.type != BUS_TRANSPORT_LOCAL) {
                log_error("Execution in user context is not supported on non-local systems.");
                r = -EINVAL;
                goto finish;
        }

        if (arg_scope && arg_transport.type != BUS_TRANSPORT_LOCAL) {
                log_error("Scope execution is not supported on non-local systems.");
                r = -EINVAL;
                goto finish;
        }

        if (arg_scope && (arg_remain_after_exit || arg_service_type)) {
                log_error("--remain-after-exit and --service-type= are not supported in --scope mode.");
                r = -EINVAL;
                goto finish;
        }

        r = find_binary(args[0], &command);
        if (r < 0) {
                log_error("Failed to find executable %s: %s", args[0], strerror(-r));
                goto finish;
        }
        args[0] = command;

        if (!arg_description) {
                description = strv_join(args, " ");
                if (!description) {
                        r = log_oom();
                        goto finish;
                }

                arg_description = description;
        }

        r = bus_open_transport_systemd(&arg_transport, &bus);
        if (r < 0) {
                log_error("Failed to create bus connection: %s", strerror(-r));
                goto finish;
        }

        if (arg_scope)
                r = start_transient_scope(bus, args, &error);
        else
                r = start_transient_service(bus, args, &error);

finish:
        strv_free(arg_environment);
        strv_free(arg_property);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
