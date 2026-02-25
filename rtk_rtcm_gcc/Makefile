# makefile for rtk
CC = gcc
SRC_DIR     = ../src
INC_DIR = ../src/
BUILD_DIR = ./

ifeq ($(OS), Windows_NT)
	OPTS    = -DENACMP -DWIN32
	LDFLAGS = -lm  -lwinmm -lpthread -lws2_32
else
	LDFLAGS = -lm -lpthread
	OPTS    = -DENACMP
endif


# for gprof
#CFLAGS  = -Wall -O3 -std=c11 -pedantic -I$(SRC) -DLAPACK $(OPTS) -pg
#LDLIBS  = -lm -lrt -llapack -lblas -pg

# for mkl
#MKLDIR  = /opt/intel
#CFLAGS  = -O3 -std=c11 -pedantic -I$(SRC) $(OPTS) -DMKL -I$(MKLDIR)/include
#LDLIBS  = -L$(MKLDIR)/lib/32 -lm -lmkl_core -lmkl_intel -lmkl_gnu_thread -liomp5 -lpthread

SRCS =  \
	$(SRC_DIR)/main_rtcm.c $(SRC_DIR)/rtkcmn.c $(SRC_DIR)/rinex.c  $(SRC_DIR)/solution.c \
	$(SRC_DIR)/lambda.c $(SRC_DIR)/myfun.c  $(SRC_DIR)/pntpos.c $(SRC_DIR)/ephemeris.c \
	$(SRC_DIR)/rtcm3.c  $(SRC_DIR)/rtkls.c  $(SRC_DIR)/rtkpos.c $(SRC_DIR)/tides.c $(SRC_DIR)/stream.c \
	$(SRC_DIR)/multipath.c $(SRC_DIR)/cJSON.c $(SRC_DIR)/BandPassFilter.c \
	$(SRC_DIR)/fft.c $(SRC_DIR)/logmod.c $(SRC_DIR)/pos_filter.c

# $(info SRCS = $(SRCS))

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:.o=.d)

# 头文件
INCS = -I$(INC_DIR)
DEPS_INC = $(INC_DIR)/rtk.h $(INC_DIR)/cJSON.h  $(INC_DIR)/fft.h \
		   $(INC_DIR)/BandPassFilter.h $(INC_DIR)/logmod.h $(INC_DIR)/pos_filter.h

# 目标文件
TARGET = main_rtcm

# ============================================================================
# 版本特定配置
# ============================================================================

# Debug版本
CFLAGS_DEBUG = -Wall -O0 -g -std=c11 -pedantic -I$(SRC_DIR) -DLAPACK $(OPTS)
BUILD_DIR_DEBUG = $(BUILD_DIR)/debug
OBJS_DEBUG = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR_DEBUG)/%.o)
DEPS_DEBUG = $(OBJS_DEBUG:.o=.d)
TARGET_DEBUG = $(BUILD_DIR_DEBUG)/$(TARGET)_debug

# Release版本
CFLAGS_RELEASE = -Wall -O2 -std=c11 -pedantic -I$(SRC_DIR) -DLAPACK $(OPTS)
BUILD_DIR_RELEASE = $(BUILD_DIR)/release
OBJS_RELEASE = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR_RELEASE)/%.o)
DEPS_RELEASE = $(OBJS_RELEASE:.o=.d)
TARGET_RELEASE = $(BUILD_DIR_RELEASE)/$(TARGET)

# ============================================================================
# 默认目标
# ============================================================================

.PHONY: all
all: debug release

.PHONY: debug
debug: $(TARGET_DEBUG)

.PHONY: release
release: $(TARGET_RELEASE)

# ============================================================================
# 编译规则
# ============================================================================

# Debug版本编译规则
$(BUILD_DIR_DEBUG)/%.o: $(SRC_DIR)/%.c $(DEPS_INC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_DEBUG) $(INCS) -c $< -o $@

$(TARGET_DEBUG): $(OBJS_DEBUG)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS_DEBUG) -o $@ $(LDFLAGS)
	@printf ">>>>>>\nDebug Exe has done: $@\n"

# Release版本编译规则
$(BUILD_DIR_RELEASE)/%.o: $(SRC_DIR)/%.c $(DEPS_INC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(CFLAGS_RELEASE) $(INCS) -c $< -o $@

$(TARGET_RELEASE): $(OBJS_RELEASE)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS_RELEASE) -o $@ $(LDFLAGS)
	@echo "Release Exe has done: $@"

# ============================================================================
# 代码格式化 (clang-format)
# ============================================================================

.PHONY: format
format:
	@echo "格式化代码..."
	@clang-format -i $(SRCS) $(DEPS_INC)
	@echo "代码格式化完成"

.PHONY: format-check
format-check:
	@echo "检查代码格式..."
	@clang-format --dry-run --Werror $(SRCS) $(DEPS_INC) || \
		(echo "Error 代码格式不符合要求，请运行 'make format'" && exit 1)
	@echo "Format check pass"

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/debug $(BUILD_DIR)/release

