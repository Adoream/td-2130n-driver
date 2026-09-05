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
	test "$$(wc -c < $(BUILD_DIR)/test-output/cups.bin)" -eq 9585
	test "$$(od -An -tx1 -j202 -N9 $(BUILD_DIR)/test-output/cups.bin | tr -d ' \n')" = "1b6961011b69557701"
	test "$$(od -An -tu1 -j213 -N1 $(BUILD_DIR)/test-output/cups.bin)" -eq 50
	./$(BUILD_DIR)/make_cups_raster physical | ./$(BUILD_DIR)/rastertobrothertd2130 2 test test 1 '' > $(BUILD_DIR)/test-output/cups-physical.bin
	test "$$(wc -c < $(BUILD_DIR)/test-output/cups-physical.bin)" -eq 9585
	sed -e 's/^\*DefaultRotate180: True$$/*DefaultRotate180: False/' \
	    -e 's/^\*DefaultPeeler: False$$/*DefaultPeeler: True/' \
	    -e 's/^\*DefaultPriority: Speed$$/*DefaultPriority: Quality/' \
	    -e 's/^\*DefaultCompress: False$$/*DefaultCompress: True/' \
	    cups/Brother-TD2130N.ppd > $(BUILD_DIR)/test-output/queue.ppd
	./$(BUILD_DIR)/make_cups_raster | PPD=$(BUILD_DIR)/test-output/queue.ppd \
	    ./$(BUILD_DIR)/rastertobrothertd2130 3 test test 1 '' > $(BUILD_DIR)/test-output/cups-ppd-defaults.bin
	test "$$(od -An -tu1 -j341 -N1 $(BUILD_DIR)/test-output/cups-ppd-defaults.bin)" -eq 206
	test "$$(od -An -tu1 -j354 -N1 $(BUILD_DIR)/test-output/cups-ppd-defaults.bin)" -eq 16
	test "$$(od -An -tu1 -j361 -N1 $(BUILD_DIR)/test-output/cups-ppd-defaults.bin)" -eq 2
	./$(BUILD_DIR)/make_cups_raster | PPD=$(BUILD_DIR)/test-output/queue.ppd \
	    ./$(BUILD_DIR)/rastertobrothertd2130 4 test test 1 \
	    'Rotate180=True Peeler=False Priority=Speed Compress=False' > $(BUILD_DIR)/test-output/cups-job-overrides.bin
	test "$$(od -An -tu1 -j341 -N1 $(BUILD_DIR)/test-output/cups-job-overrides.bin)" -eq 142
	test "$$(od -An -tu1 -j354 -N1 $(BUILD_DIR)/test-output/cups-job-overrides.bin)" -eq 8
	test "$$(od -An -tu1 -j361 -N1 $(BUILD_DIR)/test-output/cups-job-overrides.bin)" -eq 0
	cp cups/Brother-TD2130N.ppd $(BUILD_DIR)/test-output/custom.ppd
	mkdir -p $(BUILD_DIR)/test-output/media
	./$(BUILD_DIR)/td2130-paper -P Test -n Custom40x20 -w 40 -h 20 \
	    -g 3 -t 2 -b 2 -l 2 -r 2 -S 1 --media-root $(BUILD_DIR)/test-output/media \
	    --ppd $(BUILD_DIR)/test-output/custom.ppd
	media_id="$$(sed -n 's|^\(BrL[^/]\{12\}\)/Custom40x20$$|\1|p' $(BUILD_DIR)/test-output/media/custom_media.conf)"; \
	    test -n "$$media_id"; \
	    ./$(BUILD_DIR)/make_cups_raster custom | \
	    TD2130_MEDIA_ROOT=$(BUILD_DIR)/test-output/media \
	    ./$(BUILD_DIR)/rastertobrothertd2130 5 test test 1 "PageSize=$$media_id" \
	    > $(BUILD_DIR)/test-output/cups-custom.bin
	test "$$(wc -c < $(BUILD_DIR)/test-output/cups-custom.bin)" -eq 16719
	test "$$(od -An -tu1 -j213 -N1 $(BUILD_DIR)/test-output/cups-custom.bin)" -eq 40
	test "$$(od -An -tu1 -j343 -N1 $(BUILD_DIR)/test-output/cups-custom.bin)" -eq 40
	test "$$(od -An -tu1 -j344 -N1 $(BUILD_DIR)/test-output/cups-custom.bin)" -eq 20
	./$(BUILD_DIR)/make_cups_raster portrait40x60 | \
	    ./$(BUILD_DIR)/rastertobrothertd2130 6 test test 1 \
	    'PageSize=DieCut40x60 orientation-requested=5' > $(BUILD_DIR)/test-output/cups-landscape-60x40.bin
	test "$$(od -An -tu1 -j343 -N1 $(BUILD_DIR)/test-output/cups-landscape-60x40.bin)" -eq 60
	test "$$(od -An -tu1 -j344 -N1 $(BUILD_DIR)/test-output/cups-landscape-60x40.bin)" -eq 40

install-cups: all
	./packaging/install-cups.sh

clean:
	rm -rf $(BUILD_DIR)
