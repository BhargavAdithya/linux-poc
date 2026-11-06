/**
 * @file monitor.h
 * @brief Monitor Module: child exits (waitpid), heartbeats and procfs usage (Level 2).
 */
#ifndef SUP_MONITOR_H
#define SUP_MONITOR_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reaps every exited child with waitpid(WNOHANG) and classifies the exit:
 *  shutdown, intentional stop (dependent restart), normal exit or crash. */
void reapChildren(const SupervisorContext *ctx, ProcessTable *table);
/** Promotes STARTING services to RUNNING, drains heartbeat pipes and kills
 *  services whose heartbeat timed out (E010). */
void checkHeartbeats(ProcessTable *table);
/** Updates CPU % and memory of every running service; logs W002 over limit. */
void updateResourceUsage(ProcessTable *table);
/** Reads utime+stime (clock ticks) and VmRSS (KB) of pid from /proc.
 *  Returns SUP_OK or ERR_FILE. */
int readProcStats(pid_t pid, unsigned long *cpuTicks, long *memKb);

#ifdef __cplusplus
}
#endif

#endif /* SUP_MONITOR_H */
