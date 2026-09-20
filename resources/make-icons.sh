#!/usr/bin/env sh
# Regenerate the hicolor PNG icons from resources/omakeyboard-logo.png.
#
# Every size the hicolor theme declares for apps gets its own raster, because
# a launcher that asks for 32 px and receives the 1254 px source downscales it
# on the fly and the result is soft. Run after replacing the logo:
#
#     sh resources/make-icons.sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SRC="$ROOT/omakeyboard-logo.png"
OUT="$ROOT/icons"
mkdir -p "$OUT"
for size in 16 22 24 32 48 64 96 128 256 512; do
  magick "$SRC" -background none -filter Lanczos -resize "${size}x${size}" \
    -define png:color-type=6 "$OUT/${size}.png"
done
echo "Wrote $(ls "$OUT" | wc -l) icons to $OUT"
