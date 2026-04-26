#ifndef SERVER_H
#define SERVER_H

#include "config.h"
#include <time.h>

#define CONNECTION_TIMEOUT 30

enum ClientStatus {
    STATE_READING,
    STATE_WRITING
};

struct ClientState {
    int client_fd;
    char read_buffer[8192];
    size_t bytes_read;
    char *write_buffer;
    size_t bytes_to_write;
    size_t bytes_written;
    enum ClientStatus status;
    char client_ip[64];
    char document_root[256];
    time_t last_active_time;
    int file_fd;
    off_t file_offset;
    size_t file_size;
    int keep_alive;
};

// Starts the server and listens on a specific port based on the provided configuration.
// Contains the infinite loop (while(server_running)) to accept incoming connections.
void start_server(ServerConfig config);

#endif