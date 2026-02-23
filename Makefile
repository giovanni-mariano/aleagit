# SPDX-FileCopyrightText: 2026 Giovanni MARIANO
#
# SPDX-License-Identifier: MPL-2.0

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude

# Release build (set RELEASE=1)
ifdef RELEASE
    ifdef PORTABLE
        CFLAGS += -O3 -DNDEBUG
    else
        CFLAGS += -O3 -march=native -DNDEBUG
    endif
else
    CFLAGS += -g
endif

# Dependency: libalea (git submodule in vendor/libalea)
CSG_DIR = vendor/libalea
CSG_INCLUDES = -I$(CSG_DIR)/include
CSG_LIB_DIR = $(CSG_DIR)/bin

# Pass RELEASE/PORTABLE through to libalea submodule build
CSG_MAKE_FLAGS =
ifdef RELEASE
    CSG_MAKE_FLAGS += RELEASE=1
endif
ifdef PORTABLE
    CSG_MAKE_FLAGS += PORTABLE=1
endif

# Link against the full (combined) library with whole-archive so that
# format module symbols override the weak stubs in core.
UNAME_S := $(shell uname -s)
CSG_FULL_LIB = $(CSG_LIB_DIR)/libalea_full.a
ifeq ($(UNAME_S),Darwin)
  CSG_LIBS = -Wl,-force_load,$(CSG_FULL_LIB) -lm
else
  CSG_LIBS = -Wl,--whole-archive $(CSG_FULL_LIB) -Wl,--no-whole-archive -lm
endif

GIT2_CFLAGS = $(shell pkg-config --cflags libgit2)
GIT2_LIBS   = $(shell pkg-config --libs libgit2)

ALL_CFLAGS = $(CFLAGS) $(CSG_INCLUDES) $(GIT2_CFLAGS)

SRCS = src/main.c \
       src/cmd_init.c \
       src/cmd_summary.c \
       src/cmd_status.c \
       src/cmd_diff.c \
       src/cmd_diff_visual.c \
       src/cmd_log.c \
       src/cmd_blame.c \
       src/cmd_validate.c \
       src/cmd_add.c \
       src/cmd_commit.c \
       src/git_helpers.c \
       src/geom_load.c \
       src/geom_fingerprint.c \
       src/geom_diff.c \
       src/visual_diff.c \
       src/bmp_writer.c \
       src/util.c

OBJS = $(SRCS:.c=.o)
TARGET = aleagit

.PHONY: all clean csg submodule-update

all: $(TARGET)

csg:
	$(MAKE) -C $(CSG_DIR) lib $(CSG_MAKE_FLAGS)

$(TARGET): csg $(OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(CSG_LIBS) $(GIT2_LIBS)

src/%.o: src/%.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

submodule-update:
	git submodule update --remote vendor/libalea

clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C $(CSG_DIR) clean || true
