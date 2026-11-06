/**
 * @file launcher.h
 * @brief Launcher Module: fork + permissions + execvp (Level 1 and Level 5).
 */
#ifndef SUP_LAUNCHER_H
#define SUP_LAUNCHER_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Launches every service of order[] that is not FAILED.
 *  Returns SUP_OK, or ERR_FORK when at least one launch failed. */
int launchAll(Service *order[], int count);
/** Creates the service process. On fork failure the service is left in
 *  SVC_CRASHED with lastStatus -1 and ERR_FORK is returned. */
int launchService(Service *svc);
/** Child side: applies group, user and umask. Returns SUP_OK or ERR_PERMISSION. */
int applyPermissions(const ServiceConfig *cfg);
/** Splits cmd (modified in place) on blanks into argv[0..max-1], NULL-terminated.
 *  Returns the argument count, or -1 when empty or more than max arguments. */
int buildArgv(char *cmd, char *argv[], int max);

#ifdef __cplusplus
}
#endif

#endif /* SUP_LAUNCHER_H */
