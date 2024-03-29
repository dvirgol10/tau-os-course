# First Assignment
## Multi-Level Page Tables Assignment
The goal is to implement simulated OS code that handles a multi-level trie-based page table. The functions we implement create/destroy virtual memory mappings in a page table, and check what physical page number a certain virtual page number is mapped to, if exists.

# Second Assignment
## Mini Shell Assignment
The goal is to gain experience with process management, pipes, signals and so on. We implement a function which recieves a shell command and performs it, and also implement functions used for initialization/finalization of the shell.\
The shell supports executing regular commands, executing commands in the background, single piping and output redirection. It also handles SIGINT (which can be sent by pressing Ctrl-C) such that the shell and "background commands" don't terminate upon SIGINT, whether "foreground commands" (regular commands, parts of a pipe or output redirection) do.

# Third Assignment
## Message Slot Kernel Module Assignment
The goal is to gain experience with kernel programming and a better understanging on the design and implementation of IPC, kernel moudules, and drivers. We implement a new IPC mechanism, called a _message slot_. This is a character device file which has multiple _message channels_ active concurrently. These channels can be used by multiple processes, which specify (with `ioctl()`) the id of the message channel they want to receive/send messages from/to (by using `read()/write()`).

# Fourth Assignment
## Parallel File Find Assignment
The goal is to gain experience with threads (including locks and conditional variables) and filesystem system calls. We create a program which searches a directory tree for files whose name contains the search term. It parallelizes its work using threads - different directories are searched by different threads.

# Fifth Assignment
## Printable Characters Counting Server Assignment
The goal is to gain experience with sockets and network programming. We implement a client-server architecture - a printable characters counting server.\
Clients connect to the server and send it a stream of bytes (which are read from a file). The server sends the count back to the client and maintains a data structure in which it counts the number of times each printable character was observed in all the connections.\
In addition, SIGINT makes the server print these counts and exit.
