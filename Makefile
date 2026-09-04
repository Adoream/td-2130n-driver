CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Iinclude $(shell pkg-config --cflags libusb-1.0)
LDLIBS += $(shell pkg-config --libs libusb-1.0)

SRC = src/main.c src/td2130.c src/pbm.c src/usb_transport.c
BUILD_DIR = build
OBJ = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
CUPS_CFLAGS = $(shell cups-config --cflags 2>/dev/null)
CUPS_LIBS = $(shell cups-config --image --libs 2>/dev/null)

.PHONY: all clean test install-cups td2130 rastertobrothertd2130 td2130-paper td2130-config

all: $(BUILD_DIR)/td2130 $(BUILD_DIR)/rastertobrothertd2130 $(BUILD_DIR)/td2130-paper $(BUILD_DIR)/td2130-config

td2130: $(BUILD_DIR)/td2130
rastertobrothertd2130: $(BUILD_DIR)/rastertobrothertd2130
td2130-paper: $(BUILD_DIR)/td2130-paper
td2130-config: $(BUILD_DIR)/td2130-config

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/td2130: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR)/rastertobrothertd2130: src/cups_filter.c src/td2130.c include/td2130.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CUPS_CFLAGS) -o $@ src/cups_filter.c src/td2130.c $(CUPS_LIBS)

$(BUILD_DIR)/td2130-paper: src/paper_tool.c src/td2130.c include/td2130.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ src/paper_tool.c src/td2130.c

$(BUILD_DIR)/td2130-config: src/config_tool.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ src/config_tool.c

$(BUILD_DIR)/test_protocol: tests/test_protocol.c src/td2130.c include/td2130.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_protocol.c src/td2130.c

$(BUILD_DIR)/make_cups_raster: tests/make_cups_raster.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CUPS_CFLAGS) -o $@ tests/make_cups_raster.c $(CUPS_LIBS)

test: $(BUILD_DIR)/test_protocol $(BUILD_DIR)/rastertobrothertd2130 $(BUILD_DIR)/make_cups_raster $(BUILD_DIR)/td2130-paper $(BUILD_DIR)/td2130-config
	./$(BUILD_DIR)/test_protocol
	sh tests/test_config_tool.sh
	mkdir -p $(BUILD_DIR)/test-output
	./$(BUILD_DIR)/make_cups_raster | ./$(BUILD_DIR)/rastertobrothertd2130 1 test test 1 '' > $(BUILD_DIR)/test-output/cups.bin
	test "$$(wc -c < $(BUILD_DIR)/test-output/cups.bin)" -eq 9453
	./$(BUILD_DIR)/make_cups_raster physical | ./$(BUILD_DIR)/rastertobrothertd2130 2 test test 1 '' > $(BUILD_DIR)/test-output/cups-physical.bin
	test "$$(wc -c < $(BUILD_DIR)/test-output/cups-physical.bin)" -eq 9453

install-cups: all
	./packaging/install-cups.sh

clean:
	rm -rf $(BUILD_DIR)
