/**
 * This file is for implementation of MIMPI library.
 * */

#include "channel.h"
#include "mimpi.h"
#include "mimpi_common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>

#define BUFFER_SIZE 4096

static bool deadlock_detection;
static int my_no;
static int world;

void MIMPI_Init(bool enable_deadlock_detection) {
    channels_init();

    world = MIMPI_World_size();
    my_no = MIMPI_World_rank();
    deadlock_detection = enable_deadlock_detection;
    setMyRank(my_no);
    setWorldSize(world);
    setDeadlocks(enable_deadlock_detection);
    // TODO
}

void MIMPI_Finalize() {
    // TODO
    // zamykać wszystkie pipy procesu, zabijać wszystkie threads
    closeGroupPipes();

    channels_finalize();
}

int MIMPI_World_size() {
    return atoi(getenv("MIMPI_n"));
}

int MIMPI_World_rank() {
    pid_t my_pid = getpid();
    char* name = (char*) malloc(strlen("MIMPI_") + 20 + 1);
    ASSERT_SYS_OK(sprintf(name, "MIMPI_%d", my_pid));
    int rank = atoi(getenv(name));
    free(name);
    return rank;
}

MIMPI_Retcode MIMPI_Send(
    void const *data,
    int count,
    int destination,
    int tag
) {
    TODO
}

MIMPI_Retcode MIMPI_Recv(
    void *data,
    int count,
    int source,
    int tag
) {
    TODO
}

MIMPI_Retcode MIMPI_Barrier() {
    return createBarrier();
}

MIMPI_Retcode MIMPI_Bcast(
    void *data,
    int count,
    int root
) {
    if (root >= world) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }
    return createBcast(data, count, root);
}

MIMPI_Retcode MIMPI_Reduce(
    void const *send_data,
    void *recv_data,
    int count,
    MIMPI_Op op,
    int root
) {
    if (root >= world) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }
    return createReduce(send_data, recv_data, count, op, root);
}