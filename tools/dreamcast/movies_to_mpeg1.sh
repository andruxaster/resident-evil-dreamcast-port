#!/bin/sh
# movies_to_mpeg1.sh <data-dir> [video-kbit/s] [audio-kbit/s]
#
# Re-encodes every movie under <data-dir>/*/Movie/ (USA/, DC/, JPN/ ...) to
# MPEG-1 video + MP2 audio (MPEG-PS) for the Dreamcast player (video.cpp,
# pl_mpeg). The PC movies are Cinepak + uncompressed PCM (~1.8 Mbit/s); at the
# defaults (600 + 96 kbit/s) they shrink to about a third.
#
# The MPEG stream is written UNDER THE NAME THE GAME USES (<name>.avi): the
# game checks that file exists and resolves the DC/USA overlay by it, and the
# player recognises MPEG-PS by its content. A Director's Cut <name>.mp4 is
# converted to <name>.avi too (the game would otherwise pick the .mp4).
# ffmpeg-based builds (PC/Linux) also play these files: ffmpeg probes content.
#
#   - frame rate and size are kept (the PC movies are 320x240 at 10 fps; MPEG-1
#     has no 10 fps code, so ffmpeg writes its "unofficial" one and the vendored
#     pl_mpeg knows it)
#   - audio: MP2 (MPEG-1 Layer II) stereo 32 kHz
#   - originals are MOVED to <data-dir>/../movies_backup/<same path>;
#     restore by moving them back (overwriting the MPEG .avi files)
#   - files that already are MPEG-PS are skipped, so the script can be rerun
#
# Needs ffmpeg.
set -e
DATA=${1:?usage: movies_to_mpeg1.sh <data-dir> [video-kbit/s] [audio-kbit/s]}
VK=${2:-600}
AK=${3:-96}
command -v ffmpeg >/dev/null || { echo "ffmpeg not found (apt install ffmpeg)"; exit 1; }
DATA=$(cd "$DATA" && pwd)
BACKUP="$(dirname "$DATA")/movies_backup"

is_mpeg_ps() { [ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = "000001ba" ]; }

encode() {   # src out
    fps=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$1")
    # tpad clones the last frame once: pl_mpeg does not output the final
    # reference frame of a stream with B-frames, so the clone is what is lost.
    # Audio is MPEG-1 Layer II at 32 kHz: pl_mpeg rejects MPEG-2 (22.05 kHz)
    # MP2 frames. Low-passed at 10.5 kHz so the mixer's 32 -> 22.05 kHz step
    # does not alias.
    ffmpeg -hide_banner -loglevel error -y -i "$1" \
        -vf "tpad=stop_mode=clone:stop=1" \
        -c:v mpeg1video -strict unofficial -r "$fps" -b:v "${VK}k" -maxrate "$((VK * 2))k" \
        -bufsize 1835k -g 15 -bf 2 \
        -af "lowpass=f=10500" -c:a mp2 -ar 32000 -ac 2 -b:a "${AK}k" \
        -f mpeg "$2"
}

backup() {   # file
    rel=${1#"$DATA"/}
    mkdir -p "$BACKUP/$(dirname "$rel")"
    mv "$1" "$BACKUP/$rel"
}

before=0; after=0; n=0
for mdir in "$DATA"/*/[Mm][Oo][Vv][Ii][Ee]; do
    [ -d "$mdir" ] || continue
    # .mp4 first: in a Director's Cut tree it is the better source for <name>.avi
    for src in "$mdir"/*.[Mm][Pp]4 "$mdir"/*.[Aa][Vv][Ii]; do
        [ -f "$src" ] || continue
        is_mpeg_ps "$src" && continue
        base=$(basename "$src"); stem=${base%.*}
        # keep the existing .avi spelling if there is one
        avi=$(find "$mdir" -maxdepth 1 -iname "$stem.avi" | head -1)
        [ -n "$avi" ] || avi="$mdir/$stem.avi"
        echo "== $src -> $(basename "$avi") (MPEG-1)"
        tmp="$mdir/.$stem.mpg.tmp"
        encode "$src" "$tmp"
        before=$((before + $(wc -c < "$src"))); after=$((after + $(wc -c < "$tmp")))
        backup "$src"
        if [ -f "$avi" ] && [ "$avi" != "$src" ]; then backup "$avi"; fi
        mv "$tmp" "$avi"
        n=$((n + 1))
    done
done
[ "$n" -gt 0 ] || { echo "nothing to convert under $DATA/*/Movie"; exit 0; }
echo "converted $n movies: $((before / 1048576)) MB -> $((after / 1048576)) MB"
echo "originals moved to $BACKUP"
