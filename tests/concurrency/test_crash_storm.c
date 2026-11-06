/**
 * @file test_crash_storm.c
 * @brief Multiprocess stress test: 20 services crash at the same moment.
 *
 * The supervisor is single-threaded; its concurrency is many child
 * processes exiting together. Linux merges simultaneous SIGCHLD signals
 * into one, so reapChildren() must loop on waitpid(WNOHANG) or zombies
 * are left behind (see debug/fault_zombie_logic.c for the buggy version).
 *
 * Expected: every crash detected, every service restarted once, then
 * FAILED (max_restarts = 1), and 0 zombie processes at the end.
 *
 *   make crash-storm
 */
#include <dirent.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../../include/config.h"
#include "../../include/launcher.h"
#include "../../include/logger.h"
#include "../../include/monitor.h"
#include "../../include/proctable.h"
#include "../../include/restart.h"
#include "../../include/supervisor.h"

#define N_SERVICES 20

static int countZombies(void)
{
    DIR *d = opendir("/proc");
    struct dirent *ent;
    char path[300];
    char line[512];
    char state;
    int ppid;
    int zombies = 0;
    FILE *fp;

    if (d != NULL) {
        while ((ent = readdir(d)) != NULL) {
            (void)snprintf(path, sizeof(path), "/proc/%s/stat", ent->d_name);
            fp = fopen(path, "r");
            if (fp != NULL) {
                const char *after = (fgets(line, (int)sizeof(line), fp) != NULL) ? strrchr(line, ')') : NULL;
                if ((after != NULL) && (sscanf(after + 1, " %c %d", &state, &ppid) == 2) && (state == 'Z') &&
                    (ppid == (int)getpid())) {
                    zombies++;
                }
                (void)fclose(fp);
            }
        }
        (void)closedir(d);
    }
    return zombies;
}

int main(void)
{
    char log[] = "/tmp/crash_storm_XXXXXX";
    char name[MAX_NAME];
    int failed = 0;
    int restarted = 0;
    int pass = 0;
    int waited = 0;
    ServiceConfig cfg;
    Service *svc;
    int fd = mkstemp(log);

    if (fd >= 0) {
        (void)close(fd);
        (void)logInit(log, 0);
    }
    (void)installSignalHandlers();
    ptInit(&gTable);
    for (int i = 0; i < N_SERVICES; i++) {
        initServiceConfig(&cfg);
        (void)snprintf(name, sizeof(name), "svc_%02d", i);
        (void)snprintf(cfg.name, sizeof(cfg.name), "%s", name);
        (void)snprintf(cfg.command, sizeof(cfg.command), "services/demo_service %s --crash-after 1", name);
        cfg.maxRestarts = 1;
        cfg.restartDelay = 1;
        (void)ptAdd(&gTable, &cfg);
    }
    (void)resolveStartOrder(&gTable, gStartOrder, &gStartCount);
    (void)launchAll(gStartOrder, gStartCount);
    (void)printf("Launched %d services; each crashes after 1 s, is restarted once, then crashes again.\n",
                 N_SERVICES);

    /* the same steps as runEventLoop(), until every service is FAILED (max 10 s) */
    while ((failed < N_SERVICES) && (waited < 10000)) {
        (void)poll(NULL, 0U, 100);
        waited += 100;
        reapChildren(&gCtx, &gTable);
        processPendingRestarts(&gTable);
        failed = 0;
        for (svc = gTable.head; svc != NULL; svc = svc->next) {
            failed += (svc->state == SVC_FAILED) ? 1 : 0;
        }
    }
    for (svc = gTable.head; svc != NULL; svc = svc->next) {
        restarted += (svc->restartCount == 1) ? 1 : 0;
    }

    (void)printf("\n%-34s %8s %8s\n", "Check", "Expected", "Actual");
    (void)printf("%-34s %8d %8d\n", "Services restarted exactly once", N_SERVICES, restarted);
    (void)printf("%-34s %8d %8d\n", "Services FAILED after 2nd crash", N_SERVICES, failed);
    (void)printf("%-34s %8d %8d\n", "Zombie processes left", 0, countZombies());
    pass = (restarted == N_SERVICES) && (failed == N_SERVICES) && (countZombies() == 0);
    (void)printf("\nRESULT: %s (log: %s)\n", pass ? "PASS" : "FAIL", log);

    ptFree(&gTable);
    logClose();
    return pass ? 0 : 1;
}
