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
    
    // Seed rng for chunk_id
    srand(time(NULL) ^ getpid());
    
    uint8_t buffer[CHUNK_SIZE];
    chunk_info_t chunks[MAX_CHUNKS];
    int32_t chunk_count = 0;
    
    // Get active nodes
    cdfs_config_t config;
    load_config((const uint8_t *)"cdfs.conf", &config);
    int32_t meta_sock = connect_to_server((const uint8_t *)config.meta_ip, config.meta_port);
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
        size_t bytes = fread(buffer, 1, CHUNK_SIZE, fp);
        if (bytes == 0) {
            if (feof(fp)) break;
            if (ferror(fp)) { fclose(fp); return -1; }
        }
        
        if (chunk_count >= MAX_CHUNKS) { fclose(fp); return -1; }
        
        int32_t chunk_id = rand() % 1000000;
        uint32_t checksum = calculate_checksum(buffer, bytes);
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
            send_exact(store_sock, buffer, bytes);
            
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

    uint8_t buffer[CHUNK_SIZE];

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
                
                if (recv_exact(store_sock, buffer, load_resp.size) == 0) {
                    uint32_t calc_chk = calculate_checksum(buffer, load_resp.size);
                    if (calc_chk != load_resp.checksum) {
                        printf("Checksum mismatch on chunk %d from replica %s:%d! Expected %u, got %u. Fallback...\n",
                               chunks[i].chunk_id, chunks[i].node_ips[j], chunks[i].node_ports[j], load_resp.checksum, calc_chk);
                        close(store_sock);
                        continue;
                    }
                    fwrite(buffer, 1, load_resp.size, fp);
                    chunk_success = 1;
                    close(store_sock);
                    break; // Successfully got chunk, break replica loop
                }
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
