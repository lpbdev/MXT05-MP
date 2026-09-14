# ==================== 版本信息收集 ====================

# ---- 通用转义函数（供所有 git 字符串用） ----
# 把 \ 转成 \\，把 " 转成 \"
git_escape = $(shell printf '%s' '$(1)' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')

# ---- 是否在 git 仓库内 ----
GIT_IS_REPO := $(shell \
    if command -v git >/dev/null 2>&1 && \
       git rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
        echo yes; \
    else \
        echo no; \
    fi)

ifeq ($(GIT_IS_REPO),yes)

# 分支名（detached HEAD 时显示 commit）
GIT_BRANCH := $(shell \
    b=$$(git rev-parse --abbrev-ref HEAD 2>/dev/null); \
    if [ "$$b" = "HEAD" ]; then \
        b="(detached)"; \
    fi; \
    printf '%s' "$$b" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')

# 短 hash
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "(no commit)")

# 状态：纯 clean / dirty
GIT_STATUS := $(shell \
    if [ -z "$$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then \
        echo clean; \
    else \
        echo dirty; \
    fi)

# commit 标题
GIT_MSG := $(shell \
    m=$$(git log -1 --pretty=%s 2>/dev/null); \
    if [ -z "$$m" ]; then m="(no commit)"; fi; \
    printf '%s' "$$m" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')

# commit 时间（committer date，带时区）
GIT_TIME := $(shell \
    t=$$(git log -1 --date=format:'%Y-%m-%d %H:%M:%S %z' --pretty=%cd 2>/dev/null); \
    if [ -z "$$t" ]; then t="(no commit)"; fi; \
    echo "$$t")

else

GIT_BRANCH := unknown
GIT_COMMIT := unknown
GIT_STATUS := unknown
GIT_MSG    := unknown
GIT_TIME   := unknown

endif

# ---- SVN：先算状态，再拼信息 ----
SVN_STATUS := $(shell \
    if command -v svn >/dev/null 2>&1 && svn info >/dev/null 2>&1; then \
        if [ -z "$$(svn status -q 2>/dev/null)" ]; then echo clean; else echo dirty; fi; \
    else echo unknown; fi)

SVN_INFO := $(shell \
    if command -v svn >/dev/null 2>&1 && svn info >/dev/null 2>&1; then \
        svn info --show-item revision 2>/dev/null || echo unknown; \
    else echo unknown; fi)

BUILD_TIME ?= $(shell date '+%Y-%m-%d %H:%M:%S %z')
BUILD_HOST ?= $(shell \
    if [ -n "$$MSYSTEM" ]; then \
        echo "$$MSYSTEM $$(uname -m)"; \
    else \
        echo "$$(uname -s | sed 's/-[0-9.].*//') $$(uname -m)"; \
    fi)

# ★ 延迟展开：这样 $(CFLAGS) 在真正使用时才求值
DEFS = \
	-DVER_GIT_BRANCH='"$(GIT_BRANCH)"' \
    -DVER_GIT_COMMIT='"$(GIT_COMMIT)"' \
    -DVER_GIT_STATUS='"$(GIT_STATUS)"' \
    -DVER_GIT_MSG='"$(GIT_MSG)"' \
    -DVER_GIT_TIME='"$(GIT_TIME)"' \
    -DVER_SVN_INFO='"$(SVN_INFO)"' \
    -DVER_SVN_STATUS='"$(SVN_STATUS)"' \
    -DVER_BUILD_TIME='"$(BUILD_TIME)"' \
    -DVER_BUILD_HOST='"$(BUILD_HOST)"' \
    -DVER_BUILD_FLAGS='"$(DFFLAGS)"'

# ★ 兼容别名
VI_DEFS := $(DEFS)
