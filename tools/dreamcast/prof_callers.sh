#!/bin/sh
# prof_callers.sh <serial-log> <residentevil.elf> [function-regex] [top]
#
# For RE1_DC_PROF builds with diag_prof_callers.py: takes the [PROFC] samples
# of the LAST report whose pc is in a function matching the regex (default:
# the lock/wake path), and ranks the call chains that led there - pr and the
# first code words on the stack, as function names, repeats folded.
set -e
LOG=${1:?usage: prof_callers.sh <log> <elf> [regex] [top]}
ELF=${2:?usage: prof_callers.sh <log> <elf> [regex] [top]}
RX=${3:-'genwait|mutex_|arch_irq|sq_unlock'}
TOP=${4:-25}
A2L=${KOS_CC_BASE:+$KOS_CC_BASE/bin/}sh-elf-addr2line
command -v "$A2L" >/dev/null 2>&1 || A2L=sh-elf-addr2line
TMP=/tmp/profc.$$
sed 's/\[\$0D\]//g; s/\[\$0A\]/\n/g' "$LOG" | tr -d '\r' |
    awk '/^\[PROF\] [0-9]+ samples/ { buf = "" } /^\[PROFC\] / { buf = buf $0 "\n" } END { printf "%s", buf }' |
    sed 's/^\[PROFC\] //' > $TMP.rows
[ -s $TMP.rows ] || { echo "no [PROFC] lines (apply diag_prof_callers.py, build with RE1_DC_PROF=1)"; rm -f $TMP.*; exit 1; }
tr ' ' '\n' < $TMP.rows | grep -v '^00000000$' | sort -u > $TMP.uniq
"$A2L" -f -C -e "$ELF" $(cat $TMP.uniq) | paste - - | cut -f1 | sed 's/(.*//' > $TMP.names
paste $TMP.uniq $TMP.names > $TMP.map
N=$(wc -l < $TMP.rows)
awk -F'\t' -v rx="$RX" '
    NR == FNR { name[$1] = $2; next }
    {
        split($0, a, " ")
        f = name[a[1]]
        if (f !~ rx) next
        hit++
        chain = f; last = f
        for (i = 2; i <= 5; i++) {
            if (a[i] == "00000000") continue
            g = name[a[i]]
            if (g == "" || g == "??" || g == last) continue
            chain = chain " <- " g; last = g
        }
        n[chain]++
    }
    END {
        for (c in n) print n[c] "\t" c
        print hit > "/dev/stderr"
    }' $TMP.map $TMP.rows 2> $TMP.hit | sort -rn | head -n "$TOP" |
    awk -F'\t' '{ printf "%5d  %s\n", $1, $2 }'
echo "($(cat $TMP.hit) of $N samples matched /$RX/)"
rm -f $TMP.*
