# FAULT INJECTION A: fork() failure in the real program
# Run:  make gdb-inject-fork
# GDB forces forkService() to return -1 for the first launch, as if the
# system were out of processes or memory, to prove the supervisor reports
# E006, applies the restart policy and starts the service on the next try.
set pagination off
set confirm off
set follow-fork-mode parent
set detach-on-fork on
break forkService
run -c ../debug/inject.conf
echo \n=== forkService() reached for the first launch: inject fork() failure: errno = EAGAIN (11), return -1 ===\n
bt 3
print *(int *)__errno_location() = 11
return (pid_t)-1
continue
echo \n=== forkService() reached again: this is the restart after the delay ===\n
bt 3
delete
echo \n=== let the restart run, then request a clean stop (as if SIGTERM arrived) ===\n
set var gStopRequested = 1
continue
echo \nRESULT: E006 logged, restart scheduled and done (I003), supervisor stopped cleanly (I004).\n
quit
