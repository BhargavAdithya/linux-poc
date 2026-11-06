/**
 * @file supervisor.h
 * @brief Shared constants, error codes, data structures and the Supervisor
 *        Core interface of the ADAS ECU Process Supervisor (Mini "init").
 */
#ifndef SUP_SUPERVISOR_H
#define SUP_SUPERVISOR_H

#include <signal.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- limits */
#define MAX_NAME              32
#define MAX_CMD               256
#define MAX_ARGS              16
#define MAX_DEPS              8
#define MAX_SERVICES          32
#define MAX_PATH              256
#define LINE_SIZE             512
#define TICK_MS               1000      /* event loop wakes at least once per second */
#define STOP_TIMEOUT_SEC      5         /* SIGTERM -> SIGKILL grace time on shutdown  */
#define START_GRACE_SEC       1         /* STARTING -> RUNNING after this many seconds */
#define HEARTBEAT_FD          3         /* fd number a service writes heartbeats to   */
#define LOG_MAX_SIZE          (1024L * 1024L)

/* -------------------------------------------------- configuration ranges */
#define DEFAULT_MAX_RESTARTS  3
#define DEFAULT_RESTART_DELAY 2
#define MAX_RESTART_LIMIT     10
#define MIN_RESTART_DELAY     1
#define MAX_RESTART_DELAY     60
#define MAX_HEARTBEAT_TIMEOUT 60
#define MAX_UMASK             0777

/* ------------------------------------------- child exit codes (launcher) */
#define EXIT_PERM_FAIL        126       /* setgid / setuid failed  -> E008 */
#define EXIT_EXEC_FAIL        127       /* execvp failed           -> E007 */

/* --------------------------------------------------------- default files */
#define DEFAULT_CONFIG_FILE   "supervisor.conf"
#define DEFAULT_LOG_FILE      "supervisor.log"
#define DEFAULT_PID_FILE      "supervisor.pid"
#define DEFAULT_STATUS_FILE   "supervisor.status"

/* ----------------------------------------------------------- error codes */
/* Plain int constants so that status values and int status variables
 * share one essential type. Codes 1..11 map to E001..E011 (LLD Appendix A). */
typedef int SupStatus;
#define SUP_OK                0
#define ERR_CONFIG_FILE       1    /* E001 configuration file not found     */
#define ERR_CONFIG_ENTRY      2    /* E002 invalid configuration entry      */
#define ERR_DUPLICATE         3    /* E003 duplicate service name           */
#define ERR_DEPENDENCY        4    /* E004 unknown or circular dependency   */
#define ERR_MEMORY            5    /* E005 memory allocation failure        */
#define ERR_FORK              6    /* E006 fork() failure                   */
#define ERR_EXEC              7    /* E007 execvp() failure                 */
#define ERR_PERMISSION        8    /* E008 permission switch failure        */
#define ERR_FILE              9    /* E009 log / PID / status file error    */
#define ERR_HEARTBEAT         10   /* E010 heartbeat timeout                */
#define ERR_MAX_RESTARTS      11   /* E011 maximum restart attempts reached */
#define ERR_USAGE             12   /* bad command-line option (internal)    */
#define SUP_HELP              13   /* -h given: print usage (internal)      */

/* ------------------------------------------------------- data structures */
/** Service states; the same states as the UML state diagram. */
typedef enum {
    SVC_STARTING,
    SVC_RUNNING,
    SVC_CRASHED,
    SVC_RESTARTING,
    SVC_FAILED,
    SVC_STOPPED
} ServiceState;

/** One [service] block of supervisor.conf. */
typedef struct {
    char name[MAX_NAME];
    char command[MAX_CMD];
    int maxRestarts;
    int restartDelay;
    char dependsOn[MAX_DEPS][MAX_NAME];
    int depCount;
    char user[MAX_NAME];
    char group[MAX_NAME];
    int umaskValue;              /* -1 = not set  */
    int heartbeatTimeout;        /* 0 = disabled  */
    long memLimitKb;             /* 0 = disabled  */
} ServiceConfig;

/** Run-time record of one service: a node of the process table. */
typedef struct Service {
    ServiceConfig cfg;
    pid_t pid;                   /* 0 when not running              */
    ServiceState state;
    int restartCount;
    int lastStatus;              /* raw waitpid() status, -1 = fork failed */
    time_t startTime;
    time_t lastHeartbeat;
    time_t nextRestartTime;
    int hbReadFd;                /* -1 when heartbeat is disabled   */
    unsigned long prevCpuTicks;
    time_t lastSampleTime;       /* 0 = no CPU sample taken yet     */
    double cpuPercent;
    long memKb;
    int memWarned;               /* W002 already logged for this run */
    struct Service *next;
} Service;

/** The process table: singly linked list in configuration order. */
typedef struct {
    Service *head;
    int count;
} ProcessTable;

/** Supervisor-wide settings. */
typedef struct {
    char configPath[MAX_PATH];
    char logPath[MAX_PATH];
    char pidPath[MAX_PATH];
    char statusPath[MAX_PATH];
    int daemonMode;
    int stopping;                /* set during shutdown */
    time_t startTime;
} SupervisorContext;

/* ------------------------------------------------------- global variables */
extern SupervisorContext gCtx;
extern volatile sig_atomic_t gChildExited;
extern volatile sig_atomic_t gStopRequested;
extern volatile sig_atomic_t gStatusRequested;

/* ------------------------------------------------ Supervisor Core module */
/** Fills ctx from -c <file>, -d, -h. Returns SUP_OK, SUP_HELP or ERR_USAGE. */
int parseArguments(int argc, char *argv[], SupervisorContext *ctx);
/** Prints the usage text. */
void printUsage(void);
/** Installs handleSignal() for SIGCHLD, SIGTERM, SIGINT and SIGUSR1. */
int installSignalHandlers(void);
/** Signal handler: only sets the matching flag (async-signal-safe). */
void handleSignal(int sig);
/** Moves the supervisor to the background (fork + setsid). */
int daemonize(void);
/** Writes the current PID to path (mode 0644). Returns SUP_OK or ERR_FILE. */
int writePidFile(const char *path);
/** Main loop: runs until gStopRequested is set. */
void runEventLoop(const SupervisorContext *ctx, ProcessTable *table);
/** Stops all services (SIGTERM, then SIGKILL), removes the PID file,
 *  frees the table and closes the log. */
void shutdownSupervisor(SupervisorContext *ctx, ProcessTable *table);
/** Complete program flow. Returns the process exit code (0 or 1). */
int runSupervisor(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* SUP_SUPERVISOR_H */
