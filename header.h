// Anil's definitions
#define PH_NUM_SYSCALLS 256 // Size of array
#define PH_COUNT_PAGE_MAX (PAGE_SIZE / PH_NUM_SYSCALLS)
#define PH_MAX_PAGES (PH_NUM_SYSCALLS / PH_COUNT_PAGE_MAX)
#define PH_MAX_SEQLEN 9
#define PH_MAX_DISK_FILENAME 256
#define PH_LOCALITY_WIN 128
#define PH_FILE_MAGIC_LEN 20
#define PH_LOG_ACTION 3   /* actions pH takes (delays) */

typedef int pH_seqflags;

typedef struct pH_disk_profile_data {
        int sequences;  /* # sequences that have been inserted */
                        /*   NOT the number of lookahead pairs */
        unsigned long last_mod_count; /* # syscalls since last modification */
        unsigned long train_count;      /* # syscalls seen during training */
        int empty[PH_NUM_SYSCALLS];
        pH_seqflags entry[PH_NUM_SYSCALLS][PH_NUM_SYSCALLS];
} pH_disk_profile_data;

typedef struct pH_disk_profile {
        char magic[PH_FILE_MAGIC_LEN];  /* file magic: identifier, version */
        int normal;
        int frozen;
        time_t normal_time;
        int length;
        unsigned long count;
        int anomalies;
        pH_disk_profile_data train, test;
        char filename[PH_MAX_DISK_FILENAME];
} pH_disk_profile;
