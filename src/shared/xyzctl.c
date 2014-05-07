
#include "xyzctl.h"

#include "strv.h"
#include "util.h"
#include "spawn-polkit-agent.h"
#include "pager.h"

int xyzctl_main(const xyzctl_verb *verbs, sd_bus *bus, int bus_error, char **argv,
                void (*help)(void), bool use_polkit, bool use_pager) {
        int left;
        unsigned i;
        int r;

        assert(argv);

        left = strv_length(argv);

        if (left <= 0)
                /* Special rule: no arguments means first verb*/
                i = 0;
        else {
                if (streq(argv[0], "help")) {
                        if (use_pager)
                                pager_open(false);
                        help();
                        if (use_pager)
                                pager_close();
                        return 0;
                }

                for (i = 0; verbs[i].verb; i++)
                        if (streq(argv[0], verbs[i].verb))
                                break;

                if (!verbs[i].verb) {
                        log_error("Unknown operation %s", argv[0]);
                        return -EINVAL;
                }
        }

        switch (verbs[i].argc_cmp) {

        case EQUAL:
                if (left != verbs[i].argc) {
                        log_error("Invalid number of arguments.");
                        return -EINVAL;
                }

                break;

        case MORE:
                if (left < verbs[i].argc) {
                        log_error("Too few arguments.");
                        return -EINVAL;
                }

                break;

        case LESS:
                if (left > verbs[i].argc) {
                        log_error("Too many arguments.");
                        return -EINVAL;
                }

                break;

        default:
                assert_not_reached("Unknown comparison operator.");
        }

        if (verbs[i].flags & XYZCTL_BUS && !bus) {
                log_error("Failed to create bus connection: %s", strerror(-bus_error));
                return bus_error;
        }

        if (verbs[i].flags & XYZCTL_POLKIT && use_polkit)
                polkit_agent_open();

        if (verbs[i].flags & XYZCTL_PAGER && use_pager)
                pager_open(false);

        r = verbs[i].dispatch(bus, argv, left);

        if (verbs[i].flags & XYZCTL_PAGER && use_pager)
                pager_close();

        return r;
}