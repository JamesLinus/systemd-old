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

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include "systemd/sd-journal.h"

#include "util.h"
#include "option.h"
#include "strv.h"

static char *arg_identifier = NULL;
static int arg_priority = LOG_INFO;
static bool arg_level_prefix = true;

static void help(void) {
        printf("%s [OPTIONS...] {COMMAND} ...\n\n"
               "Execute process with stdout/stderr connected to the journal.\n\n"
               "  -h --help               Show this help\n"
               "     --version            Show package version\n"
               "  -t --identifier=STRING  Set syslog identifier\n"
               "  -p --priority=PRIORITY  Set priority value (0..7)\n"
               "     --level-prefix=BOOL  Control whether level prefix shall be parsed\n"
               , program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                { "identifier",   't', true, option_parse_string,    &arg_identifier   },
                { "priority",     'p', true, option_parse_log_level, &arg_priority     },
                { "level-prefix",  0 , true, option_parse_bool,      &arg_level_prefix },
                {}
        };
        char **args;
        int r, fd = -1, saved_stderr = -1;

        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, &args);
        if (r <= 0)
                goto finish;

        fd = sd_journal_stream_fd(arg_identifier, arg_priority, arg_level_prefix);
        if (fd < 0) {
                log_error("Failed to create stream fd: %s", strerror(-fd));
                r = fd;
                goto finish;
        }

        saved_stderr = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);

        if (dup3(fd, STDOUT_FILENO, 0) < 0 ||
            dup3(fd, STDERR_FILENO, 0) < 0) {
                log_error("Failed to duplicate fd: %m");
                r = -errno;
                goto finish;
        }

        if (fd >= 3)
                safe_close(fd);

        fd = -1;

        if (strv_length(args) > 0)
                execvp(args[0], args);
        else
                execl("/bin/cat", "/bin/cat", NULL);

        r = -errno;

        /* Let's try to restore a working stderr, so we can print the error message */
        if (saved_stderr >= 0)
                dup3(saved_stderr, STDERR_FILENO, 0);

        log_error("Failed to execute process: %s", strerror(-r));

finish:
        safe_close(fd);
        safe_close(saved_stderr);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
