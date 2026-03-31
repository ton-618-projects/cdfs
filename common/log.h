#ifndef LOG_H
#define LOG_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERR
} log_level_t;

#include <stdint.h>

void cdfs_log_msg(log_level_t level, const uint8_t *component, const char *fmt, ...);

#define LOG_INFO(comp, ...)  cdfs_log_msg(LOG_LEVEL_INFO, (const uint8_t *)(comp), __VA_ARGS__)
#define LOG_WARN(comp, ...)  cdfs_log_msg(LOG_LEVEL_WARN, (const uint8_t *)(comp), __VA_ARGS__)
#define LOG_ERR(comp, ...)   cdfs_log_msg(LOG_LEVEL_ERR,  (const uint8_t *)(comp), __VA_ARGS__)

#endif