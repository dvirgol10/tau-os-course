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
	if (argc != 4) {
		printf("You must pass 3 arguments\n");
		exit(1);
	}

	int fd = open(argv[1], O_WRONLY);
	if (fd == -1) {
		print_error_message_and_exit("Failed to open the file");
	}
	
	unsigned int channel_id = atoi(argv[2]);
	if (ioctl(fd, MSG_SLOT_CHANNEL, channel_id) == -1) {
		print_error_message_and_exit("Failed to open the message channel");
	}
	
	size_t message_len = strlen(argv[3]);
	ssize_t bytes_written = write(fd, argv[3], message_len); // write the message to the device file
	if (bytes_written == -1 || bytes_written != message_len) {
		print_error_message_and_exit("Failed to write the message");
	}

	if (close(fd) == -1) {
		print_error_message_and_exit("Failed to close the file");
	}

	exit(0); // success 
}
