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
| Continuous feed margin | Supported, 3-30 mm |
| Speed/quality priority | Supported through protocol flag |
| Buffered TIFF/PackBits transfer | Supported |
| Mirror | Supported |
| 180-degree rotation | Supported; validated orientation is default |
| Peeler | Supported through protocol mode bit |
| Binary/dither/error-diffusion halftone | Supported in the C filter |
| Brightness and contrast | Supported from -50 to 50; common values shown in PPD |
| Copies | Supported |
| N-up | Left to standard CUPS filters, as on modern CUPS |
| Cut-at-end/cut-every-N | Not exposed: commented out in Brother's TD-2130N PPD and no cutter command is documented |
| Trim tape after data | Not implemented: no corresponding command exists in the published TD-2130N raster protocol |

Unlike the 2013 wrapper, this filter does not create world-writable temporary
configuration files, invoke Ghostscript, depend on architecture-specific
binaries, or parse PPD defaults itself. CUPS performs document rasterization;
the filter consumes the resulting 8-bit grayscale raster, applies selected
tone/halftone options, and emits Brother Raster bytes.
