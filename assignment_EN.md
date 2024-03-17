# MIMPI

[MPI](https://en.wikipedia.org/wiki/Message_Passing_Interface) is a standard communication protocol used for exchanging data between processes of parallel programs, primarily employed in supercomputing. The goal of the task, as suggested by the name *MIMPI* - an acronym for *My Implementation of MPI* - is to implement a small, slightly modified fragment of MPI. You are required to write according to the following specification:

- the program code `mimpirun` (in `mimpirun.c`) responsible for launching parallel computations,
- the implementation of procedures declared in `mimpi.h` in `mimpi.c`.

## Program `mimpirun`

The `mimpirun` program takes the following command-line arguments:

1) $n$ - the number of copies to run (it can be assumed that a natural number in the range from 1 to $16$ inclusive will be passed)
2) $prog$ - the path to the executable file (it may be located in PATH). If the appropriate `exec` call fails (e.g., due to an incorrect path), `mimpirun` should terminate with a non-zero exit code.
3) $args$ - optionally and in any quantity, arguments to be passed to all instances of the running program $prog$

The `mimpirun` program performs the following steps sequentially (each subsequent action starts after the previous one has completed entirely):

1) Prepares the environment (the implementer has discretion over what this means).
2) Launches $n$ copies of the program $prog$, each in a separate process.
3) Waits for all created processes to finish.
4) Exits.

## Assumptions about programs $prog$

- Programs $prog$ can **once** during their execution enter an _MPI block_. To do this, they call the library function `MIMPI_Init` at the beginning and the library function `MIMPI_Finalize` at the end. The mentioned _MPI block_ is understood as the entire code fragment between the aforementioned calls.
- While in the _MPI block_, programs can execute various procedures from the `mimpi` library to communicate with other $prog$ processes.
- They can perform any operations (writing, reading, opening, closing, etc.) on files, whose file descriptor numbers are in the ranges $[0,19]$ and $[1024, \infty)$ (including `STDIN`, `STDOUT`, and `STDERR`).
- They do not modify the values of environment variables starting with the prefix `MIMPI`.
- They expect properly set arguments, i.e., the zeroth argument should be the name of the program $prog$ according to Unix convention, and the subsequent arguments should correspond to the $args$ arguments. _Hint:_ to pass additional data from `mimpirun` to $prog$, you can use functions from the `*env` family: `getenv`, `setenv`, `putenv`.

## Library `mimpi`

You are required to implement the following procedures with signatures from the header file `mimpi.h`:

### Helper Procedures

- `void MIMPI_Init(bool enable_deadlock_detection)`

  Opens the _MPI block_ by initializing the resources needed for the operation of the `mimpi` library. The `enable_deadlock_detection` flag enables deadlock detection until the end of the _MPI block_ (described below in **Enhancement4**).

- `void MIMPI_Finalize()`

  Ends the _MPI block_. All resources associated with the operation of the `mimpi` library:
  - open files
  - communication channels
  - allocated memory
  - synchronization primitives
  - etc.

  should be released before the end of this procedure.

- `int MIMPI_World_size()`

  Returns the number of $prog$ processes launched using the `mimpirun` program (it should be equal to the $n$ parameter passed to the `mimpirun` invocation).

- `void MIMPI_World_rank()`

  Returns a unique identifier within the group of processes launched by `mimpirun`. The identifiers should be consecutive natural numbers from $0$ to $n-1$.

### Point-to-Point Communication Procedures

- `MIMPI_Retcode MIMPI_Send(void const *data, int count, int destination, int tag)`

  Sends data from the address `data`, interpreting it as an array of `count` bytes,
  to the process with rank `destination`, tagging the message with `tag`.

  Performing `MIMPI_Send` for a process that has already exited the _MPI block_ should
  immediately fail with the error code `MIMPI_ERROR_REMOTE_FINISHED`.
  There is no need to handle the situation where the process for which `MIMPI_Send` was executed
  terminates later (after the successful completion of the `MIMPI_send` function in the sending process).

- `MIMPI_Retcode MIMPI_Recv(void *data, int count, int source, int tag)`

  Waits for a message of size (exactly) `count` and tag `tag` from the process
  with rank `rank` and stores its contents at the address `data`
  (the caller is responsible for ensuring sufficient memory allocation).
  The call is blocking, i.e., it finishes only after receiving the entire message.

  Performing `MIMPI_Recv` for a process that
  has not sent a matching message and has already left the _MPI block_ should
  fail with the error code `MIMPI_ERROR_REMOTE_FINISHED`.
  Similar behavior is expected even if the second process leaves the MPI block
  while waiting for `MIMPI_Recv`.

  - Basic version: it can be assumed that each process sends messages in exactly the order
    in which the receiver wants to receive them. However, it **cannot** be assumed that multiple processes **do not** simultaneously send messages to one recipient. It can be assumed that data associated with one message is no larger than 512 bytes.
  - **Enhancement1**: Sent messages can be arbitrarily (reasonably) large, especially larger than the pipe buffer.
  - **Enhancement2**: Nothing can be assumed about the order of sent packets.
    The receiver should be able to buffer incoming packets, and at the time of calling `MIMPI_Recv`, return the first
    (in terms of arrival time) message matching the parameters `count`, `source`, and `tag`
    (there is no need to implement a very sophisticated mechanism for selecting the next matching packet;
    linear complexity relative to the number of all unprocessed packets by the target process
    is sufficient).
  - **Enhancement3**: The receiver should process incoming messages
    concurrently with performing other tasks to avoid message sending channels from overflowing.
    In other words, sending a large number of messages is not blocking even if the target receiver does not process them (because they go to a continuously growing buffer).
  - **Enhancement4**: Deadlock is a situation
    where part of the system is in a state that cannot change anymore
    (there is no sequence of possible future actions of processes that would resolve this deadlock).
    Deadlock of a pair of processes is
    a situation where deadlock is caused by the state of two processes
    (considering whether it can be interrupted, we allow any actions of processes outside the pair - even those that are not allowed in their current state).

    Examples of certain situations constituting deadlocks of pairs of processes in our system are:
    1) a pair of processes mutually executes `MIMPI_Recv` on each other without previously sending a message using `MIMPI_Send` that can end the wait for either of them
    2) one process waits for a message from a process,
    which is already waiting for synchronization related to the call of a group communication procedure

    Within this enhancement, you should implement at least the detection of deadlocks of type 1).
    Detection of deadlocks of other types will not be checked (you can implement them).
    However, deadlocks should not be reported in situations where deadlocks do not occur.

    In case of deadlock detection, an active call to the library function `MIMPI_Recv` in **both processes** of the detected deadlock pair should immediately terminate with an error code `MIMPI_ERROR_DEADLOCK_DETECTED`.

    In the case of multiple deadlocked pairs occurring simultaneously, the call to the library function `MIMPI_Recv` should be interrupted
    in each process of each deadlocked pair.

    Deadlock detection may require sending multiple auxiliary messages, which can significantly slow down the system.
    Therefore, this functionality can be enabled and disabled during the entire operation of the _MPI block_ by setting the appropriate value of the `enable_deadlock detection` flag in the `MIMPI_Init` call that starts this block.

    **Note**: _Enhancement4_ (deadlock detection) requires _Enhancement2_ (message filtering). Partial deadlock detection - without implementing _Enhancement2_ - will be evaluated at 0 points.

### Group Communication Procedures

#### General Requirements

Each group communication procedure $p$ constitutes a **synchronization point** for all processes,
meaning that instructions following the $i$-th invocation of $p$ in any process execute **after** each instruction preceding the $i$-th invocation of $p$ in any other process.

If the synchronization of all processes cannot be completed because one of the processes has already left the MPI block,
calling `MIMPI_Barrier` in at least one process
should result in an error code `MIMPI_ERROR_REMOTE_FINISHED`.
If the process in which this happens reacts to the error by terminating itself,
calling `MIMPI_Barrier` should finish in at least one subsequent process.
By repeating the above behavior, we should reach a situation where each process
leaves the barrier with an error.

#### Efficiency

Each group communication procedure $p$ should be implemented efficiently.
More precisely, assuming deadlock detection is inactive, we require that from the time $p$ is called by the last process
until $p$ finishes in the last process, no more than $\lceil w / 256 \rceil(3\left \lceil\log_2(n+1)-1 \right \rceil t+\epsilon)$ time should elapse, where:

- $n$ is the number of processes
- $t$ is the longest time it takes to execute `chsend` associated with sending one message within a given invocation of the group communication function. Additional information can be found in the example implementation `channel.c` and in the provided tests in the `tests/effectiveness` directory
- $\epsilon$ is a small constant (on the order of at most milliseconds) that does not depend on $t$
- $w$ is the size in bytes of the message processed in the given invocation of the group communication function (in the case of calling `MIMPI_Barrier`, $w=1$ should be assumed)

Additionally, for an implementation to be considered efficient, the transmitted data should not be
burdened with too much metadata.
In particular, we expect that group functions called for data sizes
less than 256 bytes will invoke `chsend` and `chrecv` for packets
of size smaller than or equal to 512 bytes.

Tests from the `tests/effectiveness` directory included in the package check the above-defined concept of efficiency.
Passing them positively is a necessary condition (though not necessarily sufficient)
to earn points for an efficient implementation of group functions.

#### Available Procedures

- `MIMPI_Retcode MIMPI_Barrier()`

  Performs synchronization of all processes.

- `MIMPI_Retcode MIMPI_Bcast(void *data, int count, int root)`

  Sends data provided by the process with rank `root` to all other processes.

- `MIMPI_Retcode MIMPI_Reduce(const void *send_data, void *recv_data, int count, MPI_Op op, int root)`

  Gathers data provided by all processes into `send_data`
  (treating it as an array of `uint8_t` numbers of size `count`)
  and performs on elements with the same indices
  from the `send_data` arrays of all processes (including `root`) the reduction specified by `op`.
  The reduction result, i.e., an array of `uint8_t` of size `count`,
  is stored at the address `recv_data` **only** in the process with rank `root` (**writing to `recv_data` in other processes is not allowed**).

  The following reduction types (values of the `enum` `MIMPI_Op`) are available:
  - `MIMPI_MAX`: maximum
  - `MIMPI_MIN`: minimum
  - `MIMPI_SUM`: sum
  - `MIMPI_PROD`: product

It should be noted that all the above operations on the available data types
are commutative and associative, and `MIMPI_Reduce` should be optimized accordingly.

### `MIMPI_Retcode` Semantics

Refer to the documentation in the `mimpi.h` code:

- documentation of `MIMPI_Retcode`,
- documentation of individual procedures returning `MIMPI_Retcode`.

### Tag Semantics

We adopt the convention:

- `tag > 0` is intended for library users' own purposes,
- `tag = 0` denotes `ANY_TAG`. Its use with `MIMPI_Recv` causes matching with any tag.
It should not be used in `MIMPI_Send` (the effect of use is undefined).
- `tag < 0` is reserved for the library implementers' needs and can be applied to internal communication.

  In particular, this means that user programs (e.g., our test programs) will never directly call the `MIMPI_Send` or `MIMPI_Recv` procedure with a tag `< 0`.

## Inter-Process Communication

The MPI standard is designed with supercomputing computations in mind.
Therefore, communication between individual processes usually takes place over the network
and is slower than data exchange within a single computer.

To better simulate the environment of a real library and thus address its implementation issues,
inter-process communication must be carried out **exclusively**
using channels provided in the `channel.h` library.
The `channel.h` library provides the following functions for channel handling:

- `void channels_init()` - initializes the channel library
- `void channels_finalize()` - finalizes the channel library
- `int channel(int pipefd[2])` - creates a channel
- `int chsend(int __fd, const void *__buf, size_t __n)` - sends a message
- `int chrecv(int __fd, void *__buf, size_t __nbytes)` - receives a message

`channel`, `chsend`, `chrecv` work similarly to `pipe`, `write`, and `read`, respectively.
The idea is that the only significant (from the perspective of solving the task)
difference in the behavior of functions provided by `channel.h` is
that they may have significantly longer execution times than their originals.
In particular, the provided functions:

- have the same signature as the original functions
- similarly create entries in the file descriptor table
- guarantee atomicity of reads and writes up to 512 bytes inclusive
- guarantee the presence of a buffer of at least 4 KB
- ... _(if unclear, ask)_

**NOTE:**
It is essential to call the following helper functions: `channels_init` with `MIMPI_Init`,
and `channels_finalize` with `MIMPI_Finalize`.

All reads and writes on file descriptors returned by the `channel` function
must be performed using `chsend` and `chrecv`.
Additionally, no system functions modifying file properties such as `fcntl`
should be called on file descriptors returned by the `channel` function.

Failure to adhere to the above recommendations may result in **total loss of points.**

It should be remembered that the above guarantees regarding the `chsend` and `chrecv` functions
do not imply that they will not process fewer bytes than requested.
This may happen if the size exceeds the guaranteed channel buffer size,
or if the amount of data in the input buffer is insufficient.
Implementations must handle this situation correctly.

## Notes

### General

- The `mimpirun` program or any of the functions from the `mimpi` library **cannot** create named files in the file system.
- The `mimpirun` program and functions from the `mimpi` library may use descriptors
  with numbers in the range $[ 20, 1023 ]$ in any way.
  Additionally, it can be assumed that descriptors from the above range are not occupied at the time
  of running the `mimpirun` program.
- The `mimpirun` program or any of the functions from the `mimpi` library **cannot** modify existing
  entries in the file descriptor table from positions outside $[ 20, 1023 ]$.
- The `mimpirun` program or any of the functions from the `mimpi` library **cannot** perform any
  operations on files they did not open themselves (especially on `STDIN`, `STDOUT`, and `STDERR`).
- Active or semi-active waiting is not allowed at any point.
  - Therefore, no sleep functions should be used (`sleep`, `usleep`, `nanosleep`) or their variants with timeouts (such as `select`).
  - Only waiting for events independent of time, e.g., message arrival, is allowed.
- Solutions will be tested for memory leaks and/or other resource leaks (unclosed files, etc.).
  Carefully examine and test paths that may lead to leaks.
- It can be assumed that corresponding $i$-th calls to group communication functions
  in different processes are of the same type (the same functions) and have the same parameter values `count`, `root`, and `op`
  (if the current function type has the parameter).
- In case of an error in a system function, the calling program should exit with a nonzero exit code, e.g.,
  by using the provided `ASSERT_SYS_OK` macro.
- If programs $prog$ use the library in a way inconsistent with the guarantees mentioned in this content,
  any action may be taken (we will not check such situations).

### Library `MIMPI`
- The implemented functions do not need to be thread-safe, i.e., it can be assumed that they are not called from multiple threads simultaneously.
- Function implementations should be reasonably efficient, meaning that handling extreme loads (e.g., processing hundreds of thousands of messages) should not add a significant overhead beyond the expected runtime (e.g., waiting for a message) on the order of tens of milliseconds (or more).
- Executing a procedure other than `MIMPI_Init` outside the MPI block has undefined behavior.
- Calling `MIMPI_Init` multiple times has undefined behavior.
- We guarantee that `channels_init` sets the `SIGPIPE` signal handling to be ignored. This will facilitate dealing with the requirement that `MIMPI_Send` returns `MIMPI_ERROR_REMOTE_FINISHED` in the appropriate situation.

## Allowed Languages and Libraries

We require the use of the C language in the `gnu11` version (plain `c11` does not provide access to many useful functions from the standard library). There is no choice left, as the task is intended, among other things, to deepen skills in using the C language.

You can use the standard C library (`libc`), the `pthread` library, and the functionality provided by the system (declared in `unistd.h`, etc.).

**Not allowed** is the use of other external libraries.

You can borrow any code from the laboratories. All other potential code borrowings must be properly commented with the source provided.

## Package Description

The package contains the following files not part of the solution:

- `examples/*`: simple example programs using the `mimpi` library
- `tests/*`: tests checking various configurations of running example programs using `mimpirun`
- `assignment.md`: this description
- `channel.h`: header file declaring functions for inter-process communication
- `channel.c`: an example implementation of `channel.h`
- `mimpi.h`: header file declaring functions of the `MIMPI` library
- `Makefile`: sample file automating the compilation of `mimpirun`, example programs, and running tests
- `self`: auxiliary script to run tests provided in the assignment
- `test`: script running all tests from the `tests/` directory
- `test_on_public_repo`: script running tests according to the [grading scheme](#grading-scheme) below
- `files_allowed_for_change`: script listing files that may be modified
- `template_hash`: file specifying the version of the template used to prepare the solution

Templates to fill in:

- `mimpi.c`: file containing skeletons of the implementation of the `MIMPI` library functions
- `mimpirun.c`: file containing the skeleton of the `mimpirun` program implementation
- `mimpi_common.h`: header file for declaring common functionalities of the `MIMPI` library and `mimpirun` program
- `mimpi_common.c`: file for implementing common functionalities of the `MIMPI` library and `mimpirun` program

### Helpful Commands

- build `mimpirun` and all examples from the `examples/` directory: `make`
- run local tests: `./test`
- run tests according to the official grading scheme: `./test_on_public_repo`
  
  The above command allows ensuring that the solution meets the technical requirements listed in the [grading scheme](#grading-scheme).
- list all files opened by processes launched by `mimpirun`: e.g., `./mimpirun 2 ls -l /proc/self/fd`
- trace memory errors, memory leaks, and resources:
  
  The `valgrind` tool may be useful, especially with the flags:
  - `--track-origins=yes`
  - `--track-fds=yes`
  
  A tool with a narrower scope, but also helpful for debugging, may be _ASAN_ (Address Sanitizer).
    It is activated by passing the `-fsanitize=address` flag to `gcc`.

## Expected Solution

### Standard Variant (recommended, simple and safe)

To complete the task:

1) Fill in the solution template according to the specification, changing only the files listed in `files_allowed_for_change` (in particular, you should fill in at least the `mimpi.c` and `mimpirun.c` files).
2) Make sure the solution fits the required [grading scheme](#grading-scheme) by running `./test_on_public_repo`
3) Export the solution using `make assignment.zip && mv assignment.zip ab123456.zip`
   (replacing `ab123456` with your student login)
   and upload the resulting archive to Moodle on time.

### Non-Standard Variant

If someone does not like the provided template and is highly motivated, they can change it.
First, communicate with the task authors for initial verification of the need.
After a positive verification, the following steps need to be taken:

1) Create a fork of the public repository with the mentioned template.
2) Update the created fork with the modifications made by you **ensuring that they do not contain any parts of the solution**.
3) Open a pull request to the main repository, describing the changes.
4) During the discussion process, the modifications or the new template may be accepted or rejected.
5) Continue working using the newly created template (switching to the appropriate branch)

It is important that the updated template fits the grading scheme below, especially having the `template_hash` file appropriately updated.

#### About Student Tests

The `make` command automatically tries to build
all programs from `examples/` to `examples_build`.
Then, running `./test` executes all recognized tests from the `tests/` directory.
The following convention is accepted:

- `*.self` files are a special type of test executed using the provided auxiliary script `self`.
  In the first line, they specify the command to execute.
  Then, from the 3rd line to the end of the file, the expected `STDOUT` value that the execution of the above command should generate.
- Files `*.sh` are arbitrary shell scripts. They can execute any logic.
  They should only exit with code $0$ in case of success (passing the test) and nonzero code in case of error.

If you prepare your own tests matching the above schema, we encourage you to share them with others.
To do so, you just need to somehow provide them to the task authors (preferably via a pull request).
To encourage you for this effort, we will try to run such published tests on the reference solution
and provide feedback if they are incorrect.

## Grading Scheme

Uploaded solutions will be built and tested according to the following scheme:

1) A `zip` package with the solution with the appropriate name `ab123456.zip` (replace with your student login)
   will be downloaded from Moodle and unpacked.
2) A clean clone $K$ of the public repository containing the template provided to you will be created.
3) A branch will be selected in $K$ based on the `template_hash` value from your solution.
4) Those files from your solution that are listed in `files_allowed_for_change` will be copied to $K$ from the **version** in $K$.
5) A set of tests prepared by us (appropriate files in `examples/` and `tests/`) will be copied to $K$.
6) `make` will be called in $K$ to build your solution and all examples.
7) `./test` will be called in $K$.
8) The solution will be evaluated based on the set of tests that your solution passed.

Successfully executing the above build process
and passing the following automatic tests:

- hello
- send_recv

is a necessary condition to receive any points.
To check the compliance of your solution with the grading scheme above, use the `./test_on_public_repo` script.
We encourage you to use it.

**Note:** In case of discrepancies in the results of the above process, the evaluation will consider the operation on the `students` server.

## Evaluated Components of the Solution

**Note**: The following scoring is **solely** indicative and may change at any time.
Its purpose is to facilitate easier estimation of the difficulty/complexity of a given functionality.

### Base: (3p)

- implementation of the `mimpirun` program + procedures `MIMPI_Init` and `MIMPI_Finalize` + passing the `hello` example/test correctly (1p)
- implementation of point-to-point functions (assuming messages up to 500B in size) (1p)
- implementation of collective functions (message size as above) (1p)

### Improvements (7p)

- efficient (logarithmic) collective functions (2p)
  - `MIMPI_Barrier` (1p)
  - `MIMPI_Bcast` and `MIMPI_Reduce` (1p)
- (`MIMPI_Recv`::Improvement1) arbitrarily large messages (arbitrarily over 500B) (1p)
- (`MIMPI_Recv`::Improvement2) handling arbitrary message transmission order (filtering by tags, size), including handling the `MIMPI_ANY_TAG` tag (1p)
- (`MIMPI_Recv`::Improvement3) non-blocking channels (1p)
  - no upper limit on data size until the sending process is blocked
  - no waiting when calling `MIMPI_Send`
- (`MIMPI_Recv`::Improvement4) deadlock detection between pairs of processes (2p)

We listed improvements in the order we suggest prioritizing them.
