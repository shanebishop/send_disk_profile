#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<stdbool.h>

#include <signal.h> // For signals

#include "header.h"

#define PATH_MAX 4096
#define BUFFER_LENGTH 256               // The buffer length
static char receive[BUFFER_LENGTH];     // The receive buffer from the LKM
void* bin_receive;

int main(){
	int ret, fd;

	//freopen("test_ouput.txt", "w", stdout); // Changes stdout to ./test_output.txt

	printf("Starting device test code example...\n");
	fd = open("/dev/ebbchar", O_RDWR); // Open the device with read/write access
	if (fd < 0){
	  perror("Failed to open the device");
	  return errno;
	}
	printf("Successfully opened device\n");
	
	// Perform binary read operation
	printf("Performing binary read...\n");
	ret = read(fd, bin_receive, sizeof(bin_receive));
	if (ret < 0 || bin_receive == NULL) {
		printf("Failed to read the message from the device.%d%d%d\n", ret < 0, receive == NULL, strlen(receive) < 1);
		perror("Failed to read the message from the device");
		//close(fd);
		return errno;
	}
	printf("Successfully performed binary read on device.\n");
	
	printf("bin_receive = %p\n", bin_receive);
	printf("sizeof(bin_receive) = %ld\n", sizeof(bin_receive));
	
	pH_disk_profile* disk_profile = malloc(sizeof(pH_disk_profile*));
	if (!disk_profile) {
		printf("Failed to allocate memory for disk_profile.\n");
		return -1;
	}
	
	disk_profile = (pH_disk_profile*) bin_receive;
	printf("disk_profile->normal = %d\n", disk_profile->normal);

	printf("No segfault before close\n");
	close(fd);
	printf("No segfault after close\n");

	printf("End of the program\n");
	return 0;
}
