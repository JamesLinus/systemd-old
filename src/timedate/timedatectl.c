/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2012 Lennart Poettering
  Copyright 2013 Kay Sievers

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
#include <getopt.h>
#include <locale.h>
#include <string.h>
#include <sys/timex.h>

#include "xyzctl.h"
#include "sd-bus.h"
#include "bus-util.h"
#include "bus-error.h"
#include "util.h"
#include "option.h"
#include "strv.h"
#include "time-dst.h"

static bool arg_pager = true;
static bool arg_ask_password = true;
static BusTransport arg_transport = {BUS_TRANSPORT_LOCAL};
static bool arg_adjust_system_clock = false;

typedef struct StatusInfo {
        usec_t time;
        char *timezone;

        usec_t rtc_time;
        bool rtc_local;

        bool ntp_enabled;
        bool ntp_capable;
        bool ntp_synced;
} StatusInfo;

static const char *jump_str(int delta_minutes, char *s, size_t size) {
        if (delta_minutes == 60)
                return "one hour forward";
        if (delta_minutes == -60)
                return "one hour backwards";
        if (delta_minutes < 0) {
                snprintf(s, size, "%i minutes backwards", -delta_minutes);
                return s;
        }
        if (delta_minutes > 0) {
                snprintf(s, size, "%i minutes forward", delta_minutes);
                return s;
        }
        return "";
}

static void print_status_info(const StatusInfo *i) {
        char a[FORMAT_TIMESTAMP_MAX];
        char b[FORMAT_TIMESTAMP_MAX];
        char s[32];
        struct tm tm;
        time_t sec;
        bool have_time = false;
        _cleanup_free_ char *zc = NULL, *zn = NULL;
        time_t t, tc, tn;
        int dn = 0;
        bool is_dstc = false, is_dstn = false;
        int r;

        assert(i);

        /* Enforce the values of /etc/localtime */
        if (getenv("TZ")) {
                fprintf(stderr, "Warning: Ignoring the TZ variable. Reading the system's time zone setting only.\n\n");
                unsetenv("TZ");
        }

        if (i->time != 0) {
                sec = (time_t) (i->time / USEC_PER_SEC);
                have_time = true;
        } else if (arg_transport.type == BUS_TRANSPORT_LOCAL) {
                sec = time(NULL);
                have_time = true;
        } else
                fprintf(stderr, "Warning: Could not get time from timedated and not operating locally.\n\n");

        if (have_time) {
                zero(tm);
                assert_se(strftime(a, sizeof(a), "%a %Y-%m-%d %H:%M:%S %Z", localtime_r(&sec, &tm)) > 0);
                char_array_0(a);
                printf("      Local time: %s\n", a);

                zero(tm);
                assert_se(strftime(a, sizeof(a), "%a %Y-%m-%d %H:%M:%S UTC", gmtime_r(&sec, &tm)) > 0);
                char_array_0(a);
                printf("  Universal time: %s\n", a);
        } else {
                printf("      Local time: %s\n", "n/a");
                printf("  Universal time: %s\n", "n/a");
        }

        if (i->rtc_time > 0) {
                time_t rtc_sec;

                rtc_sec = (time_t)(i->rtc_time / USEC_PER_SEC);
                zero(tm);
                assert_se(strftime(a, sizeof(a), "%a %Y-%m-%d %H:%M:%S", gmtime_r(&rtc_sec, &tm)) > 0);
                char_array_0(a);
                printf("        RTC time: %s\n", a);
        } else
                printf("        RTC time: %s\n", "n/a");

        if (have_time) {
                zero(tm);
                assert_se(strftime(a, sizeof(a), "%Z, %z", localtime_r(&sec, &tm)) > 0);
                char_array_0(a);
        }

        printf("       Time zone: %s (%s)\n"
               "     NTP enabled: %s\n"
               "NTP synchronized: %s\n"
               " RTC in local TZ: %s\n",
               strna(i->timezone), have_time ? a : "n/a",
               i->ntp_capable ? yes_no(i->ntp_enabled) : "n/a",
               yes_no(i->ntp_synced),
               yes_no(i->rtc_local));

        if (have_time) {
                r = time_get_dst(sec, "/etc/localtime",
                                 &tc, &zc, &is_dstc,
                                 &tn, &dn, &zn, &is_dstn);
                if (r < 0)
                        printf("      DST active: %s\n", "n/a");
                else {
                        printf("      DST active: %s\n", yes_no(is_dstc));

                        t = tc - 1;
                        zero(tm);
                        assert_se(strftime(a, sizeof(a), "%a %Y-%m-%d %H:%M:%S %Z", localtime_r(&t, &tm)) > 0);
                        char_array_0(a);

                        zero(tm);
                        assert_se(strftime(b, sizeof(b), "%a %Y-%m-%d %H:%M:%S %Z", localtime_r(&tc, &tm)) > 0);
                        char_array_0(b);
                        printf(" Last DST change: DST %s at\n"
                               "                  %s\n"
                               "                  %s\n",
                               is_dstc ? "began" : "ended", a, b);

                        t = tn - 1;
                        zero(tm);
                        assert_se(strftime(a, sizeof(a), "%a %Y-%m-%d %H:%M:%S %Z", localtime_r(&t, &tm)) > 0);
                        char_array_0(a);

                        zero(tm);
                        assert_se(strftime(b, sizeof(b), "%a %Y-%m-%d %H:%M:%S %Z", localtime_r(&tn, &tm)) > 0);
                        char_array_0(b);
                        printf(" Next DST change: DST %s (the clock jumps %s) at\n"
                               "                  %s\n"
                               "                  %s\n",
                               is_dstn ? "begins" : "ends", jump_str(dn, s, sizeof(s)), a, b);
                }
        } else
                printf("      DST active: %s\n", yes_no(is_dstc));

        if (i->rtc_local)
                fputs("\n" ANSI_HIGHLIGHT_ON
                      "Warning: The system is configured to read the RTC time in the local time zone. This\n"
                      "         mode can not be fully supported. It will create various problems with time\n"
                      "         zone changes and daylight saving time adjustments. The RTC time is never updated,\n"
                      "         it relies on external facilities to maintain it. If at all possible, use\n"
                      "         RTC in UTC by calling 'timedatectl set-local-rtc 0'" ANSI_HIGHLIGHT_OFF ".\n", stdout);
}

static int show_status(sd_bus *bus, char **args, unsigned n) {
        StatusInfo info = {};
        static const struct bus_properties_map map[]  = {
                { "Timezone",        "s", NULL, offsetof(StatusInfo, timezone) },
                { "LocalRTC",        "b", NULL, offsetof(StatusInfo, rtc_local) },
                { "NTP",             "b", NULL, offsetof(StatusInfo, ntp_enabled) },
                { "CanNTP",          "b", NULL, offsetof(StatusInfo, ntp_capable) },
                { "NTPSynchronized", "b", NULL, offsetof(StatusInfo, ntp_synced) },
                { "TimeUSec",        "t", NULL, offsetof(StatusInfo, time) },
                { "RTCTimeUSec",     "t", NULL, offsetof(StatusInfo, rtc_time) },
                {}
        };
        int r;

        assert(bus);

        r = bus_map_all_properties(bus,
                                   "org.freedesktop.timedate1",
                                   "/org/freedesktop/timedate1",
                                   map,
                                   &info);
        if (r < 0) {
                log_error("Failed to query server: %s", strerror(-r));
                goto fail;
        }

        print_status_info(&info);

fail:
        free(info.timezone);
        return r;
}

static int set_time(sd_bus *bus, char **args, unsigned n) {
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        bool relative = false, interactive = arg_ask_password;
        usec_t t;
        int r;

        assert(args);
        assert(n == 2);

        r = parse_timestamp(args[1], &t);
        if (r < 0) {
                log_error("Failed to parse time specification: %s", args[1]);
                return r;
        }

        r = sd_bus_call_method(bus,
                               "org.freedesktop.timedate1",
                               "/org/freedesktop/timedate1",
                               "org.freedesktop.timedate1",
                               "SetTime",
                               &error,
                               NULL,
                               "xbb", (int64_t)t, relative, interactive);
        if (r < 0)
                log_error("Failed to set time: %s", bus_error_message(&error, -r));

        return r;
}

static int set_timezone(sd_bus *bus, char **args, unsigned n) {
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        int r;

        assert(args);
        assert(n == 2);

        r = sd_bus_call_method(bus,
                               "org.freedesktop.timedate1",
                               "/org/freedesktop/timedate1",
                               "org.freedesktop.timedate1",
                               "SetTimezone",
                               &error,
                               NULL,
                               "sb", args[1], arg_ask_password);
        if (r < 0)
                log_error("Failed to set time zone: %s", bus_error_message(&error, -r));

        return r;
}

static int set_local_rtc(sd_bus *bus, char **args, unsigned n) {
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        int r, b;

        assert(args);
        assert(n == 2);

        b = parse_boolean(args[1]);
        if (b < 0) {
                log_error("Failed to parse local RTC setting: %s", args[1]);
                return b;
        }

        r = sd_bus_call_method(bus,
                               "org.freedesktop.timedate1",
                               "/org/freedesktop/timedate1",
                               "org.freedesktop.timedate1",
                               "SetLocalRTC",
                               &error,
                               NULL,
                               "bbb", b, arg_adjust_system_clock, arg_ask_password);
        if (r < 0)
                log_error("Failed to set local RTC: %s", bus_error_message(&error, -r));

        return r;
}

static int set_ntp(sd_bus *bus, char **args, unsigned n) {
        _cleanup_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
        int b, r;

        assert(args);
        assert(n == 2);

        b = parse_boolean(args[1]);
        if (b < 0) {
                log_error("Failed to parse NTP setting: %s", args[1]);
                return b;
        }

        r = sd_bus_call_method(bus,
                               "org.freedesktop.timedate1",
                               "/org/freedesktop/timedate1",
                               "org.freedesktop.timedate1",
                               "SetNTP",
                               &error,
                               NULL,
                               "bb", b, arg_ask_password);
        if (r < 0)
                log_error("Failed to set ntp: %s", bus_error_message(&error, -r));

        return r;
}

static int list_timezones(sd_bus *bus, char **args, unsigned n) {
        _cleanup_strv_free_ char **zones = NULL;
        int r;

        assert(args);
        assert(n == 1);

        r = get_timezones(&zones);
        if (r < 0) {
                log_error("Failed to read list of time zones: %s", strerror(-r));
                return r;
        }

        strv_print(zones);

        return 0;
}

static void help(void) {
        printf("%s [OPTIONS...] COMMAND ...\n\n"
               "Query or change system time and date settings.\n\n"
               "  -h --help                Show this help message\n"
               "     --version             Show package version\n"
               "     --no-pager            Do not pipe output into a pager\n"
               "     --no-ask-password     Do not prompt for password\n"
               "  -H --host=[USER@]HOST    Operate on remote host\n"
               "  -M --machine=CONTAINER   Operate on local container\n"
               "     --adjust-system-clock Adjust system clock when changing local RTC mode\n\n"
               "Commands:\n"
               "  status                   Show current time settings\n"
               "  set-time TIME            Set system time\n"
               "  set-timezone ZONE        Set system time zone\n"
               "  list-timezones           Show known time zones\n"
               "  set-local-rtc BOOL       Control whether RTC is in local time\n"
               "  set-ntp BOOL             Control whether NTP is enabled\n",
               program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                OPTIONS_TRANSPORT_NO_USER(arg_transport),
                { "no-pager",            0, false, option_set_bool, &arg_pager,               false },
                { "no-ask-password",     0, false, option_set_bool, &arg_ask_password,        false },
                { "adjust-system-clock", 0, false, option_set_bool, &arg_adjust_system_clock, true  },
                {}
        };
        static const xyzctl_verb verbs[] = {
                { "status",         LESS,  1, show_status,    XYZCTL_BUS                 },
                { "set-time",       EQUAL, 2, set_time,       XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-timezone",   EQUAL, 2, set_timezone,   XYZCTL_BUS | XYZCTL_POLKIT },
                { "list-timezones", EQUAL, 1, list_timezones, XYZCTL_BUS | XYZCTL_PAGER  },
                { "set-local-rtc",  EQUAL, 2, set_local_rtc,  XYZCTL_BUS | XYZCTL_POLKIT },
                { "set-ntp",        EQUAL, 2, set_ntp,        XYZCTL_BUS | XYZCTL_POLKIT },
                {}
        };
        _cleanup_bus_close_unref_ sd_bus *bus = NULL;
        char **args;
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
        r = xyzctl_main(verbs, bus, r, args, &help, arg_ask_password, arg_pager);

finish:
        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
