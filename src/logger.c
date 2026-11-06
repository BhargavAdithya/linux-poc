/**
 * @file logger.c
 * @brief Logger Module: one time-stamped line per event, written to
 *        supervisor.log (mode 0640) and rotated to supervisor.log.1 at 1 MB.
 *
 * Line format:  [YYYY-MM-DD HH:MM:SS] [LEVEL] [service:pid] message
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "logger.h"

static FILE *gLogFile = NULL;
static int gLogToConsole = 0;
static char gLogPath[MAX_PATH] = "";

/* Opens path for appending with mode 0640 and close-on-exec, so that the
 * log file descriptor is never inherited by a service. */
static FILE *openLogFile(const char *path)
{
    FILE *fp = NULL;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, (mode_t)0640);

    if (fd >= 0) {
        fp = fdopen(fd, "a");
        if (fp == NULL) {
            (void)close(fd);
        }
    }
    return fp;
}

int logInit(const char *path, int toConsole)
{
    int status = SUP_OK;

    logClose();
    gLogToConsole = toConsole;
    if ((path == NULL) || (path[0] == '\0') || (strlen(path) >= sizeof(gLogPath))) {
        status = ERR_FILE;
    } else {
        (void)snprintf(gLogPath, sizeof(gLogPath), "%s", path);
        gLogFile = openLogFile(gLogPath);
        if (gLogFile == NULL) {
            status = ERR_FILE;
        }
    }
    return status;
}

int logIsOpen(void)
{
    return (gLogFile != NULL) ? 1 : 0;
}

void logClose(void)
{
    if (gLogFile != NULL) {
        (void)fclose(gLogFile);
        gLogFile = NULL;
    }
}

void getTimestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tmNow;

    if ((buf != NULL) && (size > 0U)) {
        if (localtime_r(&now, &tmNow) == NULL) {
            (void)snprintf(buf, size, "%s", "0000-00-00 00:00:00");
        } else if (strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tmNow) == 0U) {
            buf[0] = '\0';
        } else {
            /* timestamp written */
        }
    }
}

const char *logLevelName(LogLevel level)
{
    const char *name;

    switch (level) {
    case LOG_INFO:  name = "INFO "; break;
    case LOG_WARN:  name = "WARN "; break;
    case LOG_ERROR: name = "ERROR"; break;
    default:        name = "?????"; break;
    }
    return name;
}

void rotateLog(void)
{
    if ((gLogFile != NULL) && (ftell(gLogFile) >= LOG_MAX_SIZE)) {
        char backup[MAX_PATH + 4];

        (void)fclose(gLogFile);
        (void)snprintf(backup, sizeof(backup), "%s.1", gLogPath);
        (void)rename(gLogPath, backup);
        gLogFile = openLogFile(gLogPath);
    }
}

void logMsg(LogLevel level, const Service *svc, const char *fmt, ...)
{
    char stamp[24];
    char text[LINE_SIZE];
    char who[MAX_NAME + 16];
    va_list args;
    int savedErrno = errno;      /* callers may still need errno after logging */

    getTimestamp(stamp, sizeof(stamp));
    va_start(args, fmt);
    (void)vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (svc != NULL) {
        (void)snprintf(who, sizeof(who), "%s:%d", svc->cfg.name, (int)svc->pid);
    } else {
        (void)snprintf(who, sizeof(who), "supervisor:%d", (int)getpid());
    }

    rotateLog();
    if (gLogFile != NULL) {
        (void)fprintf(gLogFile, "[%s] [%s] [%s] %s\n", stamp, logLevelName(level), who, text);
        (void)fflush(gLogFile);
    } else {
        (void)fprintf(stderr, "[%s] [%s] [%s] %s\n", stamp, logLevelName(level), who, text);
    }
    if (gLogToConsole != 0) {
        (void)printf("[%s] [%s] [%s] %s\n", stamp, logLevelName(level), who, text);
        (void)fflush(stdout);
    }
    errno = savedErrno;
}

void logError(const Service *svc, SupStatus code, const char *detail)
{
    logMsg(LOG_ERROR, svc, "%s %s%s%s", errorCodeText(code), errorMessage(code),
           (detail != NULL) ? " | " : "", (detail != NULL) ? detail : "");
}

const char *errorCodeText(SupStatus code)
{
    static const char *const codes[] = {
        "E000", "E001", "E002", "E003", "E004", "E005",
        "E006", "E007", "E008", "E009", "E010", "E011"
    };
    const char *text = "E000";

    if ((code >= SUP_OK) && (code <= ERR_MAX_RESTARTS)) {
        text = codes[code];
    }
    return text;
}

const char *errorMessage(SupStatus code)
{
    const char *msg;

    switch (code) {
    case SUP_OK:           msg = "Success"; break;
    case ERR_CONFIG_FILE:  msg = "Configuration file not found or unreadable"; break;
    case ERR_CONFIG_ENTRY: msg = "Invalid configuration entry"; break;
    case ERR_DUPLICATE:    msg = "Duplicate service name"; break;
    case ERR_DEPENDENCY:   msg = "Unknown or circular dependency"; break;
    case ERR_MEMORY:       msg = "Memory allocation failure"; break;
    case ERR_FORK:         msg = "fork() failure"; break;
    case ERR_EXEC:         msg = "execvp() failure"; break;
    case ERR_PERMISSION:   msg = "Permission switch failure"; break;
    case ERR_FILE:         msg = "Log / PID / status file error"; break;
    case ERR_HEARTBEAT:    msg = "Heartbeat timeout"; break;
    case ERR_MAX_RESTARTS: msg = "Maximum restart attempts reached - service FAILED"; break;
    case ERR_USAGE:        msg = "Invalid command-line option"; break;
    default:               msg = "Unknown error"; break;
    }
    return msg;
}
