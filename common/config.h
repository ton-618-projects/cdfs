#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define MAX_IP_LEN 16

typedef struct {
    uint8_t meta_ip[MAX_IP_LEN];
    int32_t meta_port;
    uint8_t storage_dir[256]; // Directory where storage node saves chunk files
} cdfs_config_t;

int32_t load_config(const uint8_t *filename, cdfs_config_t *out_config);

#endif