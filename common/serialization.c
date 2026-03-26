#include "serialization.h"
#include <sys/socket.h>
#include <stdint.h>

int32_t send_exact(int32_t sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while (total_sent < len) {
        ssize_t s = send(sockfd, p + total_sent, len - total_sent, 0);
        if (s <= 0) return -1;
        total_sent += s;
    }
    return 0;
}

int32_t recv_exact(int32_t sockfd, void *buf, size_t len) {
    size_t total_recv = 0;
    uint8_t *p = (uint8_t *)buf;
    while (total_recv < len) {
        ssize_t r = recv(sockfd, p + total_recv, len - total_recv, 0);
        if (r <= 0) return -1;
        total_recv += r;
    }
    return 0;
}

#define FNV_OFFSET_BASIS 2166136261U
#define FNV_PRIME 16777619U

uint32_t calculate_checksum(const uint8_t *data, size_t length) {
    uint32_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}
