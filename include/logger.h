/**
 * @file logger.h
 * @brief Logger Module: time-stamped log entries, rotation and error codes.
 */
#ifndef SUP_LOGGER_H
#define SUP_LOGGER_H

#include <stddef.h>
#include "supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

/** Opens (appends to) the log file with mode 0640. toConsole = 1 also
 *  echoes every entry to stdout. Returns SUP_OK or ERR_FILE; on ERR_FILE
 *  entries go to stderr instead. */
int logInit(const char *path, int toConsole);
/** Writes one line: [time] [LEVEL] [service:pid] message.
 *  svc == NULL means the supervisor itself. */
void logMsg(LogLevel level, const Service *svc, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/** Logs "E0xx <message> | detail" at ERROR level. */
void logError(const Service *svc, SupStatus code, const char *detail);
/** Renames the log to <log>.1 and starts a new one when it reaches LOG_MAX_SIZE. */
void rotateLog(void);
/** Closes the log file if open. */
void logClose(void);
/** Returns 1 when a log file is open. */
int logIsOpen(void);
/** Writes "YYYY-MM-DD HH:MM:SS" into buf. */
void getTimestamp(char *buf, size_t size);
/** Returns "INFO", "WARN" or "ERROR". */
const char *logLevelName(LogLevel level);
/** Returns "E001".."E011", or "E000" for other codes. */
const char *errorCodeText(SupStatus code);
/** Returns the message text for an error code. */
const char *errorMessage(SupStatus code);

#ifdef __cplusplus
}
#endif

#endif /* SUP_LOGGER_H */
