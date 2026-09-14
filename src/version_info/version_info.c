#include "version_info.h"
#include <stdio.h>
#include <string.h>

/* ==================== 编译时注入的信息 ==================== */
#ifdef DEBUG
#define VER_DEBUG_FLAGS "Debug"
#elif defined(NDEBUG)
#define VER_DEBUG_FLAGS "Release"
#else
#define VER_DEBUG_FLAGS "unknown"
#endif

#ifndef VER_GIT_BRANCH
#define VER_GIT_BRANCH "unknown"
#endif
#ifndef VER_GIT_COMMIT
#define VER_GIT_COMMIT "unknown"
#endif
#ifndef VER_GIT_STATUS
#define VER_GIT_STATUS "unknown"
#endif
#ifndef VER_GIT_MSG
#define VER_GIT_MSG "unknown"
#endif
#ifndef VER_GIT_TIME
#define VER_GIT_TIME "unknown"
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

const char* ver_get_git_branch(void) { return VER_GIT_BRANCH; }
const char* ver_get_git_commit(void) { return VER_GIT_COMMIT; }
const char* ver_get_git_status(void) { return VER_GIT_STATUS; }
const char* ver_get_git_msg(void) { return VER_GIT_MSG; }
const char* ver_get_git_time(void) { return VER_GIT_TIME; }

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

const char* ver_get_c_standard(void)
{
#if !defined(__STDC_VERSION__)
    return "C89/C90";
#elif __STDC_VERSION__ == 199409L
    return "C94/C95";
#elif __STDC_VERSION__ == 199901L
    return "C99";
#elif __STDC_VERSION__ == 201112L
    return "C11";
#elif __STDC_VERSION__ == 201710L
    return "C17";
#elif __STDC_VERSION__ >= 202000L
    return "C23";
#else
    return "Unknown";
#endif
}

const char* ver_get_optimize(void)
{
#if defined(__OPTIMIZE__)
    return "YES";
#else
    return "NO";
#endif
}

/* ==================== 一次性打印 ==================== */
void ver_print_all(void)
{
    printf("================ Build Information ================\n");
    printf(
        "  %-13s : %s@%s (%s)\n", "Git Infos", ver_get_git_branch(), ver_get_git_commit(),
        ver_get_git_status()
    );
    printf("  %-13s : %s\n", "Git Message", ver_get_git_msg());
    printf("  %-13s : %s\n", "Commit Time", ver_get_git_time());
    if (ver_svn_is_clean() >= 0)
    {
        printf("\n");
        printf("  %-13s : %s\n", "SVN", ver_get_svn_info());
    }
    printf("\n");
    printf("  %-13s : %s\n", "Build Time", ver_get_build_time());
    printf("  %-13s : %s\n", "Build Host", ver_get_build_host());
    printf("  %-13s : %s\n", "Runtime OS", ver_get_runtime_os());
    printf("\n");
    printf("  %-13s : %s\n", "Compiler", ver_get_compiler());
    printf("  %-13s : %s\n", "C Standard", ver_get_c_standard());
    printf("  %-13s : %s\n", "Optimize ", ver_get_optimize());
    printf("  %-13s : %s\n", "Debug Flag", ver_get_debug_flags());
    printf("  %-13s : %s\n", "Defines", ver_get_build_flags());

    printf("===================================================\n");
}
