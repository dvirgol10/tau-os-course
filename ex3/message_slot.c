#define __KERNEL__
#define MODULE

#include "message_slot.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

struct message_slot_list {
	struct message_slot_node* head;
};

struct message_slot_node {
	struct message_slot_node* next;
	struct message_channel_node* head_message_channel_node;
	unsigned int minor_num;
};

struct message_channel_node {
	struct message_channel_node* next;
	unsigned int channel_id;
	size_t message_len;
	char* message;
};

struct message_slot_list* message_slot_list;


static void associate_channel_id_with_fd(struct file* file, unsigned int channel_id) {
	file->private_data = (void*) channel_id;
}


static struct message_slot_node* find_message_slot_node_by_minor_num(unsigned int minor_num) {
	struct message_slot_node* message_slot_node = message_slot_list->head;
	while (message_slot_node != NULL) {
		if (message_slot_node->minor_num == minor_num) {
			return message_slot_node;
		}
		message_slot_node = message_slot_node->next;
	}
	return NULL; // such message_slot_node doesn't exist
}


static int create_message_slot_node(unsigned int minor_num) {
	struct message_slot_node* new_message_slot_node = kmalloc(sizeof(struct message_slot_node), GFP_KERNEL);
	if (new_message_slot_node == NULL) {
		return -1;
	}
	new_message_slot_node->head_message_channel_node = NULL;
	new_message_slot_node->minor_num = minor_num;
	//insert the new node as the head of the list
	new_message_slot_node->next = message_slot_list->head;
	message_slot_list->head = new_message_slot_node;
	return SUCCESS;
}


static int device_open(struct inode* inode, struct file* file) {
	unsigned int minor_num = iminor(inode);
	struct message_slot_node* message_slot_node = find_message_slot_node_by_minor_num(minor_num);
	if (message_slot_node == NULL) {
		if (create_message_slot_node(minor_num) == -1) {
			return -ENOMEM;
		}
	}
	return SUCCESS;
}

static unsigned int get_channel_id_of_file(struct file* file) {
	return file->private_data;
}


static ssize_t put_message_in_user_buffer(struct message_channel_node* message_channel_node, const char __user* buffer, size_t length) {
	int i;
	for (i = 0; i < length; ++i) {
		if (put_user(message_channel_node->message[i], buffer + i) < 0) { // put_user failed
			return -1;
		}
	}
	return i;
}


static struct message_channel_node* find_message_channel_node_by_channel_id(struct message_slot_node* message_slot_node, unsigned int channel_id) {
	struct message_channel_node* message_channel_node = message_slot_node->head_message_channel_node;
	while (message_channel_node != NULL) {
		if (message_channel_node->channel_id == channel_id) {
			return message_channel_node;
		}
		message_channel_node = message_channel_node->next;
	}
	return NULL; // such message_channel_node doesn't exist
}


// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read(struct file* file, char __user* buffer, size_t length, loff_t* offset) {
	unsigned int channel_id = get_channel_id_of_file(file);
	if (channel_id == 0) { // no channel has been set on the file descriptor
		return -EINVAL;
	}
	
	unsigned int minor_num = iminor(inode);
	struct message_slot_node* message_slot_node = find_message_slot_node_by_minor_num(minor_num);
	struct message_channel_node* message_channel_node = find_message_channel_node_by_channel_id(message_slot_node, channel_id);
	if (message_channel_node == NULL || message_channel_node->message_len == 0) { // no message exists on the channel
		return -EWOULDBLOCK;
	}
	
	if (length < message_channel_node->message_len) { // the provided buffer length is too small to hold the last message written on the channel
		return -ENOSPC;
	}

	int bytes_read = put_message_in_user_buffer(message_channel_node, buffer, length);
	if (bytes_read == -1) { // copy failed
		return -EFAULT;
	}
	return bytes_read;
}


static ssize_t copy_user_buffer_to_message_channel(struct message_channel_node* message_channel_node, const char __user* buffer, size_t length) {
	int i;
	for (i = 0; i < length; ++i) {
		if (get_user(message_channel_node->message[i], buffer + i) < 0) { // get_user failed
			message_channel_node->message_len = 0; // now there is no message on the channel
			return -1;
		}
	}
	message_channel_node->message_len = i;
	return i;
}

struct message_slot_node {
	struct message_slot_node* next;
	struct message_channel_node* head_message_channel_node;
	unsigned int minor_num;
};

struct message_channel_node {
	struct message_channel_node* next;
	unsigned int channel_id;
	size_t message_len;
	char* message;
};

static struct message_channel_node* create_message_channel_node(struct message_slot_node* message_slot_node, unsigned int channel_id) {
	struct message_channel_node* new_message_channel_node = kmalloc(sizeof(struct message_channel_node), GFP_KERNEL);
	if (new_message_channel_node == NULL) {
		return NULL;
	}
	message_channel_node->channel_id = channel_id;
	message_channel_node->message_len = 0;
	message_channel_node->message = kmalloc(MAX_MSG_LEN, GFP_KERNEL);
	if (message_channel_node->message == NULL) {
		return NULL;
	}
	//insert the new node as the head of the list
	new_message_channel_node->next = message_slot_node->head_message_channel_node;
	message_slot_node->head_message_channel_node = new_message_channel_node;
	return new_message_channel_node;
}


// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write(struct file* file, const char __user* buffer, size_t length, loff_t* offset) {
	unsigned int channel_id = get_channel_id_of_file(file);
	if (channel_id == 0) { // no channel has been set on the file descriptor
		return -EINVAL;
	}
	
	if (length == 0 || length > MAX_MSG_LEN) {
		return -EMSGSIZE;
	}
	
	unsigned int minor_num = iminor(inode);
	struct message_slot_node* message_slot_node = find_message_slot_node_by_minor_num(minor_num);
	struct message_channel_node* message_channel_node = find_message_channel_node_by_channel_id(message_slot_node, channel_id);
	if (message_channel_node == NULL) {
		message_channel_node = create_message_channel_node(message_slot_node, channel_id);
		if (message_channel_node == NULL) { // creation failed
			return -ENOMEM;
		}
	}
	
	int bytes_written = copy_user_buffer_to_message_channel(message_channel_node, buffer, length);
	if (bytes_written == -1) { // copy failed
		return -EFAULT;
	}
	return bytes_written;
}


static long device_ioctl(struct file* file, unsigned int ioctl_command_id, unsigned int ioctl_param) {
	if (ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param == 0) {
		return -EINVAL;
	}
	associate_channel_id_with_fd(file, ioctl_param);

	return SUCCESS;
}



struct file_operations fops =
{
  .owner	  	  = THIS_MODULE, 
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
};

int initialize_message_slot_list() {
	message_slot_list = kmalloc(sizeof(struct message_slot_list), GFP_KERNEL);
	if (message_slot_list == NULL) {
		return -1;
	}
	message_slot_list->head = NULL;
	return SUCCESS;
}


void clear_message_slot_list() {
	struct message_slot_node* message_slot_node = message_slot_list->head;
	while (message_slot_node != NULL) {
		struct message_channel_node* message_channel_node = message_slot_node->head_message_channel_node;
		while (message_channel_node != NULL) {
			kfree(message_channel_node->message);

			struct message_channel_node* next_message_channel_node = message_channel_node->next;
			kfree(message_channel_node);
			message_channel_node = next_message_channel_node;
		}

		struct message_slot_node* next_message_slot_node = message_slot_node->next;
		kfree(message_slot_node);
		message_slot_node = next_message_slot_node;
	}

	kfree(message_slot_list);
}
// Initialize the module - Register the character device
static int __init init_module(void) {
	int rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &fops);
	if (rc < 0) { //module initialization failed
		printk(KERN_ERR "%s registration failed for %d\n", DEVICE_RANGE_NAME, MAJOR_NUM);
		return rc;
	}
	if (initialize_message_slot_list() == -1) {
		return -ENOMEM;
	}
}


static void __exit exit_module(void) {
	clear_message_slot_list();
	unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

module_init(init_module);
module_exit(exit_module);

