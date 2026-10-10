#!/bin/sh
# make_cdi.sh <residentevil.elf> <data-dir> [out.cdi]
#
# Builds a bootable CDI (Flycast / CD-R / GDEMU) with mkdcdisc
# (https://gitlab.com/simulant/mkdcdisc). <data-dir> is a folder holding YOUR
# copy of the game: USA/ (and optionally JPN/) next to config.ini. Its
# CONTENTS become the disc root, so the game sees /cd/USA/... and
# /cd/config.ini. Build the ELF with the default RE1_DC_ROOT=/cd.
#
# mkdcdisc's -d adds a directory under its own name and -f adds a single file
# to the root, so the data dir is passed entry by entry.
set -e
ELF=${1:?usage: make_cdi.sh <elf> <data-dir> [out.cdi]}
DATA=${2:?usage: make_cdi.sh <elf> <data-dir> [out.cdi]}
OUT=${3:-re1.cdi}

command -v mkdcdisc >/dev/null || { echo "mkdcdisc not found in PATH"; exit 1; }
if ! find "$DATA" -mindepth 1 -maxdepth 1 -type d \( -iname USA -o -iname JPN \) | grep -q .; then
    echo "$DATA has no USA/ or JPN/ folder (the game data must sit directly inside it). It contains:"
    ls -1 "$DATA" | sed 's/^/    /'
    exit 1
fi
[ -f "$DATA/config.ini" ] || cp "$(dirname "$0")/config.ini" "$DATA/config.ini"

# Same check as make_gdi.sh: the game prefers an .mp4 over the .avi its FMV
# table names, and the Dreamcast player decodes Cinepak only.
if find "$DATA" -iname '*.mp4' | grep -q .; then
    echo "WARNING: .mp4 movies in $DATA - the game prefers them over the .avi and the"
    echo "         Dreamcast player cannot decode H.264, so those movies would be skipped."
    echo "         Run tools/dreamcast/dc_movies_to_cinepak.sh $DATA first."
fi

# A CDI is a CD-R: ~700 MB for an 80-minute disc (the GD-ROM image holds ~1 GB).
kb=$(du -sk "$DATA" | cut -f1)
if [ "$kb" -gt $((680 * 1024)) ]; then
    echo "WARNING: $DATA is $((kb / 1024)) MB - more than fits on an 80-minute CD-R."
    echo "         Emulators may still load the CDI, burning it will fail. For the"
    echo "         full data set use the GD-ROM image (make_gdi.sh) instead."
fi

set -- -e "$ELF" -n "RESIDENT EVIL" -o "$OUT"
for entry in "$DATA"/*; do
    name=$(basename "$entry")
    case "$name" in build|.git|*.cdi|*.elf) continue ;; esac
    if [ -d "$entry" ]; then set -- "$@" -d "$entry"
    elif [ -f "$entry" ]; then set -- "$@" -f "$entry"
    fi
done

echo "mkdcdisc $*"
mkdcdisc "$@"
echo "wrote $OUT"
