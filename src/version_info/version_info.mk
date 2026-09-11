# ==================== 版本信息收集 ====================

GIT_STATUS := $(shell \
    if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
        if [ -z "$$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then \
            echo clean; \
        else echo dirty; fi; \
    else echo unknown; fi)

GIT_INFO := $(shell \
    if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
        branch=$$(git rev-parse --abbrev-ref HEAD 2>/dev/null); \
        commit=$$(git rev-parse --short HEAD 2>/dev/null); \
        echo "$$branch, $$commit, $(GIT_STATUS)"; \
    else echo unknown; fi)

# ---- SVN：先算状态，再拼信息 ----
SVN_STATUS := $(shell \
    if command -v svn >/dev/null 2>&1 && svn info >/dev/null 2>&1; then \
        if [ -z "$$(svn status -q 2>/dev/null)" ]; then echo clean; else echo dirty; fi; \
    else echo unknown; fi)

SVN_INFO := $(shell \
    if command -v svn >/dev/null 2>&1 && svn info >/dev/null 2>&1; then \
        svn info --show-item revision 2>/dev/null || echo unknown; \
    else echo unknown; fi)

BUILD_TIME ?= $(shell date '+%Y-%m-%d %H:%M:%S')
BUILD_HOST ?= $(shell \
    if [ -n "$$MSYSTEM" ]; then \
        echo "$$MSYSTEM $$(uname -m)"; \
    else \
        echo "$$(uname -s | sed 's/-[0-9.].*//') $$(uname -m)"; \
    fi)

# ★ 延迟展开：这样 $(CFLAGS) 在真正使用时才求值
DEFS = \
    -DVER_GIT_INFO='"$(GIT_INFO)"' \
    -DVER_GIT_STATUS='"$(GIT_STATUS)"' \
    -DVER_SVN_INFO='"$(SVN_INFO)"' \
    -DVER_SVN_STATUS='"$(SVN_STATUS)"' \
    -DVER_BUILD_TIME='"$(BUILD_TIME)"' \
    -DVER_BUILD_HOST='"$(BUILD_HOST)"' \
    -DVER_BUILD_FLAGS='"$(DFFLAGS)"'

# ★ 兼容别名
VI_DEFS := $(DEFS)
