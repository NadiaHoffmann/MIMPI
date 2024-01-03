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

typedef struct {
    void* data;
    int count;
    int source;
    int tag;
} MIMPI_message;

typedef struct element {
    MIMPI_message *message;
    struct element *next;
} messages_node;

/************************ VARIABLES ************************/
#define BUFFER_SIZE 4096
static int my_rank = -1;
static int world_size = 0;
static bool deadlocks = false;
static volatile bool has_finished[16] = {false};
static pthread_mutex_t has_finished_mutex[16];
static pthread_t readers[16];
static messages_node* list[16];
static pthread_mutex_t mutex_list[16];
static MIMPI_message *waiting_for;
static pthread_cond_t waiting_for_cond;
static pthread_mutex_t waiting_for_mutex;
static volatile bool added = false;
static int nums[16];


/************************ HELPER FUNCTIONS ************************/
static ssize_t tryToGroupSend(int fd, void* send_from, int count) {
    ssize_t ret = chsend(fd, send_from, count);
    if (ret == -1) {
        if (errno == EPIPE) {
            closeGroupPipes();
            return -1;
        }
        ASSERT_SYS_OK(chsend(fd, send_from, count));
    }
    return ret;
}

static ssize_t tryToGroupReceive(int fd, void* save_to, int count) {
    ssize_t ret = chrecv(fd, save_to, count);
    if (ret == 0) {
        closeGroupPipes();
        return -1;
    }
    else if (ret < 0) {
        ASSERT_SYS_OK(chrecv(fd, save_to, count));
    }
    return ret;
}

static ssize_t tryToPointSend(int fd, const void* send_from, int count) {
    ssize_t ret = chsend(fd, send_from, count);
    if (ret == -1) {
        if (errno == EPIPE) {
            return -1;
        }
        ASSERT_SYS_OK(chsend(fd, send_from, count));
    }
    return ret;
}

static ssize_t tryToPointSendConst(int fd, const void* send_from, int count) {
    ssize_t ret = chsend(fd, send_from, count);
    if (ret == -1) {
        if (errno == EPIPE) {
            return -1;
        }
        ASSERT_SYS_OK(chsend(fd, send_from, count));
    }
    return ret;
}

static ssize_t tryToPointReceive(int fd, void* save_to, int count) {
    ssize_t ret = chrecv(fd, save_to, count);
    if (ret == 0) {
        return -1;
    }
    else if (ret < 0) {
        ASSERT_SYS_OK(chrecv(fd, save_to, count));
    }
    return ret;
}

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

void initMutexes() {
    waiting_for = malloc(sizeof(MIMPI_message));
    waiting_for -> tag = -1;
    waiting_for -> count = -1;
    waiting_for -> source = -1;

    ASSERT_ZERO(pthread_mutex_init(&waiting_for_mutex, NULL));

    for (int i = 0; i < world_size; i++) {
        messages_node *guard = malloc(sizeof(messages_node));
        guard -> message = NULL;
        guard -> next = NULL;

        list[i] = guard; // pamiętać żeby to dealokować przy wychodzeniu

        ASSERT_ZERO(pthread_mutex_init(&mutex_list[i], NULL));
        ASSERT_ZERO(pthread_mutex_init(&has_finished_mutex[i], NULL));
        nums[i] = i;
    }

    ASSERT_ZERO(pthread_cond_init(&waiting_for_cond, NULL));
}

/************************ FUNCTIONS FOR FINALIZE ************************/
void destroyMutexes() {
    for (int i = 0; i < world_size; i++) {
        pthread_mutex_destroy(&mutex_list[i]);
        pthread_mutex_destroy(&has_finished_mutex[i]);
    }

    pthread_mutex_destroy(&waiting_for_mutex);

    pthread_cond_destroy(&waiting_for_cond);
}

void killReaders() {
    for (int i = 0; i < world_size; i++) {
        if (!has_finished[i] && i != my_rank) {
            pthread_cancel(readers[i]);
        }
    }

    for (int i = 0; i < world_size; i++) {
        if (i != my_rank) {
            pthread_join(readers[i], NULL);
        }
    }

    free(waiting_for);
}

void closePointToPointPipes() {
    for (int i = 0; i < world_size; i++) {
        for (int j = 0; j < world_size; j++) {
            if (i != j) {
                if (i == my_rank) {
                    close((20 * (2 * i + 1)) + j);
                }

                if (j == my_rank) {
                    close((40 * (i + 1)) + j);
                }
            }
        }
    }
}

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
static void readerCleanup(int readingFrom) {
    printf("me: %d, readingFrom %d has finished\n", my_rank, readingFrom);
    ASSERT_ZERO(pthread_mutex_lock(&has_finished_mutex[readingFrom]));
    has_finished[readingFrom] = true;
    ASSERT_ZERO(pthread_mutex_unlock(&has_finished_mutex[readingFrom]));

    ASSERT_ZERO(pthread_mutex_lock(&waiting_for_mutex));
    if (waiting_for -> source == readingFrom) {
        ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));
        ASSERT_ZERO(pthread_cond_signal(&waiting_for_cond));
    }
    else {
        ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));
    }
}

// THREADS

static void* Reader(void* _args) {
    int readingFrom = *(int*) _args;
    //free(_args);

    while (true) {
        int count;
        if (tryToPointReceive((20 * (2 * my_rank + 1) + readingFrom), 
            &count, sizeof(int)) == -1) {
            readerCleanup(readingFrom);
            pthread_exit(NULL);
        }

        size_t read_bytes = 0;
        ssize_t read = 0;
        void* buf = malloc(count);

        while (read_bytes < count) {
            size_t to_read = (count - read_bytes > BUFFER_SIZE) ? BUFFER_SIZE :
                (count - read_bytes);

            read = tryToPointReceive((20 * (2 * my_rank + 1) + readingFrom), 
                buf + read_bytes, to_read);

            if (read == -1) {
                free(buf);
                readerCleanup(readingFrom);
                pthread_exit(NULL);
            }

            read_bytes += read;
        }

        int tag;

        if (tryToPointReceive((20 * (2 * my_rank + 1) + readingFrom), 
            &tag, sizeof(int)) == -1) {
            free(buf);
            readerCleanup(readingFrom);
            pthread_exit(NULL);
        }

        MIMPI_message *to_save = malloc(sizeof(MIMPI_message));
        to_save -> data = buf;
        to_save -> count = count;
        to_save -> source = readingFrom;
        to_save -> tag = tag;

        ASSERT_ZERO(pthread_mutex_lock(&mutex_list[readingFrom]));
        messages_node *temp = list[readingFrom];
        while (temp -> next != NULL) {
            temp = temp -> next;
        }

        messages_node *new_node = malloc(sizeof(messages_node));
        temp -> next = new_node;
        new_node -> message = to_save;
        new_node -> next = NULL;

        printf("%d read a message of count %d, data %d, tag %d from %d\n",
            my_rank, new_node -> message -> count, 
            *(int*) new_node -> message -> data, 
            new_node -> message -> tag, 
            new_node -> message -> source);

        ASSERT_ZERO(pthread_mutex_lock(&waiting_for_mutex));
        if (waiting_for -> count == count && 
            waiting_for -> source == readingFrom &&
        (
            waiting_for -> tag == MIMPI_ANY_TAG ||
            waiting_for -> tag == tag
        )) {
            ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));
            added = true;
            ASSERT_ZERO(pthread_cond_signal(&waiting_for_cond));
        }
        else {
            ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));
        }

        ASSERT_ZERO(pthread_mutex_unlock(&mutex_list[readingFrom]));
    }
    pthread_exit(NULL);
}

static MIMPI_Retcode waitForMessage(int count, int source, int tag) {
    ASSERT_ZERO(pthread_mutex_lock(&waiting_for_mutex));
    waiting_for -> count = count;
    waiting_for -> tag = tag;
    waiting_for -> source = source;
    ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));

    ASSERT_ZERO(pthread_mutex_lock(&has_finished_mutex[source]));
    while (!added && !has_finished[source]) {
        ASSERT_ZERO(pthread_mutex_unlock(&has_finished_mutex[source]));
        ASSERT_ZERO(pthread_cond_wait(&waiting_for_cond, 
            &mutex_list[source]));
        ASSERT_ZERO(pthread_mutex_lock(&has_finished_mutex[source]));
    }
    ASSERT_ZERO(pthread_mutex_unlock(&has_finished_mutex[source]));

    ASSERT_ZERO(pthread_mutex_lock(&waiting_for_mutex));
    waiting_for -> tag = -1;
    waiting_for -> count = -1;
    waiting_for -> source = -1;
    ASSERT_ZERO(pthread_mutex_unlock(&waiting_for_mutex));
    
    ASSERT_ZERO(pthread_mutex_lock(&has_finished_mutex[source]));
    if (!added && has_finished[source]) {
        ASSERT_ZERO(pthread_mutex_unlock(&has_finished_mutex[source]));
        added = false;
        return MIMPI_ERROR_REMOTE_FINISHED;
    }
    ASSERT_ZERO(pthread_mutex_unlock(&has_finished_mutex[source]));

    added = false;

    return MIMPI_SUCCESS;
}

// HELPERS
MIMPI_Retcode Search(void* data, int count, int source, int tag) {
    ASSERT_ZERO(pthread_mutex_lock(&mutex_list[source]));
    messages_node *before_temp = list[source];
    messages_node *temp = list[source];

    // printf("me %d, looking for: count %d, tag %d\n", my_rank, temp->message->count,
    //     temp->message->tag);
    
    if (tag != MIMPI_ANY_TAG) {
        while (temp -> next != NULL || (
            temp -> message != NULL &&
            temp -> message -> tag != tag &&
            temp -> message -> count != count
        )) {
            before_temp = temp;
            temp = temp -> next;
        }

        if (temp -> message == NULL || 
            temp -> message -> tag != tag || 
            temp -> message -> count != count
        ) {
            if (waitForMessage(count, source, tag) 
                == MIMPI_ERROR_REMOTE_FINISHED) {
                return MIMPI_ERROR_REMOTE_FINISHED;
            }
            
            while (temp -> next != NULL || (
                temp -> message != NULL &&
                temp -> message -> tag != tag &&
                temp -> message -> count != count
            )) {
                before_temp = temp;
                temp = temp -> next;
            }
        }        
    }
    else {
        while (temp -> next != NULL || (
            temp -> message != NULL &&
            temp -> message -> count != count
        )) {
            before_temp = temp;
            temp = temp -> next;
        }

        if (temp -> message == NULL || 
            temp -> message -> count != count 
        ) {
            if (waitForMessage(count, source, MIMPI_ANY_TAG) 
                == MIMPI_ERROR_REMOTE_FINISHED) {
                return MIMPI_ERROR_REMOTE_FINISHED;
            }

            while (temp -> next != NULL || (
                temp -> message != NULL &&
                temp -> message -> count != count
            )) {
                before_temp = temp;
                temp = temp -> next;
            }
        }
    }

    printf("%d found a message with count %d, data %d, tag %d from %d\n",
        my_rank, 
        temp -> message -> count, *(int*) temp -> message -> data, 
        temp -> message -> tag, source);
    memcpy(data, temp -> message -> data, count);
            
    before_temp -> next = temp -> next;
    free(temp -> message -> data);
    free(temp -> message);
    free(temp);
    
    ASSERT_ZERO(pthread_mutex_unlock(&mutex_list[source]));
    
    return MIMPI_SUCCESS;
}

// EXTERN FUNCTIONS

MIMPI_Retcode Send(const void* data, int count, int destination, int tag) 
{
    if (tryToPointSend((40 * (destination + 1) + my_rank), 
        &count, sizeof(count)) == -1) {
        return MIMPI_ERROR_REMOTE_FINISHED;
    }

    size_t sent_bytes = 0;
    ssize_t wrote;

    while (sent_bytes < count) {
        size_t to_send = (count - sent_bytes > BUFFER_SIZE) ? BUFFER_SIZE : 
            (count - sent_bytes);

        wrote = tryToPointSendConst((40 * (destination + 1) + my_rank), 
            data + sent_bytes, to_send);

        if (wrote == -1) {
            return MIMPI_ERROR_REMOTE_FINISHED;
        }

        sent_bytes += wrote;
        
    }

    if (tryToPointSend((40 * (destination + 1) + my_rank), 
        &tag, sizeof(tag)) == -1) {
        return MIMPI_ERROR_REMOTE_FINISHED;
    }

    return MIMPI_SUCCESS;
}

void createReaders() {

    for (int i = 0; i < world_size; i++) {
        int* j = malloc(sizeof(int));
        *j = i;
        if (i != my_rank) {
            ASSERT_ZERO(pthread_create(&readers[i], NULL, Reader, (void*) j));
        }
    }
}

/************************ GROUP FUNCTIONS ************************/
// HELPER FUNCTIONS

static u_int8_t* reducer(void* tab1, const void* tab2, int count, MIMPI_Op op) {
    u_int8_t* res_tab = malloc(count);
    switch (op) 
    {
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

// EXTERN FUNCTIONS

MIMPI_Retcode Barrier(void) {
    int to_send = 3;
    int* to_receive = malloc(sizeof(int));
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (left_child < world_size + 1) {
        if (tryToGroupReceive((700 + 6 * me + 1 - 3), 
            to_receive, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) 
    {
        if (tryToGroupReceive((700 + 6 * me + 2 - 3), 
            to_receive, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me != 1) 
    {
        if (tryToGroupSend((700 + 6 * me + 0), &to_send, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }

        if(tryToGroupReceive((700 + 6 * me + 0 - 3), 
            to_receive, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 1), &to_send, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 2), &to_send, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    free(to_receive);

    return MIMPI_SUCCESS;
}

MIMPI_Retcode Bcast(void *data, int count, int root) {
    int to_send = 1;
    int* to_receive = malloc(sizeof(int));
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (root == my_rank && me != 1) {
        if(tryToGroupSend((900 + 4 * me + 2), data, count) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if (tryToGroupReceive((700 + 6 * me + 1 - 3), 
            to_receive, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if (tryToGroupReceive((700 + 6 * me + 2 - 3), 
            to_receive, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me == 1 && root != my_rank) {
        if (tryToGroupReceive((900 + 4 * (root + 1) + 1), data, count) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }   
    }

    if (me != 1) {
        if(tryToGroupSend((700 + 6 * me + 0), &to_send, sizeof(char)) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        if (tryToGroupReceive((700 + 6 * me + 0 - 3), data, count) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 1), data, count) == -1) {
            free(to_receive);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 2), data, count) == -1) {
            free(to_receive);
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
    int to_send = 2;
    int* to_receive = malloc(sizeof(int));
    void* tab1 = malloc(count);
    void* tab2 = malloc(count);
    u_int8_t* res_tab;
    u_int8_t* mid_tab;
    int me = my_rank + 1;
    int left_child = 2 * me;
    int right_child = 2 * me + 1;

    if (left_child < world_size + 1) {
        if (tryToGroupReceive((700 + 6 * me + 1 - 3), tab1, count) == -1) {
            free(to_receive);
            free(tab1);
            free(tab2);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        mid_tab = reducer(tab1, send_data, count, op); 
    }
    else {
        mid_tab = malloc(count);

        memcpy(mid_tab, send_data, count);
    }

    if (right_child < world_size + 1) {
        if (tryToGroupReceive((700 + 6 * me + 2 - 3), tab2, count) == -1) {
            free(to_receive);
            free(tab1);
            free(tab2);
            free(mid_tab);
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        res_tab = reducer(tab2, mid_tab, count, op);
    }
    else {
        res_tab = mid_tab;
    }

    if (me != 1) {
        if(tryToGroupSend((700 + 6 * me + 0), res_tab, count) == -1) {
            free(to_receive);
            free(tab1);
            free(tab2);
            if (res_tab == mid_tab) {
                free(res_tab);
            }
            else {
                free(res_tab);
                free(mid_tab);
            }
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
        if (tryToGroupReceive((700 + 6 * me + 0 - 3), 
            to_receive, sizeof(char)) == -1) {
                free(to_receive);     
            free(tab1);
            free(tab2);
            if (res_tab == mid_tab) {
                free(res_tab);
            }
            else {
                free(res_tab);
                free(mid_tab);
            }
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (left_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 1), &to_send, sizeof(char)) == -1) {
            free(to_receive);     
            free(tab1);
            free(tab2);
            if (res_tab == mid_tab) {
                free(res_tab);
            }
            else {
                free(res_tab);
                free(mid_tab);
            }
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (right_child < world_size + 1) {
        if(tryToGroupSend((700 + 6 * me + 2), &to_send, sizeof(char)) == -1) {
            free(to_receive);     
            free(tab1);
            free(tab2);
            if (res_tab == mid_tab) {
                free(res_tab);
            }
            else {
                free(res_tab);
                free(mid_tab);
            }
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (me == 1 && my_rank != root) {
        if(tryToGroupSend((900 + 4 * (root + 1) + 0), res_tab, count) == -1) {
            free(to_receive);     
            free(tab1);
            free(tab2);
            if (res_tab == mid_tab) {
                free(res_tab);
            }
            else {
                free(res_tab);
                free(mid_tab);
            }
            return MIMPI_ERROR_REMOTE_FINISHED;
        }
    }

    if (my_rank == root) {
        if (me != 1) {
            if (tryToGroupReceive((900 + 4 * me + 3), recv_data, count) == -1) {
                free(to_receive);     
                free(tab1);
                free(tab2);
                if (res_tab == mid_tab) {
                    free(res_tab);
                }
                else {
                    free(res_tab);
                    free(mid_tab);
                }
                return MIMPI_ERROR_REMOTE_FINISHED;
            }
        }
        else {
            memcpy(recv_data, res_tab, count);
        }
    }

    free(to_receive);     
    free(tab1);
    free(tab2);
    if (res_tab == mid_tab) {
        free(res_tab);
    }
    else {
        free(res_tab);
        free(mid_tab);
    }

    return MIMPI_SUCCESS;
}
