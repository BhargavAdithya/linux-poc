/**
 * @file test_shim.c
 * @brief Bridge implementation: resets module state before every test and
 *        provides the file, process and output helpers the tests use.
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "test_shim.h"

static char g_dir[64];
static char g_fileText[262144];
static char g_pathText[4][MAX_PATH];
static int g_pathSlot = 0;
static char g_demoPath[MAX_PATH] = "";
static char g_command[MAX_CMD];
static char g_captureText[65536];
static char g_capturePath[MAX_PATH];
static int g_savedStdout = -1;

/* ------------------------------------------------------------ fixture */
static void removeTestDir(void)
{
    DIR *d = opendir(g_dir);
    struct dirent *ent;
    char path[MAX_PATH + 300];

    if (d != NULL) {
        ent = readdir(d);
        while (ent != NULL) {
            if ((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0)) {
                (void)snprintf(path, sizeof(path), "%s/%s", g_dir, ent->d_name);
                (void)remove(path);
            }
            ent = readdir(d);
        }
        (void)closedir(d);
    }
    (void)rmdir(g_dir);
}

void test_reset_system(void)
{
    const int signals[] = { SIGCHLD, SIGTERM, SIGINT, SIGUSR1 };
    size_t i;

    for (i = 0U; i < (sizeof(signals) / sizeof(signals[0])); i++) {
        (void)signal(signals[i], SIG_DFL);
    }
    gChildExited = 0;
    gStopRequested = 0;
    gStatusRequested = 0;
    gStartCount = 0;
    ptInit(&gTable);

    (void)snprintf(g_dir, sizeof(g_dir), "%s", "/tmp/sup_test_XXXXXX");
    if (mkdtemp(g_dir) == NULL) {
        (void)fprintf(stderr, "test_reset_system: mkdtemp failed\n");
    }
    (void)memset(&gCtx, 0, sizeof(gCtx));
    (void)snprintf(gCtx.configPath, sizeof(gCtx.configPath), "%s", testPath("supervisor.conf"));
    (void)snprintf(gCtx.logPath, sizeof(gCtx.logPath), "%s", testPath("test.log"));
    (void)snprintf(gCtx.pidPath, sizeof(gCtx.pidPath), "%s", testPath("supervisor.pid"));
    (void)snprintf(gCtx.statusPath, sizeof(gCtx.statusPath), "%s", testPath("supervisor.status"));
    gCtx.startTime = time(NULL);
    (void)logInit(gCtx.logPath, 0);
}

void test_cleanup_system(void)
{
    Service *svc;
    int status;

    for (svc = gTable.head; svc != NULL; svc = svc->next) {
        if (svc->pid > 0) {
            (void)kill(svc->pid, SIGKILL);
            (void)waitpid(svc->pid, &status, 0);
            svc->pid = 0;
        }
    }
    while (waitpid((pid_t)-1, &status, WNOHANG) > 0) {
        /* reap helpers */
    }
    ptFree(&gTable);
    gStartCount = 0;
    logClose();
    removeTestDir();
}

/* ------------------------------------------------------------- files */
const char *testDir(void)
{
    return g_dir;
}

const char *testPath(const char *name)
{
    char *slot = g_pathText[g_pathSlot];

    g_pathSlot = (g_pathSlot + 1) % 4;
    (void)snprintf(slot, MAX_PATH, "%s/%s", g_dir, name);
    return slot;
}

void writeFile(const char *name, const char *text)
{
    FILE *fp = fopen(testPath(name), "w");

    if (fp != NULL) {
        (void)fputs(text, fp);
        (void)fclose(fp);
    }
}

const char *readFile(const char *name)
{
    FILE *fp = fopen(testPath(name), "r");
    size_t n = 0U;

    g_fileText[0] = '\0';
    if (fp != NULL) {
        n = fread(g_fileText, 1U, sizeof(g_fileText) - 1U, fp);
        g_fileText[n] = '\0';
        (void)fclose(fp);
    }
    return g_fileText;
}

const char *logText(void)
{
    return readFile("test.log");
}

int fileExists(const char *name)
{
    return (access(testPath(name), F_OK) == 0) ? 1 : 0;
}

int fileMode(const char *name)
{
    struct stat st;
    int mode = -1;

    if (stat(testPath(name), &st) == 0) {
        mode = (int)(st.st_mode & 0777U);
    }
    return mode;
}

int countOccurrences(const char *text, const char *part)
{
    int count = 0;
    const char *p = text;
    size_t len = strlen(part);

    if (len > 0U) {
        p = strstr(p, part);
        while (p != NULL) {
            count++;
            p = strstr(p + len, part);
        }
    }
    return count;
}

/* ---------------------------------------------------------- services */
const char *demoPath(void)
{
    if (g_demoPath[0] == '\0') {
        if (realpath("services/demo_service", g_demoPath) == NULL) {
            (void)snprintf(g_demoPath, sizeof(g_demoPath), "%s", "services/demo_service");
        }
    }
    return g_demoPath;
}

const char *demoCommand(const char *name, const char *extra)
{
    (void)snprintf(g_command, sizeof(g_command), "%s %s %s", demoPath(), name, (extra != NULL) ? extra : "");
    return g_command;
}

ServiceConfig makeConfig(const char *name, const char *command)
{
    ServiceConfig cfg;

    initServiceConfig(&cfg);
    (void)snprintf(cfg.name, sizeof(cfg.name), "%s", name);
    (void)snprintf(cfg.command, sizeof(cfg.command), "%s", command);
    return cfg;
}

Service *addService(const char *name, const char *command)
{
    ServiceConfig cfg = makeConfig(name, command);

    return ptAdd(&gTable, &cfg);
}

void sleepMs(int ms)
{
    (void)poll(NULL, 0U, ms);
}

void pumpOnce(void)
{
    reapChildren(&gCtx, &gTable);
    checkHeartbeats(&gTable);
    updateResourceUsage(&gTable);
    processPendingRestarts(&gTable);
}

int pumpUntil(Service *svc, ServiceState state, int timeoutMs)
{
    int waited = 0;

    pumpOnce();
    while ((svc->state != state) && (waited < timeoutMs)) {
        sleepMs(50);
        waited += 50;
        pumpOnce();
    }
    return (svc->state == state) ? 1 : 0;
}

void pumpFor(int ms)
{
    int waited = 0;

    while (waited < ms) {
        pumpOnce();
        sleepMs(50);
        waited += 50;
    }
    pumpOnce();
}

int countZombieChildren(void)
{
    DIR *d = opendir("/proc");
    struct dirent *ent;
    char path[300];
    char line[512];
    int zombies = 0;
    int ppid;
    char state;
    const char *after;
    FILE *fp;

    if (d != NULL) {
        ent = readdir(d);
        while (ent != NULL) {
            if ((ent->d_name[0] >= '0') && (ent->d_name[0] <= '9')) {
                (void)snprintf(path, sizeof(path), "/proc/%s/stat", ent->d_name);
                fp = fopen(path, "r");
                if (fp != NULL) {
                    if (fgets(line, (int)sizeof(line), fp) != NULL) {
                        after = strrchr(line, ')');
                        if ((after != NULL) && (sscanf(after + 1, " %c %d", &state, &ppid) == 2) &&
                            (state == 'Z') && (ppid == (int)getpid())) {
                            zombies++;
                        }
                    }
                    (void)fclose(fp);
                }
            }
            ent = readdir(d);
        }
        (void)closedir(d);
    }
    return zombies;
}

pid_t sendSignalLater(int sig, int delayMs)
{
    pid_t parent = getpid();
    pid_t pid = fork();

    if (pid == 0) {
        sleepMs(delayMs);
        (void)kill(parent, sig);
        _exit(0);
    }
    return pid;
}

/* ------------------------------------------------------ stdout capture */
void captureStdoutBegin(void)
{
    int fd;

    (void)fflush(stdout);
    (void)snprintf(g_capturePath, sizeof(g_capturePath), "%s", testPath("stdout.txt"));
    fd = open(g_capturePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_savedStdout = dup(STDOUT_FILENO);
    if (fd >= 0) {
        (void)dup2(fd, STDOUT_FILENO);
        (void)close(fd);
    }
}

const char *captureStdoutEnd(void)
{
    FILE *fp;
    size_t n = 0U;

    (void)fflush(stdout);
    if (g_savedStdout >= 0) {
        (void)dup2(g_savedStdout, STDOUT_FILENO);
        (void)close(g_savedStdout);
        g_savedStdout = -1;
    }
    g_captureText[0] = '\0';
    fp = fopen(g_capturePath, "r");
    if (fp != NULL) {
        n = fread(g_captureText, 1U, sizeof(g_captureText) - 1U, fp);
        g_captureText[n] = '\0';
        (void)fclose(fp);
    }
    return g_captureText;
}
