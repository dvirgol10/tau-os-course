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
#include <errno.h>


#define BACKLOG 10
#define LOWEST_PRINTABLE 32
#define HIGHEST_PRINTABLE 126
#define N_PRINTABLE HIGHEST_PRINTABLE - LOWEST_PRINTABLE + 1

void print_error_message(const char* s) {
	perror(s);
}


void print_error_message_and_exit(const char* s) {
	print_error_message(s);
	exit(1);
}


int is_printable_character(char c) {
	return LOWEST_PRINTABLE <= c && c <= HIGHEST_PRINTABLE;
}


void print_pcc(uint64_t* pcc_total) {
	for (int i = 0; i < N_PRINTABLE; i++) {
		printf("char '%c' : %lu times\n", i + LOWEST_PRINTABLE, pcc_total[i]);
	}
}


int is_tcp_error() {
	return errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE
}


int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("You must pass 1 argument\n");
		exit(1);
	}
	uint16_t serv_port = atoi(argv[1]);
	

	uint64_t pcc_total[N_PRINTABLE];
	memset(pcc_total, 0, sizeof(pcc_total));

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		print_error_message_and_exit("Failed to create a socket");
	}

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		print_error_message_and_exit("Failed to set 'SO_REUSEADDR' option");
	}

	struct sockaddr_in serv_addr; // where we want to get to
	socklen_t addrsize = sizeof(struct sockaddr_in);
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(serv_port); // Note: htons for endiannes
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(listenfd, (struct sockaddr*) &serv_addr, addrsize) == -1) {
		print_error_message_and_exit("Failed to bind");
	}

	if (listen(listenfd, BACKLOG) == -1) {
		print_error_message_and_exit("Failed to listen");
	}

	int connfd;
	int tcp_error;
	uint64_t pcc_tmp[N_PRINTABLE];
	while (1) {
		tcp_error = 0;
		if ((connfd = accept(listenfd, NULL, NULL)) == -1) {
			print_error_message_and_exit("Failed to accept a connection");
		}

		memset(pcc_tmp, 0, sizeof(pcc_tmp));

		uint64_t file_size_be;
		uint64_t file_size;
		uint64_t bytes_recv = 0;
		uint64_t bytes_read = 0;
		while (bytes_recv < sizeof(file_size_be)) {
			if ((bytes_read = read(connfd, &file_size_be + bytes_recv, sizeof(file_size_be) - bytes_recv)) == -1) {
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					break;
				}
				print_error_message_and_exit("Failed to receive data from the server");
			} else if (bytes_read == 0) {
				fprintf(stderr, "EOF error");
				tcp_error = 1;
				break;
			}
			bytes_recv += bytes_read;
		}
		
		if (tcp_error) {
			continue;
		}

		file_size = be64toh(file_size_be);

		char c;
		int n_pc = 0;
		bytes_recv = 0;
		bytes_read = 0;
		while (bytes_recv < file_size) {
			if ((bytes_read = read(connfd, &c, sizeof(c))) == -1) {
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					break;
				}
				print_error_message_and_exit("Failed to receive data from the server");
			} else if (bytes_read == 0) {
				fprintf(stderr, "EOF error");
				tcp_error = 1;
				break;
			}
			bytes_recv += bytes_read;
			if (is_printable_character(c)) {
				n_pc += 1;
				pcc_tmp[c - LOWEST_PRINTABLE] += 1;
			}
		}

		if (tcp_error) {
			continue;
		}

		uint64_t n_pc_be = htobe64(n_pc);
		uint64_t bytes_sent = 0;
		uint64_t bytes_written = 0;
		while (bytes_sent < sizeof(n_pc_be)) {
			if ((bytes_written = write(connfd, &n_pc_be + bytes_sent, sizeof(n_pc_be) - bytes_sent)) == -1) {
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					break;
				}
				print_error_message_and_exit("Failed to send data to the server");
			}
			bytes_sent += bytes_written;
		}

		if (tcp_error) {
			continue;
		}

		if (close(connfd) == -1) {
			if (is_tcp_error()) {
				print_error_message("Failed to read due to a TCP error");
				tcp_error = 1;
				break;
			}
			print_error_message_and_exit("Failed to close the socket");
		}

		if (tcp_error) {
			continue;
		}

		for (int i = 0; i < N_PRINTABLE; i++) {
			pcc_total[i] += pcc_tmp[i];
		}

		print_pcc(pcc_total); //TODO remove
	}

	return 0;
}
