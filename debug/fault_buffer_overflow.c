/**
 * FAULT 2: buffer overflow (silent memory corruption)
 *
 * Use case : reading the service name from supervisor.conf.
 * Bug      : strcpy() copies a 60-character name into name[32] without a
 *            length check, so it runs on into the next field (command).
 * Symptom  : no crash; the command silently becomes the end of the name, so
 *            execvp() would later run the wrong program.
 * GDB      : make gdb-overflow  ->  watchpoint on cfg->command stops the
 *            program at the exact write that corrupts it; bt shows
 *            strcpy <- storeName.
 * Fix      : length-checked copy, as parseServiceLine() does (build with -DFIXED).
 */
#include "fault_common.h"

static int storeName(ServiceConfig *cfg, const char *name)
{
    int status = SUP_OK;

#ifdef FIXED
    if (strlen(name) >= sizeof(cfg->name)) {
        (void)printf("E002: name rejected (%u characters, maximum %d)\n", (unsigned int)strlen(name),
                     MAX_NAME - 1);
        status = ERR_CONFIG_ENTRY;
    } else {
        (void)snprintf(cfg->name, sizeof(cfg->name), "%s", name);
    }
#else
    strcpy(cfg->name, name);                         /* no length check */
#endif
    return status;
}

int main(void)
{
    ServiceConfig cfg;
    char longName[61];
    int i;

    faultInit();
    initServiceConfig(&cfg);
    (void)snprintf(cfg.command, sizeof(cfg.command), "%s", "./services/demo_service camera");
    for (i = 0; i < 60; i++) {
        longName[i] = (char)('a' + (i % 26));
    }
    longName[60] = '\0';
    (void)storeName(&cfg, longName);
    (void)printf("Command after reading the name: \"%s\"\n", cfg.command);
    (void)printf("(expected \"./services/demo_service camera\")\n");
    logClose();
    return 0;
}
