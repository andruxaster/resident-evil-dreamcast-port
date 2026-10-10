#!/bin/sh
# census_resolve.sh <serial-log> <residentevil.elf>
#
# Takes the LAST "[MARNI] texture census" block of a RE1_DC_VERBOSE=1 log and
# prints each live texture with the code addresses of its creator resolved to
# function names - to see which game code makes textures that never go away.
set -e
LOG=${1:?usage: census_resolve.sh <serial-log> <elf>}
ELF=${2:?usage: census_resolve.sh <serial-log> <elf>}
A2L=${KOS_CC_BASE:+$KOS_CC_BASE/bin/}sh-elf-addr2line
command -v "$A2L" >/dev/null 2>&1 || A2L=sh-elf-addr2line
TMP=/tmp/census.$$
sed 's/\[\$0[DA]\]//g' "$LOG" | tr -d '\r' > $TMP.log
awk '/\[MARNI\] texture census/ { buf = $0; grab = 1; next }
     grab && /^[ \t]*$/ { next }
     grab && /^    #/ { buf = buf "\n" $0; next }
     grab && /^        owners:/ { buf = buf "\n" $0; next }
     grab { grab = 0 }
     END { print buf }' $TMP.log > $TMP.blk
[ -s $TMP.blk ] || { echo "no texture census in $LOG (RE1_DC_VERBOSE=1 build, play until it prints)"; rm -f $TMP.*; exit 1; }
head -1 $TMP.blk
grep -oE '\b8c[0-9a-f]{6}\b' $TMP.blk | sort -u > $TMP.addr
"$A2L" -f -C -e "$ELF" $(cat $TMP.addr) | paste - - | cut -f1 | sed 's/(.*//' > $TMP.names
paste $TMP.addr $TMP.names > $TMP.map
tail -n +2 $TMP.blk | while read -r line; do
    out="$line"
    for a in $(echo "$line" | grep -oE '\b8c[0-9a-f]{6}\b'); do
        n=$(awk -v a="$a" '$1 == a { print $2; exit }' $TMP.map)
        out=$(echo "$out" | sed "s/$a/${n:-?}/")
    done
    echo "$out"
done
rm -f $TMP.*
