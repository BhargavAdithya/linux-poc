/**
 * FAULT 3: logic error - zombie processes (no crash, wrong result)
 *
 * Use case : several services exit at the same moment.
 * Bug      : the reap code calls waitpid() once per SIGCHLD ("if" instead of
 *            "while"). Linux merges SIGCHLD signals that arrive together, so
 *            one signal can stand for many exited children.
 * Symptom  : only one child is reaped; the others stay as zombies (state Z)
 *            and their services are never restarted.
 * GDB      : make gdb-logic  ->  break after the reap, print the reaped count
 *            and the zombie count; shell ps shows the <defunct> processes.
 * Fix      : loop until waitpid(WNOHANG) returns 0, as reapChildren() does
 *            (build with -DFIXED).
 */
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "fault_common.h"

#define CHILDREN 5

static volatile sig_atomic_t g_sigchld = 0;

static void onChild(int sig)
{
    (void)sig;
    g_sigchld = 1;
}

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

static int reapOnSignal(void)
{
    int reaped = 0;

    if (g_sigchld != 0) {
        g_sigchld = 0;
#ifdef FIXED
        while (waitpid((pid_t)-1, NULL, WNOHANG) > 0) {
            reaped++;
        }
#else
        if (waitpid((pid_t)-1, NULL, WNOHANG) > 0) {  /* BUG: one child per signal */
            reaped++;
        }
#endif
    }
    return reaped;
}

int main(void)
{
    struct sigaction sa;
    int reaped;
    int zombies;
    int i;

    faultInit();
    (void)memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onChild;
    (void)sigaction(SIGCHLD, &sa, NULL);
    for (i = 0; i < CHILDREN; i++) {
        if (fork() == 0) {
            _exit(0);                                    /* all exit at once */
        }
    }
    (void)sleep(1U);                                     /* SIGCHLDs merge while we sleep */
    reaped = reapOnSignal();
    zombies = countZombies();
    (void)printf("Children exited: %d   reaped: %d   zombies left: %d\n", CHILDREN, reaped, zombies);
    (void)printf("%s\n", (zombies == 0) ? "OK: no zombie processes." : "BUG: zombie processes left behind.");
    logClose();
    return 0;
}
