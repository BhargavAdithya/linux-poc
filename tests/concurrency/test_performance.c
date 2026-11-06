/**
 * @file test_performance.c
 * @brief Measures the supervisor's reaction times against the HLD targets:
 *
 *   1. Crash detection   - service crash -> supervisor sees it   (target < 1 s)
 *   2. Restart latency   - restart delay elapsed -> new process  (target < 100 ms)
 *   3. Heartbeat loss    - last heartbeat -> SIGKILL             (target < timeout + 1 s)
 *
 * Each measurement runs 5 times using the same steps as runEventLoop():
 * poll() with TICK_MS, ended early by SIGCHLD.
 *
 *   make process-performance
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../../include/config.h"
#include "../../include/launcher.h"
#include "../../include/logger.h"
#include "../../include/monitor.h"
#include "../../include/proctable.h"
#include "../../include/restart.h"
#include "../../include/supervisor.h"

#define RUNS 5

static double nowMs(void)
{
    struct timespec t;

    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return ((double)t.tv_sec * 1000.0) + ((double)t.tv_nsec / 1000000.0);
}

/* One pass of the event loop: sleep until a signal or the tick, then work. */
static void loopOnce(void)
{
    (void)poll(NULL, 0U, TICK_MS);
    gChildExited = 0;
    reapChildren(&gCtx, &gTable);
    checkHeartbeats(&gTable);
    processPendingRestarts(&gTable);
}

/* Stops the service of the previous run so no process is left behind. */
static void stopAll(void)
{
    Service *svc;

    for (svc = gTable.head; svc != NULL; svc = svc->next) {
        if (svc->pid > 0) {
            (void)kill(svc->pid, SIGKILL);
            (void)waitpid(svc->pid, NULL, 0);
            svc->pid = 0;
        }
    }
    ptFree(&gTable);
}

static Service *newService(const char *command, int heartbeat)
{
    ServiceConfig cfg;

    stopAll();
    initServiceConfig(&cfg);
    (void)snprintf(cfg.name, sizeof(cfg.name), "%s", "perf");
    (void)snprintf(cfg.command, sizeof(cfg.command), "%s", command);
    cfg.heartbeatTimeout = heartbeat;
    cfg.restartDelay = 1;
    return ptAdd(&gTable, &cfg);
}

/* crash-after 0: the process crashes right after exec. */
static double crashDetectionMs(void)
{
    Service *svc = newService("services/demo_service perf --crash-after 0", 0);
    double start = nowMs();

    (void)launchService(svc);
    while (svc->state != SVC_RESTARTING) {
        loopOnce();
    }
    return nowMs() - start;
}

static double restartLatencyMs(void)
{
    Service *svc = newService("services/demo_service perf", 0);
    struct timespec t;
    double due;

    svc->state = SVC_RESTARTING;
    svc->nextRestartTime = time(NULL) + 1;
    (void)clock_gettime(CLOCK_REALTIME, &t);
    /* wall-clock moment the delay ends, converted to the monotonic clock */
    due = nowMs() + (((double)svc->nextRestartTime - (double)t.tv_sec) * 1000.0) - ((double)t.tv_nsec / 1000000.0);
    while (svc->pid == 0) {
        (void)poll(NULL, 0U, 10);
        processPendingRestarts(&gTable);
    }
    return nowMs() - due;
}

static double heartbeatLossMs(void)
{
    Service *svc = newService("services/demo_service perf --hang-after 0", 1);
    double start;

    (void)launchService(svc);
    start = nowMs();
    while (svc->state != SVC_RESTARTING) {
        loopOnce();
    }
    return nowMs() - start;
}

static void report(const char *what, double (*measure)(void), double targetMs)
{
    double sum = 0.0;
    double worst = 0.0;
    double value;
    int i;

    for (i = 0; i < RUNS; i++) {
        value = measure();
        sum += value;
        worst = (value > worst) ? value : worst;
    }
    (void)printf("%-22s %10.1f %10.1f %10.0f   %s\n", what, sum / RUNS, worst, targetMs,
                 (worst < targetMs) ? "PASS" : "FAIL");
}

int main(void)
{
    char log[] = "/tmp/perf_XXXXXX";
    int fd = mkstemp(log);

    if (fd >= 0) {
        (void)close(fd);
        (void)logInit(log, 0);
    }
    (void)installSignalHandlers();
    ptInit(&gTable);
    (void)printf("%-22s %10s %10s %10s\n", "Measurement (ms)", "average", "worst", "target");
    report("Crash detection", crashDetectionMs, 1000.0);
    report("Restart latency", restartLatencyMs, 100.0);
    report("Heartbeat loss (1 s)", heartbeatLossMs, 2000.0 + 1000.0);
    (void)printf("\nHeartbeat target = timeout (1 s) + up to 1 s for the first missing beat + 1 s tick.\n");
    stopAll();
    logClose();
    return 0;
}
