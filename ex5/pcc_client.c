#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <endian.h>


#define MAX_BUF_LEN 1024


void print_error_message(const char* s) {
	perror(s);
}


void print_error_message_and_exit(const char* s) {
	print_error_message(s);
	exit(1);
}


uint64_t get_file_size(char* path) {
	struct stat buf;
	if (stat(path, &buf) == -1) {
		print_error_message_and_exit("Failed to get information on the file");
	}
	return buf.st_size;
}


int main(int argc, char *argv[]) {
	if (argc != 4) {
		printf("You must pass 3 arguments\n");
		exit(1);
	}
	char* serv_ip = argv[1];
	uint16_t serv_port = atoi(argv[2]);
	char* file_path = argv[3];

	int fd = open(file_path, O_RDONLY);
	if (fd == -1) {
		print_error_message_and_exit("Failed to open the input file");
	}

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		print_error_message_and_exit("Failed to create a socket");
	}

	struct in_addr sin_addr;
	if (inet_pton(AF_INET, serv_ip, &sin_addr) == -1) { // we don't have to handle the invalid return value '0' because we can assume serv_ip is a valid IP address
		print_error_message_and_exit("Failed to convert the server IP address from text to binary");
	}

	struct sockaddr_in serv_addr; // where we want to get to
	socklen_t addrsize = sizeof(struct sockaddr_in);
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(serv_port); // Note: htons for endiannes
	serv_addr.sin_addr = sin_addr;

	if (connect(sockfd, (struct sockaddr*) &serv_addr, addrsize) == -1) {
		print_error_message_and_exit("Failed to connect to the server");
	}

	uint64_t bytes_sent = 0;
	uint64_t bytes_written = 0;
	uint64_t file_size = get_file_size(file_path);
	uint64_t file_size_be = htobe64(file_size);
	// part (a) of the proctocol
	while (bytes_sent < sizeof(file_size_be)) {
		if ((bytes_written = write(sockfd, &file_size_be + bytes_sent, sizeof(file_size_be) - bytes_sent)) == -1) {
			print_error_message_and_exit("Failed to send data to the server");
		}
		bytes_sent += bytes_written;
	}


	char buf[MAX_BUF_LEN];
	bytes_written = 0;
	bytes_sent = 0;
	uint64_t bytes_read = 0;
	// part (b) of the proctocol
	while (bytes_sent < file_size) {
		if ((bytes_read = read(fd, buf, sizeof(buf))) == -1) {
			print_error_message_and_exit("Failed to read the file content");
		} else if (bytes_read == 0) {
			fprintf(stderr, "EOF error");
			exit(1);
		}
		if ((bytes_written = write(sockfd, buf, bytes_read)) == -1) {
			print_error_message_and_exit("Failed to send data to the server");
		}
		bytes_sent += bytes_written;
	}

	uint64_t n_pc_be = 0;
	uint64_t bytes_recv = 0;
	bytes_read = 0;
	// part (c) of the proctocol
	while (bytes_recv < sizeof(n_pc_be)) {
		if ((bytes_read = read(sockfd, &n_pc_be + bytes_recv, sizeof(n_pc_be) - bytes_recv)) == -1) {
			print_error_message_and_exit("Failed to receive data from the server");
		} else if (bytes_read == 0) {
			fprintf(stderr, "EOF error");
			exit(1);
		}
		bytes_recv += bytes_read;
	}

	if (close(sockfd) == -1) {
		print_error_message_and_exit("Failed to close the socket");
	}

	printf("# of printable characters %lu\n", be64toh(n_pc_be));
	return 0;
}