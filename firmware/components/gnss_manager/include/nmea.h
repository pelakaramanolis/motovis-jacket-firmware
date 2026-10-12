#ifndef NMEA_H
#define NMEA_H

#include <stdbool.h>
#include <stdint.h>

#define NMEA_MAX_SENTENCE 100

typedef struct {
    int32_t lat_e7;      // degrees x 10^7, + is north
    int32_t lon_e7;      // degrees x 10^7, + is east
    uint8_t satellites;
    bool    valid;       // module reports a usable fix
} nmea_fix_t;

typedef struct {
    char    line[NMEA_MAX_SENTENCE];
    uint8_t len;
    bool    collecting;  // seen a '$', building a line
    bool    too_long;    // overran the buffer, discard this sentence
} nmea_parser_t;

void nmea_init(nmea_parser_t *p);

// one byte in; true when a full checksum-clean sentence lands in out
bool nmea_feed(nmea_parser_t *p, char c, nmea_fix_t *out);

#endif // NMEA_H