//  -*- Mode: C; -*-                                                       
// 
//  util.c
// 
//  (C) Jamie A. Jennings, 2024

#include "util.h"
#include <stdio.h>
#include <stdarg.h> 		// __VA_ARGS__ (var args)
#include <stdlib.h>		// exit()

// Not using this anymore.  It is very useful for debugging memory
// allocation.
//
//#define COLOR "\033[0;46m"
//#define ENDCOLOR "\033[0m"
//
// void *xmalloc(size_t sz) {
//   char *flag = getenv("CSC417_TRACE");
//   if (flag) {
//     fprintf(stderr, COLOR "[Trace] allocating %zu bytes" ENDCOLOR "\n", sz);
//     fflush(stderr);
//   }
//   return malloc(sz);
// };
void *xmalloc(size_t sz) {
  return malloc(sz);
}

void __attribute__((unused)) 
panic_message(const char *filename, int lineno, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "Panic at %s:%d ", filename, lineno);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

