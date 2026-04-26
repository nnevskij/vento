#ifndef RATELIMIT_H
#define RATELIMIT_H

#include <time.h>
#include <arpa/inet.h>

#define RATE_LIMIT_MAX_REQUESTS 100
#define RATE_LIMIT_WINDOW_SEC 10
#define RATE_LIMIT_TABLE_SIZE 1024

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int count;
    time_t window_start;
} RateLimitEntry;

// Initializes the rate limiter table.
void init_ratelimiter(void);

// Checks if the given IP is within the rate limit.
// Returns 1 if allowed, 0 if blocked.
int rate_limit_check(const char *ip);

#endif