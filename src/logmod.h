/* LOG Module  header */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

extern void logopen(const char* path, int filesize);
extern void logclose();
extern void logmsg(int level, const char* format, ...);
