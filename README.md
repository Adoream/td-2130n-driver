# Brother TD-2130N Driver

An unofficial, pure-C driver for the Brother TD-2130N direct thermal label
printer. It provides a CUPS Raster filter for Linux and macOS plus a direct-USB
diagnostic and PBM printing utility, without Brother's obsolete binary driver.

The implementation follows Brother's published Raster Command Reference and
has been tested with a USB-connected TD-2130N using 50 x 15 mm die-cut labels.
This project is not affiliated with or endorsed by Brother Industries, Ltd.

## Features

- 300 x 300 dpi, 672-dot print head
- USB identification and status reporting (`04f9:2058`)
- CUPS 1-bit and 8-bit grayscale Raster input
- Binary threshold, ordered dither, and error-diffusion rendering
- Brightness, contrast, speed/quality priority, and multiple copies
- Uncompressed transfer and TIFF PackBits compression
- Horizontal mirror, output orientation, and peeler mode
- Brother presets: 30x30, 40x40, 40x50, 40x60, 50x30, 51x26, and
  60x60 mm die-cut labels plus 57/58 mm continuous rolls
- Hardware-tested 50 x 15 mm die-cut profile
- Custom die-cut labels from 19-63 mm wide and 7-255 mm long
- Automatic crop/pad normalization for CUPS Raster dimension differences

See [the official-driver compatibility audit](docs/OFFICIAL_DRIVER_PARITY.md)
for a comparison with Brother's 2013 CUPS wrapper source.

## Requirements

- C17 compiler, `make`, and `pkg-config`
- libusb 1.0 development files for the direct-USB utility
- CUPS development files for the Raster filter

Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libcups2-dev cups cups-client
```

Fedora:

```sh
sudo dnf install gcc make pkgconf-pkg-config libusb1-devel cups-devel cups
```

macOS with Homebrew:

```sh
brew install libusb pkg-config
```

macOS includes CUPS 2.x headers and libraries. PPD/filter support is deprecated
upstream but remains available in current macOS CUPS releases.

## Build and test

```sh
make clean
make
make test
```

Tests cover protocol framing, media parameters, PackBits, CUPS Raster
conversion, and normalization of both `554x106` printable-area input and
`591x177` physical-page input for a 50 x 15 mm label.

## Install the CUPS driver

```sh
sudo make install-cups
```

On Linux this installs into paths reported by `cups-config`. On macOS it uses
`/Library/Printers`, avoiding SIP-protected system directories.

Restart CUPS on Linux:

```sh
sudo systemctl restart cups
```

Restart CUPS on macOS:

```sh
sudo launchctl kickstart -k system/org.cups.cupsd
```

### Create the queue on Linux

```sh
lpinfo -v | grep -i Brother
sudo lpadmin -p TD2130N -E \
  -v 'usb://Brother/TD-2130N?serial=YOUR_SERIAL' \
  -P "$(cups-config --datadir)/model/Brother-TD2130N.ppd"
sudo lpoptions -d TD2130N
```

Replace the example URI with the exact result returned by `lpinfo`.

### Create the queue on macOS

```sh
lpinfo -v | grep -i Brother
sudo lpadmin -p TD2130N -E \
  -v 'usb://Brother/TD-2130N' \
  -P /Library/Printers/PPDs/Contents/Resources/Brother-TD2130N.ppd
```

If `lpinfo` includes a serial number, use that complete URI. When upgrading,
run the `lpadmin -P ...` command again because CUPS caches a queue-specific PPD.

## Printing

```sh
# Hardware-tested 50 x 15 mm preset
lp -d TD2130N -o media=DieCut50x15 label.pdf

# Custom 40 x 20 mm die-cut label
lp -d TD2130N -o media=Custom.40x20mm label.pdf

# 58 mm continuous roll with a 3 mm feed margin
lp -d TD2130N -o media=Roll58 -o Feed=3 label.pdf

# Quality grayscale rendering with compressed transfer
lp -d TD2130N -o media=DieCut50x15 \
  -o Priority=Quality -o Halftone=ErrorDiffusion -o Compress=True label.pdf
```

List all queue options:

```sh
lpoptions -p TD2130N -l
```

`Rotate: Normal (Recommended)` is the hardware-tested result. Use
`Upside Down (Rotate 180 Degrees)` only for the opposite label-exit direction.

## Direct USB utility

The `td2130` utility works independently of CUPS:

```sh
./td2130 devices
sudo ./td2130 status
./td2130 print label.pbm --media 50x15 --dry-run label.bin
sudo ./td2130 print label.pbm --media 50x15
```

The 50 x 15 mm PBM printable area is at most 554 pixels wide and exactly 106
rows high. Binary PBM (`P4`) is required. Use `--no-rotate` only for the
opposite output direction.

### Linux USB permissions

If direct access is denied even though `lsusb` sees the printer:

```sh
sudo install -m 0644 packaging/99-td2130.rules /etc/udev/rules.d/99-td2130.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and reconnect the printer. The rule is only needed by the direct-USB
utility; the CUPS USB backend manages its own access.

## Driver architecture

The project deliberately separates printer protocol, document conversion, and
transport:

```text
Application / PDF
        |
        v
 CUPS 8-bit K Raster
        |
        v
 rastertobrothertd2130
   - tone adjustment
   - halftone conversion
   - crop/pad normalization
   - physical head-dot mapping
        |
        v
 Brother Raster byte stream
        |
        v
 CUPS USB backend ----> TD-2130N
```

`src/td2130.c` does not depend on CUPS or libusb. It accepts a packed 1-bit
bitmap and produces a complete Brother Raster job in memory. The CUPS filter
and direct-USB utility both call this same encoder, preventing the two printing
paths from developing different protocol behavior.

The CUPS filter never opens the USB device. It writes printer-ready bytes to
standard output and lets CUPS' existing USB backend manage device locking,
spooling, retries, and queue ownership. The command-line utility uses libusb
only for diagnostics and direct hardware tests.

## Protocol implementation details

Each generated job contains:

1. 200 zero bytes to invalidate any incomplete previous command stream;
2. the printer initialization command;
3. a switch to Brother Raster mode;
4. media type, physical size, raster-row count, and quality flags;
5. orientation and optional peeler mode;
6. continuous-media feed margin, or zero for die-cut labels;
7. compressed or uncompressed raster rows;
8. the final print-and-feed command.

The TD-2130N has a 672-dot (84-byte) 300 dpi print head. Brother numbers its
physical head dots in the opposite horizontal direction from conventional PBM
and CUPS coordinates. The encoder reverses that hardware mapping internally;
the user-facing `MirrorPrint` option is an additional logical mirror and is not
used to compensate for the printer's native dot order.

Uncompressed rows use the documented fixed 84-byte payload. Compressed mode
uses TIFF-style PackBits over the complete 84-byte row. Entirely white rows use
the printer's zero-raster command. Compression is optional because
uncompressed USB transfer allows the printer to begin printing while data is
still arriving, whereas compressed jobs generally start after more of the page
has been buffered.

## Media geometry

Media dimensions passed to the printer are physical dimensions in whole
millimeters. Raster dimensions describe only the printable area:

- ordinary die-cut media reserves approximately 1.55 mm at each horizontal
  edge and 3 mm at each feed-direction edge;
- 50 x 15 mm therefore maps to a 554 x 106 dot printable bitmap;
- continuous media uses its page raster height plus a separately configured
  feed margin;
- the raster is centered on the 672-dot print head.

CUPS versions and applications do not always agree on whether the filter
receives the imageable area or the complete physical page. The filter therefore
calculates the expected printable geometry from the media size and centers,
crops, or pads the incoming Raster. It also handles a width/height-swapped
Raster produced by an orientation conversion. This is why both `554x106` and
`591x177` inputs can produce the same valid 50 x 15 mm printer job.

Custom die-cut sizes are encoded using the protocol's media type, width, and
length fields. The current public interface uses whole-millimeter dimensions,
matching the printer status and print-information fields.

## Grayscale processing

The PPD requests 8-bit `K` Raster from CUPS (`0` is white and `255` is black).
The filter applies brightness and contrast before converting to 1-bit output.
Three conversion modes are available:

- `Binary`: a fixed threshold, best for already-clean text and barcodes;
- `Dither`: a 4 x 4 ordered Bayer pattern, useful for predictable graphics;
- `ErrorDiffusion`: Floyd-Steinberg-style distributed quantization error,
  normally best for photographs and gradients.

The filter also accepts existing 1-bit CUPS Raster and passes its pixels through
without a second halftone conversion.

## Current limitations

- Only the TD-2130N USB identity and 300 dpi geometry are enabled.
- Direct CLI image input is binary PBM; PDF and common image formats should be
  printed through CUPS.
- Asynchronous completion/status monitoring and cancellation are not yet
  integrated into the CUPS filter.
- Cut-at-end and cut-every-N are not exposed: they are commented out in
  Brother's TD-2130N PPD and no corresponding cutter command is documented for
  this model.
- The old wrapper's `Trim tape after data` option is not emitted because the
  published Raster protocol does not define a safe equivalent.
- PPD filters are a CUPS 2.x mechanism. The protocol core is intentionally
  independent so it can later be hosted in a PAPPL/CUPS 3 Printer Application.

## Troubleshooting

```sh
lpstat -t
lpstat -W all -o TD2130N
```

Linux:

```sh
journalctl -u cups --since '5 minutes ago'
sudo tail -n 200 /var/log/cups/error_log
```

macOS:

```sh
sudo cupsctl --debug-logging
sudo tail -n 200 /var/log/cups/error_log
```

The filter logs the input and normalized sizes, for example:

```text
TD-2130N page 1: media=50x15 mm raster=591x177 -> 554x106 rotate=yes compress=no
```

## Project layout

- `src/td2130.c`: protocol encoder and status parser
- `src/cups_filter.c`: CUPS Raster conversion and option processing
- `src/usb_transport.c`: libusb diagnostic/direct-print transport
- `cups/Brother-TD2130N.ppd`: printer and option definition
- `packaging/`: udev rule and cross-platform CUPS installer
- `tests/`: protocol and CUPS Raster tests

## Documentation and trademarks

Brother's protocol PDFs are used locally as implementation references but are
not redistributed because their terms restrict third-party redistribution.
Obtain the manuals from Brother's official developer/support site.

Brother and TD-2130N are trademarks of Brother Industries, Ltd. All trademarks
belong to their respective owners.
