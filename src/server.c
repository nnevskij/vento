#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../include/server.h"
#include "../include/http.h"

#define BUFFER_SIZE 4096

// Initializes the socket, binds it to the specified port, and enters
// an infinite loop to accept and handle incoming HTTP connections.
void start_server(int port) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Vento is blowing at [http://localhost:%d]\n\n", port);

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        char buffer[BUFFER_SIZE] = {0};
        read(client_fd, buffer, BUFFER_SIZE);

        HttpRequest req = parse_http_request(buffer);
        printf("[%s] %s %s\n", req.method, req.uri, req.version);

        char filepath[512] = "www";
        if (strcmp(req.uri, "/") == 0) {
            strcat(filepath, "/index.html");
        } else {
            strcat(filepath, req.uri);
        }

        int response_length = 0;

        char *response = build_http_response(filepath, "200 OK", &response_length);

        if (response != NULL) {
            write(client_fd, response, response_length);
            free(response);
        } else {
            response = build_http_response("www/404.html", "404 Not Found", &response_length);

            if (response != NULL) {
                write(client_fd, response, response_length);
                free(response);
            } else {
                const char *hard_fallback = "HTTP/1.1 404 Not Found\r\n"
                                            "Content-Type: text/plain\r\n"
                                            "Connection: close\r\n\r\n"
                                            "404 - Resource not found. (Vento Hard Fallback)";
                write(client_fd, hard_fallback, strlen(hard_fallback));
            }
        }

        close(client_fd);
    }
}