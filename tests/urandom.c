#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "pcg_variants.h"

#define BUFSIZE 4096

static uint64_t parse_integer(const char* str)
{
    if (str == NULL) return 0;

    uint64_t n = 0;

    for (const char* p = str; *p != '\0'; p++) {
        if (!('0' <= *p && *p <= '9')) return 0;
        n = n * 10 + (*p - '0');  // overflow?
    }

    return n;
}

int main(int argc, char** argv)
{
    pcg32_random_t rng;

    if (argc > 1) {
        uint64_t seed = parse_integer(argv[1]);
        pcg32_srandom_r(&rng, seed, seed);
    } else {
        pcg32_srandom_r(&rng, time(NULL), (intptr_t)&rng);
    }
    if (argc > 2) {
        uint64_t skip = parse_integer(argv[2]);
        pcg32_advance_r(&rng, skip);
    }

    uint8_t buf[BUFSIZE];
    size_t i = 0;

    while (1) {
        if (ferror(stdout)) return 1;
        if (i == 0) {
            while (i < BUFSIZE) {
                uint32_t byte = pcg32_boundedrand_r(&rng, 256);
                if (byte >= 256) return 2;
                buf[i++] = (uint8_t)byte;
            }
        }
        i -= fwrite(buf + BUFSIZE - i, sizeof(uint8_t), i, stdout);
    }

    return 0;
}
