/**
 * @file dashboard.c
 * @brief Status Dashboard Module: status of every service on the terminal
 *        (foreground) or in supervisor.status (daemon), and the start-up
 *        configuration summary.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "dashboard.h"
#include "logger.h"
#include "proctable.h"

#define RULE "=================================================================================="
#define LINE "----------------------------------------------------------------------------------"

void formatDuration(long seconds, char *buf, size_t size)
{
    long s = (seconds < 0L) ? 0L : seconds;

    (void)snprintf(buf, size, "%02ld:%02ld:%02ld", s / 3600L, (s % 3600L) / 60L, s % 60L);
}

void printServiceRow(FILE *out, const Service *svc)
{
    char restarts[16];

    (void)snprintf(restarts, sizeof(restarts), "%d/%d", svc->restartCount, svc->cfg.maxRestarts);
    if (svc->pid > 0) {
        char uptime[24];

        formatDuration((long)(time(NULL) - svc->startTime), uptime, sizeof(uptime));
        (void)fprintf(out, "%-18s %7d  %-11s %-9s %-9s %6.1f %9ld\n", svc->cfg.name, (int)svc->pid,
                      stateToString(svc->state), restarts, uptime, svc->cpuPercent, svc->memKb);
    } else {
        (void)fprintf(out, "%-18s %7s  %-11s %-9s %-9s %6s %9s\n", svc->cfg.name, "-",
                      stateToString(svc->state), restarts, "--", "--", "--");
    }
}

static void writeDashboard(FILE *out, const SupervisorContext *ctx, const ProcessTable *table)
{
    char stamp[24];
    char uptime[24];
    int running = 0;
    int restarting = 0;
    int failed = 0;
    const Service *svc;

    getTimestamp(stamp, sizeof(stamp));
    formatDuration((long)(time(NULL) - ctx->startTime), uptime, sizeof(uptime));
    (void)fprintf(out, "%s\n", RULE);
    (void)fprintf(out, "ADAS ECU PROCESS SUPERVISOR - STATUS DASHBOARD          %s\n", stamp);
    (void)fprintf(out, "Supervisor PID : %d   Uptime : %s\n", (int)getpid(), uptime);
    (void)fprintf(out, "%s\n", RULE);
    (void)fprintf(out, "%-18s %7s  %-11s %-9s %-9s %6s %9s\n", "NAME", "PID", "STATE", "RESTARTS",
                  "UPTIME", "CPU%", "MEM(KB)");
    (void)fprintf(out, "%s\n", LINE);
    for (svc = table->head; svc != NULL; svc = svc->next) {
        printServiceRow(out, svc);
        if ((svc->state == SVC_RUNNING) || (svc->state == SVC_STARTING)) {
            running++;
        } else if ((svc->state == SVC_RESTARTING) || (svc->state == SVC_CRASHED)) {
            restarting++;
        } else if (svc->state == SVC_FAILED) {
            failed++;
        } else {
            /* stopped */
        }
    }
    (void)fprintf(out, "%s\n", LINE);
    (void)fprintf(out, "Total : %d   Running : %d   Restarting : %d   Failed : %d\n",
                  table->count, running, restarting, failed);
    (void)fprintf(out, "%s\n", RULE);
}

/* Daemon mode: the dashboard replaces the content of the status file (mode 0644). */
static int writeStatusFile(const SupervisorContext *ctx, const ProcessTable *table)
{
    int status = SUP_OK;
    FILE *out = NULL;
    int fd = open(ctx->statusPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, (mode_t)0644);

    if (fd >= 0) {
        out = fdopen(fd, "w");
        if (out == NULL) {
            (void)close(fd);
        }
    }
    if (out == NULL) {
        logError(NULL, ERR_FILE, ctx->statusPath);
        status = ERR_FILE;
    } else {
        writeDashboard(out, ctx, table);
        (void)fclose(out);
        logMsg(LOG_INFO, NULL, "status dashboard written to %s", ctx->statusPath);
    }
    return status;
}

int showDashboard(const SupervisorContext *ctx, const ProcessTable *table)
{
    int status = SUP_OK;

    if ((ctx == NULL) || (table == NULL)) {
        status = ERR_FILE;
    } else if (ctx->daemonMode != 0) {
        status = writeStatusFile(ctx, table);
    } else {
        writeDashboard(stdout, ctx, table);
        (void)fflush(stdout);
    }
    return status;
}

/* cppcheck-suppress constParameter ; Service *order[] is passed in, C cannot add an inner const */
void printStartupSummary(FILE *out, const SupervisorContext *ctx, Service *const order[], int count)
{
    char deps[MAX_DEPS * (MAX_NAME + 2)];
    int i;
    int d;

    (void)fprintf(out, "%s\n", RULE);
    (void)fprintf(out, "ADAS ECU PROCESS SUPERVISOR v1.0 - START-UP CONFIGURATION SUMMARY\n");
    (void)fprintf(out, "%s\n", RULE);
    (void)fprintf(out, "Config File     : %s\n", ctx->configPath);
    (void)fprintf(out, "Mode            : %s\n", (ctx->daemonMode != 0) ? "Daemon" : "Foreground");
    (void)fprintf(out, "Services Loaded : %d\n", count);
    (void)fprintf(out, "%s\n", LINE);
    (void)fprintf(out, "%-5s %-18s %-30s %-8s %-6s %s\n", "ORDER", "SERVICE", "DEPENDS ON", "RESTARTS", "DELAY",
                  "HEARTBEAT");
    for (i = 0; i < count; i++) {
        deps[0] = '\0';
        for (d = 0; d < order[i]->cfg.depCount; d++) {
            if (d > 0) {
                (void)strncat(deps, ",", sizeof(deps) - strlen(deps) - 1U);
            }
            (void)strncat(deps, order[i]->cfg.dependsOn[d], sizeof(deps) - strlen(deps) - 1U);
        }
        (void)fprintf(out, "%-5d %-18s %-30s %-8d %-6d %s\n", i + 1, order[i]->cfg.name,
                      (deps[0] != '\0') ? deps : "-", order[i]->cfg.maxRestarts, order[i]->cfg.restartDelay,
                      (order[i]->cfg.heartbeatTimeout > 0) ? "yes" : "no");
    }
    (void)fprintf(out, "%s\n", LINE);
    (void)fprintf(out, "Validation      : OK\n");
    (void)fprintf(out, "%s\n", RULE);
    (void)fflush(out);
}
