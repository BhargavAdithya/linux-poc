/**
 * @file monitor.c
 * @brief Monitor Module: reaps exited services (SIGCHLD + waitpid), watches
 *        heartbeats (IPC pipe) and reads CPU / memory usage from procfs.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "logger.h"
#include "monitor.h"
#include "proctable.h"
#include "restart.h"

/* Called for every reaped child that belongs to the table. */
static void classifyExit(const SupervisorContext *ctx, Service *svc, int status)
{
    char reason[64];

    if (svc->hbReadFd >= 0) {
        (void)close(svc->hbReadFd);
        svc->hbReadFd = -1;
    }
    describeExit(status, reason, (int)sizeof(reason));
    if ((ctx != NULL) && (ctx->stopping != 0)) {
        logMsg(LOG_INFO, svc, "service stopped (%s)", reason);
        svc->pid = 0;
        svc->state = SVC_STOPPED;
    } else if (svc->state == SVC_RESTARTING) {
        /* stopped on purpose because a dependency was restarted */
        logMsg(LOG_INFO, svc, "service stopped for dependency restart (%s)", reason);
        svc->pid = 0;
        svc->nextRestartTime = time(NULL);
    } else if ((WIFEXITED(status) != 0) && (WEXITSTATUS(status) == 0)) {
        logMsg(LOG_INFO, svc, "service exited normally");
        svc->pid = 0;
        svc->state = SVC_STOPPED;
    } else {
        handleCrash(svc, status);         /* logs with the old PID, then clears it */
    }
}

void reapChildren(const SupervisorContext *ctx, ProcessTable *table)
{
    int status = 0;
    pid_t pid;

    /* Loop: one SIGCHLD may stand for several exited children. */
    pid = waitpid((pid_t)-1, &status, WNOHANG);
    while (pid > 0) {
        Service *svc = ptFindByPid(table, pid);
        if (svc != NULL) {
            svc->lastStatus = status;
            classifyExit(ctx, svc, status);
        }
        pid = waitpid((pid_t)-1, &status, WNOHANG);
    }
}

/* Reads all pending heartbeat bytes. Returns 1 when at least one arrived. */
static int drainHeartbeat(int fd)
{
    char buf[64];
    ssize_t n;
    int received = 0;

    n = read(fd, buf, sizeof(buf));
    while (n > 0) {
        received = 1;
        n = read(fd, buf, sizeof(buf));
    }
    return received;
}

void checkHeartbeats(ProcessTable *table)
{
    time_t now = time(NULL);
    Service *svc;

    for (svc = (table != NULL) ? table->head : NULL; svc != NULL; svc = svc->next) {
        if ((svc->pid > 0) && ((svc->state == SVC_STARTING) || (svc->state == SVC_RUNNING))) {
            if ((svc->state == SVC_STARTING) && ((now - svc->startTime) >= (time_t)START_GRACE_SEC)) {
                svc->state = SVC_RUNNING;
                logMsg(LOG_INFO, svc, "service running");
            }
            if (svc->hbReadFd >= 0) {
                if (drainHeartbeat(svc->hbReadFd) == 1) {
                    svc->lastHeartbeat = now;
                } else if ((now - svc->lastHeartbeat) > (time_t)svc->cfg.heartbeatTimeout) {
                    logError(svc, ERR_HEARTBEAT, "no heartbeat - sending SIGKILL");
                    (void)kill(svc->pid, SIGKILL);
                    svc->state = SVC_CRASHED;     /* reapChildren() handles the restart */
                } else {
                    /* within the timeout */
                }
            }
        }
    }
}

int readProcStats(pid_t pid, unsigned long *cpuTicks, long *memKb)
{
    int status = ERR_FILE;
    char path[64];
    char line[LINE_SIZE];
    unsigned long utime = 0UL;
    unsigned long stime = 0UL;
    long rss = 0L;
    FILE *fp;

    (void)snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    fp = fopen(path, "r");
    if (fp != NULL) {
        if (fgets(line, (int)sizeof(line), fp) != NULL) {
            /* the command name may contain spaces: parse after the last ')' */
            const char *afterName = strrchr(line, ')');

            if ((afterName != NULL) &&
                (sscanf(afterName + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                        &utime, &stime) == 2)) {
                status = SUP_OK;
            }
        }
        (void)fclose(fp);
    }
    if (status == SUP_OK) {
        (void)snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
        fp = fopen(path, "r");
        if (fp != NULL) {
            while (fgets(line, (int)sizeof(line), fp) != NULL) {
                if (strncmp(line, "VmRSS:", 6U) == 0) {
                    (void)sscanf(line + 6, "%ld", &rss);
                }
            }
            (void)fclose(fp);
        }
        *cpuTicks = utime + stime;
        *memKb = rss;
    }
    return status;
}

void updateResourceUsage(ProcessTable *table)
{
    time_t now = time(NULL);
    long ticksPerSec = sysconf(_SC_CLK_TCK);
    unsigned long ticks = 0UL;
    long mem = 0L;
    Service *svc;

    for (svc = (table != NULL) ? table->head : NULL; svc != NULL; svc = svc->next) {
        if ((svc->pid > 0) && (readProcStats(svc->pid, &ticks, &mem) == SUP_OK)) {
            time_t elapsed = now - svc->lastSampleTime;

            if ((svc->lastSampleTime != 0) && (elapsed > 0) && (ticksPerSec > 0L) && (ticks >= svc->prevCpuTicks)) {
                svc->cpuPercent = ((double)(ticks - svc->prevCpuTicks) * 100.0) /
                                  ((double)ticksPerSec * (double)elapsed);
            }
            if ((svc->lastSampleTime == 0) || (elapsed > 0)) {
                svc->prevCpuTicks = ticks;
                svc->lastSampleTime = now;
            }
            svc->memKb = mem;
            if ((svc->cfg.memLimitKb > 0L) && (mem > svc->cfg.memLimitKb)) {
                if (svc->memWarned == 0) {
                    logMsg(LOG_WARN, svc, "W002 memory limit exceeded: %ld KB > %ld KB", mem, svc->cfg.memLimitKb);
                    svc->memWarned = 1;
                }
            } else {
                svc->memWarned = 0;
            }
        }
    }
}
