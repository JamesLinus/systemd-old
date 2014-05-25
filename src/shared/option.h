#pragma once

#include <stdio.h>
#include <stdbool.h>

#include "macro.h"

struct sd_option;
typedef int (*OptionCallback)(const struct sd_option *option, char *optarg);

struct sd_option {
        const char *longopt;
        char shortopt;
        int arg;
        OptionCallback cb;
        void *userdata;
        long long userarg;
};

#define OPTIONS_BASIC(helpfunct) \
        { "help",    'h', false, option_help,    &helpfunct }, \
        { "version",  0 , false, option_version             }

/***
  options:
    array of options to parse, end with a complete NULL option.
  outargs:
    if not NULL:
      will be set to point towards the first non option argument.
    if NULL:
      do not accept any non option arguments.

  return value:
    if any option callback returned <= 0, this value is returned.
    otherwise, the number of arguments parsed is returned (like optind)
***/
int option_parse_argv(const struct sd_option *options, int argc, char *argv[], char ***outargs);

int option_help(const struct sd_option *option, char *optarg);
int option_version(const struct sd_option *option, char *optarg);
int option_set_int(const struct sd_option *option, char *optarg);
int option_set_bool(const struct sd_option *option, char *optarg);
int option_strv_extend(const struct sd_option *option, char *optarg);

int option_not_supported(const struct sd_option *option, char *optarg);
int option_read_full_file(const struct sd_option *option, char *optarg);

int option_parse_sec(const struct sd_option *option, char *optarg);
int option_parse_log_level(const struct sd_option *option, char *optarg);
int option_parse_string(const struct sd_option *option, char *optarg);
int option_parse_path(const struct sd_option *option, char *optarg);
int option_parse_signal(const struct sd_option *option, char *optarg);
int option_parse_int(const struct sd_option *option, char *optarg);
int option_parse_uint(const struct sd_option *option, char *optarg);
int option_parse_double(const struct sd_option *option, char *optarg);
int option_parse_bool(const struct sd_option *option, char *optarg);
int option_strdup_string(const struct sd_option *option, char *optarg);