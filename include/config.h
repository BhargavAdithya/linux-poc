/**
 * @file config.h
 * @brief Config Reader Module: reads supervisor.conf, validates every entry
 *        and resolves the start order from the declared dependencies.
 *
 * File format (one block per service, '#' starts a comment):
 *
 *   [service]
 *   name=camera_service
 *   command=./services/demo_service camera_service
 *   max_restarts=3
 *   restart_delay=2
 *   depends_on=radar_service,gps_service
 *   user=adas
 *   group=adas
 *   umask=027
 *   heartbeat_timeout=5
 *   mem_limit_kb=51200
 */
#ifndef SUP_CONFIG_H
#define SUP_CONFIG_H

#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Service *gStartOrder[MAX_SERVICES];
extern int gStartCount;

/** Sets the default values of a new [service] block. */
void initServiceConfig(ServiceConfig *cfg);
/** Reads path and adds one table node per [service] block.
 *  Returns SUP_OK, ERR_CONFIG_FILE, ERR_CONFIG_ENTRY or ERR_MEMORY. */
int loadConfig(const char *path, ProcessTable *table);
/** Stores one "key=value" line into cfg. Returns SUP_OK or ERR_CONFIG_ENTRY. */
int parseServiceLine(const char *line, ServiceConfig *cfg);
/** Checks mandatory fields, duplicates, dependencies, users and groups.
 *  Returns SUP_OK or the first error found. */
int validateConfig(ProcessTable *table);
/** Orders services so that dependencies come first.
 *  Returns SUP_OK or ERR_DEPENDENCY (circular dependency). */
int resolveStartOrder(ProcessTable *table, Service *order[], int *count);

#ifdef __cplusplus
}
#endif

#endif /* SUP_CONFIG_H */
