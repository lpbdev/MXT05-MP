#include "version_info.h"
#include <stdio.h>
#include <string.h>

/* ==================== 编译时注入的信息 ==================== */
#ifdef DEBUG
#define VER_DEBUG_FLAGS "debug"
#elif defined(NDEBUG)
#define VER_DEBUG_FLAGS "release"
#else
#define VER_DEBUG_FLAGS "unknown"
#endif

#ifndef VER_GIT_INFO
#define VER_GIT_INFO "unknown"
#endif
#ifndef VER_GIT_STATUS
#define VER_GIT_STATUS "unknown"
#endif
#ifndef VER_SVN_INFO
#define VER_SVN_INFO "unknown"
#endif
#ifndef VER_SVN_STATUS
#define VER_SVN_STATUS "unknown"
#endif
#ifndef VER_BUILD_TIME
#define VER_BUILD_TIME __DATE__ " " __TIME__
#endif
#ifndef VER_BUILD_HOST
#define VER_BUILD_HOST "unknown"
#endif
#ifndef VER_BUILD_FLAGS
#define VER_BUILD_FLAGS "unknown"
#endif

/* ==================== 编译器信息 ==================== */

#define VERFLAGS_HELPER(x) #x
#define VERFLAGS(x) VERFLAGS_HELPER(x)

#if defined(_MSC_VER)
#define VER_COMPILER_INFO "MSVC " VERFLAGS(_MSC_VER)
#elif defined(__clang__)
#define VER_COMPILER_INFO "Clang " __VERSION__
#elif defined(__GNUC__)
#define VER_COMPILER_INFO "GCC " __VERSION__
#else
#define VER_COMPILER_INFO "Unknown Compiler"
#endif

/* ==================== 平台检测 ==================== */
/*
 *  1. MSYS 环境：定义了 __MSYS__，提供 POSIX uname
 *  2. MINGW64/UCRT64/MSVC：定义了 _WIN32（但没 __MSYS__），用 Win32 API
 *  3. Linux/macOS/BSD：走 POSIX
 */

#if defined(__MSYS__)
/* MSYS 仿真环境：有 sys/utsname.h */
#include <sys/utsname.h>
#define VER_HAS_UNAME 1
#elif defined(_WIN32)
/* 原生 Windows（MINGW64/UCRT64/MSVC）：用 Win32 API */
#include <windows.h>
#define VER_HAS_UNAME 0
#else
/* Linux / macOS / BSD */
#include <sys/utsname.h>
#define VER_HAS_UNAME 1
#endif

/* ==================== 运行时 OS ==================== */

const char* ver_get_runtime_os(void)
{
    static char os_info[256] = {0};
    if (os_info[0] != '\0')
    {
        return os_info;
    }

#if VER_HAS_UNAME
    struct utsname u;
    if (uname(&u) == 0)
    {
        snprintf(os_info, sizeof(os_info), "%s %s (%s)", u.sysname, u.release, u.machine);
    }
    else
    {
        snprintf(os_info, sizeof(os_info), "unknown");
    }
#else
    /* 原生 Windows */
    OSVERSIONINFOA vi;
    vi.dwOSVersionInfoSize = sizeof(vi);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetVersionExA(&vi);
#pragma GCC diagnostic pop

    snprintf(
        os_info, sizeof(os_info), "Windows %lu.%lu.%lu", (unsigned long)vi.dwMajorVersion,
        (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber
    );
#endif

    return os_info;
}

/* ==================== 各字段 getter ==================== */

const char* ver_get_git_info(void) { return VER_GIT_INFO; }
const char* ver_get_git_status(void) { return VER_GIT_STATUS; }
const char* ver_get_svn_info(void) { return VER_SVN_INFO; }
const char* ver_get_svn_status(void) { return VER_SVN_STATUS; }
const char* ver_get_build_time(void) { return VER_BUILD_TIME; }
const char* ver_get_build_host(void) { return VER_BUILD_HOST; }
const char* ver_get_compiler(void) { return VER_COMPILER_INFO; }
const char* ver_get_build_flags(void) { return VER_BUILD_FLAGS; }
const char* ver_get_debug_flags(void) { return VER_DEBUG_FLAGS; }

/* ==================== 干净状态判断 ==================== */

int ver_git_is_clean(void)
{
    const char* s = VER_GIT_STATUS;
    if (strcmp(s, "clean") == 0)
    {
        return 1;
    }
    if (strcmp(s, "dirty") == 0)
    {
        return 0;
    }
    return -1;
}

int ver_svn_is_clean(void)
{
    const char* s = VER_SVN_STATUS;
    if (strcmp(s, "clean") == 0)
    {
        return 1;
    }
    if (strcmp(s, "dirty") == 0)
    {
        return 0;
    }
    return -1;
}

/* ==================== 一次性打印 ==================== */

void ver_print_all(void)
{
    printf("================ Build Information ================\n");
    printf("  Git        : %s\n", ver_get_git_info());
    printf("  SVN        : %s, %s\n", ver_get_svn_info(), ver_get_svn_status());
    printf("  Build Time : %s\n", ver_get_build_time());
    printf("  Build Host : %s,(%s)\n", ver_get_build_host(), ver_get_runtime_os());
    printf("  Compiler   : %s,%s\n", ver_get_compiler(), ver_get_debug_flags());
    printf("  DEFINES    : %s\n", ver_get_build_flags());

    printf("===================================================\n");
}
