#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <stddef.h>

#include <stdint.h>

// Helper functions for POSIX sockets to ensure all bytes are sent/received
int32_t send_exact(int32_t sockfd, const void *buf, size_t len);
int32_t recv_exact(int32_t sockfd, void *buf, size_t len);

// Data structure checksumming
uint32_t calculate_checksum(const uint8_t *data, size_t length);

#endif
