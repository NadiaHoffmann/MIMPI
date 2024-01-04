/**
 * This file is for implementation of mimpirun program.
 * */

#include "mimpi_common.h"
#include "channel.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>

static int pipes[16][16][2];
static int forGroups[16][4][2];
static int firstAndOthers[16][2][2];

int main(int argc, char **argv) {
    int no_args = argc;
    char** main_args = argv;
    if (no_args < 3) {
        return -1;
    }

    int n = atoi(main_args[1]);
    char* path = main_args[2];
    int prog_args = no_args - 1;
    char* args[prog_args];
    args[0] = (char*) malloc(strlen(basename(path)) + 3);
    ASSERT_SYS_OK(sprintf(args[0], "./%s", basename(path)));
    for (int i = 3; i < no_args; i++) {
        args[i - 2] = main_args[i];
    }
    args[prog_args - 1] = NULL;

    char* name = (char*) malloc(strlen("MIMPI_n=") + 3);
    ASSERT_SYS_OK(sprintf(name, "MIMPI_n=%d", n));
    ASSERT_ZERO(putenv(name));

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i != j) {
                ASSERT_SYS_OK(channel(pipes[i][j]));
                ASSERT_SYS_OK(dup2(pipes[i][j][0], (20 * (2 * i + 1) + j)));
                ASSERT_SYS_OK(close(pipes[i][j][0]));
                ASSERT_SYS_OK(dup2(pipes[i][j][1], (40 * (i + 1) + j)));
                ASSERT_SYS_OK(close(pipes[i][j][1]));
            }
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 2; j++) {
            int me = i + 1;
            int id = j + 1;
            ASSERT_SYS_OK(channel(forGroups[i][j]));
            ASSERT_SYS_OK(dup2(forGroups[i][j][0], (700 + 6 * me - 3 + id)));
            ASSERT_SYS_OK(close(forGroups[i][j][0]));
            ASSERT_SYS_OK(dup2(forGroups[i][j][1], (700 + 6 * (2 * me + (id - 1)))));
            ASSERT_SYS_OK(close(forGroups[i][j][1]));

            ASSERT_SYS_OK(channel(forGroups[i][j+2]));
            ASSERT_SYS_OK(dup2(forGroups[i][j+2][0], (700 + 6 * (2 * me + j) - 3)));
            ASSERT_SYS_OK(close(forGroups[i][j+2][0]));
            ASSERT_SYS_OK(dup2(forGroups[i][j+2][1], (700 + 6 * me + id)));
            ASSERT_SYS_OK(close(forGroups[i][j+2][1]));
        }
    }

    for (int i = 1; i < n; i++) {
        int me = i + 1;
        ASSERT_SYS_OK(channel(firstAndOthers[i][0]));
        ASSERT_SYS_OK(dup2(firstAndOthers[i][0][0], (900 + 4 * me + 1)));
        ASSERT_SYS_OK(close(firstAndOthers[i][0][0]));
        ASSERT_SYS_OK(dup2(firstAndOthers[i][0][1], (900 + 4 * me + 2)));
        ASSERT_SYS_OK(close(firstAndOthers[i][0][1]));

        ASSERT_SYS_OK(channel(firstAndOthers[i][1]));
        ASSERT_SYS_OK(dup2(firstAndOthers[i][1][0], (900 + 4 * me + 3)));
        ASSERT_SYS_OK(close(firstAndOthers[i][1][0]));
        ASSERT_SYS_OK(dup2(firstAndOthers[i][1][1], (900 + 4 * me + 0)));
        ASSERT_SYS_OK(close(firstAndOthers[i][1][1]));
    }

    int status;
    char* pid = (char*) malloc(strlen("MIMPI_=") + 20 + 2 + 1);
    for (int k = 0; k < n; k++) {
        status = fork();
        if (status == 0) {
            ASSERT_SYS_OK(sprintf(pid, "MIMPI_%d=%d", getpid(), k));
            ASSERT_ZERO(putenv(pid));
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    if (i != j) {
                        if (i != k) {
                            ASSERT_SYS_OK(close((20 * (2 * i + 1)) + j));
                        } 
                        
                        if (j != k) {
                            ASSERT_SYS_OK(close((40 * (i + 1)) + j));
                        } 
                    }              
                }
            }

            for (int i = 0; i < n; i++) {
                int me = i + 1;
                for(int j = 0; j < 3; j++) {
                    if (i != 0 || (i == 0 && j != 0)) {
                        if (i != k) {
                            ASSERT_SYS_OK(close(700 + 6 * me + j));
                            ASSERT_SYS_OK(close(700 + 6 * me + j - 3));
                        }
                    }
                }
            }

            if (k == 0) {
                for (int i = 2; i <= n; i++) {
                    ASSERT_SYS_OK(close(900 + 4 * i + 2));
                    ASSERT_SYS_OK(close(900 + 4 * i + 3));
                }
            }
            else {
                for (int i = 2; i <= n; i++) {
                    if (i != k + 1) {
                        for (int j = 0; j < 4; j++)
                            ASSERT_SYS_OK(close(900 + 4 * i + j));
                    }
                    else {
                        ASSERT_SYS_OK(close(900 + 4 * i + 1));
                        ASSERT_SYS_OK(close(900 + 4 * i + 0));
                    }
                }
            }

            ASSERT_SYS_OK(execvp(path, args));
        }
        else if (status < 0) {
            exit(-1);
        }
    }

    for (int i = 0; i < n; i++) {
        int me = i + 1;
        for (int j = 0; j <= 1; j++) {
            ASSERT_SYS_OK(close(700 + 6 * me - 3 + j + 1));
            ASSERT_SYS_OK(close(700 + 6 * (2 * me + j)));
            ASSERT_SYS_OK(close(700 + 6 * (2 * me + j) - 3));
            ASSERT_SYS_OK(close(700 + 6 * me + j + 1));
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i != j) {
                ASSERT_SYS_OK(close((20 * (2 * i + 1)) + j));
                ASSERT_SYS_OK(close((40 * (i + 1)) + j));
            }                
        }
    }

    for (int i = 2; i <= n; i++) {
        for (int j = 0; j < 4; j++) {
            ASSERT_SYS_OK(close(900 + 4 * i + j));
        }
    }

    char* temp = (char*) malloc(strlen("MIMPI_") + 20 + 1);
    for (int i = 0; i < n; i++) {
        pid_t child = wait(NULL);
        if (child > 0) {
            ASSERT_SYS_OK(sprintf(temp, "MIMPI_%d", child));
            ASSERT_ZERO(unsetenv(temp));
        }
        else {
            exit(-1);
        }
    }

    ASSERT_ZERO(unsetenv("MIMPI_n"));
    free(pid);
    free(name);
    free(temp);
    free(args[0]);

    return 0;
}