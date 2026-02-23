// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool ag_is_tty(void) {
    return isatty(fileno(stdout));
}

void ag_color_printf(const char* color, const char* fmt, ...) {
    bool tty = ag_is_tty();
    if (tty && color) fputs(color, stdout);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (tty && color) fputs(COL_RESET, stdout);
}

void ag_error(const char* fmt, ...) {
    bool tty = isatty(fileno(stderr));
    if (tty) fputs(COL_RED, stderr);
    fprintf(stderr, "error: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (tty) fputs(COL_RESET, stderr);
    fprintf(stderr, "\n");
}

void ag_warn(const char* fmt, ...) {
    bool tty = isatty(fileno(stderr));
    if (tty) fputs(COL_YELLOW, stderr);
    fprintf(stderr, "warning: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (tty) fputs(COL_RESET, stderr);
    fprintf(stderr, "\n");
}

bool ag_str_ends_with(const char* str, const char* suffix) {
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return false;
    return strcmp(str + slen - xlen, suffix) == 0;
}

char* ag_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}
