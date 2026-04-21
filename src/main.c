#include <stdio.h>
#include <stdlib.h>
#include "../include/server.h"

#define DEFAULT_PORT 8080

// Entry point for the Vento web server.
// Parses command line arguments to set the listening port.
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("Starting Vento on port %d...\n", port);
    start_server(port);

    return 0;
}