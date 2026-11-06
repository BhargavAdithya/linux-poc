/**
 * @file fault_common.h
 * @brief Shared setup for the fault-injection programs: a log file in /tmp
 *        and a process table with two services, all in memory.
 */
#ifndef FAULT_COMMON_H
#define FAULT_COMMON_H

#include <stdio.h>
#include <string.h>
#include "../include/config.h"
#include "../include/launcher.h"
#include "../include/logger.h"
#include "../include/monitor.h"
#include "../include/proctable.h"
#include "../include/supervisor.h"

/** Unbuffered output so every line is visible even if the program crashes. */
static inline void faultInit(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)logInit("/tmp/fault_supervisor.log", 0);
}

static inline void faultSetup(void)
{
    ServiceConfig cfg;

    faultInit();
    ptInit(&gTable);
    initServiceConfig(&cfg);
    (void)snprintf(cfg.name, sizeof(cfg.name), "%s", "camera_service");
    (void)snprintf(cfg.command, sizeof(cfg.command), "%s", "/bin/sleep 30");
    (void)ptAdd(&gTable, &cfg);
    (void)snprintf(cfg.name, sizeof(cfg.name), "%s", "radar_service");
    (void)ptAdd(&gTable, &cfg);
}

#endif /* FAULT_COMMON_H */
