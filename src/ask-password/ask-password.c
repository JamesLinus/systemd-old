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

#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <limits.h>
#include <stddef.h>

#include "log.h"
#include "macro.h"
#include "util.h"
#include "strv.h"
#include "ask-password-api.h"
#include "def.h"
#include "option.h"

static const char *arg_icon = NULL;
static const char *arg_id = NULL;
static bool arg_echo = false;
static bool arg_use_tty = true;
static usec_t arg_timeout = DEFAULT_TIMEOUT_USEC;
static bool arg_accept_cached = false;
static bool arg_multiple = false;

static void help(void) {
        printf("%s [OPTIONS...] MESSAGE\n\n"
               "Query the user for a system passphrase, via the TTY or an UI agent.\n\n"
               "  -h --help          Show this help\n"
               "     --icon=NAME     Icon name\n"
               "     --timeout=SEC   Timeout in sec\n"
               "     --echo          Do not mask input (useful for usernames)\n"
               "     --no-tty        Ask question via agent even on TTY\n"
               "     --accept-cached Accept cached passwords\n"
               "     --multiple      List multiple passwords if available\n"
               "     --id=ID         Query identifier (e.g. cryptsetup:/dev/sda5)\n"
               , program_invocation_short_name);
}

int main(int argc, char *argv[]) {
        static const struct sd_option options[] = {
                OPTIONS_BASIC(help),
                { "icon",           0 , true,  option_parse_string, &arg_icon                 },
                { "timeout",        0 , true,  option_parse_sec,    &arg_timeout              },
                { "no-tty",         0 , false, option_set_bool,     &arg_use_tty,       false },
                { "accept-cached",  0 , false, option_set_bool,     &arg_accept_cached, true  },
                { "multiple",       0 , false, option_set_bool,     &arg_multiple,      true  },
                { "id",             0 , true,  option_parse_string, &arg_id                   },
                { "echo",           0 , false, option_set_bool,     &arg_echo,          true  },
                {}
        };
        int r;
        usec_t timeout = 0;
        char **args;

        log_parse_environment();
        log_open();

        r = option_parse_argv(options, argc, argv, &args);
        if (r <= 0)
                goto finish;

        if (strv_length(args) != 1) {
                log_error("%s: required argument missing.", program_invocation_short_name);
                r = -EINVAL;
                goto finish;
        }


        if (arg_timeout > 0)
                timeout = now(CLOCK_MONOTONIC) + arg_timeout;

        if (arg_use_tty && isatty(STDIN_FILENO)) {
                char *password = NULL;

                if ((r = ask_password_tty(args[0], timeout, arg_echo, NULL, &password)) >= 0) {
                        puts(password);
                        free(password);
                }

        } else {
                char **l;

                if ((r = ask_password_agent(args[0], arg_icon, arg_id, timeout, arg_echo, arg_accept_cached, &l)) >= 0) {
                        char **p;

                        STRV_FOREACH(p, l) {
                                puts(*p);

                                if (!arg_multiple)
                                        break;
                        }

                        strv_free(l);
                }
        }

finish:

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
