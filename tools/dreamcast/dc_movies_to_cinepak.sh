#!/bin/sh
# dc_movies_to_cinepak.sh <data-dir>
#
# The asset migrator's Director's Cut tab converts the PS1 .STR movies to
# <data-dir>/DC/Movie/*.mp4 (H.264 + AAC), and the game prefers an .mp4 next to
# the .avi its FMV table names. The Dreamcast player decodes Cinepak AVI only
# (H.264 is far beyond the SH-4), so those movies would be skipped. This turns
# every DC/Movie/*.mp4 into a Cinepak AVI of the same name (320x240, the
# movie's own frame rate, 16-bit PCM 22050 Hz stereo) and moves the .mp4 out of
# the tree into <data-dir>/../dc_movies_mp4/ (kept, not deleted).
#
# Needs ffmpeg (with its built-in cinepak encoder). Cinepak encoding is slow:
# expect a few minutes per movie.
set -e
DATA=${1:?usage: dc_movies_to_cinepak.sh <data-dir>}
MOV="$DATA/DC/Movie"
[ -d "$MOV" ] || { echo "no $MOV - build the DC overlay with the asset migrator first"; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg not found (apt install ffmpeg)"; exit 1; }

KEEP="$(dirname "$DATA")/dc_movies_mp4"
mkdir -p "$KEEP"

found=0
for mp4 in "$MOV"/*.mp4 "$MOV"/*.MP4; do
    [ -f "$mp4" ] || continue
    found=1
    base=$(basename "$mp4"); name=${base%.*}
    avi="$MOV/$name.avi"
    echo "== $base -> $name.avi"
    ffmpeg -hide_banner -loglevel error -y -i "$mp4" \
        -vf "scale=320:240:flags=lanczos" -c:v cinepak \
        -c:a pcm_s16le -ar 22050 -ac 2 "$avi"
    mv "$mp4" "$KEEP/"
done
[ "$found" = 1 ] || { echo "no .mp4 in $MOV - nothing to do"; exit 0; }
echo "done. Originals kept in $KEEP"
