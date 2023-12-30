/**
 * This file is for implementation of common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#include "mimpi_common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

//#include <pthread.h>

_Noreturn void syserr(const char* fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, " (%d; %s)\n", errno, strerror(errno));
    exit(1);
}

_Noreturn void fatal(const char* fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    fprintf(stderr, "\n");
    exit(1);
}

/////////////////////////////////////////////////
// Put your implementation here
/************************ STRUCTURES ************************/
typedef struct {
    void *data;
    int count;
    int root;
} Bcast_args;

typedef struct {
    void const *send_data;
    void *recv_data;
    int count;
    MIMPI_Op op;
    int root;
} Reduce_args;

/************************ VARIABLES ************************/
static int my_rank;
static int world_size;
static bool deadlocks;

/************************ FUNCTIONS FOR INIT ************************/
void setMyRank(int rank) {
    my_rank = rank;
}

void setWorldSize(int size) {
    world_size = size;
}

void setDeadlocks(bool deadlock_detection) {
    deadlocks = deadlock_detection;
}

/************************ FUNCTIONS FOR FINALIZE ************************/
void closeGroupPipes() {
    for (int i = 0; i < 3; i++) {
        close(700 + 6 * (my_rank + 1) + i);
        close(700 + 6 * (my_rank + 1) + i - 3);
    }

    if (my_rank == 0) {
        for (int i = 2; i <= world_size; i++) {
            close(900 + 4 * i + 0);
            close(900 + 4 * i + 1);
        }
    }
    else {
        close(900 + 4 * (my_rank + 1) + 2);
        close(900 + 4 * (my_rank + 1) + 3);
    }
}

/************************ POINT TO POINT FUNCTIONS ************************/


/************************ GROUP FUNCTIONS ************************/
// HELPER FUNCTIONS

static u_int8_t* reducer(void* tab1, const void* tab2, int count, MIMPI_Op op) {
    u_int8_t* res_tab = malloc(count);
    switch (op) {
    case MIMPI_MAX:
        for (int i = 0; i < count; i++) {
            u_int8_t one = *(u_int8_t*) tab1;
            u_int8_t two = *(u_int8_t*) tab2;

            if (one > two) {
                res_tab[i] = one;
            }
            else {
                res_tab[i] = two;
            }
            tab1++;
            tab2++;
        }
        break;
    
    case MIMPI_MIN:
        for (int i = 0; i < count; i++) {
            u_int8_t one = *(u_int8_t*) tab1;
            u_int8_t two = *(u_int8_t*) tab2;

            if (one < two) {
                res_tab[i] = one;
            }
            else {
                res_tab[i] = two;
            }
            tab1++;
            tab2++;
        }
        break;

    case MIMPI_PROD:
        for (int i = 0; i < count; i++) {
            u_int8_t one = *(u_int8_t*) tab1;
            u_int8_t two = *(u_int8_t*) tab2;

            res_tab[i] = one * two;

            tab1++;
            tab2++;
        }
        break;

    case MIMPI_SUM:
        for (int i = 0; i < count; i++) {
            u_int8_t one = *(u_int8_t*) tab1;
            u_int8_t two = *(u_int8_t*) tab2;

            res_tab[i] = one + two;

            tab1++;
            tab2++;
        }
        break;
    
    default:
        break;
    }

    return res_tab;
}

static bool tryToSend(int fd, void* send_from, int count) {
    int ret = chsend(fd, send_from, count);
    if (ret == -1) {
        if (errno == EPIPE) {
            closeGroupPipes();
            return false;
        }
        ASSERT_SYS_OK(chsend(fd, send_from, count));
    }
    return true;
}

static bool tryToReceive(int fd, void* save_to, int count) {
    int ret = chrecv(fd, save_to, count);
    if (ret == 0) {
        closeGroupPipes();
        return false;
    }
    else if (ret < 0) {
        ASSERT_SYS_OK(chrecv(fd, save_to, count));
    }
    return true;
}

// EXTERN FUNCTIONS

MIMPI_Retcode Barrier(void) {
    char to_send = '0';
    char* to_receive = malloc(sizeof(char));
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (left_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 1 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 2 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me != 1) {
        if (!tryToSend((700 + 6 * me + 0), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }

        if(!tryToReceive((700 + 6 * me + 0 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 1), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 2), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    free(to_receive);

    return MIMPI_SUCCESS;
}

MIMPI_Retcode Bcast(void *data, int count, int root) {
    char to_send = '1';
    char* to_receive = malloc(sizeof(char));
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (root == my_rank && me != 1) {
        if(!tryToSend((900 + 4 * me + 2), data, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 1 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 2 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me == 1 && root != my_rank) {
        if (!tryToReceive((900 + 4 * (root + 1) + 1), data, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }   
    }

    if (me != 1) {
        if(!tryToSend((700 + 6 * me + 0), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        if (!tryToReceive((700 + 6 * me + 0 - 3), data, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 1), data, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 2), data, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    free(to_receive);

    return MIMPI_SUCCESS;
}

MIMPI_Retcode Reduce(
    void const *send_data,
    void *recv_data,
    int count,
    MIMPI_Op op,
    int root
) {
    char to_send = '2';
    char* to_receive = malloc(sizeof(char));
    void* tab1 = malloc(count);
    void* tab2 = malloc(count);
    u_int8_t* res_tab;
    u_int8_t* mid_tab;
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (left_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 1 - 3), tab1, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        mid_tab = reducer(tab1, send_data, count, op); 
    }
    else {
        mid_tab = malloc(count);
        for (int i = 0; i < count; i++) {
            u_int8_t num = *(u_int8_t*) send_data;

            mid_tab[i] = num;

            send_data++;
        }
    }

    if (right_child < world_size + 1) {
        if (!tryToReceive((700 + 6 * me + 2 - 3), tab2, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        res_tab = reducer(tab2, mid_tab, count, op);
    }
    else {
        res_tab = mid_tab;
    }

    if (me != 1) {
        if(!tryToSend((700 + 6 * me + 0), res_tab, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        if (!tryToReceive((700 + 6 * me + 0 - 3), to_receive, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 1), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(!tryToSend((700 + 6 * me + 2), &to_send, sizeof(char))) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me == 1 && my_rank != root) {
        if(!tryToSend((900 + 4 * (root + 1) + 0), res_tab, count)) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (my_rank == root) {
        if (me != 1) {
            if (!tryToReceive((900 + 4 * me + 3), recv_data, count)) {
                return MIMPI_ERROR_REMOTE_FINISHED;
            }
        }
        else {
            u_int8_t* placeholder = recv_data;
            for (int i = 0; i < count; i++) {
                u_int8_t num = *(u_int8_t*) res_tab;

                placeholder[i] = num;

                recv_data++;
            }
        }
    }

    free(to_receive);
    free(mid_tab);      
    free(tab1);
    free(tab2);

    return MIMPI_SUCCESS;
}
