#include <string.h>

#include "nmea.h"

void nmea_init(nmea_parser_t *p)
{
    p->len        = 0;
    p->collecting = false;
    p->too_long   = false;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

// line holds everything after '$', ending in "*HH"
static bool checksum_ok(const char *line, uint8_t len)
{
    if (len < 4 || line[len - 3] != '*') {
        return false;
    }

    int hi = hex_val(line[len - 2]);
    int lo = hex_val(line[len - 1]);
    if (hi < 0 || lo < 0) {
        return false;
    }

    uint8_t sum = 0;
    for (uint8_t i = 0; i < len - 3; i++) {
        sum ^= (uint8_t) line[i];
    }

    return sum == (uint8_t) ((hi << 4) | lo);
}

// field 0 is the sentence id, then comma-separated after that
static const char *field(const char *line, uint8_t len, int n, uint8_t *flen)
{
    int     idx   = 0;
    uint8_t start = 0;

    for (uint8_t i = 0; i <= len; i++) {
        char c = (i < len) ? line[i] : ',';

        if (c == ',' || c == '*') {
            if (idx == n) {
                *flen = (uint8_t) (i - start);
                return line + start;
            }
            idx++;
            start = (uint8_t) (i + 1);
            if (c == '*') { break; }
        }
    }

    return NULL;
}

static uint32_t to_uint(const char *s, uint8_t len)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') { break; }
        v = v * 10 + (uint32_t) (s[i] - '0');
    }
    return v;
}

// "4807.038" (48 deg, 7.038 min) -> 481173000
static int32_t dm_to_e7(const char *s, uint8_t len)
{
    uint8_t dot = len;
    for (uint8_t i = 0; i < len; i++) {
        if (s[i] == '.') { dot = i; break; }
    }
    if (dot < 3) { return 0; }

    uint32_t whole = to_uint(s, dot);
    uint32_t deg   = whole / 100;
    uint32_t min   = whole % 100;

    uint32_t frac = 0;
    uint8_t  got  = 0;
    for (uint8_t i = (uint8_t) (dot + 1); i < len && got < 4; i++, got++) {
        if (s[i] < '0' || s[i] > '9') { break; }
        frac = frac * 10 + (uint32_t) (s[i] - '0');
    }
    while (got < 4) { frac *= 10; got++; }

    uint32_t min_e4 = min * 10000U + frac;

    return (int32_t) (deg * 10000000UL + (min_e4 * 100UL) / 6UL);
}

// $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,...
static bool parse_gga(const char *line, uint8_t len, nmea_fix_t *out)
{
    uint8_t n = 0;

    const char *q = field(line, len, 6, &n);
    if (q == NULL) {
        return false;
    }

    out->valid = (to_uint(q, n) > 0);

    const char *sats = field(line, len, 7, &n);
    out->satellites = (uint8_t) (sats ? to_uint(sats, n) : 0);

    if (!out->valid) {
        out->lat_e7 = 0;
        out->lon_e7 = 0;
        return true;
    }

    uint8_t latn = 0, lonn = 0, tmp = 0;
    const char *lat = field(line, len, 2, &latn);
    const char *ns  = field(line, len, 3, &tmp);
    const char *lon = field(line, len, 4, &lonn);
    const char *ew  = field(line, len, 5, &tmp);

    if (!lat || !ns || !lon || !ew || latn == 0 || lonn == 0) {
        out->valid  = false;
        out->lat_e7 = 0;
        out->lon_e7 = 0;
        return true;
    }

    out->lat_e7 = dm_to_e7(lat, latn);
    if (ns[0] == 'S') { out->lat_e7 = -out->lat_e7; }

    out->lon_e7 = dm_to_e7(lon, lonn);
    if (ew[0] == 'W') { out->lon_e7 = -out->lon_e7; }

    return true;
}

bool nmea_feed(nmea_parser_t *p, char c, nmea_fix_t *out)
{
    if (c == '$') {
        p->len        = 0;
        p->collecting = true;
        p->too_long   = false;
        return false;
    }

    if (!p->collecting) {
        return false;
    }

    if (c == '\r' || c == '\n') {
        bool got = false;

        if (!p->too_long && p->len > 6 && checksum_ok(p->line, p->len) &&
            memcmp(p->line + 2, "GGA", 3) == 0) {
            got = parse_gga(p->line, p->len, out);
        }

        p->collecting = false;
        p->len        = 0;
        p->too_long   = false;
        return got;
    }

    if (p->len >= NMEA_MAX_SENTENCE) {
        p->too_long = true;
        return false;
    }

    p->line[p->len++] = c;
    return false;
}