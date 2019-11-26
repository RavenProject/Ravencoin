#include <stdint.h>
#include <string.h>

uint32_t be32dec(uint32_t *pp) {
        const uint8_t *p = (uint8_t const *)pp;

        return ((uint32_t)(p[3]) + ((uint32_t)(p[2]) << 8) +
            ((uint32_t)(p[1]) << 16) + ((uint32_t)(p[0]) << 24));
}

void be32enc(void *pp, uint32_t x) {
        uint8_t * p = (uint8_t *)pp;

        p[3] = x & 0xff;
        p[2] = (x >> 8) & 0xff;
        p[1] = (x >> 16) & 0xff;
        p[0] = (x >> 24) & 0xff;
}

uint32_t le32dec(uint32_t *pp) {
        const uint8_t *p = (uint8_t const *)pp;

        return ((uint32_t)(p[0]) + ((uint32_t)(p[1]) << 8) +
            ((uint32_t)(p[2]) << 16) + ((uint32_t)(p[3]) << 24));
}

void le32enc(void *pp, uint32_t x) {
        uint8_t * p = (uint8_t *)pp;

        p[0] = x & 0xff;
        p[1] = (x >> 8) & 0xff;
        p[2] = (x >> 16) & 0xff;
        p[3] = (x >> 24) & 0xff;
}

