/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2010-2013 Lennart Poettering
  Copyright 2013 Simon Peeters

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
#include <stdlib.h>
#include <getopt.h>
#include <locale.h>
#include <sys/utsname.h>

#include "install.h"
#include "log.h"
#include "sd-bus-common.h"
#include "build.h"
#include "util.h"
#include "strxcpyx.h"
#include "fileio.h"

#define SCALE_X (0.1 / 1000.0)   /* pixels per us */
#define SCALE_Y 20.0

#define compare(a, b) (((a) > (b))? 1 : (((b) > (a))? -1 : 0))

#define svg(...) printf(__VA_ARGS__)

#define svg_bar(class, x1, x2, y)                                       \
        svg("  <rect class=\"%s\" x=\"%.03f\" y=\"%.03f\" width=\"%.03f\" height=\"%.03f\" />\n", \
            (class),                                                    \
            SCALE_X * (x1), SCALE_Y * (y),                              \
            SCALE_X * ((x2) - (x1)), SCALE_Y - 1.0)

#define svg_text(b, x, y, format, ...)                                  \
        do {                                                            \
                svg("  <text class=\"%s\" x=\"%.03f\" y=\"%.03f\">", (b) ? "left" : "right", SCALE_X * (x) + (b ? 5.0 : -5.0), SCALE_Y * (y) + 14.0); \
                svg(format, ## __VA_ARGS__);                            \
                svg("</text>\n");                                       \
        } while(false)

static UnitFileScope arg_scope = UNIT_FILE_SYSTEM;
static enum dot {
        DEP_ALL,
        DEP_ORDER,
        DEP_REQUIRE
} arg_dot = DEP_ALL;

struct boot_times {
        usec_t firmware_time;
        usec_t loader_time;
        usec_t kernel_time;
        usec_t kernel_done_time;
        usec_t initrd_time;
        usec_t userspace_time;
        usec_t finish_time;
};
struct unit_times {
        char *name;
        usec_t ixt;
        usec_t iet;
        usec_t axt;
        usec_t aet;
        usec_t time;
};

static int compare_unit_time(const void *a, const void *b) {
        return compare(((struct unit_times *)b)->time,
                       ((struct unit_times *)a)->time);
}

static int compare_unit_start(const void *a, const void *b) {
        return compare(((struct unit_times *)a)->ixt,
                       ((struct unit_times *)b)->ixt);
}

static int get_os_name(char **_n) {
        char *n = NULL;
        int r;

        r = parse_env_file("/etc/os-release", NEWLINE, "PRETTY_NAME", &n, NULL);
        if (r < 0)
                return r;

        if (!n)
                return -ENOENT;

        *_n = n;
        return 0;
}

static void free_unit_times(struct unit_times *t, unsigned n) {
        struct unit_times *p;

        for (p = t; p < t + n; p++)
                free(p->name);

        free(t);
}

static int acquire_time_data(sd_bus *bus, struct unit_times **out) {
        _cleanup_sd_bus_message_unref_ sd_bus_message *reply = NULL;
        int r, c = 0, n_units = 0;
        struct unit_times *unit_times = NULL;

        r = sd_bus_call_method(
                        bus,
                        "org.freedesktop.systemd1",
                        "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager",
                        "ListUnits",
                        NULL,
                        &reply,
                        NULL);
        if (r < 0)
                goto fail;

        r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
        if (r < 0) {
                log_error("Failed to parse reply.");
                goto fail;
        }

        while (sd_bus_message_exit_container(reply) == -EBUSY) {
                struct unit_info u;
                struct unit_times *t;

                if (c >= n_units) {
                        struct unit_times *w;

                        n_units = MAX(2*c, 16);
                        w = realloc(unit_times, sizeof(struct unit_times) * n_units);

                        if (!w) {
                                r = log_oom();
                                goto fail;
                        }

                        unit_times = w;
                }
                t = unit_times+c;
                t->name = NULL;

                r = bus_parse_unit_info(reply, &u);
                if (r < 0)
                        goto fail;

                assert_cc(sizeof(usec_t) == sizeof(uint64_t));

                r = bus_get_property(bus,
                                     "org.freedesktop.systemd1",
                                     u.unit_path,
                                     "org.freedesktop.systemd1.Unit",
                                     "InactiveExitTimestampMonotonic",
                                     SD_BUS_TYPE_UINT64,
                                     &t->ixt);
                if (r < 0)
                        goto fail;

                r = bus_get_property(bus,
                                     "org.freedesktop.systemd1",
                                     u.unit_path,
                                     "org.freedesktop.systemd1.Unit",
                                     "ActiveEnterTimestampMonotonic",
                                     SD_BUS_TYPE_UINT64,
                                     &t->aet);
                if (r < 0)
                        goto fail;

                r = bus_get_property(bus,
                                     "org.freedesktop.systemd1",
                                     u.unit_path,
                                     "org.freedesktop.systemd1.Unit",
                                     "ActiveExitTimestampMonotonic",
                                     SD_BUS_TYPE_UINT64,
                                     &t->axt);
                if (r < 0)
                        goto fail;

                r = bus_get_property(bus,
                                     "org.freedesktop.systemd1",
                                     u.unit_path,
                                     "org.freedesktop.systemd1.Unit",
                                     "InactiveEnterTimestampMonotonic",
                                     SD_BUS_TYPE_UINT64,
                                     &t->iet);
                if (r < 0)
                        goto fail;

                if (t->aet >= t->ixt)
                        t->time = t->aet - t->ixt;
                else if (t->iet >= t->ixt)
                        t->time = t->iet - t->ixt;
                else
                        t->time = 0;

                if (t->ixt == 0)
                        continue;

                t->name = strdup(u.id);
                if (t->name == NULL) {
                        r = log_oom();
                        goto fail;
                }
                c++;
        }

        *out = unit_times;
        return c;

fail:
        free_unit_times(unit_times, (unsigned) c);
        return r;
}

static int acquire_boot_times(sd_bus *bus, struct boot_times **bt) {
        static struct boot_times times;
        static bool cached = false;

        if (cached)
                goto finish;

        assert_cc(sizeof(usec_t) == sizeof(uint64_t));

        if (bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "FirmwareTimestampMonotonic",
                             SD_BUS_TYPE_UINT64,
                             &times.firmware_time) < 0 ||
            bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "LoaderTimestampMonotonic",
                             SD_BUS_TYPE_UINT64,
                             &times.loader_time) < 0 ||
            bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "KernelTimestamp",
                             SD_BUS_TYPE_UINT64,
                             &times.kernel_time) < 0 ||
            bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "InitRDTimestampMonotonic",
                             SD_BUS_TYPE_UINT64,
                             &times.initrd_time) < 0 ||
            bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "UserspaceTimestampMonotonic",
                             SD_BUS_TYPE_UINT64,
                             &times.userspace_time) < 0 ||
            bus_get_property(bus,
                             "org.freedesktop.systemd1",
                             "/org/freedesktop/systemd1",
                             "org.freedesktop.systemd1.Manager",
                             "FinishTimestampMonotonic",
                             SD_BUS_TYPE_UINT64,
                             &times.finish_time) < 0)
                return -EIO;

        if (times.finish_time <= 0) {
                log_error("Bootup is not yet finished. Please try again later.");
                return -EAGAIN;
        }

        if (times.initrd_time)
                times.kernel_done_time = times.initrd_time;
        else
                times.kernel_done_time = times.userspace_time;

        cached = true;

finish:
        *bt = &times;
        return 0;
}

static int pretty_boot_time(sd_bus *bus, char **_buf) {
        char ts[FORMAT_TIMESPAN_MAX];
        struct boot_times *t;
        static char buf[4096];
        size_t size;
        char *ptr;
        int r;

        r = acquire_boot_times(bus, &t);
        if (r < 0)
                return r;

        ptr = buf;
        size = sizeof(buf);

        size = strpcpyf(&ptr, size, "Startup finished in ");
        if (t->firmware_time)
                size = strpcpyf(&ptr, size, "%s (firmware) + ", format_timespan(ts, sizeof(ts), t->firmware_time - t->loader_time, USEC_PER_MSEC));
        if (t->loader_time)
                size = strpcpyf(&ptr, size, "%s (loader) + ", format_timespan(ts, sizeof(ts), t->loader_time, USEC_PER_MSEC));
        if (t->kernel_time)
                size = strpcpyf(&ptr, size, "%s (kernel) + ", format_timespan(ts, sizeof(ts), t->kernel_done_time, USEC_PER_MSEC));
        if (t->initrd_time > 0)
                size = strpcpyf(&ptr, size, "%s (initrd) + ", format_timespan(ts, sizeof(ts), t->userspace_time - t->initrd_time, USEC_PER_MSEC));

        size = strpcpyf(&ptr, size, "%s (userspace) ", format_timespan(ts, sizeof(ts), t->finish_time - t->userspace_time, USEC_PER_MSEC));
        if (t->kernel_time > 0)
                size = strpcpyf(&ptr, size, "= %s", format_timespan(ts, sizeof(ts), t->firmware_time + t->finish_time, USEC_PER_MSEC));
        else
                size = strpcpyf(&ptr, size, "= %s", format_timespan(ts, sizeof(ts), t->finish_time - t->userspace_time, USEC_PER_MSEC));

        ptr = strdup(buf);
        if (!ptr)
                return log_oom();

        *_buf = ptr;
        return 0;
}

static void svg_graph_box(double height, double begin, double end) {
        long long i;

        /* outside box, fill */
        svg("<rect class=\"box\" x=\"0\" y=\"0\" width=\"%.03f\" height=\"%.03f\" />\n",
            SCALE_X * (end - begin), SCALE_Y * height);

        for (i = ((long long) (begin / 100000)) * 100000; i <= end; i+=100000) {
                /* lines for each second */
                if (i % 5000000 == 0)
                        svg("  <line class=\"sec5\" x1=\"%.03f\" y1=\"0\" x2=\"%.03f\" y2=\"%.03f\" />\n"
                            "  <text class=\"sec\" x=\"%.03f\" y=\"%.03f\" >%.01fs</text>\n",
                            SCALE_X * i, SCALE_X * i, SCALE_Y * height, SCALE_X * i, -5.0, 0.000001 * i);
                else if (i % 1000000 == 0)
                        svg("  <line class=\"sec1\" x1=\"%.03f\" y1=\"0\" x2=\"%.03f\" y2=\"%.03f\" />\n"
                            "  <text class=\"sec\" x=\"%.03f\" y=\"%.03f\" >%.01fs</text>\n",
                            SCALE_X * i, SCALE_X * i, SCALE_Y * height, SCALE_X * i, -5.0, 0.000001 * i);
                else
                        svg("  <line class=\"sec01\" x1=\"%.03f\" y1=\"0\" x2=\"%.03f\" y2=\"%.03f\" />\n",
                            SCALE_X * i, SCALE_X * i, SCALE_Y * height);
        }
}

static int analyze_plot(sd_bus *bus) {
        struct unit_times *times;
        struct boot_times *boot;
        struct utsname name;
        int n, m = 1, y=0;
        double width;
        _cleanup_free_ char *pretty_times = NULL, *osname = NULL;
        struct unit_times *u;

        n = acquire_boot_times(bus, &boot);
        if (n < 0)
                return n;

        n = pretty_boot_time(bus, &pretty_times);
        if (n < 0)
                return n;

        get_os_name(&osname);
        assert_se(uname(&name) >= 0);

        n = acquire_time_data(bus, &times);
        if (n <= 0)
                return n;

        qsort(times, n, sizeof(struct unit_times), compare_unit_start);

        width = SCALE_X * (boot->firmware_time + boot->finish_time);
        if (width < 800.0)
                width = 800.0;

        if (boot->firmware_time > boot->loader_time)
                m++;
        if (boot->loader_time) {
                m++;
                if (width < 1000.0)
                        width = 1000.0;
        }
        if (boot->initrd_time)
                m++;
        if (boot->kernel_time)
                m++;

        for (u = times; u < times + n; u++) {
                double len;

                if (u->ixt < boot->userspace_time ||
                    u->ixt > boot->finish_time) {
                        free(u->name);
                        u->name = NULL;
                        continue;
                }
                len = ((boot->firmware_time + u->ixt) * SCALE_X)
                        + (10.0 * strlen(u->name));
                if (len > width)
                        width = len;

                if (u->iet > u->ixt && u->iet <= boot->finish_time
                                && u->aet == 0 && u->axt == 0)
                        u->aet = u->axt = u->iet;
                if (u->aet < u->ixt || u->aet > boot->finish_time)
                        u->aet = boot->finish_time;
                if (u->axt < u->aet || u->aet > boot->finish_time)
                        u->axt = boot->finish_time;
                if (u->iet < u->axt || u->iet > boot->finish_time)
                        u->iet = boot->finish_time;
                m++;
        }

        svg("<?xml version=\"1.0\" standalone=\"no\"?>\n"
            "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
            "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n");

        svg("<svg width=\"%.0fpx\" height=\"%.0fpx\" version=\"1.1\" "
            "xmlns=\"http://www.w3.org/2000/svg\">\n\n",
                        80.0 + width, 150.0 + (m * SCALE_Y));

        /* write some basic info as a comment, including some help */
        svg("<!-- This file is a systemd-analyze SVG file. It is best rendered in a   -->\n"
            "<!-- browser such as Chrome, Chromium or Firefox. Other applications     -->\n"
            "<!-- that render these files properly but much slower are ImageMagick,   -->\n"
            "<!-- gimp, inkscape, etc. To display the files on your system, just      -->\n"
            "<!-- point your browser to this file.                                    -->\n\n"
            "<!-- This plot was generated by systemd-analyze version %-16.16s -->\n\n", VERSION);

        /* style sheet */
        svg("<defs>\n  <style type=\"text/css\">\n    <![CDATA[\n"
            "      rect       { stroke-width: 1; stroke-opacity: 0; }\n"
            "      rect.activating   { fill: rgb(255,0,0); fill-opacity: 0.7; }\n"
            "      rect.active       { fill: rgb(200,150,150); fill-opacity: 0.7; }\n"
            "      rect.deactivating { fill: rgb(150,100,100); fill-opacity: 0.7; }\n"
            "      rect.kernel       { fill: rgb(150,150,150); fill-opacity: 0.7; }\n"
            "      rect.initrd       { fill: rgb(150,150,150); fill-opacity: 0.7; }\n"
            "      rect.firmware     { fill: rgb(150,150,150); fill-opacity: 0.7; }\n"
            "      rect.loader       { fill: rgb(150,150,150); fill-opacity: 0.7; }\n"
            "      rect.userspace    { fill: rgb(150,150,150); fill-opacity: 0.7; }\n"
            "      rect.box   { fill: rgb(240,240,240); stroke: rgb(192,192,192); }\n"
            "      line       { stroke: rgb(64,64,64); stroke-width: 1; }\n"
            "//    line.sec1  { }\n"
            "      line.sec5  { stroke-width: 2; }\n"
            "      line.sec01 { stroke: rgb(224,224,224); stroke-width: 1; }\n"
            "      text       { font-family: Verdana, Helvetica; font-size: 10; }\n"
            "      text.left  { font-family: Verdana, Helvetica; font-size: 10; text-anchor: start; }\n"
            "      text.right { font-family: Verdana, Helvetica; font-size: 10; text-anchor: end; }\n"
            "      text.sec   { font-size: 8; }\n"
            "    ]]>\n   </style>\n</defs>\n\n");

        svg("<text x=\"20\" y=\"50\">%s</text>", pretty_times);
        svg("<text x=\"20\" y=\"30\">%s %s (%s %s) %s</text>",
            isempty(osname) ? "Linux" : osname,
            name.nodename, name.release, name.version, name.machine);
        svg("<text x=\"20\" y=\"%.0f\">Legend: Red = Activating; Pink = Active; Dark Pink = Deactivating</text>",
                        120.0 + (m *SCALE_Y));

        svg("<g transform=\"translate(%.3f,100)\">\n", 20.0 + (SCALE_X * boot->firmware_time));
        svg_graph_box(m, -boot->firmware_time, boot->finish_time);

        if (boot->firmware_time) {
                svg_bar("firmware", -(double) boot->firmware_time, -(double) boot->loader_time, y);
                svg_text(true, -(double) boot->firmware_time, y, "firmware");
                y++;
        }
        if (boot->loader_time) {
                svg_bar("loader", -(double) boot->loader_time, 0, y);
                svg_text(true, -(double) boot->loader_time, y, "loader");
                y++;
        }
        if (boot->kernel_time) {
                svg_bar("kernel", 0, boot->kernel_done_time, y);
                svg_text(true, 0, y, "kernel");
                y++;
        }
        if (boot->initrd_time) {
                svg_bar("initrd", boot->initrd_time, boot->userspace_time, y);
                svg_text(true, boot->initrd_time, y, "initrd");
                y++;
        }
        svg_bar("userspace", boot->userspace_time, boot->finish_time, y);
        svg_text("left", boot->userspace_time, y, "userspace");
        y++;

        for (u = times; u < times + n; u++) {
                char ts[FORMAT_TIMESPAN_MAX];
                bool b;

                if (!u->name)
                        continue;

                svg_bar("activating",   u->ixt, u->aet, y);
                svg_bar("active",       u->aet, u->axt, y);
                svg_bar("deactivating", u->axt, u->iet, y);

                b = u->ixt * SCALE_X > width * 2 / 3;
                if (u->time)
                        svg_text(b, u->ixt, y, "%s (%s)",
                                 u->name, format_timespan(ts, sizeof(ts), u->time, USEC_PER_MSEC));
                else
                        svg_text(b, u->ixt, y, "%s", u->name);
                y++;
        }
        svg("</g>\n\n");

        svg("</svg>");

        free_unit_times(times, (unsigned) n);

        return 0;
}

static int analyze_blame(sd_bus *bus) {
        struct unit_times *times;
        unsigned i;
        int n;

        n = acquire_time_data(bus, &times);
        if (n <= 0)
                return n;

        qsort(times, n, sizeof(struct unit_times), compare_unit_time);

        for (i = 0; i < (unsigned) n; i++) {
                char ts[FORMAT_TIMESPAN_MAX];

                if (times[i].time > 0)
                        printf("%16s %s\n", format_timespan(ts, sizeof(ts), times[i].time, USEC_PER_MSEC), times[i].name);
        }

        free_unit_times(times, (unsigned) n);
        return 0;
}

static int analyze_time(sd_bus *bus) {
        _cleanup_free_ char *buf = NULL;
        int r;

        r = pretty_boot_time(bus, &buf);
        if (r < 0)
                return r;

        puts(buf);
        return 0;
}

static int graph_one_property(const char *name, sd_bus_message *m) {

        static const char * const colors[] = {
                "Requires",              "[color=\"black\"]",
                "RequiresOverridable",   "[color=\"black\"]",
                "Requisite",             "[color=\"darkblue\"]",
                "RequisiteOverridable",  "[color=\"darkblue\"]",
                "Wants",                 "[color=\"grey66\"]",
                "Conflicts",             "[color=\"red\"]",
                "ConflictedBy",          "[color=\"red\"]",
                "After",                 "[color=\"green\"]"
        };

        const char *c = NULL;
        const char *s;
        const char *prop;
        unsigned i;
        int r;


        assert(name);
        assert(m);

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
        if (r < 0)
                return r;
        r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &prop);
        if (r < 0)
                return r;

        for (i = 0; i < ELEMENTSOF(colors); i += 2)
                if (streq(colors[i], prop)) {
                        c = colors[i+1];
                        break;
                }

        if (!c)
                goto finish;

        if (arg_dot != DEP_ALL)
                if ((arg_dot == DEP_ORDER) != streq(prop, "After"))
                        goto finish;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as");
        if (r == -ENXIO)
                goto finish;
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s");
        if (r < 0)
                return r;

        while ((r = sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &s)) > 0)
                printf("\t\"%s\"->\"%s\" %s;\n", name, s, c);
        if (r < 0)
                return r;
finish:
        r = bus_exit_container_force(m, SD_BUS_TYPE_DICT_ENTRY);
        if (r < 0)
                return r;
        return 0;
}

static int graph_one(sd_bus *bus, const struct unit_info *u) {
        _cleanup_sd_bus_message_unref_ sd_bus_message *reply = NULL;
        int r;

        assert(bus);
        assert(u);

        r = sd_bus_call_method(
                        bus,
                        "org.freedesktop.systemd1",
                        u->unit_path,
                        "org.freedesktop.DBus.Properties",
                        "GetAll",
                        NULL,
                        &reply,
                        "s",
                        "org.freedesktop.systemd1.Unit");
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
        if (r < 0) {
                log_error("Failed to parse reply.");
                return -EIO;
        }

        while (sd_bus_message_exit_container(reply) == -EBUSY) {
                r = graph_one_property(u->id, reply);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int dot(sd_bus *bus) {
        _cleanup_sd_bus_message_unref_ sd_bus_message *reply = NULL;
        int r;

        r = sd_bus_call_method(
                        bus,
                        "org.freedesktop.systemd1",
                        "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager",
                        "ListUnits",
                        NULL,
                        &reply,
                        NULL);
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
        if (r < 0) {
                log_error("Failed to parse reply.");
                return r;
        }

        printf("digraph systemd {\n");

        while (sd_bus_message_exit_container(reply) == -EBUSY) {
                struct unit_info u;

                r = bus_parse_unit_info(reply, &u);
                if (r < 0)
                        return -EIO;

                r = graph_one(bus, &u);
                if (r < 0)
                        return r;
        }

        printf("}\n");

        log_info("   Color legend: black     = Requires\n"
                 "                 dark blue = Requisite\n"
                 "                 dark grey = Wants\n"
                 "                 red       = Conflicts\n"
                 "                 green     = After\n");

        if (on_tty())
                log_notice("-- You probably want to process this output with graphviz' dot tool.\n"
                           "-- Try a shell pipeline like 'systemd-analyze dot | dot -Tsvg > systemd.svg'!\n");

        return 0;
}

static void analyze_help(void)
{
        printf("%s [OPTIONS...] {COMMAND} ...\n\n"
               "Process systemd profiling information\n\n"
               "  -h --help           Show this help\n"
               "     --version        Show package version\n"
               "     --system         Connect to system manager\n"
               "     --user           Connect to user service manager\n"
               "     --order          When generating a dependency graph, show only order\n"
               "     --require        When generating a dependency graph, show only requirement\n\n"
               "Commands:\n"
               "  time                Print time spent in the kernel before reaching userspace\n"
               "  blame               Print list of running units ordered by time to init\n"
               "  plot                Output SVG graphic showing service initialization\n"
               "  dot                 Dump dependency graph (in dot(1) format)\n\n",
               program_invocation_short_name);
}

static int parse_argv(int argc, char *argv[])
{
        enum {
                ARG_VERSION = 0x100,
                ARG_ORDER,
                ARG_REQUIRE,
                ARG_USER,
                ARG_SYSTEM
        };

        static const struct option options[] = {
                { "help",      no_argument,       NULL, 'h'           },
                { "version",   no_argument,       NULL, ARG_VERSION   },
                { "order",     no_argument,       NULL, ARG_ORDER     },
                { "require",   no_argument,       NULL, ARG_REQUIRE   },
                { "user",      no_argument,       NULL, ARG_USER      },
                { "system",    no_argument,       NULL, ARG_SYSTEM    },
                { NULL,        0,                 NULL, 0             }
        };

        assert(argc >= 0);
        assert(argv);

        for (;;) {
                switch (getopt_long(argc, argv, "h", options, NULL)) {

                case 'h':
                        analyze_help();
                        return 0;

                case ARG_VERSION:
                        puts(PACKAGE_STRING "\n" SYSTEMD_FEATURES);
                        return 0;

                case ARG_USER:
                        arg_scope = UNIT_FILE_USER;
                        break;

                case ARG_SYSTEM:
                        arg_scope = UNIT_FILE_SYSTEM;
                        break;

                case ARG_ORDER:
                        arg_dot = DEP_ORDER;
                        break;

                case ARG_REQUIRE:
                        arg_dot = DEP_REQUIRE;
                        break;

                case -1:
                        return 1;

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }
        }
}

int main(int argc, char *argv[]) {
        int r;
        sd_bus *bus = NULL;

        setlocale(LC_ALL, "");
        setlocale(LC_NUMERIC, "C"); /* we want to format/parse floats in C style */
        log_parse_environment();
        log_open();

        r = parse_argv(argc, argv);
        if (r < 0)
                return EXIT_FAILURE;
        else if (r <= 0)
                return EXIT_SUCCESS;

        if (arg_scope == UNIT_FILE_SYSTEM)
                r = sd_bus_open_system(&bus);
        else
                r = sd_bus_open_user(&bus);

        if (r < 0)
                return EXIT_FAILURE;

        if (!argv[optind] || streq(argv[optind], "time"))
                r = analyze_time(bus);
        else if (streq(argv[optind], "blame"))
                r = analyze_blame(bus);
        else if (streq(argv[optind], "plot"))
                r = analyze_plot(bus);
        else if (streq(argv[optind], "dot"))
                r = dot(bus);
        else
                log_error("Unknown operation '%s'.", argv[optind]);

        sd_bus_close(bus);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
