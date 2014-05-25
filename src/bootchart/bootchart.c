/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright (C) 2009-2013 Intel Corporation

  Authors:
    Auke Kok <auke-jan.h.kok@intel.com>

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

/***

  Many thanks to those who contributed ideas and code:
  - Ziga Mahkovec - Original bootchart author
  - Anders Norgaard - PyBootchartgui
  - Michael Meeks - bootchart2
  - Scott James Remnant - Ubuntu C-based logger
  - Arjan van der Ven - for the idea to merge bootgraph.pl functionality

 ***/

#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include "systemd/sd-journal.h"

#include "util.h"
#include "fileio.h"
#include "macro.h"
#include "conf-parser.h"
#include "strxcpyx.h"
#include "path-util.h"
#include "store.h"
#include "svg.h"
#include "bootchart.h"
#include "list.h"
#include "option.h"

double graph_start;
double log_start;
struct ps_struct *ps_first;
int pscount;
int cpus;
double interval;
FILE *of = NULL;
int overrun = 0;
static int exiting = 0;
int sysfd=-1;

#define DEFAULT_SAMPLES_LEN 500
#define DEFAULT_HZ 25.0
#define DEFAULT_SCALE_X 100.0 /* 100px = 1sec */
#define DEFAULT_SCALE_Y 20.0  /* 16px = 1 process bar */
#define DEFAULT_INIT "/sbin/init"
#define DEFAULT_OUTPUT "/run/log"

/* graph defaults */
bool arg_entropy = false;
bool initcall = true;
bool arg_relative = false;
bool arg_filter = true;
bool arg_show_cmdline = false;
bool arg_show_cgroup = false;
bool arg_pss = false;
int samples;
int arg_samples_len = DEFAULT_SAMPLES_LEN; /* we record len+1 (1 start sample) */
double arg_hz = DEFAULT_HZ;
double arg_scale_x = DEFAULT_SCALE_X;
double arg_scale_y = DEFAULT_SCALE_Y;
static struct list_sample_data *sampledata;
struct list_sample_data *head;

const char *arg_init_path = DEFAULT_INIT;
const char *arg_output_path = DEFAULT_OUTPUT;

static void signal_handler(int sig) {
        if (sig++)
                sig--;
        exiting = 1;
}

#define BOOTCHART_CONF "/etc/systemd/bootchart.conf"

#define BOOTCHART_MAX (16*1024*1024)

static void help(void) {
        fprintf(stdout,
                "Usage: %s [OPTIONS]\n\n"
                "Options:\n"
                "  -r, --rel             Record time relative to recording\n"
                "  -f, --freq=FREQ       Sample frequency [%g]\n"
                "  -n, --samples=N       Stop sampling at [%d] samples\n"
                "  -x, --scale-x=N       Scale the graph horizontally [%g] \n"
                "  -y, --scale-y=N       Scale the graph vertically [%g] \n"
                "  -p, --pss             Enable PSS graph (CPU intensive)\n"
                "  -e, --entropy         Enable the entropy_avail graph\n"
                "  -o, --output=PATH     Path to output files [%s]\n"
                "  -i, --init=PATH       Path to init executable [%s]\n"
                "  -F, --no-filter       Disable filtering of unimportant or ephemeral processes\n"
                "  -C, --cmdline         Display full command lines with arguments\n"
                "  -c, --control-group   Display process control group\n"
                "  -h, --help            Display this message\n\n"
                "See bootchart.conf for more information.\n",
                program_invocation_short_name,
                DEFAULT_HZ,
                DEFAULT_SAMPLES_LEN,
                DEFAULT_SCALE_X,
                DEFAULT_SCALE_Y,
                DEFAULT_OUTPUT,
                DEFAULT_INIT);
}

static void do_journal_append(char *file) {
        struct iovec iovec[5];
        int r, f, j = 0;
        ssize_t n;
        _cleanup_free_ char *bootchart_file = NULL, *bootchart_message = NULL,
                *p = NULL;

        bootchart_file = strappend("BOOTCHART_FILE=", file);
        if (bootchart_file)
                IOVEC_SET_STRING(iovec[j++], bootchart_file);

        IOVEC_SET_STRING(iovec[j++], "MESSAGE_ID=9f26aa562cf440c2b16c773d0479b518");
        IOVEC_SET_STRING(iovec[j++], "PRIORITY=7");
        bootchart_message = strjoin("MESSAGE=Bootchart created: ", file, NULL);
        if (bootchart_message)
                IOVEC_SET_STRING(iovec[j++], bootchart_message);

        p = malloc(9 + BOOTCHART_MAX);
        if (!p) {
                log_oom();
                return;
        }

        memcpy(p, "BOOTCHART=", 10);

        f = open(file, O_RDONLY|O_CLOEXEC);
        if (f < 0) {
                log_error("Failed to read bootchart data: %m");
                return;
        }
        n = loop_read(f, p + 10, BOOTCHART_MAX, false);
        if (n < 0) {
                log_error("Failed to read bootchart data: %s", strerror(-n));
                close(f);
                return;
        }
        close(f);

        iovec[j].iov_base = p;
        iovec[j].iov_len = 10 + n;
        j++;

        r = sd_journal_sendv(iovec, j);
        if (r < 0)
                log_error("Failed to send bootchart: %s", strerror(-r));
}

int main(int argc, char *argv[]) {
        static const ConfigTableItem items[] = {
                { "Bootchart", "Samples",          config_parse_int,    0, &arg_samples_len },
                { "Bootchart", "Frequency",        config_parse_double, 0, &arg_hz          },
                { "Bootchart", "Relative",         config_parse_bool,   0, &arg_relative    },
                { "Bootchart", "Filter",           config_parse_bool,   0, &arg_filter      },
                { "Bootchart", "Output",           config_parse_path,   0, &arg_output_path },
                { "Bootchart", "Init",             config_parse_path,   0, &arg_init_path   },
                { "Bootchart", "PlotMemoryUsage",  config_parse_bool,   0, &arg_pss         },
                { "Bootchart", "PlotEntropyGraph", config_parse_bool,   0, &arg_entropy     },
                { "Bootchart", "ScaleX",           config_parse_double, 0, &arg_scale_x     },
                { "Bootchart", "ScaleY",           config_parse_double, 0, &arg_scale_y     },
                { "Bootchart", "ControlGroup",     config_parse_bool,   0, &arg_show_cgroup },
                {}
        };
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                { "rel",           'r', false, option_set_bool,     &arg_relative,     true  },
                { "freq",          'f', true,  option_parse_double, &arg_hz,                 },
                { "samples",       'n', true,  option_parse_int,    &arg_samples_len,        },
                { "scale-x",       'x', true,  option_parse_double, &arg_scale_x,            },
                { "scale-y",       'y', true,  option_parse_double, &arg_scale_y,            },
                { "pss",           'p', false, option_set_bool,     &arg_pss,          true  },
                { "entropy",       'e', false, option_set_bool,     &arg_entropy,      true  },
                { "output",        'o', true,  option_parse_path,   &arg_output_path,        },
                { "init",          'i', true,  option_parse_path,   &arg_init_path,          },
                { "no-filter",     'F', false, option_set_bool,     &arg_filter,       false },
                { "cmdline",       'C', false, option_set_bool,     &arg_show_cmdline, true  },
                { "control-group", 'c', false, option_set_bool,     &arg_show_cgroup,  true  },
                {}
        };
        _cleanup_free_ char *build = NULL;
        struct sigaction sig = {
                .sa_handler = signal_handler,
        };
        struct ps_struct *ps;
        char output_file[PATH_MAX];
        char datestr[200];
        time_t t = 0;
        int r;
        struct rlimit rlim;
        bool has_procfs = false;

        r = config_parse(NULL, BOOTCHART_CONF, NULL,
                         NULL,
                         config_item_table_lookup, items,
                         true, false, true, NULL);
        if (r < 0)
                log_warning("Failed to parse configuration file: %s", strerror(-r));

        r = option_parse_argv(options, argc, argv, NULL);
        if (r <= 0)
                return r == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

        if (arg_hz <= 0.0) {
                fprintf(stderr, "Error: Frequency needs to be > 0\n");
                return EXIT_FAILURE;
        }

        /*
         * If the kernel executed us through init=/usr/lib/systemd/systemd-bootchart, then
         * fork:
         * - parent execs executable specified via init_path[] (/sbin/init by default) as pid=1
         * - child logs data
         */
        if (getpid() == 1) {
                if (fork()) {
                        /* parent */
                        execl(arg_init_path, arg_init_path, NULL);
                }
        }
        argv[0][0] = '@';

        rlim.rlim_cur = 4096;
        rlim.rlim_max = 4096;
        (void) setrlimit(RLIMIT_NOFILE, &rlim);

        /* start with empty ps LL */
        ps_first = new0(struct ps_struct, 1);
        if (!ps_first) {
                log_oom();
                return EXIT_FAILURE;
        }

        /* handle TERM/INT nicely */
        sigaction(SIGHUP, &sig, NULL);

        interval = (1.0 / arg_hz) * 1000000000.0;

        log_uptime();

        if (graph_start < 0.0) {
                fprintf(stderr,
                        "Failed to setup graph start time.\n\nThe system uptime "
                        "probably includes time that the system was suspended. "
                        "Use --rel to bypass this issue.\n");
                exit (EXIT_FAILURE);
        }

        has_procfs = access("/proc/vmstat", F_OK) == 0;

        LIST_HEAD_INIT(head);

        /* main program loop */
        for (samples = 0; !exiting && samples < arg_samples_len; samples++) {
                int res;
                double sample_stop;
                struct timespec req;
                time_t newint_s;
                long newint_ns;
                double elapsed;
                double timeleft;

                sampledata = new0(struct list_sample_data, 1);
                if (sampledata == NULL) {
                        log_oom();
                        return EXIT_FAILURE;
                }

                sampledata->sampletime = gettime_ns();
                sampledata->counter = samples;

                if (!of && (access(arg_output_path, R_OK|W_OK|X_OK) == 0)) {
                        t = time(NULL);
                        r = strftime(datestr, sizeof(datestr), "%Y%m%d-%H%M", localtime(&t));
                        assert_se(r > 0);

                        snprintf(output_file, PATH_MAX, "%s/bootchart-%s.svg", arg_output_path, datestr);
                        of = fopen(output_file, "we");
                }

                if (sysfd < 0)
                        sysfd = open("/sys", O_RDONLY|O_CLOEXEC);

                if (!build) {
                        if (parse_env_file("/etc/os-release", NEWLINE, "PRETTY_NAME", &build, NULL) == -ENOENT)
                                parse_env_file("/usr/lib/os-release", NEWLINE, "PRETTY_NAME", &build, NULL);
                }

                if (has_procfs)
                        log_sample(samples, &sampledata);
                else
                        /* wait for /proc to become available, discarding samples */
                        has_procfs = access("/proc/vmstat", F_OK) == 0;

                sample_stop = gettime_ns();

                elapsed = (sample_stop - sampledata->sampletime) * 1000000000.0;
                timeleft = interval - elapsed;

                newint_s = (time_t)(timeleft / 1000000000.0);
                newint_ns = (long)(timeleft - (newint_s * 1000000000.0));

                /*
                 * check if we have not consumed our entire timeslice. If we
                 * do, don't sleep and take a new sample right away.
                 * we'll lose all the missed samples and overrun our total
                 * time
                 */
                if (newint_ns > 0 || newint_s > 0) {
                        req.tv_sec = newint_s;
                        req.tv_nsec = newint_ns;

                        res = nanosleep(&req, NULL);
                        if (res) {
                                if (errno == EINTR) {
                                        /* caught signal, probably HUP! */
                                        break;
                                }
                                log_error("nanosleep() failed: %m");
                                exit(EXIT_FAILURE);
                        }
                } else {
                        overrun++;
                        /* calculate how many samples we lost and scrap them */
                        arg_samples_len -= (int)(newint_ns / interval);
                }
                LIST_PREPEND(link, head, sampledata);
        }

        /* do some cleanup, close fd's */
        ps = ps_first;
        while (ps->next_ps) {
                ps = ps->next_ps;
                if (ps->schedstat)
                        close(ps->schedstat);
                if (ps->sched)
                        close(ps->sched);
                if (ps->smaps)
                        fclose(ps->smaps);
        }

        if (!of) {
                t = time(NULL);
                r = strftime(datestr, sizeof(datestr), "%Y%m%d-%H%M", localtime(&t));
                assert_se(r > 0);

                snprintf(output_file, PATH_MAX, "%s/bootchart-%s.svg", arg_output_path, datestr);
                of = fopen(output_file, "we");
        }

        if (!of) {
                fprintf(stderr, "opening output file '%s': %m\n", output_file);
                exit (EXIT_FAILURE);
        }

        svg_do(strna(build));

        fprintf(stderr, "systemd-bootchart wrote %s\n", output_file);

        do_journal_append(output_file);

        if (of)
                fclose(of);

        closedir(proc);
        if (sysfd >= 0)
                close(sysfd);

        /* nitpic cleanups */
        ps = ps_first->next_ps;
        while (ps->next_ps) {
                struct ps_struct *old;

                old = ps;
                old->sample = ps->first;
                ps = ps->next_ps;
                while (old->sample->next) {
                        struct ps_sched_struct *oldsample = old->sample;

                        old->sample = old->sample->next;
                        free(oldsample);
                }
                free(old->cgroup);
                free(old->sample);
                free(old);
        }
        free(ps->cgroup);
        free(ps->sample);
        free(ps);

        sampledata = head;
        while (sampledata->link_prev) {
                struct list_sample_data *old_sampledata = sampledata;
                sampledata = sampledata->link_prev;
                free(old_sampledata);
        }
        free(sampledata);
        /* don't complain when overrun once, happens most commonly on 1st sample */
        if (overrun > 1)
                fprintf(stderr, "systemd-boochart: Warning: sample time overrun %i times\n", overrun);

        return 0;
}
