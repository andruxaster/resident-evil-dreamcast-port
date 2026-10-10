#!/bin/sh
# ftrace_resolve.sh <serial-log> <residentevil.elf> [count]
#
# For RE1_DC_FTRACE=1 builds: takes the function addresses printed after the
# last "[FTRACE ON]" in the serial log and prints the last [count] (default
# 60) as function names, oldest first. The last line is the last game function
# entered before the machine stopped.
set -e
LOG=${1:?usage: ftrace_resolve.sh <serial-log> <elf> [count]}
ELF=${2:?usage: ftrace_resolve.sh <serial-log> <elf> [count]}
N=${3:-60}
A2L=${KOS_CC_BASE:+$KOS_CC_BASE/bin/}sh-elf-addr2line
command -v "$A2L" >/dev/null 2>&1 || A2L=sh-elf-addr2line

# Strip the [$0D][$0A] markers some terminals log, keep what follows the last
# "[FTRACE ON]", and pick out the 6-hex-digit words.
sed 's/\[\$0D\]//g; s/\[\$0A\]/\n/g' "$LOG" | tr -d '\r' |
    awk '/\[FTRACE ON\]/ { buf = ""; on = 1; next } on { buf = buf " " $0 } END { print buf }' |
    tr ' ' '\n' | grep -E '^[0-9a-f]{6}$' | tail -n "$N" |
    sed 's/^/0x8c/' > /tmp/ftrace_addrs.$$
[ -s /tmp/ftrace_addrs.$$ ] || { echo "no [FTRACE ON] addresses in $LOG"; rm -f /tmp/ftrace_addrs.$$; exit 1; }
"$A2L" -f -C -e "$ELF" $(cat /tmp/ftrace_addrs.$$) | paste - - | nl -w3 -s'  '
rm -f /tmp/ftrace_addrs.$$
