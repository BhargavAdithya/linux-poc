/**
 * @file proctable.h
 * @brief Process Table Module: linked list of Service nodes.
 */
#ifndef SUP_PROCTABLE_H
#define SUP_PROCTABLE_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ProcessTable gTable;

/** Empties the table header (does not free nodes; use ptFree for that). */
void ptInit(ProcessTable *table);
/** Appends a new node holding a copy of cfg. Returns NULL on allocation failure. */
Service *ptAdd(ProcessTable *table, const ServiceConfig *cfg);
/** Returns the first service with this name, or NULL. */
Service *ptFindByName(ProcessTable *table, const char *name);
/** Returns the service currently running with this PID, or NULL. */
Service *ptFindByPid(ProcessTable *table, pid_t pid);
/** Frees every node, closes heartbeat pipes and empties the table. */
void ptFree(ProcessTable *table);
/** Returns "STARTING", "RUNNING", "CRASHED", "RESTARTING", "FAILED" or "STOPPED". */
const char *stateToString(ServiceState state);
/** Returns 1 when svc lists name in depends_on. */
int dependsOnService(const Service *svc, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SUP_PROCTABLE_H */
