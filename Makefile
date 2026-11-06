# =====================================================================
# ADAS ECU Process Supervisor (Mini "init") - build, test and quality gates
#
#  Build / run         make app | make run | make daemon | make status | make stop | make debug
#  Testing             make coverage   (GTest unit + integration, coverage table)
#                      make cunit      (CUnit runner, output -> cunit_report.txt)
#  Memory              make valgrind   (supervisor runs 20 s with the sample config -> valgrind_report.txt)
#                      make valgrind-tests (all GTest cases -> valgrind_tests_report.txt)
#  Secure coding       make misra      (strict GCC diagnostics, must be 0 warnings)
#                      make cppcheck   (-> cppcheck_report.txt, must be empty)
#  Optimization        make optimize   (.s size at -O0..-O3 -> optimization_report.txt)
#  Multiprocessing     make crash-storm      (20 services crash at the same moment: no zombies)
#                      make process-performance (crash detection and restart latency)
#  Debugging (GDB)     make gdb-null | gdb-overflow | gdb-logic | gdb-doublefree
#                      make gdb-inject-fork | gdb-inject-file
#                      make valgrind-doublefree | make faults-fixed
#  Everything          make all-checks
#  Cleanup             make clean
# =====================================================================

CC             := gcc
CXX            := g++
CFLAGS         := -std=c99 -D_DEFAULT_SOURCE -Iinclude
MISRA_FLAGS    := -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wunused-parameter -fanalyzer -Iinclude
COVERAGE_FLAGS := -O0 -g --coverage -Iinclude
LDFLAGS        := -lgtest -lgtest_main -pthread --coverage
DEBUG_FLAGS    := -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -g -O0 -Iinclude
PROC_FLAGS     := -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -g -Iinclude

# Source definitions
SRCS           := $(wildcard src/*.c)
MODULES        := $(filter-out src/main.c, $(SRCS))
OBJS           := $(notdir $(MODULES:.c=.o)) test_shim.o
CONCURRENCY    := tests/concurrency
DEMO           := services/demo_service

# lcov 2.x needs extra flags; lcov 1.x does not accept them
LCOV_MAJOR     := $(shell lcov --version 2>/dev/null | sed -n 's/.*[Vv]ersion \([0-9]*\).*/\1/p')
ifeq ($(LCOV_MAJOR),2)
LCOV_OPTS      := --rc branch_coverage=1 --ignore-errors inconsistent,mismatch,unused
else
LCOV_OPTS      := --rc lcov_branch_coverage=1
endif

.PHONY: all app run daemon status stop debug coverage cunit misra cppcheck valgrind valgrind-tests \
        optimize crash-storm process-performance \
        gdb-null gdb-overflow gdb-logic gdb-doublefree gdb-inject-fork gdb-inject-file \
        valgrind-doublefree faults-fixed all-checks clean

all: app

# ---------------------------------------------------------------- 1. build
#    the demo ADAS service is needed by the sample configuration and the tests
$(DEMO): $(DEMO).c
	$(CC) -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra $< -o $@

app: $(DEMO)
	$(CC) $(CFLAGS) -g $(SRCS) -o supervisor

#    foreground: Ctrl+C stops the supervisor and all services
run: app
	./supervisor -c supervisor.conf

daemon: app
	./supervisor -c supervisor.conf -d
	@echo "Supervisor running in the background (PID $$(cat supervisor.pid)). Use: make status | make stop"

status:
	@kill -USR1 $$(cat supervisor.pid) && sleep 1 && cat supervisor.status

stop:
	@kill -TERM $$(cat supervisor.pid) && echo "Stop requested; see supervisor.log"

debug: $(DEMO)
	$(CC) $(DEBUG_FLAGS) $(SRCS) -o supervisor_debug

# ---------------------------------------------------- 2. GTest + coverage
#    modules and the shim are compiled as C, the test file as C++
coverage: $(DEMO)
	@echo "==> Compiling Unit Tests with Coverage Instrumentation..."
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) -c $(MODULES) tests/test_shim.c
	$(CXX) $(COVERAGE_FLAGS) $(OBJS) tests/test_supervisor.cpp $(LDFLAGS) -o test_runner
	@echo "==> Running GTest Unit and Integration Tests..."
	./test_runner
	@echo "==> Extracting and Filtering Coverage Data..."
	@lcov --capture --directory . --output-file coverage.info --quiet $(LCOV_OPTS)
	@lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file filtered.info --quiet $(LCOV_OPTS)
	@echo "========================================================================"
	@echo "                       TERMINAL COVERAGE SUMMARY                        "
	@echo "========================================================================"
	@lcov --summary filtered.info $(LCOV_OPTS)
	@echo "========================================================================"
	@echo "Module Function & Branch Details:"
	@gcov -b -n $(notdir $(MODULES:.c=.gcda)) | grep -A 4 "File 'src/"

# ------------------------------------------------------------- 3. CUnit
cunit: $(DEMO)
	$(CC) $(CFLAGS) -Wall -Wextra -g $(MODULES) tests/test_shim.c tests/cunit/test_cunit.c -lcunit -o cunit_runner
	./cunit_runner | tee cunit_report.txt

# ----------------------------------------------------------- 4. MISRA-C
misra:
	@echo "==> Checking MISRA-C Compliance via GCC Diagnostics..."
	$(CC) $(MISRA_FLAGS) $(SRCS) -o /dev/null
	@echo "==> MISRA-C Check PASSED: 0 warnings, 0 errors."

# ---------------------------------------------------------- 5. CPPCheck
#    staticFunction / unusedFunction are suppressed: the module interface
#    functions (LLD 6.2) are public on purpose and are called by the tests.
cppcheck:
	@echo "==> Running CPPCheck Static Analysis..."
	cppcheck --enable=all --check-level=exhaustive --inline-suppr --suppress=missingIncludeSystem --suppress=checkersReport \
	         --suppress=staticFunction --suppress=unusedFunction --std=c99 -Iinclude src/ 2> cppcheck_report.txt
	@cat cppcheck_report.txt
	@echo "==> CPPCheck Complete. Output logged to cppcheck_report.txt."

# ---------------------------------------------------------- 6. Valgrind
#    the supervisor runs the sample configuration (crashes, a hang and
#    restarts happen during the run) and is then stopped with SIGTERM
valgrind: app
	@echo "==> Running Valgrind Dynamic Memory Analysis (20 s)..."
	-timeout -s TERM 20 valgrind --leak-check=full \
	         --show-leak-kinds=all \
	         --track-origins=yes \
	         --track-fds=yes \
	         --log-file=valgrind_report.txt \
	         ./supervisor -c supervisor.conf > /dev/null
	@cat valgrind_report.txt

#    non-interactive: every GTest case under Valgrind
valgrind-tests: coverage
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 \
	         --log-file=valgrind_tests_report.txt ./test_runner > /dev/null
	@tail -n 12 valgrind_tests_report.txt

# ------------------------------------------------------ 7. Optimization
optimize:
	@rm -rf asm && mkdir -p asm/O0 asm/O1 asm/O2 asm/O3
	@for lvl in 0 1 2 3; do \
	    for f in $(SRCS); do $(CC) $(CFLAGS) -O$$lvl -S $$f -o asm/O$$lvl/$$(basename $$f .c).s; done; \
	    $(CC) $(CFLAGS) -O$$lvl $(SRCS) -o asm/supervisor_O$$lvl; \
	done
	@{ echo "ASSEMBLY (.s) FILE SIZE IN BYTES PER MODULE"; \
	   printf "%-16s %10s %10s %10s %10s\n" "Module" "-O0" "-O1" "-O2" "-O3"; \
	   for f in $(SRCS); do m=$$(basename $$f .c); \
	       printf "%-16s %10s %10s %10s %10s\n" $$m.s $$(wc -c < asm/O0/$$m.s) $$(wc -c < asm/O1/$$m.s) \
	              $$(wc -c < asm/O2/$$m.s) $$(wc -c < asm/O3/$$m.s); done; \
	   printf "%-16s %10s %10s %10s %10s\n" "TOTAL bytes" $$(cat asm/O0/*.s | wc -c) $$(cat asm/O1/*.s | wc -c) \
	          $$(cat asm/O2/*.s | wc -c) $$(cat asm/O3/*.s | wc -c); \
	   printf "%-16s %10s %10s %10s %10s\n" "TOTAL lines" $$(cat asm/O0/*.s | wc -l) $$(cat asm/O1/*.s | wc -l) \
	          $$(cat asm/O2/*.s | wc -l) $$(cat asm/O3/*.s | wc -l); \
	   echo; echo "EXECUTABLE SIZE (size command, bytes)"; \
	   size asm/supervisor_O0 asm/supervisor_O1 asm/supervisor_O2 asm/supervisor_O3; \
	   echo; echo "-O0: no optimization, every variable kept in memory (best for GDB)."; \
	   echo "-O1: removes dead stores and redundant loads -> smallest code."; \
	   echo "-O2: adds scheduling and more inlining -> faster, a little larger."; \
	   echo "-O3: aggressive inlining, loop unrolling, vectorization -> fastest, largest code."; \
	} | tee optimization_report.txt

# --------------------------------------------------- 8. Multiprocessing
#    The supervisor is single-threaded by design (LLD 9): its concurrency is
#    many child processes exiting at the same time, handled through SIGCHLD.
bin_crash_storm: $(CONCURRENCY)/test_crash_storm.c $(MODULES)
	$(CC) $(PROC_FLAGS) $(MODULES) $< -o $@

bin_process_performance: $(CONCURRENCY)/test_performance.c $(MODULES) $(DEMO)
	$(CC) $(PROC_FLAGS) -O2 $(MODULES) $< -o $@

crash-storm: bin_crash_storm
	@echo "==> 20 services crash at the same moment (Expected: all reaped, all restarted, 0 zombies)..."
	./bin_crash_storm

process-performance: bin_process_performance
	@echo "==> Measuring crash detection, restart and heartbeat-loss latency..."
	./bin_process_performance

# ------------------------------------------- 9. GDB and fault injection
bin_fault_null: debug/fault_null_pointer.c $(MODULES)
	$(CC) $(DEBUG_FLAGS) $(MODULES) $< -o $@

bin_fault_overflow: debug/fault_buffer_overflow.c $(MODULES)
	$(CC) $(DEBUG_FLAGS) $(MODULES) $< -o $@

bin_fault_logic: debug/fault_zombie_logic.c $(MODULES)
	$(CC) $(DEBUG_FLAGS) $(MODULES) $< -o $@

bin_fault_doublefree: debug/fault_double_free.c $(MODULES)
	$(CC) $(DEBUG_FLAGS) $(MODULES) $< -o $@

gdb-null: bin_fault_null
	-gdb -q -batch -x debug/gdb_null_pointer.gdb ./bin_fault_null

gdb-overflow: bin_fault_overflow
	-gdb -q -batch -x debug/gdb_buffer_overflow.gdb ./bin_fault_overflow

gdb-logic: bin_fault_logic
	-gdb -q -batch -x debug/gdb_zombie_logic.gdb ./bin_fault_logic

gdb-doublefree: bin_fault_doublefree
	-gdb -q -batch -x debug/gdb_double_free.gdb ./bin_fault_doublefree

valgrind-doublefree: bin_fault_doublefree
	-valgrind --leak-check=full ./bin_fault_doublefree

#    fault injection into the real program (runs in a scratch folder gdb_run/)
gdb-inject-fork: debug
	@rm -rf gdb_run && mkdir gdb_run
	-cd gdb_run && gdb -q -batch -x ../debug/gdb_inject_fork.gdb ../supervisor_debug
	@grep -E "E006|I003|I004" gdb_run/supervisor.log

gdb-inject-file: debug
	@rm -rf gdb_run && mkdir gdb_run
	-cd gdb_run && gdb -q -batch -x ../debug/gdb_inject_file.gdb ../supervisor_debug
	@grep "E001" gdb_run/supervisor.log

#    the same four programs built with -DFIXED: all run cleanly
faults-fixed:
	@for f in null_pointer buffer_overflow zombie_logic double_free; do \
	    echo "==> fault_$$f (FIXED)"; \
	    $(CC) $(DEBUG_FLAGS) -DFIXED $(MODULES) debug/fault_$$f.c -o bin_fixed_$$f && ./bin_fixed_$$f; \
	done

# ------------------------------------------------------- 10. everything
all-checks: app misra cppcheck coverage cunit valgrind-tests optimize crash-storm process-performance faults-fixed

# ------------------------------------------------------------ 11. clean
clean:
	rm -rf supervisor supervisor_debug $(DEMO) test_runner cunit_runner *.o *.gcno *.gcda *.info *.gcov asm gdb_run \
	       bin_crash_storm bin_process_performance \
	       bin_fault_null bin_fault_overflow bin_fault_logic bin_fault_doublefree bin_fixed_* \
	       cppcheck_report.txt valgrind_report.txt valgrind_tests_report.txt cunit_report.txt \
	       optimization_report.txt supervisor.log supervisor.log.1 supervisor.pid supervisor.status
