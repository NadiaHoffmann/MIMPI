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
    createReaders();
    // TODO
}

void MIMPI_Finalize() {
    // TODO
    // zamykać wszystkie pipy procesu, zabijać wszystkie threads
    closeGroupPipes();
    closePointToPointPipes();
    killReaders();

    printf("me: %d\n", my_no);

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
    printf("%d in send\n", my_no);
    if (destination == my_no) {
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;
    }

    if (destination >= world || destination < 0) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }

    return Send(data, count, destination, tag);
}

MIMPI_Retcode MIMPI_Recv(
    void *data,
    int count,
    int source,
    int tag
) {
    //TODO
    printf("%d in recv\n", my_no);
    if (source == my_no) {
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;
    }

    if (source >= world || source < 0) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }

   return Search(data, count, source, tag);
}

MIMPI_Retcode MIMPI_Barrier() {
    printf("%d in Barrier\n", my_no);
    return Barrier();
}

MIMPI_Retcode MIMPI_Bcast(
    void *data,
    int count,
    int root
) {
    if (root >= world || root < 0) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }
    return Bcast(data, count, root);
}

MIMPI_Retcode MIMPI_Reduce(
    void const *send_data,
    void *recv_data,
    int count,
    MIMPI_Op op,
    int root
) {
    if (root >= world || root < 0) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }
    return Reduce(send_data, recv_data, count, op, root);
}