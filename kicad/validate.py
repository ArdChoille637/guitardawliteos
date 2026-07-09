#!/usr/bin/env python3
"""Parse + schema-check the generated KiCad files (stdlib only).
Substitutes for kicad-cli when KiCad isn't installed: confirms the s-expr
parses, the netlist schema is intact, every net node references a real
component, and the symbol library has well-formed symbols with pins.
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))

def parse(s):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', s)
    pos = 0
    def rd():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == '(':
            lst = []
            while toks[pos] != ')': lst.append(rd())
            pos += 1; return lst
        return t.strip('"') if t.startswith('"') else t
    return rd()

def find(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]

def check_netlist(path):
    t = parse(open(path).read())
    assert t[0] == "export", "not a KiCad netlist"
    comps = find(find(t, "components")[0], "comp")
    nets = find(find(t, "nets")[0], "net")
    refs = set()
    for c in comps:
        ref = find(c, "ref")[0][1]; refs.add(ref)
        assert find(c, "footprint")[0][1], f"{ref} no footprint"
    nodecount = 0
    for n in nets:
        for nd in find(n, "node"):
            r = find(nd, "ref")[0][1]; nodecount += 1
            assert r in refs, f"net node references unknown comp {r}"
    print(f"netlist OK: {len(comps)} components, {len(nets)} nets, {nodecount} nodes, all refs resolve")
    return len(comps), len(nets)

def check_symbols(path):
    t = parse(open(path).read())
    assert t[0] == "kicad_symbol_lib", "not a symbol lib"
    syms = [x for x in find(t, "symbol")]
    for s in syms:
        name = s[1]
        pins = []
        for sub in find(s, "symbol"):
            pins += find(sub, "pin")
        assert pins, f"symbol {name} has no pins"
        for p in pins:
            assert find(p, "number") and find(p, "name"), f"{name} pin missing number/name"
        print(f"  symbol {name}: {len(pins)} pins OK")
    print(f"symbol lib OK: {len(syms)} symbols")
    return len(syms)

if __name__ == "__main__":
    try:
        check_netlist(os.path.join(HERE, "guitardawliteos.net"))
        check_symbols(os.path.join(HERE, "guitardawliteos.kicad_sym"))
        print("ALL CHECKS PASSED")
    except (AssertionError, Exception) as e:
        print("VALIDATION FAILED:", e); sys.exit(1)
