#ifndef ALEAGIT_UTIL_H
#define ALEAGIT_UTIL_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* Color output */
#define COL_RESET   "\033[0m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_BLUE    "\033[34m"
#define COL_CYAN    "\033[36m"
#define COL_DIM     "\033[2m"
#define COL_BOLD    "\033[1m"

/* Check if stdout is a terminal */
bool ag_is_tty(void);

/* Print with optional color (color stripped if not tty) */
void ag_color_printf(const char* color, const char* fmt, ...);

/* Error reporting */
void ag_error(const char* fmt, ...);
void ag_warn(const char* fmt, ...);

/* String utilities */
bool ag_str_ends_with(const char* str, const char* suffix);
char* ag_strdup(const char* s);

#endif /* ALEAGIT_UTIL_H */
