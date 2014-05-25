#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#include "option.h"
#include "strv.h"
#include "build.h"
#include "util.h"
#include "fileio.h"
#include "path-util.h"

int option_help(const struct sd_option *option, char *optarg) {
        void (*help)(void) = option->userdata;

        help();
        return 0;
}

int option_version(const struct sd_option *option, char *optarg) {
        puts(PACKAGE_STRING);
        puts(SYSTEMD_FEATURES);

        return 0;
}

int option_strv_extend(const struct sd_option *option, char *optarg) {
        char ***l = (char ***) option->userdata;
        int r;
        if (!optarg)
                return -EINVAL;

        r = strv_extend(l, optarg);
        return r < 0 ? r : 1;
}

#define DEFINE_SETTER(type, vartype)                                           \
        int option_set_##type(const struct sd_option *option, char *optarg) {  \
                vartype *i = option->userdata;                                 \
                                                                               \
                *i = (vartype) option->userarg;                                \
                return 1;                                                      \
        }


#define DEFINE_PARSER(type, vartype, conv_func)                                \
        int option_parse_##type(const struct sd_option *option, char *optarg) {\
                vartype *i = option->userdata;                                 \
                int r;                                                         \
                                                                               \
                assert(option->userdata);                                      \
                                                                               \
                if (!optarg) {                                                 \
                        log_error("Argument required for --%s", option->longopt);\
                        return -EINVAL;                                        \
                }                                                              \
                r = conv_func(optarg, i);                                      \
                if (r < 0) {                                                   \
                        log_error("Failed to parse --%s parameter %s",         \
                                        option->longopt, optarg);              \
                        return r;                                              \
                }                                                              \
                                                                               \
                return r < 0 ? r : 1;                                          \
        }

DEFINE_SETTER(int, int)
//DEFINE_SETTER(long, long)
DEFINE_SETTER(bool, bool)
//DEFINE_SETTER(uint64, uint64_t)
//DEFINE_SETTER(unsigned, unsigned)
//DEFINE_SETTER(double, double)
//DEFINE_SETTER(nsec, nsec_t)
//DEFINE_SETTER(sec, usec_t)


DEFINE_PARSER(int, int, safe_atoi)
DEFINE_PARSER(uint, unsigned int, safe_atou)
//DEFINE_PARSER(long, long, safe_atoli)
//DEFINE_PARSER(uint64, uint64_t, safe_atou64)
//DEFINE_PARSER(unsigned, unsigned, safe_atou)
DEFINE_PARSER(double, double, safe_atod)
//DEFINE_PARSER(nsec, nsec_t, parse_nsec)
DEFINE_PARSER(sec, usec_t, parse_sec)

int option_parse_bool(const struct sd_option *option, char *optarg) {
        bool *b = option->userdata;
        int r;

        assert(option->userdata);

        if (!optarg) {
                *b = true;
                return 1;
        }

        r = parse_boolean(optarg);
        if (r < 0) {
                log_error("Failed to parse --%s parameter %s",
                                option->longopt, optarg);
                return -EINVAL;
        }
        *b = r;

        return 1;
}


int option_not_supported(const struct sd_option *option, char *optarg) {
        log_error("--%s is not supported", option->longopt);
        return -ENOTSUP;
}

int option_read_full_file(const struct sd_option *option, char *optarg) {
        int r;
        char **data = (char **) option->userdata;

        if (!optarg) {
                log_error("--%s requires an argument", option->longopt);
                return -EINVAL;
        }
        if (*data) {
                log_error("--%s specified twice", option->longopt);
                return -EINVAL;
        }

        r = read_full_file(optarg, data, NULL);
        if (r < 0) {
                log_error("Failed to read %s: %s", optarg, strerror(-r));
                return r;
        }

        return 1;
}

int option_parse_log_level(const struct sd_option *option, char *optarg) {
        int r;

        r = log_level_from_string(optarg);
        if (r < 0) {
                log_error("Failed to parse priority value.");
                return r;
        }
        *((int *) option->userdata) = r;
        return 1;
}

int option_strdup_string(const struct sd_option *option, char *optarg) {
        char **data = ((char **) option->userdata);
        char *s;

        s = strdup(optarg);
        if (!s)
                return log_oom();
        free(*data);
        *data = s;
        return 1;
}

int option_parse_string(const struct sd_option *option, char *optarg) {
        *((char **) option->userdata) = optarg;
        return 1;
}

int option_parse_path(const struct sd_option *option, char *optarg) {
        *((char **) option->userdata) = path_kill_slashes(optarg);
        return 1;
}

int option_parse_signal(const struct sd_option *option, char *optarg) {
        int signal;

        signal = signal_from_string_try_harder(optarg);
        if (signal < 0) {
                log_error("Failed to parse signal string %s.", optarg);
                return -EINVAL;
        }
        *((int *) option->userdata) = signal;

        return 1;
}

int option_parse_argv(const struct sd_option *options, int argc, char *argv[], char ***outargs) {
        _cleanup_free_ char **opts = NULL;    //options we have parsed
        _cleanup_free_ char **args = NULL;    //arguments we didn't parse (i.e. non option arguments.)
        char **current;
        int i, r;

        STRV_FOREACH(current, argv+1) {
                char **next = current + 1;

                if (streq(*current, "--")){
                        strv_push(&opts, *current);
                        STRV_FOREACH(current, next) {
                                r = strv_push(&args, *current);
                                if (r < 0)
                                        return r;
                        }
                        break;
                } if (startswith(*current, "--")) {
                        char *longopt = *current + 2;
                        char *optarg = NULL;
                        int len;
                        const struct sd_option *opt;

                        strv_push(&opts, *current);
                        len = strchrnul(longopt, '=') - longopt;

                        for (opt = options; opt->cb; opt++) {
                                if (strneq(opt->longopt, longopt, len))
                                        break;
                        }
                        if (!opt->cb) {
                                log_error("unknown option %s", *current);
                                return -EINVAL;
                        }

                        if (opt->arg) {
                                if (longopt[len]) { //got equals sign
                                        optarg = longopt + len + 1;
                                } else if (*next && !startswith(*next, "-")) {
                                        optarg = *next;
                                        strv_push(&opts, *next);
                                        current++;
                                }
                        }

                        r = opt->cb(opt, optarg);
                        if (r <= 0)
                                return r;

                } else if (startswith(*current, "-")) {
                        char *shortopt = *current +1;

                        strv_push(&opts, *current);

                        while (*shortopt) {
                                char *optarg = NULL;
                                const struct sd_option *opt;

                                for (opt = options; opt->cb; opt++) {
                                        if (opt->shortopt == *shortopt)
                                                break;
                                }
                                if (!opt->cb) {
                                        log_error("unknown option %c", *shortopt);
                                        return -EINVAL;
                                }

                                if (opt->arg) {
                                        if (*(shortopt + 1)) { //got trailing data
                                                optarg = shortopt + 1;
                                        } else if (*next && !startswith(*next, "-")) {
                                                optarg = *next;
                                                strv_push(&opts, *next);
                                                current++;
                                        }
                                }

                                r = opt->cb(opt, optarg);
                                if (r <= 0)
                                        return r;

                                if (optarg) //we used everything as an argument
                                        break;

                                shortopt++;
                        }
                } else
                        strv_push(&args, *current);
        }

        i = 1; // skip argv[0]
        STRV_FOREACH(current, opts)
                argv[i++] = *current;

        STRV_FOREACH(current, args)
                argv[i++] = *current;

        assert(i == argc);

        i = strv_length(opts) + 1;

        if (outargs)
                *outargs = argv + i;
        else if (strv_length(args) > 0) {
                log_error("Too many arguments.");
                return -EINVAL;
        }

        return i;
}