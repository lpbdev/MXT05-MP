#include "version_info.h"
#include <stdio.h>

int main(void)
{
    ver_print_all();

    printf("\nSingle Function Test:\n");
    printf("Git:   %s\n", ver_get_git_status());
    printf("SVN:   %s\n", ver_get_svn_info());
    printf("Time:  %s\n", ver_get_build_time());

    return 0;
}
