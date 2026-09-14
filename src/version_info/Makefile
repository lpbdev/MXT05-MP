CC       ?= gcc
AR       ?= ar
CFLAGS   ?= -O2 -Wall -Wextra -std=c11 
DFFLAGS  ?= -DNDEBUG
LDFLAGS  ?=

# ---- 平台判断 ----
ifeq ($(OS),Windows_NT)
    EXE_EXT  := .exe
    LIB_SO   := libversion_info.dll
    SO_FLAG  := -shared
else
    UNAME_S  := $(shell uname -s)
    EXE_EXT  :=
    ifeq ($(UNAME_S),Darwin)
        LIB_SO  := libversion_info.dylib
        SO_FLAG := -dynamiclib
    else
        LIB_SO  := libversion_info.so
        SO_FLAG := -shared
    endif
endif

TARGET_EXE := version_info_test$(EXE_EXT)
OBJS       := version_info.o

include ./version_info.mk
# ==================== 目标 ====================

.PHONY: all lib so clean

# 默认生成可执行文件
all: $(TARGET_EXE)

# 可执行文件（直接链接 version_info.o）
$(TARGET_EXE): version_info_test.c version_info.o version_info.h
	$(CC) $(CFLAGS) $(DFFLAGS) -I. version_info_test.c version_info.o $(LDFLAGS) -o $@

# 编译 version_info.o（含版本信息宏）
version_info.o: version_info.c version_info.h
	$(CC) $(CFLAGS) $(DFFLAGS) $(DEFS) -c $< -o $@

# ---- 可选目标：静态库 ----
lib: libversion_info.a

libversion_info.a: $(OBJS)
	$(AR) rcs $@ $^

# ---- 可选目标：动态库 ----
so: $(LIB_SO)

$(LIB_SO): $(OBJS)
	$(CC) $(SO_FLAG) -o $@ $^

clean:
	rm -f $(OBJS) libversion_info.a $(LIB_SO) $(TARGET_EXE)
