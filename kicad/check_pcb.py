#!/usr/bin/env python3
"""
Gate: does guitardawliteos.kicad_pcb still agree with gen.py?

gen.py's erc() proves the *netlist* is self-consistent, and validate.py checks
the emitted .net — but nothing checked that the BOARD matches either. That gap
let RV1 (the 100 k gain trimmer) go missing from the PCB without a single
check failing.

    python3 kicad/check_pcb.py [--rev A|B] [board.kicad_pcb]

Exits nonzero on any component or net disagreement.
"""
import argparse, importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pcb as R                                    # noqa: E402


def load_gen(rev):
    os.environ["GDAW_REV"] = rev
    spec = importlib.util.spec_from_file_location(f"gen_{rev}", os.path.join(HERE, "gen.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    nets = {k: {(r, str(p)) for r, p in v} for k, v in m.NETS.items()}
    return set(m.COMP), nets


def load_board(path):
    fps = R.load(path)
    comps = {f["ref"] for f in fps}
    nets = {}
    for f in fps:
        for p in f["pads"]:
            if p["net"]:
                nets.setdefault(p["net"], set()).add((f["ref"], p["num"]))
    return comps, nets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", default="A", choices=["A", "B"])
    ap.add_argument("board", nargs="?",
                    default=os.path.join(HERE, "guitardawliteos.kicad_pcb"))
    a = ap.parse_args()

    gc, gn = load_gen(a.rev)
    bc, bn = load_board(a.board)
    problems = []

    for ref in sorted(gc - bc):
        problems.append(f"component {ref} is in gen.py rev {a.rev} but NOT on the board")
    for ref in sorted(bc - gc):
        problems.append(f"component {ref} is on the board but NOT in gen.py rev {a.rev}")

    for net in sorted(set(gn) - set(bn)):
        problems.append(f"net {net} exists in gen.py but nowhere on the board")
    for net in sorted(set(bn) - set(gn)):
        problems.append(f"net {net} exists on the board but not in gen.py")

    for net in sorted(set(gn) & set(bn)):
        only_gen = gn[net] - bn[net]
        only_pcb = bn[net] - gn[net]
        if only_gen:
            problems.append(f"net {net}: missing on board {sorted(only_gen)}")
        if only_pcb:
            problems.append(f"net {net}: extra on board {sorted(only_pcb)}")

    # a net reduced to one node is a floating pin, worth calling out by name
    for net, nodes in sorted(bn.items()):
        if len(nodes) < 2:
            problems.append(f"net {net} has only ONE node on the board {sorted(nodes)} "
                            f"- floating pin")

    print(f"board : {a.board}")
    print(f"gen.py: rev {a.rev}  ({len(gc)} components, {len(gn)} nets)")
    print(f"board : {len(bc)} components, {len(bn)} nets")
    if problems:
        print(f"\nFAIL - {len(problems)} disagreement(s):")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    print("\nOK - board matches gen.py")


if __name__ == "__main__":
    main()
