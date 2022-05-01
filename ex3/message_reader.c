#include "message_slot.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>


void print_error_message(const char* s) {
	perror(s);
}


void print_error_message_and_exit(const char* s) {
	print_error_message(s);
	exit(1);
}


int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("You must pass 2 arguments\n");
		exit(1);
	}

	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		print_error_message_and_exit("Failed to open the file");
	}
	
	unsigned int channel_id = atoi(argv[2]);
	if (ioctl(fd, MSG_SLOT_CHANNEL, channel_id) == -1) {
		print_error_message_and_exit("Failed to open the message channel");
	}
	
	char buf[MAX_MSG_LEN + 1];
	ssize_t bytes_read = read(fd, buf, MAX_MSG_LEN);
	if (bytes_read == -1) {
		print_error_message_and_exit("Failed to read the message");
	}

	if (close(fd) == -1) {
		print_error_message_and_exit("Failed to close the file");
	}

	ssize_t bytes_written = write(STDOUT_FILENO, buf, bytes_read); // print the message to standard output
	if (bytes_written == -1 || bytes_written != bytes_read) {
		print_error_message_and_exit("Failed to print the message");
	}

	exit(0); // success 
}
