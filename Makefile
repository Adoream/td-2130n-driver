CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Iinclude $(shell pkg-config --cflags libusb-1.0)
LDLIBS += $(shell pkg-config --libs libusb-1.0)

SRC = src/main.c src/td2130.c src/pbm.c src/usb_transport.c
OBJ = $(SRC:.c=.o)
CUPS_CFLAGS = $(shell cups-config --cflags 2>/dev/null)
CUPS_LIBS = $(shell cups-config --image --libs 2>/dev/null)

.PHONY: all clean test install-cups

all: td2130 rastertobrothertd2130

td2130: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

rastertobrothertd2130: src/cups_filter.c src/td2130.c include/td2130.h
	$(CC) $(CFLAGS) $(CUPS_CFLAGS) -o $@ src/cups_filter.c src/td2130.c $(CUPS_LIBS)

tests/test_protocol: tests/test_protocol.c src/td2130.c include/td2130.h
	$(CC) $(CFLAGS) -o $@ tests/test_protocol.c src/td2130.c

tests/make_cups_raster: tests/make_cups_raster.c
	$(CC) $(CFLAGS) $(CUPS_CFLAGS) -o $@ tests/make_cups_raster.c $(CUPS_LIBS)

test: tests/test_protocol rastertobrothertd2130 tests/make_cups_raster
	./tests/test_protocol
	./tests/make_cups_raster | ./rastertobrothertd2130 1 test test 1 '' > /tmp/td2130-cups-test.bin
	test "$$(wc -c < /tmp/td2130-cups-test.bin)" -eq 9453
	./tests/make_cups_raster physical | ./rastertobrothertd2130 2 test test 1 '' > /tmp/td2130-cups-physical-test.bin
	test "$$(wc -c < /tmp/td2130-cups-physical-test.bin)" -eq 9453

install-cups: all
	./packaging/install-cups.sh

clean:
	rm -f $(OBJ) td2130 rastertobrothertd2130 tests/test_protocol tests/make_cups_raster
