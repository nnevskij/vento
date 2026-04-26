#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "../include/server.h"
#include "../include/http.h"
#include "../include/utils.h"
#include "../include/logger.h"
#include "../include/config.h"
#include "../include/event.h"
#include "../include/ratelimit.h"
#include "../include/io.h"

#define MAX_EVENTS 1024
#define MAX_FDS 8192

volatile sig_atomic_t server_running = 1;

struct ClientState clients[MAX_FDS];

// Signal handler to gracefully shut down the server
void handle_signal(int sig) {
    (void)sig;
    server_running = 0;
}

// Initializes the client state array
void init_clients() {
    for (int i = 0; i < MAX_FDS; i++) {
        clients[i].client_fd = -1;
        clients[i].write_buffer = NULL;
        clients[i].file_fd = -1;
        clients[i].keep_alive = 0;
    }
}

// Cleans up client state and closes the socket
void free_client(int fd) {
    if (fd >= 0 && fd < MAX_FDS) {
        if (clients[fd].write_buffer) {
            free(clients[fd].write_buffer);
            clients[fd].write_buffer = NULL;
        }
        if (clients[fd].file_fd != -1) {
            close(clients[fd].file_fd);
            clients[fd].file_fd = -1;
        }
        if (clients[fd].client_fd != -1) {
            close(fd);
            clients[fd].client_fd = -1;
        }
    }
}

void cleanup_timeouts(int efd) {
    (void)efd;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_FDS; i++) {
        if (clients[i].client_fd != -1) {
            if ((now - clients[i].last_active_time) > CONNECTION_TIMEOUT) {
                free_client(i);
            }
        }
    }
}

void start_server(ServerConfig config) {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(server_fd);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1024) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    init_clients();

    int efd = event_init();
    if (efd == -1) {
        perror("event_init failed");
        exit(EXIT_FAILURE);
    }

    if (event_add(efd, server_fd, 1) == -1) {
        perror("event_add server_fd failed");
        exit(EXIT_FAILURE);
    }

    printf("          ▄▄ ▄▄ ▄▄▄▄▄ ▄▄  ▄▄ ▄▄▄▄▄▄ ▄▄▄            \n"
           "▄▀▀▄  █   ██▄██ ██▄▄  ███▄██   ██  ██▀██   ▄▀▀▄  █ \n"
           "▀   ▀▀     ▀█▀  ██▄▄▄ ██ ▀██   ██  ▀███▀   ▀   ▀▀  \n\n"
           "[ Vento WS is Blowing ]\n"
           "-------------------------------\n"
           "Listening on : http://localhost:%d\n"
           "Document Root: %s\n"
           "Press Ctrl+C to shut down\n\n",
           config.port, config.document_root);

    struct VentoEvent ev_list[MAX_EVENTS];

    while (server_running) {
        int nev = event_wait(efd, ev_list, MAX_EVENTS, 5000);
        if (nev < 0) {
            if (errno == EINTR) continue;
            perror("event_wait failed");
            break;
        }

        for (int i = 0; i < nev; i++) {
            int fd = ev_list[i].fd;

            if (ev_list[i].type == EVENT_ERROR) {
                free_client(fd);
                continue;
            }

            if (fd == server_fd) {
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        perror("accept failed");
                        break;
                    }

                    if (client_fd >= MAX_FDS) {
                        close(client_fd);
                        continue;
                    }

                    set_nonblocking(client_fd);

                    clients[client_fd].client_fd = client_fd;
                    clients[client_fd].bytes_read = 0;
                    clients[client_fd].bytes_to_write = 0;
                    clients[client_fd].bytes_written = 0;
                    clients[client_fd].file_fd = -1;
                    clients[client_fd].file_offset = 0;
                    clients[client_fd].file_size = 0;
                    clients[client_fd].keep_alive = 0;

                    if (clients[client_fd].write_buffer) {
                        free(clients[client_fd].write_buffer);
                        clients[client_fd].write_buffer = NULL;
                    }
                    clients[client_fd].status = STATE_READING;
                    clients[client_fd].last_active_time = time(NULL);
                    inet_ntop(AF_INET, &(client_addr.sin_addr), clients[client_fd].client_ip, INET_ADDRSTRLEN);
                    strncpy(clients[client_fd].document_root, config.document_root, sizeof(clients[client_fd].document_root) - 1);
                    clients[client_fd].document_root[sizeof(clients[client_fd].document_root) - 1] = '\0';

                    event_add(efd, client_fd, 0);

                    if (!rate_limit_check(clients[client_fd].client_ip)) {
                        const char *too_many = "HTTP/1.1 429 Too Many Requests\r\nConnection: close\r\n\r\n";
                        clients[client_fd].write_buffer = strdup(too_many);
                        clients[client_fd].bytes_to_write = strlen(too_many);
                        clients[client_fd].bytes_written = 0;
                        clients[client_fd].status = STATE_WRITING;
                        event_mod(efd, client_fd, EVENT_WRITE);
                    }
                }
            } else if (ev_list[i].type == EVENT_READ) {
                struct ClientState *client = &clients[fd];
                if (client->client_fd == -1) continue;

                int bytes_to_read = sizeof(client->read_buffer) - 1 - client->bytes_read;
                if (bytes_to_read > 0) {
                    ssize_t bytes_read = read(fd, client->read_buffer + client->bytes_read, bytes_to_read);
                    if (bytes_read > 0) {
                        client->bytes_read += bytes_read;
                        client->read_buffer[client->bytes_read] = '\0';
                        client->last_active_time = time(NULL);

                        if (strstr(client->read_buffer, "\r\n\r\n") != NULL) {
                            HttpRequest req = parse_http_request(client->read_buffer);

                            if (strstr(req.version, "1.1") != NULL) {
                                client->keep_alive = 1;
                            } else {
                                client->keep_alive = 0;
                            }

                            if (strstr(client->read_buffer, "Keep-Alive") != NULL ||
                                strstr(client->read_buffer, "keep-alive") != NULL) {
                                client->keep_alive = 1;
                            }

                            if (strstr(client->read_buffer, "Connection: close") != NULL ||
                                strstr(client->read_buffer, "connection: close") != NULL) {
                                client->keep_alive = 0;
                            }

                            char *body_start = strstr(client->read_buffer, "\r\n\r\n") + 4;
                            size_t headers_len = body_start - client->read_buffer;
                            size_t body_read = client->bytes_read - headers_len;

                            int request_complete = 1;
                            if (req.content_length > 0) {
                                size_t to_read = req.content_length;
                                if (to_read > sizeof(req.body) - 1) {
                                    to_read = sizeof(req.body) - 1;
                                }
                                if (body_read < to_read) {
                                    request_complete = 0;
                                } else {
                                    req = parse_http_request(client->read_buffer);
                                }
                            }

                            if (request_complete) {
                                char decoded_uri[256];
                                url_decode(req.path, decoded_uri);
                                int status_code = 200;

                                printf("[%s] %s %s\n", req.method, decoded_uri, req.version);

                                if (!is_safe_uri(decoded_uri)) {
                                    printf("WARNING: Blocked potential path traversal attack: %s\n", decoded_uri);
                                    const char *conn_str = client->keep_alive ? "keep-alive" : "close";
                                    char forbidden[512];
                                    snprintf(forbidden, sizeof(forbidden),
                                             "HTTP/1.1 403 Forbidden\r\n"
                                             "Content-Type: text/plain\r\n"
                                             "Connection: %s\r\n\r\n"
                                             "403 - Forbidden.", conn_str);
                                    client->write_buffer = strdup(forbidden);
                                    client->bytes_to_write = strlen(forbidden);
                                    status_code = 403;
                                } else if (strcmp(req.method, "POST") == 0 && strcmp(decoded_uri, "/api/echo") == 0) {
                                    char response[4096];
                                    const char *conn_str = client->keep_alive ? "keep-alive" : "close";
                                    int response_len = snprintf(response, sizeof(response),
                                                                "HTTP/1.1 200 OK\r\n"
                                                                "Content-Type: text/plain\r\n"
                                                                "Content-Length: %zu\r\n"
                                                                "Connection: %s\r\n\r\n"
                                                                "%s", strlen(req.body), conn_str, req.body);
                                    client->write_buffer = malloc(response_len + 1);
                                    memcpy(client->write_buffer, response, response_len);
                                    client->write_buffer[response_len] = '\0';
                                    client->bytes_to_write = response_len;
                                    status_code = 200;
                                } else {
                                    char filepath[512];
                                    strcpy(filepath, client->document_root);
                                    strcat(filepath, decoded_uri);

                                    struct stat path_stat;
                                    int redirect = 0;
                                    if (stat(filepath, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
                                        if (filepath[strlen(filepath) - 1] != '/') {
                                            redirect = 1;
                                            char redirect_response[1024];
                                            const char *conn_str = client->keep_alive ? "keep-alive" : "close";
                                            int resp_len = snprintf(redirect_response, sizeof(redirect_response),
                                                     "HTTP/1.1 301 Moved Permanently\r\n"
                                                     "Location: %s/\r\n"
                                                     "Content-Length: 0\r\n"
                                                     "Connection: %s\r\n\r\n", req.path, conn_str);
                                            client->write_buffer = malloc(resp_len + 1);
                                            memcpy(client->write_buffer, redirect_response, resp_len);
                                            client->write_buffer[resp_len] = '\0';
                                            client->bytes_to_write = resp_len;
                                            status_code = 301;
                                        } else {
                                            strcat(filepath, "index.html");
                                        }
                                    }

                                    if (!redirect) {
                                        int file_fd = open(filepath, O_RDONLY);
                                        if (file_fd != -1) {
                                            struct stat path_stat;
                                            fstat(file_fd, &path_stat);
                                            const char *mime_type = get_mime_type(filepath);
                                            char header_buffer[1024];
                                            const char *conn_str = client->keep_alive ? "keep-alive" : "close";
                                            int header_len = snprintf(header_buffer, sizeof(header_buffer),
                                                "HTTP/1.1 200 OK\r\n"
                                                "Server: Vento/1.0\r\n"
                                                "Content-Type: %s\r\n"
                                                "Content-Length: %lld\r\n"
                                                "Connection: %s\r\n\r\n",
                                                mime_type, (long long)path_stat.st_size, conn_str);

                                            client->write_buffer = malloc(header_len + 1);
                                            memcpy(client->write_buffer, header_buffer, header_len);
                                            client->write_buffer[header_len] = '\0';
                                            client->bytes_to_write = header_len;

                                            client->file_fd = file_fd;
                                            client->file_size = path_stat.st_size;
                                            client->file_offset = 0;
                                            status_code = 200;
                                        } else {
                                            int response_length = 0;
                                            char error_path[512];
                                            snprintf(error_path, sizeof(error_path), "%s/404.html", client->document_root);
                                            char *response = build_http_response(error_path, "404 Not Found", &response_length, client->keep_alive);

                                            if (response != NULL) {
                                                client->write_buffer = response;
                                                client->bytes_to_write = response_length;
                                                status_code = 404;
                                            } else {
                                                const char *conn_str = client->keep_alive ? "keep-alive" : "close";
                                                char hard_fallback[512];
                                                snprintf(hard_fallback, sizeof(hard_fallback),
                                                         "HTTP/1.1 404 Not Found\r\n"
                                                         "Content-Type: text/plain\r\n"
                                                         "Connection: %s\r\n\r\n"
                                                         "404 - Resource not found. (Vento Hard Fallback)", conn_str);
                                                client->write_buffer = strdup(hard_fallback);
                                                client->bytes_to_write = strlen(hard_fallback);
                                                status_code = 404;
                                            }
                                        }
                                    }
                                }

                                log_access(client->client_ip, req.method, decoded_uri, req.version, status_code);

                                client->status = STATE_WRITING;
                                event_mod(efd, fd, EVENT_WRITE);
                            }
                        }
                    } else if (bytes_read == 0) {
                        free_client(fd);
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            free_client(fd);
                        }
                    }
                } else {
                    free_client(fd);
                }
            } else if (ev_list[i].type == EVENT_WRITE) {
                struct ClientState *client = &clients[fd];
                if (client->client_fd == -1) continue;

                if (client->status == STATE_WRITING) {
                    if (client->write_buffer != NULL && client->bytes_written < client->bytes_to_write) {
                        size_t remaining = client->bytes_to_write - client->bytes_written;
                        ssize_t bytes_written = write(fd, client->write_buffer + client->bytes_written, remaining);
                        if (bytes_written > 0) {
                            client->bytes_written += bytes_written;
                            client->last_active_time = time(NULL);
                        } else if (bytes_written < 0) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                free_client(fd);
                                continue;
                            }
                        }
                    }

                    if (client->bytes_written == client->bytes_to_write) {
                        if (client->file_fd != -1) {
                            while (client->file_offset < (off_t)client->file_size) {
                                ssize_t sent = vento_sendfile(fd, client->file_fd, &client->file_offset, client->file_size - client->file_offset);
                                if (sent > 0) {
                                    client->last_active_time = time(NULL);
                                } else if (sent == 0) {
                                    break;
                                } else {
                                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                        break;
                                    }
                                    client->keep_alive = 0;
                                    break;
                                }
                            }
                            if (client->file_offset >= (off_t)client->file_size) {
                                if (client->keep_alive) {
                                    close(client->file_fd);
                                    client->file_fd = -1;
                                    client->bytes_read = 0;
                                    client->bytes_to_write = 0;
                                    client->bytes_written = 0;
                                    client->file_offset = 0;
                                    client->file_size = 0;
                                    memset(client->read_buffer, 0, sizeof(client->read_buffer));
                                    if (client->write_buffer) {
                                        free(client->write_buffer);
                                        client->write_buffer = NULL;
                                    }
                                    client->last_active_time = time(NULL);
                                    client->status = STATE_READING;
                                    event_mod(efd, fd, EVENT_READ);
                                } else {
                                    free_client(fd);
                                }
                            }
                        } else {
                            if (client->keep_alive) {
                                client->bytes_read = 0;
                                client->bytes_to_write = 0;
                                client->bytes_written = 0;
                                memset(client->read_buffer, 0, sizeof(client->read_buffer));
                                if (client->write_buffer) {
                                    free(client->write_buffer);
                                    client->write_buffer = NULL;
                                }
                                client->last_active_time = time(NULL);
                                client->status = STATE_READING;
                                event_mod(efd, fd, EVENT_READ);
                            } else {
                                free_client(fd);
                            }
                        }
                    }
                }
            }
        }

        cleanup_timeouts(efd);
    }

    printf("\nShutting down Vento...\n");
    close(server_fd);
    close(efd);

    for (int i = 0; i < MAX_FDS; i++) {
        free_client(i);
    }
}