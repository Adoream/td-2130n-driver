#!/bin/sh
set -eu

if [ "$(uname -s)" = "Darwin" ]; then
    # SIP makes /usr/libexec/cups and /usr/share/cups read-only on current
    # macOS. Apple reserves /Library/Printers for third-party printer code.
    filter_dir="/Library/Printers/TD2130N/Filters"
    media_dir="/Library/Printers/TD2130N/Media"
    legacy_media_dir="/usr/local/share/brother/PTouch/td2130n"
    ppd_dir="/Library/Printers/PPDs/Contents/Resources"
    filter_path="$filter_dir/rastertobrothertd2130"
    ppd_path="$ppd_dir/Brother-TD2130N.ppd"
    temporary_ppd="$(mktemp -t Brother-TD2130N.ppd.XXXXXX)"
    trap 'rm -f "$temporary_ppd"' EXIT HUP INT TERM

    mkdir -p "$filter_dir" "$media_dir" "$ppd_dir"
    # CUPS filters on macOS are sandboxed and cannot reliably read custom
    # media from /usr/local/share. Preserve registrations made by older
    # versions by copying them into the driver-owned /Library tree.
    if [ -d "$legacy_media_dir" ]; then
        cp -R "$legacy_media_dir"/. "$media_dir"/
    fi
    chmod 0755 "$media_dir"
    if [ -d "$media_dir/customtape" ]; then
        chmod 0755 "$media_dir/customtape"
        chmod 0644 "$media_dir"/customtape/*.bin 2>/dev/null || true
    fi
    if [ -f "$media_dir/custom_media.conf" ]; then
        chmod 0644 "$media_dir/custom_media.conf"
    fi
    cp build/rastertobrothertd2130 "$filter_path"
    chmod 0755 "$filter_path"
    sed "s| 0 rastertobrothertd2130\"| 0 $filter_path\"|" \
        cups/Brother-TD2130N.ppd > "$temporary_ppd"
    cp "$temporary_ppd" "$ppd_path"
    chmod 0644 "$ppd_path"
else
    filter_dir="$(cups-config --serverbin)/filter"
    ppd_dir="$(cups-config --datadir)/model"
    filter_path="$filter_dir/rastertobrothertd2130"
    ppd_path="$ppd_dir/Brother-TD2130N.ppd"

    install -m 0755 build/rastertobrothertd2130 "$filter_path"
    install -m 0644 cups/Brother-TD2130N.ppd "$ppd_path"
fi

echo "Installed filter to $filter_path"
echo "Installed PPD to $ppd_path"
if [ "$(uname -s)" = "Darwin" ]; then
    echo "Custom media directory: $media_dir"
fi
echo "Restart CUPS, then add the printer using the Brother TD-2130N PPD."
echo "PPD path for lpadmin: $ppd_path"
