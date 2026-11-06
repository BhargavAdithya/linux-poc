# FAULT 1: NULL pointer dereference
# Run:  make gdb-null
set pagination off
set confirm off
echo \n=== Step 1: run the program until it crashes ===\n
run
echo \n=== Step 2: where did it crash? (backtrace) ===\n
bt
echo \n=== Step 3: look at the variables in the crashing function ===\n
info args
info locals
echo \n=== Step 4: the pointer is NULL, the PID is not a service ===\n
print svc
print pid
print ptFindByPid(&gTable, pid)
print gTable.count
echo \nROOT CAUSE: ptFindByPid() returned NULL for a child that is not a service, and svc->lastStatus was written without a check.\n
echo FIX: check svc == NULL and ignore the child (see #ifdef FIXED, and reapChildren() in src/monitor.c).\n
kill
quit
