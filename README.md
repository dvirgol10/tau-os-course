# First Assignment
## Multi-Level Page Tables Assignment
The goal is to implement simulated OS code that handles a multi-level trie-based page table. The functions we implement create/destroy virtual memory mappings in a page table, and check what physical page number a certain virtual page number is mapped to, if exists.

# Second Assignment
## Mini Shell Assignment
The goal is to gain experience with process management, pipes, signals and so on. We implement a function which recieves a shell command and performs it, and also implement functions used for initialization/finalization of the shell.\
The shell supports executing regular commands, executing commands in the background, single piping and output redirection. It also handles SIGINT (which can be sent by pressing Ctrl-C) such that the shell and "background commands" don't terminate upon SIGINT, whether "foreground commands" (regular commands, parts of a pipe or output redirection) do.
