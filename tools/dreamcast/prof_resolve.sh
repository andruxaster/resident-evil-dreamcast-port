#!/bin/sh
# prof_resolve.sh <serial-log> <residentevil.elf> [top]
#
# For RE1_DC_PROF=1 builds: collects every [PROF] PC sample in the log (or only
# the last report with LAST=1), maps them to functions and prints the busiest
# [top] (default 30) with their share of the samples.
set -e
LOG=${1:?usage: prof_resolve.sh <serial-log> <elf> [top]}
ELF=${2:?usage: prof_resolve.sh <serial-log> <elf> [top]}
TOP=${3:-30}
A2L=${KOS_CC_BASE:+$KOS_CC_BASE/bin/}sh-elf-addr2line
command -v "$A2L" >/dev/null 2>&1 || A2L=sh-elf-addr2line
TMP=/tmp/prof.$$
sed 's/\[\$0D\]//g; s/\[\$0A\]/\n/g' "$LOG" | tr -d '\r' > $TMP.log
if [ "${LAST:-0}" = 1 ]; then
    awk '/\[PROF\] [0-9]+ samples/ { buf = "" } /^\[PROF\] / { buf = buf "\n" $0 } END { print buf }' $TMP.log > $TMP.sel
else
    grep '^\[PROF\] ' $TMP.log > $TMP.sel || true
fi
grep -oE '\b8c[0-9a-f]{6}\b' $TMP.sel > $TMP.pcs || true
N=$(wc -l < $TMP.pcs)
[ "$N" -gt 0 ] || { echo "no [PROF] samples in $LOG (build with RE1_DC_VERBOSE=1 RE1_DC_PROF=1)"; rm -f $TMP.*; exit 1; }
# addr2line once per distinct PC, then count per function
sort -u $TMP.pcs > $TMP.uniq
"$A2L" -f -C -e "$ELF" $(cat $TMP.uniq) | paste - - | cut -f1 > $TMP.names
paste $TMP.uniq $TMP.names > $TMP.map
awk -F'\t' 'NR==FNR { name[$1] = $2; next } { n[name[$1]]++ } END { for (f in n) print n[f] "\t" f }' $TMP.map $TMP.pcs |
    sort -rn | head -n "$TOP" | awk -F'\t' -v N="$N" '{ printf "%5.1f%%  %5d  %s\n", 100 * $1 / N, $1, $2 }'
echo "($N samples)"
rm -f $TMP.*
