ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

ifeq ($(strip $(MICROKIT_CONFIG)),)
$(error MICROKIT_CONFIG must be specified)
endif

GUEST_FILE ?= ./../example-kernels/block_test.hvt

# Make absolute
override MICROKIT_SDK := $(realpath $(MICROKIT_SDK))
override GUEST_FILE := $(realpath $(GUEST_FILE))

# Since the clean deletes the folder, best disallow user from changing build dir
override BUILD_DIR := $(abspath ./build)

# Core paths
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := $(shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4)
DEPS := $(realpath ./../deps)
INC_DIR := $(realpath ./)
SRC_DIR := $(realpath ./)

# Core build files
SYSTEM := full.system
IMAGE_FILE := loader.img
REPORT_FILE := report.txt
BUILD_MAKE := full.mk

# Export new variables for sub-makes
export MICROKIT_BOARD
export MICROKIT_CONFIG
export BOARD_DIR
export ARCH
export IMAGE_FILE
export REPORT_FILE
export INC_DIR
export SRC_DIR
export DEPS
export SYSTEM
export GUEST_FILE


### Build rules ###
all: clean build 

build: $(BUILD_DIR)/$(IMAGE_FILE) $(BUILD_DIR)/$(REPORT_FILE) 

clean:
	rm -rf $(BUILD_DIR)

# Copy minimal makefile into build dir and run
$(BUILD_DIR)/$(IMAGE_FILE) $(BUILD_DIR)/$(REPORT_FILE): 
	mkdir -p $(BUILD_DIR)
	cp $(BUILD_MAKE) $(BUILD_DIR)/Makefile
	$(MAKE) -C $(BUILD_DIR) MICROKIT_SDK=$(MICROKIT_SDK) BUILD_DIR=$(BUILD_DIR) GUEST_FILE=$(GUEST_FILE) $(notdir $@)

# Only allow qemu when built for qemu_virt_aarch64
ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
qemu_disk:
	$(DEPS)/sddf/tools/mkvirtdisk disk 1 512 16777216 GPT

qemu: all qemu_disk
	qemu-system-aarch64 -machine virt,virtualization=on,highmem=off,secure=off \
				-cpu cortex-a53 \
				-device loader,file=$(BUILD_DIR)/$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
				-m size=2G \
				-d guest_errors \
				-serial mon:stdio \
				-global virtio-mmio.force-legacy=false \
				-drive file=disk,if=none,format=raw,id=hd \
            	-device virtio-blk-device,drive=hd \
				-nographic
else
qemu:
	echo "QEMU not supported for $(MICROKIT_BOARD) (skipping build and simulation)"
endif