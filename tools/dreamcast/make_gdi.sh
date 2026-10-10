#!/bin/sh
# make_gdi.sh <residentevil.elf> <data-dir> [out-dir]
#
# Builds a GD-ROM image (GDI) for GDEMU / Flycast / burning tools:
#
#   track01.iso   LBA 0      300 sectors, low-density data (placeholder)
#   track02.raw   LBA 450    300 sectors of audio silence
#   track03.iso   LBA 45000  high-density data: IP.BIN in the system area,
#                            1ST_READ.BIN (NOT scrambled - that is CD-R only)
#                            and the CONTENTS of <data-dir> at the root
#   disc.gdi      the track list
#
# <data-dir> is YOUR copy of the game: USA/ (and/or JPN/) next to config.ini.
# Build the ELF with the default RE1_DC_ROOT=/cd.
#
# Needs: genisoimage (or mkisofs / xorriso), $KOS_BASE (environ.sh sourced),
# and KOS's makeip (built here on first use; needs libpng-dev).
set -e

ELF=${1:?usage: make_gdi.sh <elf> <data-dir> [out-dir]}
DATA=${2:?usage: make_gdi.sh <elf> <data-dir> [out-dir]}
OUT=${3:-build/dc/gdi}

[ -n "$KOS_BASE" ] || { echo "source \$KOS_BASE/environ.sh first"; exit 1; }
[ -f "$ELF" ] || { echo "no such ELF: $ELF"; exit 1; }
if ! find "$DATA" -mindepth 1 -maxdepth 1 -type d \( -iname USA -o -iname JPN \) | grep -q .; then
    echo "$DATA has no USA/ or JPN/ folder (the game data must sit directly inside it). It contains:"
    ls -1 "$DATA" | sed 's/^/    /'
    exit 1
fi
[ -f "$DATA/config.ini" ] || cp "$(dirname "$0")/config.ini" "$DATA/config.ini"
if find "$DATA" -iname '*.mp4' | grep -q .; then
    echo "WARNING: .mp4 movies in $DATA - the game prefers them over the .avi and the"
    echo "         Dreamcast player cannot decode H.264, so those movies would be skipped."
    echo "         Run tools/dreamcast/dc_movies_to_cinepak.sh $DATA first."
fi

# ISO tool
if command -v genisoimage >/dev/null; then MKISO="genisoimage"
elif command -v mkisofs >/dev/null; then MKISO="mkisofs"
elif command -v xorriso >/dev/null; then MKISO="xorriso -as mkisofs"
else echo "need genisoimage, mkisofs or xorriso (apt install genisoimage)"; exit 1; fi

# makeip from KOS
MAKEIP=${MAKEIP:-$KOS_BASE/utils/makeip/makeip}
if [ ! -x "$MAKEIP" ]; then
    echo "building makeip ($KOS_BASE/utils/makeip, needs libpng-dev)"
    make -C "$KOS_BASE/utils/makeip" >/dev/null
fi
[ -x "$MAKEIP" ] || { echo "makeip not found at $MAKEIP"; exit 1; }

OBJCOPY=${KOS_OBJCOPY:-sh-elf-objcopy}

mkdir -p "$OUT"
WORK="$OUT/work"
rm -rf "$WORK"; mkdir -p "$WORK/t1"

# 1ST_READ.BIN: raw binary of the ELF, unscrambled for GD-ROM.
$OBJCOPY -R .stack -O binary "$ELF" "$WORK/1ST_READ.BIN"

# IP.BIN
cat > "$WORK/ip.txt" <<'EOF'
Hardware ID   : SEGA SEGAKATANA
Maker ID      : SEGA ENTERPRISES
Device Info   : GD-ROM1/1
Area Symbols  : JUE
Peripherals   : E000F10
Product No    : T0000
Version       : V1.000
Release Date  : 20261003
Boot Filename : 1ST_READ.BIN
SW Maker Name : RE1 DC PORT
Game Title    : RESIDENT EVIL
EOF
"$MAKEIP" -f "$WORK/ip.txt" "$WORK/IP.BIN" >/dev/null

# Track 3: high-density area, addressed from LBA 45000 (-C 0,45000), with
# IP.BIN in the 16-sector system area. Joliet keeps the game's long names
# (the port's ISO reader and KOS both prefer it); Rock Ridge for good measure.
$MKISO -quiet -C 0,45000 -V RESIDENT_EVIL -G "$WORK/IP.BIN" -l -J -joliet-long -r \
    -graft-points -o "$OUT/track03.iso" \
    "/1ST_READ.BIN=$WORK/1ST_READ.BIN" "/=$DATA"

# Track 1: a small placeholder data track, exactly 300 sectors.
echo "RESIDENT EVIL - Dreamcast port. This is a Dreamcast GD-ROM." > "$WORK/t1/README.TXT"
$MKISO -quiet -V RESIDENT_EVIL -l -o "$OUT/track01.iso" "$WORK/t1"
size=$(wc -c < "$OUT/track01.iso")
[ "$size" -le $((300 * 2048)) ] || { echo "track01 larger than 300 sectors"; exit 1; }
truncate -s $((300 * 2048)) "$OUT/track01.iso"

# Track 2: 300 sectors of CD audio silence (2352 bytes per sector).
dd if=/dev/zero of="$OUT/track02.raw" bs=2352 count=300 2>/dev/null

cat > "$OUT/disc.gdi" <<'EOF'
3
1 0 4 2048 track01.iso 0
2 450 0 2352 track02.raw 0
3 45000 4 2048 track03.iso 0
EOF

rm -rf "$WORK"
t3=$(wc -c < "$OUT/track03.iso")
max=$(( (549150 - 45000) * 2048 ))
[ "$t3" -le "$max" ] || echo "WARNING: track03 ($t3 bytes) exceeds the GD high-density area ($max bytes)"
echo "wrote $OUT/disc.gdi (track03: $((t3 / 1048576)) MB)"
echo "GDEMU: copy disc.gdi, track01.iso, track02.raw, track03.iso into one folder on the SD card."
