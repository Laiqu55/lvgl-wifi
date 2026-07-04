# =============================================================================
# Makefile — lvgl-wifi application
#
# Targets the ATK-DLT113IS (Allwinner T113-i, ARM Cortex-A7).
# Buildroot 2019 / Linux kernel 5.4.61
#
# Prerequisites (submodules or system paths):
#   lvgl/       – LVGL v8.3 source tree  (git submodule)
#   lv_drivers/ – lv_drivers source tree (git submodule)
#
# Quick start:
#   git submodule update --init --recursive
#   make CROSS_COMPILE=arm-linux-gnueabihf-
#   scp build/wifi_app root@<board-ip>:/usr/bin/
#
# Variables you can override on the command line:
#   CROSS_COMPILE – toolchain prefix          (default: arm-linux-gnueabihf-)
#   SYSROOT       – cross-compilation sysroot (default: empty)
#   DISP_HOR_RES  – display width in pixels   (default: 800)
#   DISP_VER_RES  – display height in pixels  (default: 480)
#   WIFI_IFACE    – WiFi interface name       (default: wlan0)
#   EVDEV_DEV     – evdev device node         (default: /dev/input/event0)
# =============================================================================

# --------------------------------------------------------------------------- #
# Toolchain                                                                    #
# --------------------------------------------------------------------------- #

CROSS_COMPILE ?= arm-linux-gnueabihf-

CC  = $(CROSS_COMPILE)gcc
AR  = $(CROSS_COMPILE)ar
LD  = $(CROSS_COMPILE)gcc

# --------------------------------------------------------------------------- #
# Directories                                                                  #
# --------------------------------------------------------------------------- #

LVGL_DIR      := lvgl
LV_DRV_DIR    := lv_drivers
SRC_DIR       := src
BUILD_DIR     := build

TARGET        := $(BUILD_DIR)/wifi_app

# --------------------------------------------------------------------------- #
# Display / platform settings                                                  #
# --------------------------------------------------------------------------- #

DISP_HOR_RES  ?= 800
DISP_VER_RES  ?= 480
WIFI_IFACE    ?= wlan0
EVDEV_DEV     ?= /dev/input/event0

# --------------------------------------------------------------------------- #
# Compiler flags                                                                #
# --------------------------------------------------------------------------- #

CFLAGS  = -Wall -Wextra -Wno-unused-parameter -O2 -g
CFLAGS += -std=c99 -D_GNU_SOURCE
CFLAGS += -ffunction-sections -fdata-sections

# Include paths
CFLAGS += -I.                   # lv_conf.h lives here
CFLAGS += -I$(LVGL_DIR)         # lvgl/lvgl.h
CFLAGS += -I$(LV_DRV_DIR)       # lv_drivers/display/fbdev.h etc.

# Application-level defines
CFLAGS += -DDISP_HOR_RES=$(DISP_HOR_RES)
CFLAGS += -DDISP_VER_RES=$(DISP_VER_RES)
CFLAGS += -DWIFI_IFACE=\"$(WIFI_IFACE)\"
CFLAGS += -DEVDEV_INPUT_DEV=\"$(EVDEV_DEV)\"
CFLAGS += -DLV_CONF_INCLUDE_SIMPLE

ifdef SYSROOT
    CFLAGS  += --sysroot=$(SYSROOT)
endif

# --------------------------------------------------------------------------- #
# Linker flags                                                                  #
# --------------------------------------------------------------------------- #

LDFLAGS  = -lm -lpthread
LDFLAGS += -Wl,--gc-sections

ifdef SYSROOT
    LDFLAGS += --sysroot=$(SYSROOT)
endif

# --------------------------------------------------------------------------- #
# Source files                                                                  #
# --------------------------------------------------------------------------- #

# Application sources
APP_SRCS := \
    $(SRC_DIR)/main.c \
    $(SRC_DIR)/wifi_manager.c \
    $(SRC_DIR)/wifi_ui.c

# LVGL core sources  (relies on lvgl/lvgl.mk)
include $(LVGL_DIR)/lvgl.mk

# lv_drivers – only the modules we actually use
LV_DRV_SRCS := \
    $(LV_DRV_DIR)/display/fbdev.c \
    $(LV_DRV_DIR)/indev/evdev.c

# All sources combined
ALL_SRCS := $(APP_SRCS) $(CSRCS) $(LV_DRV_SRCS)

# Object files – mirror the source tree under $(BUILD_DIR)
ALL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_SRCS))

# --------------------------------------------------------------------------- #
# Rules                                                                         #
# --------------------------------------------------------------------------- #

.PHONY: all clean run install submodules help

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	@echo "  LD    $@"
	@$(LD) $^ $(LDFLAGS) -o $@
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# --------------------------------------------------------------------------- #
# Convenience targets                                                           #
# --------------------------------------------------------------------------- #

## Init git submodules (lvgl + lv_drivers)
submodules:
	git submodule update --init --recursive

## Remove all build artefacts
clean:
	rm -rf $(BUILD_DIR)

## Run locally (requires a framebuffer and evdev device)
run: $(TARGET)
	@echo "Starting WiFi app on $(EVDEV_DEV) @ $(DISP_HOR_RES)x$(DISP_VER_RES)"
	@$(TARGET)

## Copy the binary to a target board
install: $(TARGET)
	@if [ -z "$(BOARD_IP)" ]; then echo "Set BOARD_IP=<address>"; exit 1; fi
	scp $(TARGET) root@$(BOARD_IP):/usr/bin/wifi_app

## Print usage information
help:
	@echo ""
	@echo "Usage:"
	@echo "  make [CROSS_COMPILE=arm-linux-gnueabihf-] [SYSROOT=/path/to/sysroot]"
	@echo ""
	@echo "Targets:"
	@echo "  all         Build the wifi_app binary (default)"
	@echo "  clean       Remove build artefacts"
	@echo "  submodules  Initialise LVGL and lv_drivers submodules"
	@echo "  install     Copy binary to BOARD_IP via scp"
	@echo "  help        Show this message"
	@echo ""
	@echo "Configurable variables:"
	@echo "  CROSS_COMPILE  Toolchain prefix      [$(CROSS_COMPILE)]"
	@echo "  SYSROOT        Cross-compilation sysroot []"
	@echo "  DISP_HOR_RES   Display width (px)    [$(DISP_HOR_RES)]"
	@echo "  DISP_VER_RES   Display height (px)   [$(DISP_VER_RES)]"
	@echo "  WIFI_IFACE     WiFi interface name   [$(WIFI_IFACE)]"
	@echo "  EVDEV_DEV      evdev device node     [$(EVDEV_DEV)]"
	@echo ""
