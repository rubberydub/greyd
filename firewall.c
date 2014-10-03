/**
 * @file   firewall.c
 * @brief  Implements firewall interface.
 * @author Mikey Austin
 * @date   2014
 */

#include "failures.h"
#include "firewall.h"
#include "config_section.h"
#include "mod.h"

#include <unistd.h>

extern FILE
*FW_setup_cntl_pipe(char *command, char **argv)
{
    int pdes[2];
    FILE *out;

    if(pipe(pdes) != 0)
        return NULL;

    switch(fork()) {
    case -1:
        close(pdes[0]);
        close(pdes[1]);
        return NULL;

    case 0:
        /* Close all output in child. */
        close(pdes[1]);
        close(STDERR_FILENO);
        close(STDOUT_FILENO);
        if(pdes[0] != STDIN_FILENO) {
            dup2(pdes[0], STDIN_FILENO);
            close(pdes[0]);
        }
        execvp(command, argv);
        _exit(1);
    }

    /* parent */
    close(pdes[0]);
    out = fdopen(pdes[1], "w");
    if(out == NULL) {
        close(pdes[1]);
        return NULL;
    }

    return out;
}

extern int
FW_replace_networks(Config_section_T section, List_T cidrs)
{
    void *handle;
    int (*replace_networks)(Config_section_T section, List_T cidrs), ret;

    handle = Mod_open(section, "firewall");
    replace_networks = (int (*)(Config_section_T, List_T))
        Mod_get(handle, "Mod_fw_replace_networks");
    ret = (*replace_networks)(section, cidrs);
    Mod_close(handle);

    return ret;
}

extern void
FW_init(Config_section_T section)
{
    void *handle;
    void (*init)(Config_section_T section);

    handle = Mod_open(section, "firewall");
    init = (void (*)(Config_section_T)) Mod_get(handle, "Mod_fw_init");
    (*init)(section);
    Mod_close(handle);
}