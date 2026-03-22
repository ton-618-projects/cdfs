#ifndef DFS_H
#define DFS_H

#include <stddef.h>
#include <stdint.h>

#define CHUNK_SIZE (64 * 1024) //each file divided into 64KB chunks
#define MAX_CHUNKS 1024 //max chunks per file, can be adjusted as needed. This allows files up to 64MB in size.
#define MAX_FILES 128 //max files in the system, can be adjusted as needed
#define MAX_FILENAME 256 //file name max length


// Metadata Structure
typedef struct {
    int32_t chunk_id;
    size_t chunk_size;
}chunk_info_t;

typedef struct {
    char filename[MAX_FILENAME];
    uint32_t file_mode;
    int64_t created_at;
    int64_t modified_at;
    uint64_t file_size;
    int32_t chunk_count;
    chunk_info_t chunks[MAX_CHUNKS];
}file_metadata_t;
//adv future work - replica, deletion tag, duplicate filename prevention,

// Storage layer api
int32_t store_chunk(const uint8_t *data,size_t size);//stores chunk to disk
int32_t load_chunk(int32_t chunk_id, uint8_t *buffer, size_t buffer_size,size_t *bytes_read);//buffer is a block of memory where the chunk data will be loaded (copied) into. buffer = array of bytes

// Metadata layer api
int32_t register_file(const char *filename, const chunk_info_t *chunks, int32_t chunk_count,
                      uint32_t file_mode, int64_t created_at, int64_t modified_at,
                      uint64_t file_size);
int32_t get_file_metadata(const char *filename, file_metadata_t *out);

// Client API
int32_t cdfs_put(const char *local_path, const char *cdfs_path);
int32_t cdfs_get(const char *cdfs_path, const char *local_path);

#endif
