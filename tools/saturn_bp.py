"""Arm a breakpoint on the Saturn module decompressor.

FUN_0602b040 (GAME1 offset +0x40) has no static callers, so the only way to get
its real src/dst/stream-start is to catch it executing.  GAME1 is already loaded
and already expanded, so the hit has to be provoked by loading a *different*
overlay -- i.e. the user has to trigger a module load in the emulator.

Run with no arguments to report status; `arm` to set the breakpoint.
"""
import sys, os
sys.path.insert(0, "tools")
from verify_saturn_ram import Mcp, URL

BP = 0x0602B040
CAPDIR = os.path.join("assets", "SATURN_RAM_DUMP", "captures")


def dump(mcp, addr, length, fh, chunk=0x100):
    """Write `length` bytes from `addr` to fh.  The MCP read caps out well
    below 64 KB per call, so this chunks."""
    total = 0
    while total < length:
        n = min(chunk, length - total)
        txt = str(mcp.call("memory_read",
                           {"address": "0x%08X" % (addr + total), "length": n}))
        got = bytearray()
        for line in txt.splitlines():
            # NB: do NOT filter on "|" - the read format does not always emit
            # the ASCII column, and requiring it silently yields zero bytes.
            if ":" not in line or line.count(" ") < 8:
                continue
            for tok in line.split(":")[-1].split():
                if len(tok) == 2:
                    try:
                        got.append(int(tok, 16))
                    except ValueError:
                        pass
        if not got:
            break
        fh.write(got)
        total += len(got)
    return total


def regs(mcp):
    """R4/R5 are param_1 (src) and param_2 (dst) for FUN_0602b040."""
    r = mcp.call("registers_get", {"cpu": "master"})
    print(r)
    vals = {}
    for tok in str(r).replace("\n", " ").split():
        for sep in ("=", ":"):
            if sep in tok:
                k, _, v = tok.partition(sep)
                if v.lower().startswith("0x"):
                    try:
                        vals[k] = int(v, 16)
                    except ValueError:
                        pass
                break
    return vals


def main():
    mcp = Mcp()
    mcp.handshake()
    action = sys.argv[1] if len(sys.argv) > 1 else "status"
    if action == "watch":
        # The room loader runs on every ROOM change, which is far easier to
        # provoke than a module load (just walk through a door).  A write
        # watchpoint on the decoded-room buffer fires inside the writer, and
        # that is the code Phase 2 actually needs.
        addr = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x060AA00F
        print(mcp.call("set_debug_option", {"cpu_core": "interpreter"}))
        print(mcp.call("breakpoint_add",
                       {"address": "0x%08X" % addr, "type": "memory",
                        "write": True, "read": False, "cpu": "master"}))
        print("watching 0x%08X for writes - now change rooms" % addr)
        print(mcp.call("debug_status", {}))
        return
    if action == "bp":
        # A code breakpoint on the decoder's copy loop (0x602B098) is the clean
        # way to capture a real match: on entry R2 = back-reference source,
        # R5 = output pointer, R0 = length, R4 = input pointer.  That gives a
        # verified offset/length pair AND a real address to read the compressed
        # stream from, which the watchpoint route did not (its R4 came back
        # non-RAM and faulted).
        addr = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x0602B098
        print(mcp.call("set_debug_option", {"cpu_core": "interpreter"}))
        print(mcp.call("breakpoint_add",
                       {"address": "0x%08X" % addr, "type": "code", "cpu": "master"}))
        print("code breakpoint at 0x%08X - now change rooms" % addr)
        print(mcp.call("debug_status", {}))
        return
    if action == "arm":
        # A console reset reverts the core to the dynamic recompiler, which
        # silently ignores breakpoints, and leaves junk entries at 0x00000000.
        # Clear those, force the interpreter, then re-add.
        for _ in range(16):
            r = str(mcp.call("breakpoint_list", {}))
            if "00000000" not in r:
                break
            try:
                mcp.call("breakpoint_remove", {"address": "0x00000000"})
            except Exception:
                break
        print(mcp.call("set_debug_option", {"cpu_core": "interpreter", "history": "on"}))
        print(mcp.call("breakpoint_add", {"address": "0x%08X" % BP,
                                          "type": "code", "cpu": "master"}))
        print(mcp.call("debug_status", {}))
        print("armed: now START A NEW GAME so GAME1.PRG actually loads")
        return
    elif action == "catch":
        st = str(mcp.call("wait_for_stop", {}))
        v = regs(mcp)
        pc = v.get("PC")
        print("wait_for_stop: %s" % st.strip())
        if pc is None or not (BP <= pc <= BP + 0x94):
            # A console reset reverts to the dynamic recompiler, which ignores
            # breakpoints and leaves wait_for_stop returning a stale reason.
            # Refuse to report registers from an unrelated stop.  A memory
            # watchpoint stops *inside* the writer, so accept anywhere in the
            # decoder, not just its entry.
            print("NOT A HIT: PC=%s, expected inside %s..%s"
                  % (hex(pc) if pc else "?",
                     hex(BP), hex(BP + 0x94)))
            dbg = str(mcp.call("debug_status", {}))
            if "interpreter" not in dbg.lower():
                print("  -> core is NOT the interpreter; re-run with `arm`.")
            else:
                print("  -> core is the interpreter, so the decoder simply")
                print("     has not run since arming.")
            print(mcp.call("debug_status", {}))
            return
        print("HIT: PC=%s%s"
              % (hex(pc), "" if pc == BP else "  (inside the decoder)"))
        v = regs(mcp)
        src = v.get("R4")
        dst = v.get("R5")
        print("param_1 (src, R4) = %s" % (hex(src) if src else "?"))
        print("param_2 (dst, R5) = %s" % (hex(dst) if dst else "?"))
        # Capture the streams to disk NOW.  The emulator overwrites these
        # buffers on the next room load, on a save-state load, and on a
        # restart, so any analysis that depends on them has to happen against a
        # saved copy - which is exactly what went wrong once already.
        if src and dst:
            os.makedirs(CAPDIR, exist_ok=True)
            got = 0
            with open(os.path.join(CAPDIR, "src.bin"), "wb") as f:
                got = dump(mcp, src, 0x4000, f)
            print("saved %d source bytes -> %s/src.bin" % (got, CAPDIR))
            with open(os.path.join(CAPDIR, "dst.bin"), "wb") as f:
                got = dump(mcp, dst, 0x4000, f)
            print("saved %d output bytes -> %s/dst.bin" % (got, CAPDIR))
            with open(os.path.join(CAPDIR, "params.txt"), "w") as f:
                f.write("src=0x%08X\ndst=0x%08X\n" % (src, dst))
        for label, addr in (("src", src), ("dst", dst)):
            if addr:
                print("--- %s @ %s ---" % (label, hex(addr)))
                print(mcp.call("memory_read",
                              {"address": "0x%08X" % addr, "length": 64}))
        print("--- the two literals beside the stub ---")
        for a in (0x0602B04C, 0x0602B04E):
            print("0x%08X: %s" % (a, mcp.call("memory_read",
                                               {"address": "0x%08X" % a, "length": 8})))
    print("--- status ---")
    print(mcp.call("debug_status", {}))
    print("--- breakpoints ---")
    print(mcp.call("breakpoint_list", {}))


if __name__ == "__main__":
    main()
