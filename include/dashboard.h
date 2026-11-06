/**
 * @file dashboard.h
 * @brief Status Dashboard Module (Level 4) and the start-up summary report.
 */
#ifndef SUP_DASHBOARD_H
#define SUP_DASHBOARD_H

#include <stdio.h>
#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Prints the dashboard to stdout, or to ctx->statusPath in daemon mode.
 *  Returns SUP_OK or ERR_FILE. */
int showDashboard(const SupervisorContext *ctx, const ProcessTable *table);
/** Prints one service row. */
void printServiceRow(FILE *out, const Service *svc);
/** Prints the start-up configuration summary (Report 3). */
void printStartupSummary(FILE *out, const SupervisorContext *ctx, Service *const order[], int count);
/** Writes seconds as HH:MM:SS. */
void formatDuration(long seconds, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SUP_DASHBOARD_H */
