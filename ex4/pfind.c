#include <threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>


char* root_dir;
char* search_term;
int num_threads;
atomic_int num_remaining_threads; // the number of threads which haven't exited as the result of an error
atomic_int files_found = 0;

mtx_t start_threads_mutex;
mtx_t q_lock;
cnd_t start_threads_cv;
int start_threads_flag = 0; // whether all the threads are created 
int num_threads_waiting_to_lock = 0; // the number of threads that have been already woken up but couldn't get the lock; requires q_lock

// insert nodes to tail, extract from head; requires q_lock
struct dir_queue {
	struct dir_queue_node* head;
	struct dir_queue_node* tail;
	int len;
};

struct dir_queue_node {
	struct dir_queue_node* next;
	char* path;
};

// insert nodes to tail, extract from head; requires q_lock
struct thread_queue {
	struct thread_queue_node* head;
	struct thread_queue_node* tail;
	int len;
};

struct thread_queue_node {
	struct thread_queue_node* next;
	cnd_t* p_cv;
}; 

struct dir_queue* dir_queue;
struct thread_queue* thread_queue;


void exit_from_program() {
	printf("Done searching, found %d files\n", files_found);
	// if there are less searching threads than in the beginning, it means that there was an error
	exit(num_threads != num_remaining_threads); // this is "exit" and not "thrd_exit" on purpose
}


void print_error_message(const char* s) {
	perror(s);
}


void print_error_message_and_exit(const char* s) {
	print_error_message(s);
	exit(1);
}


void print_error_message_and_exit_thread(const char* s) {
	print_error_message(s);
	if (--num_remaining_threads == 0) { // if there are no threads, we need to exit totally
		exit_from_program();
	}
	thrd_exit(1);
}


void print_permission_denied(char* path) {
	printf("Directory %s: Permission denied.\n", path);
}


int is_dir_queue_empty() {
	return dir_queue->len == 0;
}


void dir_queue_enqueue(char* dir_path) {
	struct dir_queue_node* new_dir_queue_node = malloc(sizeof(struct dir_queue_node));
	new_dir_queue_node->next = NULL;
	new_dir_queue_node->path = dir_path;
	if (is_dir_queue_empty()) {
		dir_queue->head = new_dir_queue_node;
	} else {
		dir_queue->tail->next = new_dir_queue_node;
	}
	dir_queue->tail = new_dir_queue_node;
	dir_queue->len += 1;
}


char* dir_queue_dequeue() {
	struct dir_queue_node* dir_queue_head_node = dir_queue->head;
	dir_queue->head = dir_queue_head_node->next;
	char* dir_path = dir_queue_head_node->path;
	free(dir_queue_head_node);
	dir_queue->len -= 1;
	return dir_path;
}


char* dir_queue_remove(int i) {
	if (i >= dir_queue->len) {
		return NULL;
	}
	if (i == 0) {
		return dir_queue_dequeue();
	}
	struct dir_queue_node* dir_queue_node = dir_queue->head;
	for (int j = 0; j < i - 1; j++) { // arrive at the node right before the node to remove, so we can easily do the pointers' stuff
		dir_queue_node = dir_queue_node->next;
	}
	struct dir_queue_node* dir_queue_node_i = dir_queue_node->next;
	dir_queue_node->next = dir_queue_node_i->next;
	if (i == dir_queue->len - 1) { // if the node to remove is the tail
		dir_queue->tail = dir_queue_node;
	}
	char* dir_path = dir_queue_node_i->path;
	free(dir_queue_node_i);
	dir_queue->len -= 1;
	return dir_path;
}


int is_thread_queue_empty() {
	return thread_queue->len == 0;
}


void thread_queue_enqueue(cnd_t* p_cv) {
	struct thread_queue_node* new_thread_queue_node = malloc(sizeof(struct thread_queue_node));
	new_thread_queue_node->p_cv = p_cv;
	if (is_thread_queue_empty()) {
		thread_queue->head = new_thread_queue_node;
		thread_queue->tail = new_thread_queue_node;
	} else {
		thread_queue->tail->next = new_thread_queue_node;
		thread_queue->tail = new_thread_queue_node;
	}
	thread_queue->len += 1;
}


cnd_t* thread_queue_dequeue() {
	struct thread_queue_node* thread_queue_head_node = thread_queue->head;
	thread_queue->head = thread_queue_head_node->next;
	cnd_t* p_cv = thread_queue_head_node->p_cv;
	free(thread_queue_head_node);
	thread_queue->len -= 1;
	num_threads_waiting_to_lock += 1;
	return p_cv;
}


// a directory is searchable iff we can open it
int is_searchable_dir(char* dir_path) {
	DIR* dir = opendir(dir_path);
	if (dir == NULL) {
		return 0;
	}
	if (closedir(dir) == -1) {
		return 0;
	}
	return 1;
}


void wait_for_all_threads_to_be_created() {
	mtx_lock(&start_threads_mutex);
	while (!start_threads_flag) { // if the main thread marks that all the searching threads are already created, this thread doesn't need to wait
		cnd_wait(&start_threads_cv, &start_threads_mutex);
	}
	mtx_unlock(&start_threads_mutex);
}


void notify_all_threads_are_created() {
	mtx_lock(&start_threads_mutex);
	start_threads_flag = 1; // mark that all the threads are created, so that searching threads which didn't go to sleep know that they can continue freely
	cnd_broadcast(&start_threads_cv); // signal the searching threads to start searching (after all of them are created)
	mtx_unlock(&start_threads_mutex);
}


void validate_and_initialize_arguments(int argc, char *argv[]) {
	if (argc != 4) {
		print_error_message_and_exit("You must pass 3 arguments");
	}
	root_dir = argv[1];
	if (!is_searchable_dir(root_dir)) {
		print_error_message_and_exit("The search root directory must be searchable");
	}
	search_term = argv[2];
	num_threads = atoi(argv[3]);
	if (num_threads == 0) {
		print_error_message_and_exit("The number of searching threads must be a valid integet greater that 0");
	}
	num_remaining_threads = num_threads;
}


int is_ignored(struct dirent* dirent) {
	return strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0;
}


int is_dir(char* dir_path) {
	struct stat buf;
	if (stat(dir_path, &buf) == -1) {
		if (errno != 2) { // if errno equals 2 it means that we have a symlink which is not a directory
			print_error_message_and_exit_thread("'stat' failed");
		}
		return 0;
	}
	return S_ISDIR(buf.st_mode);
}


void search_in_dir(char* dir_path) {
	char* new_path;
	char* dirent_name;
	int is_dir_new_path;
	DIR* dir = opendir(dir_path);
	if (dir == NULL) {
		print_permission_denied(dir_path);
		return;
	}
	struct dirent* dirent;
	while ((dirent = readdir(dir)) != NULL) {
		if (is_ignored(dirent)) { // if the name is "." or ".."
			continue;
		}
		dirent_name = dirent->d_name;
		// construct the path of the current dir entry (using the "base" path and the name of the entry)
		new_path = calloc(strlen(dir_path) + strlen(dirent_name) + 2, sizeof(char)); // "+2": 1 for the "/" character and 1 for the null terminator 
		new_path = strcpy(new_path, dir_path);
		new_path = strcat(new_path, "/");
		new_path = strcat(new_path, dirent_name);
		is_dir_new_path = is_dir(new_path);
		if (is_dir_new_path == 1) { // if the entry is a directory
			if (!is_searchable_dir(new_path)) { // if it isn't searchable we don't want to insert it to the dir queue
				print_permission_denied(new_path);
				free(new_path);
			} else { // append the directory to the queue beacuse it's a valid one
				mtx_lock(&q_lock);
				dir_queue_enqueue(new_path);
				if (!is_thread_queue_empty()) {
					cnd_signal(thread_queue_dequeue());	// we need to signal the waiting thread that there is a new directory for searching
				}
				mtx_unlock(&q_lock);
			}
		} else {
			if (strstr(dirent_name, search_term) != NULL) {
				files_found++;
				printf("%s\n", new_path);
			}
			free(new_path);
		}
	}
	if (closedir(dir) == -1) {
		print_error_message_and_exit_thread("Failed to close a directory");
	}
}


// if there is at least one directory waiting for search or there is at least one busy searching thread, we need to continue the searching
int are_there_directories_to_search() {
	return !is_dir_queue_empty() || thread_queue->len + 1 != num_remaining_threads; // thread_queue->len + 1 == num_remaining_threads iff all the remaning threads except this one are idle (in the queue). Also this thread is idle beacuse when it calls this function it doesn't search
}


int thread_func(void *thread_param) {
	wait_for_all_threads_to_be_created(); // wait for all other searching threads to be created and for the main thread to signal that the searching should start

	char* dir_path;
	cnd_t* p_not_empty_q_and_my_turn_cv = malloc(sizeof(cnd_t)); // the cv of the current thread
	cnd_init(p_not_empty_q_and_my_turn_cv);
	while (1) { // we want to continue the searching if we can
		mtx_lock(&q_lock);
		if (!are_there_directories_to_search()) {
			mtx_unlock(&q_lock);
			break;
		}
		/* in order to satisfy the requirement of not going to sleep when the dir queue isn't "logically" empty,
		we need to check if there are less directories than threads which waiting for searching
		(the number of them is equal to the number of threads sleeping waiting in the queue plus
		the number of threads which are already woke up but didn't manage obtaining the lock yet)
		If this is indeed the case, we want to sleep. Otherwise, we can "take" a directory from the "middle" of the dir queue,
		the one that matches to the current thread.
		*/
		if (dir_queue->len <= thread_queue->len + num_threads_waiting_to_lock) {
			thread_queue_enqueue(p_not_empty_q_and_my_turn_cv);
			cnd_wait(p_not_empty_q_and_my_turn_cv, &q_lock);
			num_threads_waiting_to_lock -= 1;
			dir_path = dir_queue_dequeue();
		} else {
			dir_path = dir_queue_remove(thread_queue->len + num_threads_waiting_to_lock); // remove the next directory in the queue, the one which is after all the directories that match to the already waiting threads
		}
		mtx_unlock(&q_lock);
		search_in_dir(dir_path);
	}
	cnd_destroy(p_not_empty_q_and_my_turn_cv);
	free(p_not_empty_q_and_my_turn_cv);

	exit_from_program();
	return 0;
}


int main(int argc, char *argv[]) {
	validate_and_initialize_arguments(argc, argv);
	// a FIFO queue that holds directories
	dir_queue = malloc(sizeof(struct dir_queue));
	dir_queue->head = NULL;
	dir_queue->tail = NULL;
	dir_queue->len = 0;
	// a FIFO queue that holds cv-s of threads to handle the signaling
	thread_queue = malloc(sizeof(struct dir_queue));
	thread_queue->head = NULL;
	thread_queue->tail = NULL;
	thread_queue->len = 0;

	dir_queue_enqueue(root_dir); // put the seartch root directory in the queue

	thrd_t* threads = calloc(num_threads, sizeof(thrd_t));

	// initialize mutex and condition variable objects
	mtx_init(&start_threads_mutex, mtx_plain);
	mtx_init(&q_lock, mtx_plain);
	cnd_init(&start_threads_cv);

	for (int i = 0; i < num_threads; i++) {
		if (thrd_create(&threads[i], thread_func, NULL) != thrd_success) {
			print_error_message_and_exit("There has been an error while creating a thread");
		}
	}
	notify_all_threads_are_created(); // after all searching threads are created, the main thread signals them to start searching
	thrd_exit(0);
}