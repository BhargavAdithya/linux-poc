/**
 * @file proctable.c
 * @brief Process Table Module: singly linked list of Service nodes, kept in
 *        configuration-file order.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "proctable.h"

ProcessTable gTable = { NULL, 0 };

void ptInit(ProcessTable *table)
{
    if (table != NULL) {
        table->head = NULL;
        table->count = 0;
    }
}

Service *ptAdd(ProcessTable *table, const ServiceConfig *cfg)
{
    Service *node = NULL;
    Service *cur;

    if ((table != NULL) && (cfg != NULL)) {
        node = (Service *)calloc(1U, sizeof(Service));
        if (node != NULL) {
            node->cfg = *cfg;
            node->pid = 0;
            node->state = SVC_STOPPED;
            node->lastStatus = 0;
            node->hbReadFd = -1;
            node->next = NULL;
            if (table->head == NULL) {
                table->head = node;
            } else {
                cur = table->head;
                while (cur->next != NULL) {
                    cur = cur->next;
                }
                cur->next = node;
            }
            table->count++;
        }
    }
    return node;
}

Service *ptFindByName(ProcessTable *table, const char *name)
{
    Service *cur = NULL;

    if ((table != NULL) && (name != NULL)) {
        cur = table->head;
        while ((cur != NULL) && (strcmp(cur->cfg.name, name) != 0)) {
            cur = cur->next;
        }
    }
    return cur;
}

Service *ptFindByPid(ProcessTable *table, pid_t pid)
{
    Service *cur = NULL;

    if ((table != NULL) && (pid > 0)) {
        cur = table->head;
        while ((cur != NULL) && (cur->pid != pid)) {
            cur = cur->next;
        }
    }
    return cur;
}

void ptFree(ProcessTable *table)
{
    Service *cur;
    Service *next;

    if (table != NULL) {
        cur = table->head;
        while (cur != NULL) {
            next = cur->next;
            if (cur->hbReadFd >= 0) {
                (void)close(cur->hbReadFd);
            }
            free(cur);
            cur = next;
        }
        table->head = NULL;      /* a second ptFree() is harmless */
        table->count = 0;
    }
}

const char *stateToString(ServiceState state)
{
    const char *text;

    switch (state) {
    case SVC_STARTING:   text = "STARTING"; break;
    case SVC_RUNNING:    text = "RUNNING"; break;
    case SVC_CRASHED:    text = "CRASHED"; break;
    case SVC_RESTARTING: text = "RESTARTING"; break;
    case SVC_FAILED:     text = "FAILED"; break;
    case SVC_STOPPED:    text = "STOPPED"; break;
    default:             text = "UNKNOWN"; break;
    }
    return text;
}

int dependsOnService(const Service *svc, const char *name)
{
    int found = 0;

    if ((svc != NULL) && (name != NULL)) {
        int i;

        for (i = 0; (i < svc->cfg.depCount) && (found == 0); i++) {
            if (strcmp(svc->cfg.dependsOn[i], name) == 0) {
                found = 1;
            }
        }
    }
    return found;
}
