# FAULT 3: logic error - zombie processes
# Run:  make gdb-logic
set pagination off
set confirm off
echo \n=== Step 1: stop right after the reap (when the zombies are counted) ===\n
break countZombies
run
up
echo \n=== Step 2: five children exited, how many were reaped? ===\n
print reaped
echo \n=== Step 3: the others are still zombies (state Z, <defunct>) ===\n
shell ps -eo pid,ppid,stat,cmd | grep "[d]efunct"
echo \n=== Step 4: five exits, one merged SIGCHLD, one waitpid() call ===\n
print 5 - reaped
delete
continue
echo \nROOT CAUSE: waitpid() is called once per SIGCHLD, but merged signals stand for several children.\n
echo FIX: loop while waitpid(-1, &status, WNOHANG) > 0 (see #ifdef FIXED, and reapChildren() in src/monitor.c).\n
quit
