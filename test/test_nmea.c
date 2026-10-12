#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nmea.h"

#define GGA_FIX   "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"
#define GGA_NOFIX "GPGGA,123519,,,,,0,00,,,M,,M,,"
#define GGA_SOUTH "GPGGA,123519,3352.000,S,15112.000,W,1,06,1.0,10.0,M,0.0,M,,"
#define RMC_OTHER "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"

static int failures = 0;

static void check(const char *name, bool ok)
{
    printf("  %-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) { failures++; }
}

// wraps a body in $...*HH\r\n with a correct checksum
static void make_sentence(char *out, const char *body)
{
    uint8_t sum = 0;
    for (const char *p = body; *p != '\0'; p++) {
        sum ^= (uint8_t) *p;
    }
    sprintf(out, "$%s*%02X\r\n", body, sum);
}

static int feed_all(const char *s, nmea_fix_t *out)
{
    nmea_parser_t p;
    nmea_init(&p);

    int count = 0;
    for (const char *c = s; *c != '\0'; c++) {
        nmea_fix_t f;
        if (nmea_feed(&p, *c, &f)) {
            *out   = f;
            count += 1;
        }
    }
    return count;
}

int main(void)
{
    char s[160];
    nmea_fix_t f = {0};
    int n;

    printf("\nnmea parser\n");

    make_sentence(s, GGA_FIX);
    n = feed_all(s, &f);
    check("good sentence reports one fix", n == 1);
    check("latitude 4807.038 N -> 481173000", f.lat_e7 == 481173000);
    check("longitude 01131.000 E -> 115166666", f.lon_e7 == 115166666);
    check("satellite count is 8", f.satellites == 8);
    check("fix is marked valid", f.valid);

    make_sentence(s, GGA_SOUTH);
    n = feed_all(s, &f);
    check("south latitude is negative", n == 1 && f.lat_e7 == -338666666);
    check("west longitude is negative", f.lon_e7 == -1512000000);

    make_sentence(s, GGA_NOFIX);
    f.valid = true;
    n = feed_all(s, &f);
    check("quality 0 still parses", n == 1);
    check("quality 0 marked invalid", !f.valid);
    check("quality 0 zeroes coordinates", f.lat_e7 == 0 && f.lon_e7 == 0);

    make_sentence(s, GGA_FIX);
    s[20] = (s[20] == '0') ? '1' : '0';     // corrupt a digit, checksum now wrong
    n = feed_all(s, &f);
    check("bad checksum is rejected", n == 0);

    make_sentence(s, RMC_OTHER);
    n = feed_all(s, &f);
    check("non-GGA sentence ignored", n == 0);

    // same sentence, delivered in three awkward chunks
    {
        make_sentence(s, GGA_FIX);

        nmea_parser_t p;
        nmea_init(&p);

        nmea_fix_t g = {0};
        size_t len   = strlen(s);
        size_t cuts[3] = { 10, 35, len };
        size_t pos   = 0;
        int    count = 0;

        for (int c = 0; c < 3; c++) {
            for (; pos < cuts[c]; pos++) {
                if (nmea_feed(&p, s[pos], &g)) { count++; }
            }
        }

        check("sentence split across three reads still parses",
              count == 1 && g.lat_e7 == 481173000);
    }

    // 400 bytes of junk with no line ending, then a good sentence
    {
        nmea_parser_t p;
        nmea_init(&p);

        nmea_fix_t g = {0};
        int count = 0;

        nmea_feed(&p, '$', &g);
        for (int i = 0; i < 400; i++) {
            nmea_feed(&p, 'X', &g);
        }
        nmea_feed(&p, '\n', &g);

        make_sentence(s, GGA_FIX);
        for (const char *c = s; *c != '\0'; c++) {
            if (nmea_feed(&p, *c, &g)) { count++; }
        }

        check("recovers after an oversized sentence", count == 1);
    }

    printf("\n%s\n\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}