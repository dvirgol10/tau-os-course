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
#include <signal.h>
#include <stdatomic.h>


#define BACKLOG 10
#define LOWEST_PRINTABLE 32
#define HIGHEST_PRINTABLE 126
#define N_PRINTABLE HIGHEST_PRINTABLE - LOWEST_PRINTABLE + 1


int listenfd;
atomic_int sigint_flag = 0;


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
	return errno == ETIMEDOUT || errno == ECONNRESET || errno == EPIPE;
}


void SIGINTHandler() {
	if (close(listenfd) == -1) { // we do this so a call to "accept" would fail and we will be able to identify that SIGINT occurred
		print_error_message_and_exit("Failed to close the listening socket while handling SIGINT");
	}
	sigint_flag = 1;
}


void register_SIGINT_handler() {
	struct sigaction newActionForSIGINT = {
		.sa_handler = SIGINTHandler,
  	};
	if (sigaction(SIGINT, &newActionForSIGINT, NULL) == -1) { // sigaction failed
		print_error_message_and_exit("Failed to register new action for SIGINT");
	}
}


int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "You must pass 1 argument\n");
		exit(1);
	}
	uint16_t serv_port = atoi(argv[1]);
	
	uint64_t pcc_total[N_PRINTABLE];
	memset(pcc_total, 0, sizeof(pcc_total));

	register_SIGINT_handler();

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		print_error_message_and_exit("Failed to create a socket");
	}

	// using this we are able to reuse the current address in the following executions
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		print_error_message_and_exit("Failed to set 'SO_REUSEADDR' option");
	}

	//construct the server address data structure
	struct sockaddr_in serv_addr; // where we want to get to
	socklen_t addrsize = sizeof(struct sockaddr_in);
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(serv_port); // Note: htons for endiannes
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // we use it to accept connections on all network interfaces

	if (bind(listenfd, (struct sockaddr*) &serv_addr, addrsize) == -1) {
		print_error_message_and_exit("Failed to bind");
	}

	if (listen(listenfd, BACKLOG) == -1) {
		print_error_message_and_exit("Failed to listen");
	}

	int connfd;
	int tcp_error;
	uint64_t pcc_tmp[N_PRINTABLE]; // the pcc data structure which is relevant for current connection
	while (!sigint_flag) { // while we aren't getting SIGINT
		/* in each stage we want to verify that we don't get a TCP error.
		If we do, we want to continue to the next connection without updating the pcc data structure */
		tcp_error = 0;
		if ((connfd = accept(listenfd, NULL, NULL)) == -1) {
			/* because of "close(listenfd)" in SIGINTHandler, a SIGINT will cause "accept" to return -1 (indicating an error),
			and the sigint_flag is set */
			if (sigint_flag) {
				break;
			}
			print_error_message_and_exit("Failed to accept a connection");
		}

		memset(pcc_tmp, 0, sizeof(pcc_tmp));

		// part (a) of the proctocol
		uint64_t file_size_be;
		uint64_t file_size;
		uint64_t bytes_recv = 0;
		uint64_t bytes_read = 0;
		while (bytes_recv < sizeof(file_size_be)) { // receive the message length (file size in bytes) from client
			if ((bytes_read = read(connfd, &file_size_be + bytes_recv, sizeof(file_size_be) - bytes_recv)) == -1) {
				/* if we get a SIGINT while the syscall is being run,
				the SIGINT handler executes and the return value of the syscall is -1 */
				if (sigint_flag) {
					/* we continue because we want to end the current session and
					after that the outer while loop will end */
					continue;
				}
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					/* break from the current while loop, and the check after it would recognize
					the problem and move on to the next connection */
					break;
				}
				print_error_message_and_exit("Failed to receive data from the server");
			} else if (bytes_read == 0) {
				fprintf(stderr, "EOF error\n");
				tcp_error = 1;
				break;
			}
			bytes_recv += bytes_read;
		}
		
		if (tcp_error) { // check for a tcp error
			continue;
		}

		// part (b) of the proctocol
		file_size = be64toh(file_size_be);
		char c;
		int n_pc = 0;
		bytes_recv = 0;
		bytes_read = 0;
		while (bytes_recv < file_size) { // receive the message itself (the file's content) from client
			if ((bytes_read = read(connfd, &c, sizeof(c))) == -1) {
				/* if we get a SIGINT while the syscall is being run,
				the SIGINT handler executes and the return value of the syscall is -1 */
				if (sigint_flag) {
					/* we continue because we want to end the current session and
					after that the outer while loop will end */
					continue;
				}
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					/* break from the current while loop, and the check after it would recognize
					the problem and move on to the next connection */
					break;
				}
				print_error_message_and_exit("Failed to receive data from the server");
			} else if (bytes_read == 0) {
				fprintf(stderr, "EOF error\n");
				tcp_error = 1;
				break;
			}
			bytes_recv += bytes_read;
			if (is_printable_character(c)) {
				n_pc += 1;
				pcc_tmp[c - LOWEST_PRINTABLE] += 1; // upadte the right cell of the current printable character
			}
		}

		if (tcp_error) { // check for a tcp error
			continue;
		}

		// part (c) of the proctocol
		uint64_t n_pc_be = htobe64(n_pc);
		uint64_t bytes_sent = 0;
		uint64_t bytes_written = 0;
		while (bytes_sent < sizeof(n_pc_be)) { // send the amount of printable characters to client
			if ((bytes_written = write(connfd, &n_pc_be + bytes_sent, sizeof(n_pc_be) - bytes_sent)) == -1) {
				/* if we get a SIGINT while the syscall is being run,
				the SIGINT handler executes and the return value of the syscall is -1 */
				if (sigint_flag) {
					/* we continue because we want to end the current session and
					after that the outer while loop will end */
					continue;
				}
				if (is_tcp_error()) {
					print_error_message("Failed to read due to a TCP error");
					tcp_error = 1;
					/* break from the current while loop, and the check after it would recognize
					the problem and move on to the next connection */
					break;
				}
				print_error_message_and_exit("Failed to send data to the server");
			}
			bytes_sent += bytes_written;
		}

		if (tcp_error) { // check for a tcp error
			continue;
		}

		if (close(connfd) == -1) {
			print_error_message_and_exit("Failed to close the socket");
		}

		// update the pcc data structure with the stats from the current connection
		for (int i = 0; i < N_PRINTABLE; i++) {
			pcc_total[i] += pcc_tmp[i];
		}
	}

	print_pcc(pcc_total);
	
	return 0;
}
