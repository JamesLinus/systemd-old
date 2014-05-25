/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2012 Lennart Poettering

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

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>
#include <sys/timex.h>
#include <sys/utsname.h>

#include "sd-bus.h"

#include "bus-util.h"
#include "bus-error.h"
#include "util.h"
#include "clock-util.h"
#include "option.h"
#include "strv.h"
#include "sd-id128.h"
#include "virt.h"
#include "architecture.h"
#include "fileio.h"
#include "xyzctl.h"

static bool arg_ask_password = true;
static BusTransport arg_transport = {BUS_TRANSPORT_LOCAL};
static bool arg_transient = false;
static bool arg_pretty = false;
static bool arg_static = false;

typedef struct StatusInfo {
        char *hostname;
        char *static_hostname;
        char *pretty_hostname;
        char *icon_name;
        char *chassis;
        char *deployment;
        char *location;
        char *kernel_name;
        char *kernel_release;
        char *os_pretty_name;
        char *os_cpe_name;
        char *virtualization;
        char *architecture;
} StatusInfo;

static void print_status_info(StatusInfo *i) {
        sd_id128_t mid = {}, bid = {};
        int r;

        assert(i);

        printf("   Static hostname: %s\n", strna(i->static_hostname));

        if (!isempty(i->pretty_hostname) &&
            !streq_ptr(i->pretty_hostname, i->static_hostname))
                printf("   Pretty hostname: %s\n", i->pretty_hostname);

        if (!isempty(i->hostname) &&
            !streq_ptr(i->hostname, i->static_hostname))
                printf("Transient hostname: %s\n", i->hostname);

        if (!isempty(i->icon_name))
                printf("         Icon name: %s\n",
                       strna(i->icon_name));

        if (!isempty(i->chassis))
                printf("           Chassis: %s\n",
                       strna(i->chassis));

        if (!isempty(i->deployment))
                printf("        Deployment: %s\n", i->deployment);

        if (!isempty(i->location))
                printf("          Location: %s\n", i->location);

        r = sd_id128_get_machine(&mid);
        if (r >= 0)
                printf("        Machine ID: " SD_ID128_FORMAT_STR "\n", SD_ID128_FORMAT_VAL(mid));

        r = sd_id128_get_boot(&bid);
        if (r >= 0)
                printf("           Boot ID: " SD_ID128_FORMAT_STR "\n", SD_ID128_FORMAT_VAL(bid));

        if (!isempty(i->virtualization))
                printf("    Virtualization: %s\n", i->virtualization);

        if (!isempty(i->os_pretty_name))
                printf("  Operating System: %s\n", i->os_pretty_name);

        if (!isempty(i->os_cpe_name))
                printf("       CPE OS Name: %s\n", i->os_cpe_name);

        if (!isempty(i->kernel_name) && !isempty(i->kernel_release))
                printf("            Kernel: %s %s\n", i->kernel_name, i->kernel_release);

        if (!isempty(i->architecture))
                printf("      Architecture: %s\n", i->architecture);

}

static int show_one_name(sd_bus *bus, const char* attr) {
        _cleanup_bus_message_unref_ sd_bus_message *reply = NULL;
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        const char *s;
        int r;

        r = sd_bus_get_property(
                        bus,
                        "org.freedesktop.hostname1",
                        "/org/freedesktop/hostname1",
                        "org.freedesktop.hostname1",
                        attr,
                        &error, &reply, "s");
        if (r < 0) {
                log_error("Could not get property: %s", bus_error_message(&error, -r));
                return r;
        }

        r = sd_bus_message_read(reply, "s", &s);
        if (r < 0)
                return bus_log_parse_error(r);

        printf("%s\n", s);

        return 0;
}

static int show_all_names(sd_bus *bus) {
        StatusInfo info = {};

        static const struct bus_properties_map hostname_map[]  = {
                { "Hostname",                  "s", NULL, offsetof(StatusInfo, hostname)        },
                { "StaticHostname",            "s", NULL, offsetof(StatusInfo, static_hostname) },
                { "PrettyHostname",            "s", NULL, offsetof(StatusInfo, pretty_hostname) },
                { "IconName",                  "s", NULL, offsetof(StatusInfo, icon_name)       },
                { "Chassis",                   "s", NULL, offsetof(StatusInfo, chassis)         },
                { "Deployment",                "s", NULL, offsetof(StatusInfo, deployment)      },
                { "Location",                  "s", NULL, offsetof(StatusInfo, location)        },
                { "KernelName",                "s", NULL, offsetof(StatusInfo, kernel_name)     },
                { "KernelRelease",             "s", NULL, offsetof(StatusInfo, kernel_release)  },
                { "OperatingSystemPrettyName", "s", NULL, offsetof(StatusInfo, os_pretty_name)  },
                { "OperatingSystemCPEName",    "s", NULL, offsetof(StatusInfo, os_cpe_name)     },
                {}
        };

        static const struct bus_properties_map manager_map[] = {
                { "Virtualization",            "s", NULL, offsetof(StatusInfo, virtualization)  },
                { "Architecture",              "s", NULL, offsetof(StatusInfo, architecture)    },
                {}
        };

        int r;

        r = bus_map_all_properties(bus,
                                   "org.freedesktop.hostname1",
                                   "/org/freedesktop/hostname1",
                                   hostname_map,
                                   &info);
        if (r < 0)
                goto fail;

        bus_map_all_properties(bus,
                               "org.freedesktop.systemd1",
                               "/org/freedesktop/systemd1",
                               manager_map,
                               &info);

        print_status_info(&info);

fail:
        free(info.hostname);
        free(info.static_hostname);
        free(info.pretty_hostname);
        free(info.icon_name);
        free(info.chassis);
        free(info.deployment);
        free(info.location);
        free(info.kernel_name);
        free(info.kernel_release);
        free(info.os_pretty_name);
        free(info.os_cpe_name);
        free(info.virtualization);
        free(info.architecture);

        return r;
}

static int show_status(sd_bus *bus, char **args, unsigned n) {
        assert(args);

        if (arg_pretty || arg_static || arg_transient) {
                const char *attr;

                if (!!arg_static + !!arg_pretty + !!arg_transient > 1) {
                        log_error("Cannot query more than one name type at a time");
                        return -EINVAL;
                }

                attr = arg_pretty ? "PrettyHostname" :
                        arg_static ? "StaticHostname" : "Hostname";

                return show_one_name(bus, attr);
        } else
                return show_all_names(bus);
}

static int set_simple_string(sd_bus *bus, const char *method, const char *value) {
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        int r = 0;

        r = sd_bus_call_method(
                        bus,
                        "org.freedesktop.hostname1",
                        "/org/freedesktop/hostname1",
                        "org.freedesktop.hostname1",
                        method,
                        &error, NULL,
                        "sb", value, arg_ask_password);
        if (r < 0)
                log_error("Could not set property: %s", bus_error_message(&error, -r));
        return r;
}

static int set_hostname(sd_bus *bus, char **args, unsigned n) {
        _cleanup_free_ char *h = NULL;
        const char *hostname = args[1];
        int r;

        assert(args);
        assert(n == 2);

        if (!arg_pretty && !arg_static && !arg_transient)
                arg_pretty = arg_static = arg_transient = true;

        if (arg_pretty) {
                const char *p;

                /* If the passed hostname is already valid, then
                 * assume the user doesn't know anything about pretty
                 * hostnames, so let's unset the pretty hostname, and
                 * just set the passed hostname as static/dynamic
                 * hostname. */

                h = strdup(hostname);
                if (!h)
                        return log_oom();

                hostname_cleanup(h, true);

                if (arg_static && streq(h, hostname))
                        p = "";
                else {
                        p = hostname;
                        hostname = h;
                }

                r = set_simple_string(bus, "SetPrettyHostname", p);
                if (r < 0)
                        return r;
        }

        if (arg_static) {
                r = set_simple_string(bus, "SetStaticHostname", hostname);
                if (r < 0)
                        return r;
        }

        if (arg_transient) {
                r = set_simple_string(bus, "SetHostname", hostname);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int set_icon_name(sd_bus *bus, char **args, unsigned n) {
        assert(args);
        assert(n == 2);

        return set_simple_string(bus, "SetIconName", args[1]);
}

static int set_chassis(sd_bus *bus, char **args, unsigned n) {
        assert(args);
        assert(n == 2);

        return set_simple_string(bus, "SetChassis", args[1]);
}

static int set_deployment(sd_bus *bus, char **args, unsigned n) {
        assert(args);
        assert(n == 2);

        return set_simple_string(bus, "SetDeployment", args[1]);
}

static int set_location(sd_bus *bus, char **args, unsigned n) {
        assert(args);
        assert(n == 2);

        return set_simple_string(bus, "SetLocation", args[1]);
}

static void help(void) {
        printf("%s [OPTIONS...] COMMAND ...\n\n"
               "Query or change system hostname.\n\n"
               "  -h --help              Show this help\n"
               "     --version           Show package version\n"
               "     --no-ask-password   Do not prompt for password\n"
               "  -H --host=[USER@]HOST  Operate on remote host\n"
               "  -M --machine=CONTAINER Operate on local container\n"
               "     --transient         Only set transient hostname\n"
               "     --static            Only set static hostname\n"
               "     --pretty            Only set pretty hostname\n\n"
               "Commands:\n"
               "  status                 Show current hostname settings\n"
               "  set-hostname NAME      Set system hostname\n"
               "  set-icon-name NAME     Set icon name for host\n"
               "  set-chassis NAME       Set chassis type for host\n"
               "  set-deployment NAME    Set deployment environment for host\n"
               "  set-location NAME      Set location for host\n"
               , program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                OPTIONS_TRANSPORT_NO_USER(arg_transport),
                { "transient",       0 , false, option_set_bool, &arg_transient,    true  },
                { "static",          0 , false, option_set_bool, &arg_static,       true  },
                { "pretty",          0 , false, option_set_bool, &arg_pretty,       true  },
                { "no-ask-password", 0 , false, option_set_bool, &arg_ask_password, false },
                {}
        };
        static const xyzctl_verb verbs[] = {
                { "status",         LESS,  1, show_status,    XYZCTL_BUS                 },
                { "set-hostname",   EQUAL, 2, set_hostname,   XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-icon-name",  EQUAL, 2, set_icon_name,  XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-chassis",    EQUAL, 2, set_chassis,    XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-deployment", EQUAL, 2, set_deployment, XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-location",   EQUAL, 2, set_location,   XYZCTL_BUS | XYZCTL_POLKIT },
                {}
        };
        _cleanup_bus_close_unref_ sd_bus *bus = NULL;
        char **args = NULL;
        int r;

        setlocale(LC_ALL, "");
        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, &args);
        if (r <= 0)
                goto finish;

        if (arg_transport.type != BUS_TRANSPORT_LOCAL)
                arg_ask_password = false;

        r = bus_open_transport(&arg_transport, &bus);
        r = xyzctl_main(verbs, bus, r, args, &help, arg_ask_password, false);

finish:
        return r < 0 ? EXIT_FAILURE : r;
}
