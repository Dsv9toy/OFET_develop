#include "proto.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int proto_split(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        char *s = strchr(p, ';');
        if (!s)
            break;
        *s = '\0';
        p = s + 1;
    }
    return n;
}

bool proto_float(const char *s, float *out)
{
    if (!s || !*s)
        return false;
    char *end;
    float v = strtof(s, &end);
    if (*end != '\0' || !isfinite(v))
        return false;
    *out = v;
    return true;
}

bool proto_int(const char *s, long *out)
{
    if (!s || !*s)
        return false;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0')
        return false;
    *out = v;
    return true;
}

void proto_fmt_u(float v, char *buf)
{
    long q = lroundf(fabsf(v) * 10000.0f);
    const char *sign = (v < 0 && q != 0) ? "-" : "";
    sprintf(buf, "%s%ld.%04ld", sign, q / 10000, q % 10000);
}

void proto_fmt_e(float v, char *buf)
{
    if (v == 0.0f || !isfinite(v)) {
        strcpy(buf, "0.0000E+00");
        return;
    }
    float a = fabsf(v);
    int e = (int)floorf(log10f(a));
    long m = lroundf(a / powf(10.0f, (float)e) * 10000.0f);
    if (m >= 100000) { m = lroundf((float)m / 10.0f); e++; }
    if (m < 10000)   { m *= 10; e--; }
    sprintf(buf, "%s%ld.%04ldE%c%02d", v < 0 ? "-" : "", m / 10000, m % 10000,
            e < 0 ? '-' : '+', e < 0 ? -e : e);
}

bool proto_ip(const char *s, unsigned char ip[4])
{
    int part = 0;
    long v = -1;
    for (const char *p = s; ; ++p) {
        if (*p >= '0' && *p <= '9') {
            v = (v < 0 ? 0 : v * 10) + (*p - '0');
            if (v > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            if (v < 0 || part > 3) return false;
            ip[part++] = (unsigned char)v;
            v = -1;
            if (*p == '\0') break;
        } else {
            return false;
        }
    }
    return part == 4;
}

void proto_fmt_ip(const unsigned char ip[4], char *buf)
{
    sprintf(buf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
