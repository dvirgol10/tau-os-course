#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>


void print_error_message(void) {
	perror("");
}


void print_error_message_and_exit(void) {
	print_error_message();
	exit(1);
}


int handle_waitpid(int pid) {
	if (waitpid(pid, NULL, 0) == -1) { // waitpid failed
		if (errno != ECHILD && errno != EINTR) { // those aren't considered as actual errors in our definition 
			print_error_message_and_exit();
		}
		return 0;
	}
	return 1;
}


void SIGCHLDHandler(int signum, siginfo_t *info, void *ptr) { // its formal purpose is to remove zombies
	handle_waitpid(info->si_pid);
}


int prepare(void) {
	signal(SIGINT, SIG_IGN); // ignore SIGINT, that is not terminate upon SIGINT
	struct sigaction newActionForSIGCHLD = {
	  .sa_sigaction = SIGCHLDHandler,
	  .sa_flags = SA_SIGINFO | SA_RESTART
  	};
  	if (sigaction(SIGCHLD, &newActionForSIGCHLD, NULL) != 0) { // sigaction failed
  		return 1;
  	}
	return 0;
}


int finalize(void) {
	while (handle_waitpid(-1)); // wait for all the child processes (more precisely, the background ones) to end (in order to remove zombies). Note that waitpid(-1, ...) is equivalent to wait(...);
	return 0;
}


void register_foreground_signal_handler(void) {
	signal(SIGINT, SIG_DFL); // terminate upon SIGINT
}


void register_background_signal_handler(void) {
	signal(SIGINT, SIG_IGN); // ignore SIGINT, that is not terminate upon SIGINT
}


void execute_command(char** arglist) {
	if (execvp(arglist[0], arglist) == -1) { // execute failed
		print_error_message_and_exit();
	}
}


int is_background(int count, char** arglist) {
	return !strcmp(arglist[count - 1], "&");
}


int is_piping(int count, char** arglist) {
	for (int i = 1; i < count; i++) { // it is guaranteed that the pipe sign "|" would appear before and after at least one word
		if (!strcmp(arglist[i], "|")) {
			return i;
		}
	}
	return 0;
}


int is_output_redirection(int count, char** arglist) {
	return count >= 2 && !strcmp(arglist[count - 2], ">>"); // if count < 2, it means that the command is only one word, which can't be output redirection
}


int exeucte_regular(int count, char** arglist) {
	pid_t pid = fork();
	if (pid == -1) { // fork failed
		print_error_message();
		return 0;
	} else if (pid == 0) { // child process
		register_foreground_signal_handler();
		execute_command(arglist);
	} else { // parent process
		handle_waitpid(pid);
	}
	return 1;
}


int execute_background(int count, char** arglist) {
	pid_t pid = fork();
	if (pid == -1) { // fork failed
		print_error_message();
		return 0;
	} else if (pid == 0) { // child process
		register_background_signal_handler();
		arglist[count - 1] = NULL; // override the "&" sign with NULL because we do not pass this as an argument to execvp
		execute_command(arglist);
	} else { // parent process
		// there is no "wait" here because this execution is in background
	}
	return 1;
}


int execute_piping(int count, char** arglist, int pipe_index) {
	int pipefd[2];
	if (pipe(pipefd) == -1) { // pipe failed
		print_error_message();
		return 0;
	}
	pid_t pid_first = fork();
	if (pid_first == -1) { // fork failed
		close(pipefd[0]);
		close(pipefd[1]);
		print_error_message();
		return 0;
	} else if (pid_first == 0) { // child process
		register_foreground_signal_handler();
		close(pipefd[0]); // this process is the write end, it sends data to the second child
		dup2(pipefd[1], STDOUT_FILENO); // now the stdout will be the write end of the pipe
		arglist[pipe_index] = NULL; // override the "|" sign with NULL because we do not pass this as an argument to execvp, and want to designate that this is the end of the first command
		execute_command(arglist);
	} else { // parent process
		close(pipefd[1]); // there is no more need for the write end
		pid_t pid_second = fork();
		if (pid_second == -1) { // fork failed
			close(pipefd[0]); // close its end - the read end
			handle_waitpid(pid_first); // wait for the first child process to terminate
			print_error_message();
			return 0;
		} else if (pid_second == 0) { // child process
			register_foreground_signal_handler();
			close(pipefd[1]); // this process is the read end, it reads data from the first child
			dup2(pipefd[0], STDIN_FILENO); // now the stdin will be the read end of the pipe
			execute_command(arglist + pipe_index + 1); // the beginning of the second command is right after the "|" sign.
		} else { // parent process
			close(pipefd[0]);
			handle_waitpid(pid_first);
			handle_waitpid(pid_second);
		}
	}
	return 1;
}


int execute_output_redirection(int count, char** arglist) {
	pid_t pid = fork();
	int fd = open(arglist[count - 1], O_APPEND  | O_CREAT | O_WRONLY, S_IRWXU);
	if (fd == -1) { // open failed
		print_error_message();
		return 0;	}
	if (pid == -1) { // fork failed
		close(fd);
		print_error_message();
		return 0;
	} else if (pid == 0) { // child process
		register_foreground_signal_handler();
		dup2(fd, STDOUT_FILENO); // now the stdout will be the file
		arglist[count - 2] = NULL; // override the ">>" sign with NULL because we do not pass this as an argument to execvp, and want to designate that this is the end of the first command
		execute_command(arglist);
	} else { // parent process
		close(fd);
		handle_waitpid(pid);
	}
	return 1;
}


int process_arglist(int count, char** arglist) {
	int pipe_index;
	if (is_background(count, arglist)) {
		return execute_background(count, arglist);
	} else if ((pipe_index = is_piping(count, arglist))){
		return execute_piping(count, arglist, pipe_index);
	} else if (is_output_redirection(count, arglist)) {
		return execute_output_redirection(count, arglist);
	} else {
		return exeucte_regular(count, arglist);
	}
}
