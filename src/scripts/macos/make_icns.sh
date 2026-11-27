#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd "$script_dir/../../.." && pwd)
source_icon=${1:-"$root/src/logo/logo.svg"}
output=${2:-"$root/build/generated/SwiftMarkdown.icns"}
if [[ $# -gt 2 || ! -f "$source_icon" ]]; then
    echo "Usage: $0 <square SVG or PNG> [output.icns]" >&2
    exit 2
fi
mkdir -p -- "$(dirname -- "$output")"
work=$(/usr/bin/mktemp -d "$(dirname -- "$output")/.icns.XXXXXXXX")
trap 'rm -rf "$work"' EXIT

if [[ "$source_icon" == *.[sS][vV][gG] ]]; then
    png="$work/source.png"
    if ! /usr/bin/sips -s format png "$source_icon" --out "$png" >/dev/null 2>&1 || [[ ! -s "$png" ]]; then
        mkdir -p "$root/build"
        /usr/bin/swift -module-cache-path "$root/build/swift-module-cache" "$script_dir/rasterize_svg.swift" "$source_icon" "$png"
    fi
else
    png=$source_icon
fi

read_dimension() {
    /usr/bin/sips -g "$1" "$png" | /usr/bin/awk -v key="$1:" '$1 == key { print $2 }'
}
width=$(read_dimension pixelWidth)
height=$(read_dimension pixelHeight)
if [[ ! "$width" =~ ^[0-9]+$ || "$width" != "$height" ]]; then
    echo "App icon must have a square canvas: $source_icon" >&2
    exit 2
fi

iconset="$work/SwiftMarkdown.iconset"
mkdir "$iconset"
for size in 16 32 128 256 512; do
    /usr/bin/sips -z "$size" "$size" "$png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    /usr/bin/sips -z "$((size*2))" "$((size*2))" "$png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
if ! /usr/bin/iconutil -c icns "$iconset" -o "$work/SwiftMarkdown.icns" 2>"$work/iconutil.log"; then
    /usr/bin/swift -module-cache-path "$root/build/swift-module-cache" "$script_dir/pack_iconset.swift" \
        "$iconset" "$work/SwiftMarkdown.icns"
fi
mv -f -- "$work/SwiftMarkdown.icns" "$output"
printf '%s\n' "$output"
