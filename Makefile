# ============================================================
# Makefile — HT-SUA33GM-T1V-C Calibration Suite
#
# Requirements:
#   - HuaTeng Vision Linux SDK installed system-wide
#     Headers : /usr/include/CameraApi.h
#     Library : /usr/lib/libMVSDK.so
#
# Build all:   make
# Clean:       make clean
# ============================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lMVSDK -lm -lpthread

SRC_DIR  := src
INC_DIR  := include

# Shared sources compiled into every executable
COMMON_SRCS := $(SRC_DIR)/camera_utils.c \
               $(SRC_DIR)/file_utils.c

TARGETS := ptc_verify ptc_acquire qe_acquire

.PHONY: all clean

all: $(TARGETS)

ptc_verify: $(SRC_DIR)/ptc_verify.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $@"

ptc_acquire: $(SRC_DIR)/ptc_acquire.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $@"

qe_acquire: $(SRC_DIR)/qe_acquire.c $(COMMON_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $@"

clean:
	rm -f $(TARGETS)
	@echo "Cleaned."
