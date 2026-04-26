#ifndef IO_H
#define IO_H

#include <sys/types.h>

// Cross-platform wrapper for zero-copy file transmission.
ssize_t vento_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);

#endif