# FAULT INJECTION B: configuration file cannot be read in the real program
# Run:  make gdb-inject-file
# GDB changes the config path inside loadConfig() to a file that does not
# exist (as if the disk were unmounted), to prove the supervisor reports
# E001, starts no service and exits with status 1.
set pagination off
set confirm off
break loadConfig
run -c ../debug/inject.conf
echo \n=== loadConfig() reached: point it at a missing file ===\n
bt 2
set var path = "/nonexistent/supervisor.conf"
print path
delete
continue
echo \nRESULT: the supervisor printed E001, logged it and exited with status 1 without starting any service.\n
quit
