/**
 * @file restart.c
 * @brief Restart Manager Module: applies the restart policy (attempts and
 *        delay from the configuration), restarts dependent services and
 *        marks a service FAILED when its restart limit is reached.
 */
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include "launcher.h"
#include "logger.h"
#include "proctable.h"
#include "restart.h"

void describeExit(int status, char *buf, int size)
{
    size_t len = (size > 0) ? (size_t)size : 0U;

    if (len > 0U) {
        if (status == -1) {
            (void)snprintf(buf, len, "%s", "launch failed: fork()");
        } else if ((WIFEXITED(status) != 0) && (WEXITSTATUS(status) == EXIT_EXEC_FAIL)) {
            (void)snprintf(buf, len, "%s %s", errorCodeText(ERR_EXEC), "exit code 127: command could not be executed");
        } else if ((WIFEXITED(status) != 0) && (WEXITSTATUS(status) == EXIT_PERM_FAIL)) {
            (void)snprintf(buf, len, "%s %s", errorCodeText(ERR_PERMISSION), "exit code 126: user/group switch failed");
        } else if (WIFEXITED(status) != 0) {
            (void)snprintf(buf, len, "exit code %d", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status) != 0) {
            (void)snprintf(buf, len, "signal %d", WTERMSIG(status));
        } else {
            (void)snprintf(buf, len, "status %d", status);
        }
    }
}

void markFailed(Service *svc)
{
    svc->state = SVC_FAILED;
    logMsg(LOG_ERROR, svc, "%s service FAILED (%d/%d restarts used)",
           errorCodeText(ERR_MAX_RESTARTS), svc->restartCount, svc->cfg.maxRestarts);
    svc->pid = 0;
}

void handleCrash(Service *svc, int status)
{
    if (svc != NULL) {
        char reason[96];

        describeExit(status, reason, (int)sizeof(reason));
        logMsg(LOG_ERROR, svc, "service crashed (%s)", reason);
        svc->lastStatus = status;
        svc->state = SVC_CRASHED;
        if (svc->restartCount < svc->cfg.maxRestarts) {
            svc->restartCount++;
            svc->state = SVC_RESTARTING;
            svc->nextRestartTime = time(NULL) + (time_t)svc->cfg.restartDelay;
            logMsg(LOG_INFO, svc, "restart %d/%d scheduled in %d s",
                   svc->restartCount, svc->cfg.maxRestarts, svc->cfg.restartDelay);
            svc->pid = 0;
        } else {
            markFailed(svc);
        }
    }
}

void restartDependents(ProcessTable *table, const Service *parent)
{
    Service *svc;

    for (svc = (table != NULL) ? table->head : NULL; svc != NULL; svc = svc->next) {
        if ((svc != parent) && (svc->pid > 0) && (dependsOnService(svc, parent->cfg.name) == 1) &&
            ((svc->state == SVC_RUNNING) || (svc->state == SVC_STARTING))) {
            logMsg(LOG_INFO, svc, "stopping dependent of %s for restart", parent->cfg.name);
            svc->state = SVC_RESTARTING;      /* reapChildren() relaunches it at once */
            (void)kill(svc->pid, SIGTERM);
        }
    }
}

void processPendingRestarts(ProcessTable *table)
{
    time_t now = time(NULL);
    Service *svc;

    for (svc = (table != NULL) ? table->head : NULL; svc != NULL; svc = svc->next) {
        if ((svc->state == SVC_CRASHED) && (svc->pid == 0)) {
            handleCrash(svc, svc->lastStatus);     /* launch failed (fork) */
        }
        if ((svc->state == SVC_RESTARTING) && (svc->pid == 0) && (now >= svc->nextRestartTime)) {
            if (launchService(svc) == SUP_OK) {
                logMsg(LOG_INFO, svc, "I003 service restarted (restarts used %d/%d)",
                       svc->restartCount, svc->cfg.maxRestarts);
                restartDependents(table, svc);
            }
        }
    }
}
