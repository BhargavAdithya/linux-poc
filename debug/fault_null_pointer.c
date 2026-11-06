/**
 * FAULT 1: NULL pointer dereference (crash, SIGSEGV)
 *
 * Use case : the supervisor reaps an exited child after SIGCHLD.
 * Bug      : the child is not a configured service (e.g. a helper process),
 *            so ptFindByPid() returns NULL, and the code writes to the
 *            pointer without checking it.
 * Symptom  : "Segmentation fault" the first time a non-service child exits.
 * GDB      : make gdb-null  ->  run, bt, frame, print svc  (svc = 0x0)
 * Fix      : check for NULL and ignore unknown children, as reapChildren()
 *            does (build with -DFIXED).
 */
#include <sys/wait.h>
#include <unistd.h>
#include "fault_common.h"

static void recordExit(pid_t pid, int status)
{
    Service *svc = ptFindByPid(&gTable, pid);

#ifdef FIXED
    if (svc == NULL) {
        (void)printf("pid %d is not a service: ignored\n", (int)pid);
        return;
    }
#endif
    svc->lastStatus = status;                        /* svc may be NULL */
    (void)printf("recorded exit of %s\n", svc->cfg.name);
}

int main(void)
{
    pid_t helper;
    int status = 0;

    faultSetup();
    helper = fork();
    if (helper == 0) {
        _exit(0);                                    /* a child that is not a service */
    }
    (void)waitpid(helper, &status, 0);
    recordExit(helper, status);
    ptFree(&gTable);
    logClose();
    (void)printf("Finished normally.\n");
    return 0;
}
