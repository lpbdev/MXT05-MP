#ifndef VERSION_INFO_H
#define VERSION_INFO_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* ---- 版本控制信息 ---- */
    const char* ver_get_git_info(void);
    const char* ver_get_git_status(void);
    int         ver_git_is_clean(void);

    const char* ver_get_svn_info(void);
    const char* ver_get_svn_status(void);
    int         ver_svn_is_clean(void);

    /* ---- 编译信息 ---- */
    const char* ver_get_build_time(void);
    const char* ver_get_build_host(void);
    const char* ver_get_runtime_os(void);
    const char* ver_get_compiler(void);
    const char* ver_get_build_flags(void);

    /* ---- 汇总 ---- */
    void ver_print_all(void);

#ifdef __cplusplus
}
#endif

#endif /* MYPROJECT_VERSION_H */
