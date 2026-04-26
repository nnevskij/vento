#include <string.h>
#include <time.h>
#include "../include/ratelimit.h"

static RateLimitEntry table[RATE_LIMIT_TABLE_SIZE];

static unsigned int hash_ip(const char *ip) {
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*ip++)) {
        hash = ((hash << 5) + hash) + c; 
    }
    return hash % RATE_LIMIT_TABLE_SIZE;
}

// Initializes the rate limiter table.
void init_ratelimiter(void) {
    for (int i = 0; i < RATE_LIMIT_TABLE_SIZE; i++) {
        table[i].ip[0] = '\0';
        table[i].count = 0;
        table[i].window_start = 0;
    }
}

// Checks if the given IP is within the rate limit.
// Returns 1 if allowed, 0 if blocked.
int rate_limit_check(const char *ip) {
    time_t now = time(NULL);
    unsigned int index = hash_ip(ip);
    unsigned int start_index = index;

    while (table[index].ip[0] != '\0') {
        if (strcmp(table[index].ip, ip) == 0) {
            if (now - table[index].window_start >= RATE_LIMIT_WINDOW_SEC) {
                table[index].window_start = now;
                table[index].count = 1;
                return 1;
            }
            if (table[index].count < RATE_LIMIT_MAX_REQUESTS) {
                table[index].count++;
                return 1;
            }
            return 0;
        }

        if (now - table[index].window_start >= RATE_LIMIT_WINDOW_SEC) {
            break;
        }
        
        index = (index + 1) % RATE_LIMIT_TABLE_SIZE;
        if (index == start_index) {
            index = start_index;
            break;
        }
    }

    strncpy(table[index].ip, ip, INET_ADDRSTRLEN - 1);
    table[index].ip[INET_ADDRSTRLEN - 1] = '\0';
    table[index].window_start = now;
    table[index].count = 1;
    return 1;
}