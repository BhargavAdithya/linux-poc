/**
 * FAULT 4: double free / dangling pointer (crash, SIGABRT)
 *
 * Use case : on shutdown, the code frees a service it looked up earlier
 *            ("camera_service"), then frees the process table.
 * Bug      : the node belongs to the process table, so ptFree() frees the
 *            same node a second time.
 * Symptom  : "free(): double free detected" and the program aborts.
 * GDB      : make gdb-doublefree  ->  bt shows main -> ptFree -> free;
 *            print saved shows the address that was already freed.
 * Valgrind : make valgrind-doublefree  ->  "Invalid read" / "Invalid free()".
 * Fix      : the table owns its nodes. Never free a node yourself; only
 *            ptFree() frees them (build with -DFIXED).
 */
#include <stdlib.h>
#include "fault_common.h"

int main(void)
{
    Service *saved;

    faultSetup();
    saved = ptFindByName(&gTable, "camera_service");
    (void)printf("Stopping service %s\n", saved->cfg.name);
#ifdef FIXED
    saved = NULL;                                    /* the table frees it */
#else
    free(saved);                                     /* BUG: node still linked in the table */
#endif
    ptFree(&gTable);                                 /* frees every node, again */
    logClose();
    (void)printf("Finished normally.\n");
    return 0;
}
