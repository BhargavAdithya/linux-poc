/**
 * @file launcher.c
 * @brief Launcher Module: creates each service with fork(), sets up the
 *        heartbeat pipe, applies group/user/umask (preserve permissions)
 *        and runs the command with execvp().
 */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "launcher.h"
#include "logger.h"

/* Small wrapper so that GDB can inject a fork() failure (debug/gdb_inject_fork.gdb). */
static pid_t forkService(void)
{
    return fork();
}

int buildArgv(char *cmd, char *argv[], int max)
{
    int argc = 0;
    char *saveptr = NULL;
    char *token;

    if ((cmd == NULL) || (argv == NULL) || (max <= 0)) {
        argc = -1;
    } else {
        token = strtok_r(cmd, " \t", &saveptr);
        while ((token != NULL) && (argc >= 0)) {
            if (argc >= max) {
                argc = -1;                 /* too many arguments */
            } else {
                argv[argc] = token;
                argc++;
                token = strtok_r(NULL, " \t", &saveptr);
            }
        }
        if (argc == 0) {
            argc = -1;                     /* empty command */
        }
        if (argc > 0) {
            argv[argc] = NULL;
        }
    }
    return argc;
}

int applyPermissions(const ServiceConfig *cfg)
{
    int status = SUP_OK;
    const struct group *gr;
    const struct passwd *pw;
    gid_t gid = 0;

    /* Order matters: group first, then supplementary groups, user last
     * (after setuid() the process can no longer change its group). */
    if (cfg->group[0] != '\0') {
        gr = getgrnam(cfg->group);
        if ((gr == NULL) || (setgid(gr->gr_gid) != 0)) {
            status = ERR_PERMISSION;
        } else {
            gid = gr->gr_gid;
            if ((cfg->user[0] == '\0') && (setgroups(1U, &gid) != 0)) {
                status = ERR_PERMISSION;
            }
        }
    }
    if ((status == SUP_OK) && (cfg->user[0] != '\0')) {
        pw = getpwnam(cfg->user);
        if (pw == NULL) {
            status = ERR_PERMISSION;
        } else {
            if (cfg->group[0] == '\0') {
                gid = pw->pw_gid;
                if (setgid(gid) != 0) {
                    status = ERR_PERMISSION;
                }
            }
            if ((status == SUP_OK) && ((initgroups(pw->pw_name, gid) != 0) || (setuid(pw->pw_uid) != 0))) {
                status = ERR_PERMISSION;
            }
        }
    }
    if ((status == SUP_OK) && (cfg->umaskValue >= 0)) {
        (void)umask((mode_t)cfg->umaskValue);
    }
    return status;
}

/* Child side after fork(): never returns. */
static void runChild(const Service *svc, int hbRead, int hbWrite) __attribute__((noreturn));
static void runChild(const Service *svc, int hbRead, int hbWrite)
{
    char cmd[MAX_CMD];
    char *argv[MAX_ARGS + 1];

    /* Own process group: Ctrl+C on the supervisor's terminal does not hit
     * the services; the supervisor stops them in reverse start order. */
    (void)setpgid(0, 0);

    if (hbWrite >= 0) {
        (void)close(hbRead);
        if (hbWrite != HEARTBEAT_FD) {
            (void)dup2(hbWrite, HEARTBEAT_FD);
            (void)close(hbWrite);
        }
        (void)setenv("HEARTBEAT_FD", "3", 1);
    }
    if (applyPermissions(&svc->cfg) != SUP_OK) {
        _exit(EXIT_PERM_FAIL);
    }
    (void)snprintf(cmd, sizeof(cmd), "%s", svc->cfg.command);
    if (buildArgv(cmd, argv, MAX_ARGS) > 0) {
        (void)execvp(argv[0], argv);
    }
    _exit(EXIT_EXEC_FAIL);
}

int launchService(Service *svc)
{
    int status = SUP_OK;
    int hb[2] = { -1, -1 };

    if (svc == NULL) {
        status = ERR_FORK;
    } else {
        int havePipe = 0;

        if (svc->cfg.heartbeatTimeout > 0) {
            if (pipe(hb) != 0) {
                hb[0] = -1;
                hb[1] = -1;
                status = ERR_FORK;
            } else {
                havePipe = 1;
                /* The read end stays with the supervisor only. */
                (void)fcntl(hb[0], F_SETFD, FD_CLOEXEC);
            }
        }
        (void)fflush(NULL);              /* do not duplicate buffered output in the child */
        pid_t pid = (status == SUP_OK) ? forkService() : (pid_t)-1;
        if (pid == 0) {
            runChild(svc, hb[0], hb[1]);
        } else if (pid < 0) {
            if (havePipe == 1) {
                (void)close(hb[0]);
                (void)close(hb[1]);
            }
            logError(svc, ERR_FORK, strerror(errno));
            svc->pid = 0;
            svc->state = SVC_CRASHED;
            svc->lastStatus = -1;
            status = ERR_FORK;
        } else {
            if (havePipe == 1) {
                (void)close(hb[1]);
                (void)fcntl(hb[0], F_SETFL, O_NONBLOCK);
            }
            if (svc->hbReadFd >= 0) {
                (void)close(svc->hbReadFd);
            }
            svc->hbReadFd = hb[0];
            svc->pid = pid;
            svc->state = SVC_STARTING;
            svc->startTime = time(NULL);
            svc->lastHeartbeat = svc->startTime;
            svc->prevCpuTicks = 0UL;
            svc->lastSampleTime = 0;
            svc->cpuPercent = 0.0;
            svc->memKb = 0L;
            svc->memWarned = 0;
            logMsg(LOG_INFO, svc, "I002 service started: %s", svc->cfg.command);
        }
    }
    return status;
}

int launchAll(Service *order[], int count)
{
    int status = SUP_OK;
    int i;

    for (i = 0; i < count; i++) {
        if ((order[i] != NULL) && (order[i]->state != SVC_FAILED) && (order[i]->pid == 0)) {
            if (launchService(order[i]) != SUP_OK) {
                status = ERR_FORK;      /* the Restart Manager will retry it */
            }
        }
    }
    return status;
}
