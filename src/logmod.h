/* LOG Module  header */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef _MSC_VER
#pragma warning( disable : 4996) 
#endif

extern void logopen(const char* path, int filesize);
extern void logclose();
extern void logmsg(int level, const char* format, ...);
