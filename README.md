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
All executables, objects, and generated test output are kept under `build/`.

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
./build/td2130 devices
sudo ./build/td2130 status
./build/td2130 print label.pbm --media 50x15 --dry-run label.bin
sudo ./build/td2130 print label.pbm --media 50x15
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

## Legacy-style configuration editor

`td2130-config` is the clean-room replacement for the behavior recovered from
`brprintconfpt1_td2130n`. It validates settings against `brtd2130nfunc`, applies
all requested changes as one transaction, preserves unrelated rc-file content,
and keeps the previous file as `<rc-file>.old`:

```sh
./build/td2130-config -P TD2130N \
  -copy 2 -brit -10 -half ERROR -quality SPEED \
  -reso 300 -feed 3 -media 50x30 \
  --root /tmp/staging-root --show
```

The recovered option spellings are accepted: `-copy`, `-cutlabel`, `-cutend`,
`-trimtape`, `-compress`, `-brit`, `-cont`, `-half`, `-mirro`, `-rotate`,
`-peeler`, `-quality`, `-reso`, `-feed`, `-media`, and `-collate`. Use
`--rc-file` (or legacy spelling `-rcfile`) and `--func-file` for explicit files.
Resolution values, including 203 or 300 ppi profiles, are accepted only when
listed by the loaded function-definition file. The original binary had a
hard-coded 300-only resolution table; this project deliberately extends that
known-value table with 203 ppi for related 448-dot hardware. The recovered
TD-2130N function file still enables only 300 ppi, so selecting 203 also
requires a model definition that explicitly enables it. Other settings must be
both known to this tool and enabled by the function-definition range or
selection list. Registered custom-media IDs and display names are both
accepted and normalized to the 15-character ID stored by the legacy rc format.
Unknown options or values return 2, known values disabled by the loaded schema
return 11, and invalid or out-of-range numeric settings return 12. Validation
is completed before the rc file is replaced, so a failed multi-option command
does not partially apply earlier settings.

Unlike the old binary, this utility keeps a recoverable `.old` backup and does
not create the world-writable
`/var/tmp/lprng_*_rcname` hand-off file. The native CUPS filter receives job
options directly, so that global, race-prone mechanism is unnecessary.

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

### Gap and black-mark sensing

The printer has two physical media sensors:

- the transmissive (gap) sensor detects the gap between ordinary die-cut
  labels;
- the reflective (black-mark) sensor detects a black registration mark on the
  back of the media.

These are sensor modes, not additional values in the Raster print-information
command. That command distinguishes only continuous media (`0x0a`) from
die-cut labels (`0x0b`); `0x0b` is used for both paper and film. Consequently,
the CUPS PPD does not expose misleading `BlackMark` or `Transparent` paper-type
switches.

Transparent label stock is not itself a sensor mode. Use gap sensing only when
the liner and label provide enough transmissive contrast for reliable gap
detection; otherwise use media manufactured with black registration marks and
the printer's reflective sensor.

Brother's legacy Linux package handled custom paper in two distinct steps.
`brpapertoolcups` added the custom name and page geometry to the CUPS queue's
PPD. The separate, binary-only `brpapertoollpr_td2130n` accepted the sensor
type (`-S 0/1/2` for continuous, die-cut, or media with marks), gap, margins,
black-mark length, and black-mark offset. It stored a per-media binary file
under `inf/customtape/`; the binary `rastertobrpt1` loaded that file when the
matching media name was selected. The open-source `brcupsconfig` only mapped
the selected CUPS media name into the temporary Brother configuration file.

Those per-media files contain the `ESC i U` additional-media-information data
described by Brother's Raster reference. This project now provides a clean-room
generator compatible with the recovered 132-byte file format:

```sh
make td2130-paper
./build/td2130-paper -P TD-2130N -n Marked30 -w 30 -h 30 \
  -g 3 -t 3 -b 3 -l 1.5 -r 1.5 -S 2 -m 5 -o 2 -O Marked30.bin
```

`-S 0`, `1`, and `2` select continuous, die-cut/gap, and black-mark sensing.
The TD-2130N-compatible default is 300 dpi with a 672-dot head. For related
203 dpi hardware, pass `-d 203` (448 dots by default); `-H` can override the
head width when a model differs. Without `--install-root`, the tool only
creates the requested media binary. To register it in an installed tree and
an existing CUPS queue PPD, explicitly request installation:

```sh
sudo ./build/td2130-paper -P TD2130N -n Marked30 -w 30 -h 30 \
  -g 3 -t 3 -b 3 -l 1.5 -r 1.5 -S 2 -m 5 -o 2 -d 203 \
  --install-root /
```

For tests and packaging, a staging directory such as `--install-root /tmp/root`
redirects all system paths. `--ppd /path/to/queue.ppd` overrides the derived
`/etc/cups/ppd/<queue>.ppd`. Registration updates existing entries instead of
duplicating them. The filter resolves the selected `BrL...` ID, loads its media
definition, and sends it immediately after printer initialization.

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

- The print transport/filter targets the TD-2130N USB identity and 300 dpi,
  672-dot raster geometry. The custom-media generator can also calculate
  203 dpi definitions for related hardware.
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
