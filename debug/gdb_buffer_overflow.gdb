# FAULT 2: buffer overflow
# Run:  make gdb-overflow
set pagination off
set confirm off
echo \n=== Step 1: stop where the service name is stored ===\n
break storeName
run
echo \n=== Step 2: command before the copy, and the size of the name field ===\n
print cfg->command
print sizeof(cfg->name)
print (int)strlen(name)
echo \n=== Step 3: watch the command field, stop when anything writes to it ===\n
watch -l cfg->command[0]
continue
echo \n=== Step 4: who wrote into command? ===\n
bt
echo \n=== Step 5: command is now the end of the name ===\n
finish
print cfg->command
delete
continue
echo \nROOT CAUSE: strcpy() wrote 60 characters into name[32] and ran into command.\n
echo FIX: length check + snprintf(cfg->name, sizeof(cfg->name), ...) (see #ifdef FIXED, and parseServiceLine()).\n
quit
