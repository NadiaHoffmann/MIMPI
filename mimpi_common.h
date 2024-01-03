/**
 * This file is for declarations of  common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#ifndef MIMPI_COMMON_H
#define MIMPI_COMMON_H

#include <assert.h>
#include <stdbool.h>
#include <stdnoreturn.h>

#include <pthread.h>
#include "mimpi.h"
#include "channel.h"

/*
    Assert that expression doesn't evaluate to -1 (as almost every system function does in case of error).

    Use as a function, with a semicolon, like: ASSERT_SYS_OK(close(fd));
    (This is implemented with a 'do { ... } while(0)' block so that it can be used between if () and else.)
*/
#define ASSERT_SYS_OK(expr)                                                                \
    do {                                                                                   \
        if ((expr) == -1)                                                                  \
            syserr(                                                                        \
                "system command failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ", \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Assert that expression evaluates to zero (otherwise use result as error number, as in pthreads). */
#define ASSERT_ZERO(expr)                                                                  \
    do {                                                                                   \
        int const _errno = (expr);                                                         \
        if (_errno != 0)                                                                   \
            syserr(                                                                        \
                "Failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ",                \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Prints with information about system error (errno) and quits. */
_Noreturn extern void syserr(const char* fmt, ...);

/* Prints (like printf) and quits. */
_Noreturn extern void fatal(const char* fmt, ...);

#define TODO fatal("UNIMPLEMENTED function %s", __PRETTY_FUNCTION__);


/////////////////////////////////////////////
// Put your declarations here

/************************ FUNCTIONS FOR INIT ************************/
void setMyRank(int);
void setWorldSize(int);
void setDeadlocks(bool);
void initMutexes();
void createReaders();

/************************ FUNCTIONS FOR FINALIZE ************************/
void closeGroupPipes();
void closePointToPointPipes();
void destroyMutexes();
void killReaders();

/************************ POINT TO POINT FUNCTIONS ************************/
MIMPI_Retcode Send(const void*, int, int, int);
MIMPI_Retcode Search(void*, int, int, int);

/************************ GROUP FUNCTIONS ************************/
MIMPI_Retcode Barrier();
MIMPI_Retcode Bcast(void*, int, int);
MIMPI_Retcode Reduce(void const *, void*, int, MIMPI_Op, int);

#endif // MIMPI_COMMON_H