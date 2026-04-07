#include "../common/dfs.h"
#include "../common/protocol.h"
#include "../common/serialization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "../common/config.h"

static int32_t connect_to_server(const uint8_t *ip, int32_t port) {
    int32_t sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, (const char *)ip, &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int32_t cdfs_put(const uint8_t *local_path, const uint8_t *cdfs_path) {
    FILE *fp = fopen((const char *)local_path, "rb");
    if (!fp) return -1;
    
    chunk_info_t chunks[MAX_CHUNKS];
    int32_t chunk_count = 0;

    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);

    // Calculate how many chunks we need
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    int32_t needed_chunks = (int32_t)(file_size / CHUNK_SIZE);
    if (file_size % CHUNK_SIZE != 0 || file_size == 0) needed_chunks++;

    // 1. Allocate chunk IDs from Metadata Server

    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) { fclose(fp); return -1; }
    
    req_allocate_chunks_t alloc_req = { needed_chunks };
    net_header_t alloc_hdr = { OP_ALLOCATE_CHUNKS, sizeof(alloc_req) };
    send_exact(meta_sock, &alloc_hdr, sizeof(alloc_hdr));
    send_exact(meta_sock, &alloc_req, sizeof(alloc_req));

    net_header_t alloc_resp_hdr;
    resp_allocate_chunks_t alloc_resp;
    if (recv_exact(meta_sock, &alloc_resp_hdr, sizeof(alloc_resp_hdr)) != 0 ||
        recv_exact(meta_sock, &alloc_resp, sizeof(alloc_resp)) != 0 || alloc_resp.status != 0) {
        close(meta_sock); fclose(fp); return -1;
    }
    close(meta_sock);
    int32_t start_chunk_id = alloc_resp.start_chunk_id;
    
    // 2. Get active nodes
    meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) { fclose(fp); return -1; }

    req_get_active_nodes_t req_nodes;
    memset(&req_nodes, 0, sizeof(req_nodes));
    net_header_t hdr_nodes = { OP_GET_ACTIVE_NODES, sizeof(req_nodes) };
    send_exact(meta_sock, &hdr_nodes, sizeof(hdr_nodes));
    send_exact(meta_sock, &req_nodes, sizeof(req_nodes));
    
    net_header_t resp_hdr_nodes;
    resp_get_active_nodes_t active_nodes;
    if (recv_exact(meta_sock, &resp_hdr_nodes, sizeof(resp_hdr_nodes)) != 0 ||
        recv_exact(meta_sock, &active_nodes, sizeof(active_nodes)) != 0) {
        close(meta_sock); fclose(fp); return -1;
    }
    close(meta_sock);
    
    if (active_nodes.node_count == 0) {
        printf("No active storage nodes available!\n");
        fclose(fp); return -1;
    }

    while (1) {
        long chunk_start = ftell(fp);
        uint8_t mbuf[8192];
        size_t bytes = 0;
        uint32_t checksum = CHKSUM_INIT;
        
        while (bytes < CHUNK_SIZE) {
            size_t to_read = (CHUNK_SIZE - bytes) < sizeof(mbuf) ? (CHUNK_SIZE - bytes) : sizeof(mbuf);
            size_t r = fread(mbuf, 1, to_read, fp);
            if (r == 0) break;
            checksum = update_checksum(checksum, mbuf, r);
            bytes += r;
        }
        if (bytes == 0) break; // EOF
        
        if (chunk_count >= MAX_CHUNKS) { fclose(fp); return -1; }
        int32_t chunk_id = start_chunk_id + chunk_count;
        chunks[chunk_count].chunk_id = chunk_id;
        chunks[chunk_count].chunk_size = bytes;
        chunks[chunk_count].node_count = 0;
        
        // Push to replicas
        for (int32_t i = 0; i < active_nodes.node_count; i++) {
            int32_t store_sock = connect_to_server(active_nodes.node_ips[i], active_nodes.node_ports[i]);
            if (store_sock < 0) continue;

            req_store_chunk_t req = { chunk_id, bytes, checksum };
            net_header_t hdr = { OP_STORE_CHUNK, sizeof(req) + bytes };
            send_exact(store_sock, &hdr, sizeof(hdr));
            send_exact(store_sock, &req, sizeof(req));
            
            fseek(fp, chunk_start, SEEK_SET);
            size_t sent = 0;
            while (sent < bytes) {
                size_t to_read = (bytes - sent) < sizeof(mbuf) ? (bytes - sent) : sizeof(mbuf);
                size_t r = fread(mbuf, 1, to_read, fp);
                send_exact(store_sock, mbuf, r);
                sent += r;
            }
            
            net_header_t resp_hdr;
            resp_store_chunk_t resp;
            if (recv_exact(store_sock, &resp_hdr, sizeof(resp_hdr)) == 0 &&
                recv_exact(store_sock, &resp, sizeof(resp)) == 0 && resp.status == 0) {
                int32_t nc = chunks[chunk_count].node_count;
                strncpy((char *)chunks[chunk_count].node_ips[nc], (const char *)active_nodes.node_ips[i], 16);
                chunks[chunk_count].node_ports[nc] = active_nodes.node_ports[i];
                chunks[chunk_count].node_count++;
            }
            close(store_sock);
        }
        
        if (chunks[chunk_count].node_count == 0) {
            printf("Failed to store chunk %d to any node!\n", chunk_id);
            fclose(fp); return -1;
        }
        chunk_count++;
        fseek(fp, chunk_start + bytes, SEEK_SET); // Seek past this chunk for the next one
    }
    fclose(fp);

    // Register with metadata server
    meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    req_register_file_t reg_req;
    memset(&reg_req, 0, sizeof(reg_req));
    strncpy((char *)reg_req.filename, (const char *)cdfs_path, MAX_FILENAME - 1);
    reg_req.chunk_count = chunk_count;

    net_header_t meta_hdr = { OP_REGISTER_FILE, sizeof(reg_req) + chunk_count * sizeof(chunk_info_t) };
    send_exact(meta_sock, &meta_hdr, sizeof(meta_hdr));
    send_exact(meta_sock, &reg_req, sizeof(reg_req));
    send_exact(meta_sock, chunks, chunk_count * sizeof(chunk_info_t));

    net_header_t meta_resp_hdr;
    int32_t status;
    if (recv_exact(meta_sock, &meta_resp_hdr, sizeof(meta_resp_hdr)) != 0 ||
        recv_exact(meta_sock, &status, sizeof(status)) != 0) {
        close(meta_sock); return -1;
    }
    close(meta_sock);
    return status;
}

int32_t cdfs_get(const uint8_t *cdfs_path, const uint8_t *local_path) {
    // Contact metadata server
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    req_get_metadata_t meta_req;
    memset(&meta_req, 0, sizeof(meta_req));
    strncpy((char *)meta_req.filename, (const char *)cdfs_path, MAX_FILENAME - 1);

    net_header_t meta_hdr = { OP_GET_METADATA, sizeof(meta_req) };
    send_exact(meta_sock, &meta_hdr, sizeof(meta_hdr));
    send_exact(meta_sock, &meta_req, sizeof(meta_req));

    net_header_t meta_resp_hdr;
    resp_get_metadata_t meta_resp;
    if (recv_exact(meta_sock, &meta_resp_hdr, sizeof(meta_resp_hdr)) != 0 ||
        recv_exact(meta_sock, &meta_resp, sizeof(meta_resp)) != 0) {
        close(meta_sock); return -1;
    }

    if (meta_resp.status != 0) { close(meta_sock); return -1; }

    chunk_info_t *chunks = NULL;
    if (meta_resp.chunk_count > 0) {
        chunks = malloc(meta_resp.chunk_count * sizeof(chunk_info_t));
        if (!chunks || recv_exact(meta_sock, chunks, meta_resp.chunk_count * sizeof(chunk_info_t)) != 0) {
            if(chunks) free(chunks);
            close(meta_sock); return -1;
        }
    }
    close(meta_sock);

    FILE *fp = fopen((const char *)local_path, "wb");
    if (!fp) { if(chunks) free(chunks); return -1; }

    for (int32_t i = 0; i < meta_resp.chunk_count; i++) {
        int32_t chunk_success = 0;
        
        for (int32_t j = 0; j < chunks[i].node_count; j++) {
            int32_t store_sock = connect_to_server(chunks[i].node_ips[j], chunks[i].node_ports[j]);
            if (store_sock < 0) continue;

            req_load_chunk_t load_req = { chunks[i].chunk_id, chunks[i].chunk_size };
            net_header_t load_hdr = { OP_LOAD_CHUNK, sizeof(load_req) };
            send_exact(store_sock, &load_hdr, sizeof(load_hdr));
            send_exact(store_sock, &load_req, sizeof(load_req));

            net_header_t load_resp_hdr;
            resp_load_chunk_t load_resp;
            if (recv_exact(store_sock, &load_resp_hdr, sizeof(load_resp_hdr)) == 0 &&
                recv_exact(store_sock, &load_resp, sizeof(load_resp)) == 0 && load_resp.status == 0) {
                uint32_t calc_chk = CHKSUM_INIT;
                size_t received = 0;
                uint8_t mbuf[8192];
                long write_start = ftell(fp);
                int32_t net_fail = 0;
                
                while (received < load_resp.size) {
                    size_t to_recv = (load_resp.size - received) < sizeof(mbuf) ? (load_resp.size - received) : sizeof(mbuf);
                    if (recv_exact(store_sock, mbuf, to_recv) != 0) { net_fail = 1; break; }
                    fwrite(mbuf, 1, to_recv, fp);
                    calc_chk = update_checksum(calc_chk, mbuf, to_recv);
                    received += to_recv;
                }
                
                if (net_fail) {
                    fseek(fp, write_start, SEEK_SET);
                    close(store_sock);
                    continue;
                }
                
                if (calc_chk != load_resp.checksum) {
                    printf("Checksum mismatch on chunk %d from replica %s:%d! Expected %u, got %u. Fallback...\n",
                           chunks[i].chunk_id, chunks[i].node_ips[j], chunks[i].node_ports[j], load_resp.checksum, calc_chk);
                    fseek(fp, write_start, SEEK_SET);
                    close(store_sock);
                    continue;
                }
                chunk_success = 1;
                close(store_sock);
                break; // Successfully got chunk, break replica loop

            }
            close(store_sock);
        }
        
        if (!chunk_success) {
            printf("Failed to retrieve chunk %d from all replicas!\n", chunks[i].chunk_id);
            if(chunks) free(chunks); 
            fclose(fp); 
            return -1;
        }
    }

    if (chunks) free(chunks);
    fclose(fp);
    return 0;
}

int32_t cdfs_ls(const uint8_t *cdfs_path) {
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;
    
    req_list_files_t req;
    memset(&req, 0, sizeof(req));
    strncpy((char *)req.directory, (const char *)cdfs_path, MAX_FILENAME - 1);
    
    net_header_t hdr = { OP_LIST_FILES, sizeof(req) };
    send_exact(meta_sock, &hdr, sizeof(hdr));
    send_exact(meta_sock, &req, sizeof(req));
    
    net_header_t resp_hdr;
    resp_list_files_t resp;
    if (recv_exact(meta_sock, &resp_hdr, sizeof(resp_hdr)) == 0 &&
        recv_exact(meta_sock, &resp, sizeof(resp)) == 0) {
        
        for (int32_t i = 0; i < resp.file_count; i++) {
            uint8_t fname[MAX_FILENAME];
            if (recv_exact(meta_sock, fname, MAX_FILENAME) == 0) {
                printf("%s\n", fname);
            }
        }
    }
    close(meta_sock);
    return 0;
}

int32_t cdfs_status() {
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    net_header_t hdr = { OP_GET_METRICS, 0 };
    send_exact(meta_sock, &hdr, sizeof(hdr));

    net_header_t resp_hdr;
    if (recv_exact(meta_sock, &resp_hdr, sizeof(resp_hdr)) == 0 && resp_hdr.payload_size > 0) {
        char *json = malloc(resp_hdr.payload_size + 1);
        if (recv_exact(meta_sock, json, resp_hdr.payload_size) == 0) {
            json[resp_hdr.payload_size] = '\0';
            printf("%s", json);
        }
        free(json);
    }
    close(meta_sock);
    return 0;
}

int32_t cdfs_rm(const uint8_t *cdfs_path) {
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    req_delete_file_t req;
    memset(&req, 0, sizeof(req));
    strncpy((char *)req.filename, (const char *)cdfs_path, MAX_FILENAME - 1);

    net_header_t hdr = { OP_DELETE_FILE, sizeof(req) };
    send_exact(meta_sock, &hdr, sizeof(hdr));
    send_exact(meta_sock, &req, sizeof(req));

    net_header_t resp_hdr;
    int32_t status = -1;
    if (recv_exact(meta_sock, &resp_hdr, sizeof(resp_hdr)) == 0 &&
        recv_exact(meta_sock, &status, sizeof(status)) == 0) {
        // status received
    }
    close(meta_sock);
    return status;
}

int32_t cdfs_status() {
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    net_header_t hdr = { OP_GET_METRICS, 0 };
    send_exact(meta_sock, &hdr, sizeof(hdr));

    net_header_t resp_hdr;
    if (recv_exact(meta_sock, &resp_hdr, sizeof(resp_hdr)) == 0 && resp_hdr.payload_size > 0) {
        char *json = malloc(resp_hdr.payload_size + 1);
        if (recv_exact(meta_sock, json, resp_hdr.payload_size) == 0) {
            json[resp_hdr.payload_size] = '\0';
            printf("%s", json);
        }
        free(json);
    }
    close(meta_sock);
    return 0;
}

int32_t cdfs_rm(const uint8_t *cdfs_path) {
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
    if (meta_sock < 0) return -1;

    req_delete_file_t req;
    memset(&req, 0, sizeof(req));
    strncpy((char *)req.filename, (const char *)cdfs_path, MAX_FILENAME - 1);

    net_header_t hdr = { OP_DELETE_FILE, sizeof(req) };
    send_exact(meta_sock, &hdr, sizeof(hdr));
    send_exact(meta_sock, &req, sizeof(req));

    net_header_t resp_hdr;
    int32_t status = -1;
    if (recv_exact(meta_sock, &resp_hdr, sizeof(resp_hdr)) == 0 &&
        recv_exact(meta_sock, &status, sizeof(status)) == 0) {
        // status received
    }
    close(meta_sock);
    return status;
}

