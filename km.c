#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

#include <linux/slab.h> // For memory allocation and deallocation

#include "header.h"

#define  DEVICE_NAME "ebbchar"
#define  CLASS_NAME  "ebb"

static int    majorNumber;
static char   message[256] = {0};
static short  size_of_message;
static int    numberOpens = 0;
static struct class*  ebbcharClass  = NULL;
static struct device* ebbcharDevice = NULL;

const char *PH_FILE_MAGIC="pH profile 0.18\n";

void* bin_receive_ptr;

static DEFINE_MUTEX(ebbchar_mutex);

static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

typedef struct pH_seq {
        int last;
        int length;
        u8 data[PH_MAX_SEQLEN];
	struct list_head seqList;
} pH_seq;

typedef struct pH_profile_data {
	int sequences;					// # sequences that have been inserted NOT the number of lookahead pairs
	unsigned long last_mod_count;	// # syscalls since last modification
	unsigned long train_count;		// # syscalls seen during training
	void *pages[PH_MAX_PAGES];
	int current_page;				// pages[current_page] contains free space
	int count_page;					// How many arrays have been allocated in the current page
	pH_seqflags *entry[PH_NUM_SYSCALLS];
} pH_profile_data;

typedef struct pH_profile pH_profile;

struct pH_profile {
	// My new fields
	struct hlist_node hlist; // Must be first field
	int identifier;
	
	// Anil's old fields
	int normal;				// Is test profile normal?
	int frozen;				// Is train profile frozen (potential normal)?
	time_t normal_time;		// When will forzen become true normal?
	int length;
	unsigned long count;	// Number of calls seen by this profile
	int anomalies;			// NOT LFC - decide if normal should be reset
	pH_profile_data train, test;
	char *filename;
	atomic_t refcount;
	pH_profile *next;
	struct file *seq_logfile;
	struct semaphore lock;
	pH_seq seq;
};

static int __init ebbchar_init(void){
	printk(KERN_INFO "%s: Initializing the EBBChar LKM\n", DEVICE_NAME);

	bin_receive_ptr = kmalloc(sizeof(pH_disk_profile), GFP_KERNEL);
	if (!bin_receive_ptr) {
		printk(KERN_INFO "%s: Unable to allocate memory for bin_receive_ptr", DEVICE_NAME);
		return -ENOMEM;
	}

	// Try to dynamically allocate a major number for the device
	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber<0){
	  printk(KERN_ALERT "%s: Failed to register a major number\n", DEVICE_NAME);
	  return majorNumber;
	}
	printk(KERN_INFO "%s: registered correctly with major number %d\n", DEVICE_NAME, majorNumber);

	// Register the device class
	ebbcharClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(ebbcharClass)){           // Check for error and clean up if there is
	  unregister_chrdev(majorNumber, DEVICE_NAME);
	  printk(KERN_ALERT "%s: Failed to register device class\n", DEVICE_NAME);
	  return PTR_ERR(ebbcharClass);
	}
	printk(KERN_INFO "%s: device class registered correctly\n", DEVICE_NAME);

	// Register the device driver
	ebbcharDevice = device_create(ebbcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(ebbcharDevice)){          // Clean up if there is an error
	  class_destroy(ebbcharClass);      // Repeated code but the alternative is goto statements
	  unregister_chrdev(majorNumber, DEVICE_NAME);
	  printk(KERN_ALERT "%s: Failed to create the device\n", DEVICE_NAME);
	  return PTR_ERR(ebbcharDevice);
	}
	printk(KERN_INFO "%s: device class created correctly\n", DEVICE_NAME); // Device was initialized
	mutex_init(&ebbchar_mutex); // Initialize the mutex dynamically
	return 0;
}

static void __exit ebbchar_exit(void){
	kfree(bin_receive_ptr);
	
	mutex_destroy(&ebbchar_mutex);
	device_destroy(ebbcharClass, MKDEV(majorNumber, 0));
	class_unregister(ebbcharClass);
	class_destroy(ebbcharClass);
	unregister_chrdev(majorNumber, DEVICE_NAME);
	printk(KERN_INFO "%s: Goodbye from the LKM!\n", DEVICE_NAME);
}

static int dev_open(struct inode *inodep, struct file *filep){
	if(!mutex_trylock(&ebbchar_mutex)){
		printk(KERN_ALERT "%s: Device in use by another process", DEVICE_NAME);
		return -EBUSY;
	}
	numberOpens++;
	printk(KERN_INFO "%s: Device has been opened %d time(s)\n", DEVICE_NAME, numberOpens);
	return 0;
}

void pH_profile_mem2disk(pH_profile *, pH_disk_profile *);

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	pH_profile* current_profile;
	pH_disk_profile* disk_profile;
	int error_count;
	
	current_profile = kmalloc(sizeof(pH_profile), GFP_KERNEL);
	if (current_profile == NULL) {
		printk(KERN_INFO "%s: Unable to allocate memory for current_profile", DEVICE_NAME);
		return -ENOMEM;
	}

	disk_profile = kmalloc(sizeof(pH_disk_profile*), GFP_KERNEL);
	if (!disk_profile) {
		printk(KERN_INFO "%s: Unable to allocate memory for disk profile", DEVICE_NAME);
		return -ENOMEM;
	}
	printk(KERN_INFO "%s: Successfully allocated memory for disk profile", DEVICE_NAME);

	pH_profile_mem2disk(current_profile, disk_profile);
	printk(KERN_INFO "%s: Done conversion", DEVICE_NAME);

	printk(KERN_INFO "%s: Copying to user...", DEVICE_NAME);
	error_count = copy_to_user(bin_receive_ptr, disk_profile, sizeof(pH_disk_profile*));
	if (error_count==0){           // success
	  printk(KERN_INFO "%s: Successfully performed binary write to user space app\n", DEVICE_NAME);
	  return 0; // clear the position to the start and return 0
	}
	else {
	  printk(KERN_INFO "%s: Failed to send all of the data to the user", DEVICE_NAME);
	  return -EFAULT;      // Failed - return a bad address message
	}
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	sprintf(message, "%s(%zu letters)", buffer, len);
	size_of_message = strlen(message);
	printk(KERN_INFO "%s: Received %zu characters from the user\n", DEVICE_NAME, len);
	return len;
}

static int dev_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&ebbchar_mutex);
	printk(KERN_INFO "%s: Device successfully closed\n", DEVICE_NAME);
	return 0;
}

void pH_profile_data_mem2disk(pH_profile_data *mem, pH_disk_profile_data *disk)
{
	//int i, j;

	disk->sequences = mem->sequences;
	disk->last_mod_count = mem->last_mod_count;
	disk->train_count = mem->train_count;
	printk(KERN_INFO "%s: Successfully completed first block of code in pH_profile_data_mem2disk", DEVICE_NAME);

	/*
	for (i = 0; i < PH_NUM_SYSCALLS; i++) {
		    if (mem->entry[i] == NULL) {
		            disk->empty[i] = 1;
		            for (j = 0; j < PH_NUM_SYSCALLS; j++) {
		                    disk->entry[i][j] = 0;
		            }
		    } else {
		            disk->empty[i] = 0;
		            //memcpy(disk->entry[i], mem->entry[i], PH_NUM_SYSCALLS);
		    }
	}
	*/

	printk(KERN_INFO "%s: Successfully reached end of pH_profile_data_mem2disk function", DEVICE_NAME);
}

void pH_profile_mem2disk(pH_profile *profile, pH_disk_profile *disk_profile)
{
	/* make sure magic is less than PH_FILE_MAGIC_LEN! */
	strcpy(disk_profile->magic, PH_FILE_MAGIC);
	disk_profile->normal = 1234; // Fix this
	disk_profile->frozen = profile->frozen;
	disk_profile->normal_time = profile->normal_time;
	disk_profile->length = profile->length;
	disk_profile->count = profile->count;
	disk_profile->anomalies = profile->anomalies;
	strcpy(disk_profile->filename, "");
	printk(KERN_INFO "%s: Made it through first block of pH_profile_mem2disk", DEVICE_NAME);

	//pH_profile_data_mem2disk(&(profile->train), &(disk_profile->train));
	//pH_profile_data_mem2disk(&(profile->test), &(disk_profile->test));

	printk(KERN_INFO "%s: Made it to the end of pH_profile_mem2disk function", DEVICE_NAME);
}

module_init(ebbchar_init);
module_exit(ebbchar_exit);
