#ifndef DFS_H
#define DFS_H
#include <stddef.h>
#include <stdint.h>

#define CHUNK_SIZE (64 * 1024) //each file divided into 64KB chunks
#define CHUNK_SIZE (16 * 1024 * 1024) //each file divided into 16MB chunks
#define MAX_CHUNKS 1024 //max chunks per file, can be adjusted as needed. This allows files up to 64MB in size.
#define MAX_FILES 128 //max files in the system, can be adjusted as needed
#define MAX_FILENAME 256 //file name max length

#define REPLICATION_FACTOR 3

// Metadata Structure
typedef struct {
    int32_t chunk_id;
    size_t chunk_size;
}chunk_info_t;
    int32_t node_count;
    uint8_t node_ips[REPLICATION_FACTOR][16];
    int32_t node_ports[REPLICATION_FACTOR];
chunk_info_t;

typedef struct {
    char filename[MAX_FILENAME];
    uint8_t filename[MAX_FILENAME];
    int32_t chunk_count;
    chunk_info_t chunks[MAX_CHUNKS];
}file_metadata_t;
//future work - add file permissions, timestamps, file size. to file_metadata_t
//adv future work - replica, deletion tag, duplicate filename prevention,
// adv future work - duplicate filename prevention, deletions

// Storage layer api
int32_t store_chunk(const uint8_t *data,size_t size);//stores chunk to disk
int32_t load_chunk(int32_t chunk_id, uint8_t *buffer, size_t buffer_size,size_t *bytes_read);//buffer is a block of memory where the chunk data will be loaded (copied) into. buffer = array of bytes
int32_t store_chunk_stream(int32_t chunk_id, int32_t sockfd, size_t size, uint32_t expected_checksum);
int32_t load_chunk_stream(int32_t chunk_id, int32_t sockfd, size_t file_size, uint32_t *out_checksum);

// Metadata layer api
int32_t register_file(const char *filename,chunk_info_t* chunks, int32_t chunk_count);
int32_t get_file_metadata(const char *filename, file_metadata_t *out);
int32_t register_file(const uint8_t *filename, const chunk_info_t* chunks, int32_t chunk_count);
int32_t get_file_metadata(const uint8_t *filename, file_metadata_t *out);

// Client API
int32_t cdfs_put(const char *local_path, const char *cdfs_path);
int32_t cdfs_get(const char *cdfs_path, const char *local_path);
int32_t cdfs_put(const uint8_t *local_path, const uint8_t *cdfs_path);
int32_t cdfs_get(const uint8_t *cdfs_path, const uint8_t *local_path);
int32_t cdfs_ls(const uint8_t *cdfs_path);
int32_t cdfs_rm(const uint8_t *cdfs_path);
int32_t cdfs_status(void);

#endif