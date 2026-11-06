/**
 * @file test_cunit.c
 * @brief CUnit test runner (pure C) for the ADAS ECU Process Supervisor.
 *
 * One CUnit suite per module, each with usual, unusual and edge cases.
 * It reuses tests/test_shim.c, so every test starts from a clean system
 * in its own temporary folder.
 *
 *   make cunit  ->  verbose CUnit output on the terminal and in cunit_report.txt
 */
#include <CUnit/Basic.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../test_shim.h"

static int suiteSetUp(void) { return 0; }
static int suiteTearDown(void) { return 0; }
static void setUpEach(void) { test_reset_system(); }
static void tearDownEach(void) { test_cleanup_system(); }

/* ---------------------------------------------------------------- logger */
static void logger_usual_timestampedLine(void)
{
    logMsg(LOG_INFO, NULL, "hello");
    CU_ASSERT_PTR_NOT_NULL(strstr(logText(), "[INFO ] [supervisor:"));
    CU_ASSERT_PTR_NOT_NULL(strstr(logText(), "] hello"));
}
static void logger_unusual_missingFolder(void)
{
    CU_ASSERT_EQUAL(logInit(testPath("none/x.log"), 0), ERR_FILE);
    CU_ASSERT_EQUAL(logIsOpen(), 0);
}
static void logger_edge_errorCodes(void)
{
    CU_ASSERT_STRING_EQUAL(errorCodeText(ERR_CONFIG_FILE), "E001");
    CU_ASSERT_STRING_EQUAL(errorCodeText(ERR_MAX_RESTARTS), "E011");
    CU_ASSERT_STRING_EQUAL(errorCodeText(ERR_MAX_RESTARTS + 1), "E000");
}

/* ----------------------------------------------------------- proctable */
static void proctable_usual_addAndFind(void)
{
    Service *a = addService("a", "/bin/true");
    a->pid = 500;
    CU_ASSERT_PTR_EQUAL(ptFindByName(&gTable, "a"), a);
    CU_ASSERT_PTR_EQUAL(ptFindByPid(&gTable, 500), a);
    CU_ASSERT_EQUAL(gTable.count, 1);
}
static void proctable_unusual_notFound(void)
{
    CU_ASSERT_PTR_NULL(ptFindByName(&gTable, "ghost"));
    CU_ASSERT_PTR_NULL(ptFindByPid(&gTable, 0));
}
static void proctable_edge_doubleFree(void)
{
    (void)addService("a", "x");
    ptFree(&gTable);
    ptFree(&gTable);
    CU_ASSERT_PTR_NULL(gTable.head);
    CU_ASSERT_STRING_EQUAL(stateToString(SVC_FAILED), "FAILED");
}

/* -------------------------------------------------------------- config */
static void config_usual_loadValidateOrder(void)
{
    writeFile("supervisor.conf", "[service]\nname=b\ncommand=/bin/true\ndepends_on=a\n"
                                 "[service]\nname=a\ncommand=/bin/true\n");
    CU_ASSERT_EQUAL(loadConfig(gCtx.configPath, &gTable), SUP_OK);
    CU_ASSERT_EQUAL(validateConfig(&gTable), SUP_OK);
    CU_ASSERT_EQUAL(resolveStartOrder(&gTable, gStartOrder, &gStartCount), SUP_OK);
    CU_ASSERT_STRING_EQUAL(gStartOrder[0]->cfg.name, "a");
}
static void config_unusual_errors(void)
{
    CU_ASSERT_EQUAL(loadConfig(testPath("missing.conf"), &gTable), ERR_CONFIG_FILE);
    (void)addService("a", "/bin/true");
    (void)addService("a", "/bin/true");
    CU_ASSERT_EQUAL(validateConfig(&gTable), ERR_DUPLICATE);
}
static void config_edge_ranges(void)
{
    ServiceConfig c;
    initServiceConfig(&c);
    CU_ASSERT_EQUAL(parseServiceLine("max_restarts=10", &c), SUP_OK);
    CU_ASSERT_EQUAL(parseServiceLine("max_restarts=11", &c), ERR_CONFIG_ENTRY);
    CU_ASSERT_EQUAL(parseServiceLine("restart_delay=0", &c), ERR_CONFIG_ENTRY);
    CU_ASSERT_EQUAL(parseServiceLine("umask=777", &c), SUP_OK);
}

/* ------------------------------------------------------------ launcher */
static void launcher_usual_launch(void)
{
    Service *s = addService("s", "/bin/sleep 30");
    CU_ASSERT_EQUAL(launchService(s), SUP_OK);
    CU_ASSERT(s->pid > 0);
    CU_ASSERT_EQUAL(s->state, SVC_STARTING);
}
static void launcher_unusual_badCommand(void)
{
    Service *s = addService("s", "/no/such/program");
    CU_ASSERT_EQUAL(launchService(s), SUP_OK);
    CU_ASSERT_EQUAL(pumpUntil(s, SVC_RESTARTING, 3000), 1);
    CU_ASSERT_PTR_NOT_NULL(strstr(logText(), "E007"));
}
static void launcher_edge_argvLimit(void)
{
    char exact[] = "a b";
    char over[] = "a b c";
    char *argv[3];
    CU_ASSERT_EQUAL(buildArgv(exact, argv, 2), 2);
    CU_ASSERT_EQUAL(buildArgv(over, argv, 2), -1);
}

/* ------------------------------------------------------------- monitor */
static void monitor_usual_normalExit(void)
{
    Service *s = addService("s", "/bin/true");
    CU_ASSERT_EQUAL(launchService(s), SUP_OK);
    CU_ASSERT_EQUAL(pumpUntil(s, SVC_STOPPED, 3000), 1);
}
static void monitor_unusual_heartbeatLoss(void)
{
    Service *s = addService("s", "/bin/sleep 30");
    s->cfg.heartbeatTimeout = 1;
    CU_ASSERT_EQUAL(launchService(s), SUP_OK);
    CU_ASSERT_EQUAL(pumpUntil(s, SVC_RESTARTING, 4000), 1);
    CU_ASSERT_PTR_NOT_NULL(strstr(logText(), "E010"));
}
static void monitor_edge_noSuchPid(void)
{
    unsigned long ticks = 0;
    long mem = 0;
    CU_ASSERT_EQUAL(readProcStats((pid_t)0x7fffffff, &ticks, &mem), ERR_FILE);
    CU_ASSERT_EQUAL(readProcStats(getpid(), &ticks, &mem), SUP_OK);
}

/* ------------------------------------------------------------- restart */
static void restart_usual_schedule(void)
{
    Service *s = addService("s", "x");
    handleCrash(s, 11);
    CU_ASSERT_EQUAL(s->state, SVC_RESTARTING);
    CU_ASSERT_EQUAL(s->restartCount, 1);
}
static void restart_unusual_limitReached(void)
{
    Service *s = addService("s", "x");
    s->restartCount = s->cfg.maxRestarts;
    handleCrash(s, 11);
    CU_ASSERT_EQUAL(s->state, SVC_FAILED);
    CU_ASSERT_PTR_NOT_NULL(strstr(logText(), "E011"));
}
static void restart_edge_zeroRestarts(void)
{
    Service *s = addService("s", "x");
    s->cfg.maxRestarts = 0;
    handleCrash(s, 11);
    CU_ASSERT_EQUAL(s->state, SVC_FAILED);
    CU_ASSERT_EQUAL(s->restartCount, 0);
}

/* ----------------------------------------------------------- dashboard */
static void dashboard_usual_statusFile(void)
{
    (void)addService("camera", "x");
    gCtx.daemonMode = 1;
    CU_ASSERT_EQUAL(showDashboard(&gCtx, &gTable), SUP_OK);
    CU_ASSERT_PTR_NOT_NULL(strstr(readFile("supervisor.status"), "camera"));
}
static void dashboard_unusual_badPath(void)
{
    gCtx.daemonMode = 1;
    (void)snprintf(gCtx.statusPath, sizeof(gCtx.statusPath), "%s", testPath("no/dir/status"));
    CU_ASSERT_EQUAL(showDashboard(&gCtx, &gTable), ERR_FILE);
}
static void dashboard_edge_duration(void)
{
    char buf[24];
    formatDuration(59L, buf, sizeof(buf));
    CU_ASSERT_STRING_EQUAL(buf, "00:00:59");
    formatDuration(3600L, buf, sizeof(buf));
    CU_ASSERT_STRING_EQUAL(buf, "01:00:00");
}

/* ---------------------------------------------------------- supervisor */
static void core_usual_arguments(void)
{
    char prog[] = "supervisor", c[] = "-c", file[] = "x.conf", dflag[] = "-d";
    char *argv[] = { prog, c, file, dflag, NULL };
    SupervisorContext ctx;
    CU_ASSERT_EQUAL(parseArguments(4, argv, &ctx), SUP_OK);
    CU_ASSERT_STRING_EQUAL(ctx.configPath, "x.conf");
    CU_ASSERT_EQUAL(ctx.daemonMode, 1);
}
static void core_unusual_badOption(void)
{
    char prog[] = "supervisor", x[] = "-x";
    char *argv[] = { prog, x, NULL };
    SupervisorContext ctx;
    CU_ASSERT_EQUAL(parseArguments(2, argv, &ctx), ERR_USAGE);
}
static void core_edge_signalFlags(void)
{
    handleSignal(SIGUSR1);
    CU_ASSERT_EQUAL(gStatusRequested, 1);
    handleSignal(SIGHUP);
    CU_ASSERT_EQUAL(gStopRequested, 0);
    handleSignal(SIGTERM);
    CU_ASSERT_EQUAL(gStopRequested, 1);
}

/* ---------------------------------------------------------------- main */
typedef struct {
    const char *name;
    CU_TestFunc func;
} TestEntry;

static int addSuite(const char *name, const TestEntry *tests, int count)
{
    CU_pSuite suite = CU_add_suite_with_setup_and_teardown(name, suiteSetUp, suiteTearDown, setUpEach, tearDownEach);
    int ok = (suite != NULL) ? 1 : 0;
    int i;

    for (i = 0; (i < count) && (ok == 1); i++) {
        ok = (CU_add_test(suite, tests[i].name, tests[i].func) != NULL) ? 1 : 0;
    }
    return ok;
}

int main(void)
{
    static const TestEntry logger[] = {
        { "usual: time-stamped line", logger_usual_timestampedLine },
        { "unusual: missing folder", logger_unusual_missingFolder },
        { "edge: error code range", logger_edge_errorCodes } };
    static const TestEntry table[] = {
        { "usual: add and find", proctable_usual_addAndFind },
        { "unusual: not found", proctable_unusual_notFound },
        { "edge: double free is safe", proctable_edge_doubleFree } };
    static const TestEntry config[] = {
        { "usual: load, validate, order", config_usual_loadValidateOrder },
        { "unusual: missing file, duplicate", config_unusual_errors },
        { "edge: numeric ranges", config_edge_ranges } };
    static const TestEntry launcher[] = {
        { "usual: launch", launcher_usual_launch },
        { "unusual: command not found (E007)", launcher_unusual_badCommand },
        { "edge: argument limit", launcher_edge_argvLimit } };
    static const TestEntry monitor[] = {
        { "usual: normal exit", monitor_usual_normalExit },
        { "unusual: heartbeat loss (E010)", monitor_unusual_heartbeatLoss },
        { "edge: procfs missing pid", monitor_edge_noSuchPid } };
    static const TestEntry restart[] = {
        { "usual: restart scheduled", restart_usual_schedule },
        { "unusual: limit reached (E011)", restart_unusual_limitReached },
        { "edge: zero restarts", restart_edge_zeroRestarts } };
    static const TestEntry dashboard[] = {
        { "usual: status file", dashboard_usual_statusFile },
        { "unusual: bad path (E009)", dashboard_unusual_badPath },
        { "edge: duration format", dashboard_edge_duration } };
    static const TestEntry core[] = {
        { "usual: -c and -d", core_usual_arguments },
        { "unusual: bad option", core_unusual_badOption },
        { "edge: signal flags", core_edge_signalFlags } };
    int ok;
    unsigned int failures;

    if (CU_initialize_registry() != CUE_SUCCESS) {
        return (int)CU_get_error();
    }
    ok = addSuite("Logger", logger, 3) && addSuite("Process Table", table, 3) &&
         addSuite("Config Reader", config, 3) && addSuite("Launcher", launcher, 3) &&
         addSuite("Monitor", monitor, 3) && addSuite("Restart Manager", restart, 3) &&
         addSuite("Status Dashboard", dashboard, 3) && addSuite("Supervisor Core", core, 3);
    if (ok == 0) {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }
    CU_basic_set_mode(CU_BRM_VERBOSE);
    (void)CU_basic_run_tests();
    failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (failures == 0U) ? 0 : 1;
}
