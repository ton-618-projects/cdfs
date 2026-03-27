#include "dfs.h"
#include <stdint.h>
#include <stdio.h>

int32_t cdfs_put(const char *local_path, const char *cdfs_path) {
    // static int32_t global_chunk_id = 0;

    FILE *fp = fopen(local_path, "rb");
    if(!fp){
        return -1;
    }
    
    uint8_t buffer[CHUNK_SIZE];
    chunk_info_t chunks[MAX_CHUNKS];
    int32_t chunk_count = 0;

    while (1) {
        size_t bytes = fread(buffer, 1, CHUNK_SIZE, fp);
        if(bytes == 0){
            if(feof(fp)){
                break; //end of file reached
            } 
            if(ferror(fp)){ 
                fclose(fp);//error reading file, cleanup and return error
                return -1;
            }
        }
        
        if(chunk_count >= MAX_CHUNKS){
            fclose(fp);
            return -1;
        }
        
        int32_t chunk_id =store_chunk(buffer, bytes);
        if(chunk_id < 0){
            fclose(fp);
            return -1;
        }
        chunks[chunk_count].chunk_id = chunk_id;
        chunks[chunk_count].chunk_size = bytes;
        chunk_count++;
    }
    fclose(fp);

    if(register_file(cdfs_path, chunks, chunk_count) != 0){
        return -1;
    }
    return 0;
}

int32_t cdfs_get(const char *cdfs_path, const char *local_path) {
    file_metadata_t metadata;
    if(get_file_metadata(cdfs_path, &metadata) != 0){
        return -1;
    }
    int32_t chunk_count = metadata.chunk_count; 

    FILE *fp = fopen(local_path, "wb");
    if(!fp){
        return -1;
    }

    uint8_t buffer[CHUNK_SIZE];
    size_t bytes_read;

    for (int32_t i = 0; i < chunk_count; i++) {
        int32_t chunk_id = metadata.chunks[i].chunk_id;

        if(load_chunk(chunk_id, buffer, CHUNK_SIZE, &bytes_read) != 0){
            fclose(fp);
            return -1;
        }
        fwrite(buffer, 1, bytes_read, fp);
    }
    fclose(fp);
    return 0;
}
