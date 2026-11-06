/**
 * @file restart.h
 * @brief Restart Manager Module: restart policy, delay, dependents, FAILED (Level 3).
 */
#ifndef SUP_RESTART_H
#define SUP_RESTART_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Logs the crash, then schedules a restart (RESTARTING) or marks FAILED. */
void handleCrash(Service *svc, int status);
/** Handles failed launches and relaunches services whose delay has elapsed. */
void processPendingRestarts(ProcessTable *table);
/** Stops running services that depend on parent so they restart after it. */
void restartDependents(ProcessTable *table, const Service *parent);
/** Sets FAILED and logs E011. */
void markFailed(Service *svc);
/** Writes a short text for a waitpid() status ("exit code 1", "signal 11", ...). */
void describeExit(int status, char *buf, int size);

#ifdef __cplusplus
}
#endif

#endif /* SUP_RESTART_H */
