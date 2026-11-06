# FAULT 4: double free
# Run:  make gdb-doublefree
set pagination off
set confirm off
echo \n=== Step 1: remember the node that is freed by hand ===\n
break fault_double_free.c:28
run
print saved
print *saved
echo \n=== Step 2: run on; ptFree() frees the same node again ===\n
continue
echo \n=== Step 3: backtrace of the abort ===\n
bt
echo \nROOT CAUSE: the node was freed with free(saved) while still linked in the table; ptFree() freed it again.\n
echo FIX: never free a table node yourself; ptFree() owns them (see #ifdef FIXED).\n
kill
quit
