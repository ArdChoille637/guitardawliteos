#!/usr/bin/env python3
"""
Regression gate for gen.py's two revisions.  Pure stdlib; run from anywhere:

    python3 kicad/test_rev.py

Rev A  — must regenerate BYTE-IDENTICALLY (it is the learning-vehicle board;
         retro-deck-design.md §5 says it stays unchanged).
Rev B  — must satisfy the 2026-07-10 pivot structurally.

The rev-B checks assert against gen.py's imported NETS/COMP dicts, NOT against
greps of the emitted netlist: the .net carries only refs and pin NUMBERS, never
pin names, so a grep for "MD0.*GND" or "J3.7" can never match and would pass
even on an un-migrated rev-A file.
"""
import hashlib, importlib.util, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "gen.py")


def load(rev):
    """Import gen.py fresh with GDAW_REV set, returning the module."""
    os.environ["GDAW_REV"] = rev
    spec = importlib.util.spec_from_file_location(f"gen_{rev}", GEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def check(results, label, ok):
    results.append((label, bool(ok)))


def rev_a_byte_identical(results):
    """Regenerate rev A and confirm git sees no change to its artifacts."""
    tracked = ["guitardawliteos.net", "guitardawliteos.kicad_sym", "bom.csv"]
    before = {f: sha(os.path.join(HERE, f)) for f in tracked}
    env = dict(os.environ, GDAW_REV="A")
    r = subprocess.run([sys.executable, GEN], cwd=HERE, env=env,
                       capture_output=True, text=True)
    check(results, "rev A: gen.py exits 0", r.returncode == 0)
    for f in tracked:
        check(results, f"rev A: {f} byte-identical",
              sha(os.path.join(HERE, f)) == before[f])


def rev_b_structure(results):
    g = load("B")
    N, C = g.NETS, g.COMP

    # --- PCM1808 is the I2S bus master: MD0/MD1 pulled high, not grounded ---
    check(results, "rev B: MD0 (U2.10) off GND", ("U2", 10) not in N["GND"])
    check(results, "rev B: MD1 (U2.11) off GND", ("U2", 11) not in N["GND"])
    check(results, "rev B: MD0 on +3V3", ("U2", 10) in N["+3V3"])
    check(results, "rev B: MD1 on +3V3", ("U2", 11) in N["+3V3"])

    # --- MCLK comes from the Nano, never from the Pi (GPIO4 is a dead route) ---
    check(results, "rev B: J3.7/GPIO4 on no net",
          not any(("J3", 7) in nodes for nodes in N.values()))
    check(results, "rev B: GPIO4 dropped from J3", 7 not in C["J3"][2])
    check(results, "rev B: MCLK = R_mclk.2 -> U2.6 (SCKI)",
          sorted(N["MCLK"]) == sorted([("R_mclk", 2), ("U2", 6)]))
    check(results, "rev B: MCLK_SRC = J4.D2 -> R_mclk.1",
          sorted(N["MCLK_SRC"]) == sorted([("J4", 1), ("R_mclk", 1)]))
    check(results, "rev B: 33R clock damping resistor present",
          "R_mclk" in C and C["R_mclk"][0] == "33")
    check(results, "rev B: both Nano grounds on the star",
          ("J4", 2) in N["GND"] and ("J4", 3) in N["GND"])

    # --- 3.3 V from Pi J8 pin 1; onboard LDO not populated ---
    check(results, "rev B: +3V3 fed from J3 pin 1", ("J3", 1) in N["+3V3"])
    check(results, "rev B: J3 pin 1 named 3V3", C["J3"][2].get(1) == "3V3")
    check(results, "rev B: AP2112K LDO absent",
          not {"U4", "Cli", "Clo"} & set(C))
    check(results, "rev B: no LDO nodes stranded on a net",
          not any(r in ("U4", "Cli", "Clo")
                  for nodes in N.values() for r, _ in nodes))

    # --- things the pivot must NOT have broken ---
    check(results, "rev B: XSMT (U3.17) still high = DAC unmuted",
          ("U3", 17) in N["+3V3"])
    check(results, "rev B: BCLK endpoints unchanged",
          sorted(N["BCLK"]) == sorted([("J3", 12), ("U2", 8), ("U3", 13)]))
    check(results, "rev B: LRCLK endpoints unchanged",
          sorted(N["LRCLK"]) == sorted([("J3", 35), ("U2", 7), ("U3", 15)]))
    check(results, "rev B: VDD (U2.4) on 3V3, never 5V",
          ("U2", 4) in N["+3V3"] and ("U2", 4) not in N["+5V"])

    errs, _ = g.erc()
    check(results, "rev B: ERC clean", not errs)
    if errs:
        for e in errs:
            print("    ERC:", e)


def main():
    results = []
    rev_a_byte_identical(results)
    rev_b_structure(results)
    for label, ok in results:
        print(("  PASS  " if ok else "  FAIL  ") + label)
    failed = [l for l, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    if failed:
        print("FAILED:", *failed, sep="\n  - ")
        sys.exit(1)


if __name__ == "__main__":
    main()
