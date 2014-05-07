
#pragma once

#include <stdbool.h>

#include "sd-bus.h"


typedef struct xyzctl_verb {
        const char* verb;
        const enum {
                MORE,
                LESS,
                EQUAL
        } argc_cmp;
        const int argc;
        int (* const dispatch)(sd_bus *bus, char **args, unsigned n);
        const enum {
                XYZCTL_BUS    = 1,
                XYZCTL_PAGER  = 2,
                XYZCTL_POLKIT = 4
        } flags;
} xyzctl_verb;

int xyzctl_main(const xyzctl_verb *verbs, sd_bus *bus, int bus_error, char **argv,
                void (*help)(void), bool use_polkit, bool use_pager);