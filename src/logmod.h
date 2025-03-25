/* LOG Module  header */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

extern void logopen(const char* path, int filesize);
extern void logclose();
extern void logmsg(int level, const char* format, ...);