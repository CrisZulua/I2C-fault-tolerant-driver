# Compiler
CC = arm-none-eabi-gcc

# Architecture for the STM32F446RE (Cortex-M4 with hardware floating point)
MCU = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

# Required macro to enable STM32F446 in the ST headers
DEFS = -DSTM32F446xx

# Strict warning flags
CFLAGS = $(MCU) $(DEFS) -Wall -Wextra -Werror -O0

# Include paths
CFLAGS += -I.
CFLAGS += -Isrc
CFLAGS += -ICMSIS/Include
CFLAGS += -ICMSIS/Device/ST/STM32F4xx/Include

# Directories
SRC_DIR = src
BUILD_DIR = build
OBJS_DIR = $(BUILD_DIR)/objs

# Source files
SRC = i2c_driver.c i2c_isr.c 
OBJS = $(SRC:%.c=$(OBJS_DIR)/%.o)

# Default target: compile to .o files without linking
all: $(OBJS)

$(OBJS_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJS_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
