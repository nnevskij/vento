#include "../include/io.h"
#include <errno.h>
#include <stddef.h>

#ifdef __linux__
#include <sys/sendfile.h>

// Cross-platform wrapper for zero-copy file transmission.
ssize_t vento_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    ssize_t ret = sendfile(out_fd, in_fd, offset, count);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return ret;
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/socket.h>
#include <sys/uio.h>

// Cross-platform wrapper for zero-copy file transmission.
ssize_t vento_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    off_t len = count;
    int ret = sendfile(in_fd, out_fd, *offset, &len, NULL, 0);
    
    if (ret == 0) {
        *offset += len;
        return len;
    } else if (ret == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *offset += len;
            if (len > 0) return len;
            return 0;
        }
        return -1;
    }
    return -1;
}

#else
#error "Unsupported OS for Vento Sendfile"
#endif