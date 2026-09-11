# makefile for rtk
CC = gcc
SRC_DIR     = ../src
INC_DIR = ../src/
BUILD_DIR = ./

ifeq ($(OS), Windows_NT)
	DFFLAGS    = -DENACMP -DWIN32 -DLAPACK
	LDFLAGS = -lm  -lwinmm -lpthread -lws2_32 -lopenblas
else
	LDFLAGS = -lm -lpthread
	DFFLAGS    = -DENACMP -DLAPACK
endif


# for gprof
CFLAGS  =  -I$(SRC)  $(DFFLAGS)  -std=c11 -pedantic 

SRCS =  \
	$(SRC_DIR)/main_rtcm.c $(SRC_DIR)/rtkcmn.c $(SRC_DIR)/rinex.c  $(SRC_DIR)/solution.c \
	$(SRC_DIR)/lambda.c $(SRC_DIR)/myfun.c  $(SRC_DIR)/pntpos.c $(SRC_DIR)/ephemeris.c \
	$(SRC_DIR)/rtcm3.c  $(SRC_DIR)/rtkls.c  $(SRC_DIR)/rtkpos.c $(SRC_DIR)/tides.c $(SRC_DIR)/stream.c \
	$(SRC_DIR)/multipath.c $(SRC_DIR)/cJSON.c $(SRC_DIR)/BandPassFilter.c \
	$(SRC_DIR)/fft.c $(SRC_DIR)/logmod.c $(SRC_DIR)/pos_filter.c \
	$(SRC_DIR)/version_info/version_info.c

# 头文件
VERSION_DIR = $(SRC_DIR)/version_info

INCS = -I$(INC_DIR) -I$(VERSION_DIR)
DEPS_INC = $(INC_DIR)/rtk.h $(INC_DIR)/cJSON.h  $(INC_DIR)/fft.h \
		   $(INC_DIR)/BandPassFilter.h $(INC_DIR)/logmod.h $(INC_DIR)/pos_filter.h \
		   $(INC_DIR)/version_info/version_info.h

# 目标文件
TARGET = main_rtcm

# Debug版本
#
CFLAGS_DEBUG   = $(CFLAGS) -g -O0 -DDEBUG -Wall
BUILD_DIR_DEBUG = $(BUILD_DIR)/debug
OBJS_DEBUG = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR_DEBUG)/%.o)
DEPS_DEBUG = $(OBJS_DEBUG:.o=.d)
TARGET_DEBUG = $(BUILD_DIR_DEBUG)/$(TARGET)_debug

# Release版本
CFLAGS_RELEASE = $(CFLAGS) -O2 -DNDEBUG
BUILD_DIR_RELEASE = $(BUILD_DIR)/release
OBJS_RELEASE = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR_RELEASE)/%.o)
DEPS_RELEASE = $(OBJS_RELEASE:.o=.d)
TARGET_RELEASE = $(BUILD_DIR_RELEASE)/$(TARGET)

# ============================================================================
# 默认目标
# ============================================================================

.PHONY: all
all: debug  #release

.PHONY: debug
debug: $(TARGET_DEBUG)

.PHONY: release
release: $(TARGET_RELEASE)


include $(SRC_DIR)/version_info/version_info.mk

# ============================================================================
# 编译规则
# ============================================================================

# Debug版本编译规则
$(BUILD_DIR_DEBUG)/%.o: $(SRC_DIR)/%.c $(DEPS_INC) 
	@mkdir -p $(dir $@)
	$(CC)  $(CFLAGS_DEBUG)  $(DEFS) $(INCS) -c $< -o $@


$(TARGET_DEBUG): $(OBJS_DEBUG)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS_DEBUG) -o $@ $(LDFLAGS)
	@printf ">>>>>>\nDebug Exe has done: $@\n"

# Release版本编译规则
$(BUILD_DIR_RELEASE)/%.o: $(SRC_DIR)/%.c $(DEPS_INC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RELEASE) $(DFFLAGS) $(INCS) -c $< -o $@

$(BUILD_DIR_RELEASE)/version_info.o: $(VERSION_DIR)/version_info.c $(DEPS_INC)
	@mkdir -p $(dir $@)
	$(CC) $(DEFS) $(CFLAGS_BASE) -O2 -DNDEBUG $(INCS) \
	      -DVER_DEFS_CUR=\"$(DEFS)\" \
	      -DVER_CFLAGS_CUR=\"$(CFLAGS_BASE) -O2 -DNDEBUG\" \
	      -DVER_LDFLAGS_CUR=\"$(LDFLAGS)\" \
	      -DVER_INCS_CUR=\"$(INCS)\" \
	      -c $< -o $@

$(TARGET_RELEASE): $(OBJS_RELEASE)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS_RELEASE) -o $@ $(LDFLAGS)
	@printf "Release Exe has done: $@\n"

# ============================================================================
# 代码格式化 (clang-format)
# ============================================================================

.PHONY: format
format:
	@printf ">>>>>>\nFormating source files ...\n"
	@clang-format -i $(SRCS) $(DEPS_INC)
	@printf "Format work done\n"

.PHONY: format-check
format-check:
	@printf "检查代码格式..."
	@clang-format --dry-run --Werror $(SRCS) $(DEPS_INC) || \
		(printf "Error Code format not OK，run 'make format'" && exit 1)
	@printf "Format check pass"

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/debug $(BUILD_DIR)/release
 
