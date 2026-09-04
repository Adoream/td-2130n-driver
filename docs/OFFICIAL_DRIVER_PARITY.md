# Official TD-2130N wrapper compatibility

The Brother `cupswrapper-td2130n-src-1.1.1-1` directory contains the GPL CUPS
wrapper, option parser, and PPD. It does not contain the raster engine invoked
as `/opt/brother/PTouch/td2130n/lpd/filtertd2130n`; that component came from the
separate binary LPR package. This project replaces that missing component with
a native CUPS Raster implementation based on Brother's published protocol.

| Official exposed feature | This driver |
| --- | --- |
| 300 dpi | Supported |
| 30x30, 40x40, 40x50, 40x60 die-cut | Supported |
| 50x30, 51x26, 60x60 die-cut | Supported |
| 57/58 mm continuous | Supported |
| 50x15 die-cut | Added and hardware validated |
| Arbitrary custom die-cut size | Added, 19-63 x 7-255 mm |
| Gap (transmissive) / black-mark (reflective) custom media | Implemented: clean-room `ESC i U` generation, idempotent lookup-table/PPD registration, and filter injection |
| Continuous feed margin | Supported, 3-30 mm |
| Speed/quality priority | Supported through protocol flag |
| Buffered TIFF/PackBits transfer | Supported |
| Mirror | Supported |
| 180-degree rotation | Supported; validated orientation is default |
| Peeler | Supported through protocol mode bit |
| Binary/dither/error-diffusion halftone | Supported in the C filter |
| Brightness and contrast | Supported from -50 to 50; common values shown in PPD |
| Copies | Supported |
| Legacy rc configuration editing | Implemented as `td2130-config`, with schema validation, atomic multi-option updates, backup, and name-to-custom-media-ID lookup; resolution is schema-controlled |
| N-up | Left to standard CUPS filters, as on modern CUPS |
| Cut-at-end/cut-every-N | Not exposed: commented out in Brother's TD-2130N PPD and no cutter command is documented |
| Trim tape after data | Not implemented: no corresponding command exists in the published TD-2130N raster protocol |

Unlike the 2013 wrapper, this filter does not create world-writable temporary
configuration files, invoke Ghostscript, depend on architecture-specific
binaries, or parse PPD defaults itself. CUPS performs document rasterization;
the filter consumes the resulting 8-bit grayscale raster, applies selected
tone/halftone options, and emits Brother Raster bytes.

The recovered `brprintconfpt1_td2130n` behavior is provided under the distinct
project name `td2130-config`. It accepts the legacy setting flags, reads the
selection/range schema from `brtd2130nfunc`, updates `brtd2130nrc` atomically,
and saves the previous file as `.old`. It deliberately omits the original
world-writable `/var/tmp/lprng_*_rcname` pointer-file protocol because the
native filter does not require it. The original executable's static resolution
table contains only 300; this implementation adds 203 as a known project value,
but it remains disabled unless the loaded model schema also selects it.

The official package separates three similarly named components. The
open-source `brcupsconfig` reads the PPD and converts the selected page name to
the binary raster engine's `-media` setting. The binary `brpapertoolcups` adds
the custom name and geometry to a queue PPD. Finally, the binary
`brpapertoollpr_td2130n` accepts sensor type, gap, margins, mark length, and
mark offset and creates `inf/customtape/<name>.bin`, which `rastertobrpt1`
loads while printing.

The Raster reference identifies the contents sent to the printer as a
127-byte `ESC i U` additional-media-information payload but does not document
its fields. The clean-room `td2130-paper` implementation reconstructs
the legacy 6-byte command prefix plus 126-byte remainder, including continuous,
gap, and black-mark sensor fields. It accepts 300 dpi/672-dot geometry by
default and `-d 203` for 203 dpi/448-dot related hardware. With
`--install-root`, it updates the lookup tables and queue PPD, and the native
CUPS filter loads and sends the selected definition.
