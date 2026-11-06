/**
 * @file test_shim.h
 * @brief C/C++ bridge between the GoogleTest suite (test_supervisor.cpp)
 *        and the C modules. extern "C" stops C++ name mangling so GTest can
 *        link to the C functions. Also used by the CUnit runner.
 */
#ifndef TEST_SHIM_H
#define TEST_SHIM_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All module headers */
#include "../include/supervisor.h"
#include "../include/logger.h"
#include "../include/proctable.h"
#include "../include/config.h"
#include "../include/launcher.h"
#include "../include/monitor.h"
#include "../include/restart.h"
#include "../include/dashboard.h"

/* ---- lifecycle: called from the fixture's SetUp() / TearDown() ---- */
/** Clean state for every test: default signal handlers, cleared flags, an
 *  empty gTable, a fresh temporary folder, gCtx paths inside that folder and
 *  a fresh log file (no console echo). */
void test_reset_system(void);
/** Kills and reaps every service still running, frees the table, closes
 *  the log and deletes the temporary folder. */
void test_cleanup_system(void);

/* ---- files ---- */
const char *testDir(void);
const char *testPath(const char *name);
void writeFile(const char *name, const char *text);
/** Content of a file in the test folder ("" when missing). */
const char *readFile(const char *name);
/** Content of the test log. */
const char *logText(void);
int fileExists(const char *name);
/** Permission bits of a file in the test folder (e.g. 0640), -1 when missing. */
int fileMode(const char *name);
/** How many times part appears in text. */
int countOccurrences(const char *text, const char *part);

/* ---- services ---- */
/** Absolute path of services/demo_service. */
const char *demoPath(void);
/** "<demoPath> <name> <extra>" in a static buffer. */
const char *demoCommand(const char *name, const char *extra);
/** A config with defaults, the given name and command. */
ServiceConfig makeConfig(const char *name, const char *command);
/** ptAdd(&gTable, makeConfig(name, command)). */
Service *addService(const char *name, const char *command);
/** Runs one supervision pass: reap, heartbeats, resources, restarts. */
void pumpOnce(void);
/** Runs supervision passes until svc reaches state or timeoutMs passes.
 *  Returns 1 when the state was reached. */
int pumpUntil(Service *svc, ServiceState state, int timeoutMs);
/** Runs supervision passes for ms milliseconds. */
void pumpFor(int ms);
/** Sleeps ms milliseconds. */
void sleepMs(int ms);
/** Number of zombie children of this process (read from /proc). */
int countZombieChildren(void);
/** Forks a helper that sends sig to this process after delayMs, then exits. */
pid_t sendSignalLater(int sig, int delayMs);

/* ---- stdout capture ---- */
void captureStdoutBegin(void);
/** Restores stdout and returns everything printed since captureStdoutBegin(). */
const char *captureStdoutEnd(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_SHIM_H */
