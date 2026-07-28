#!/usr/bin/env python3
"""
GuitarDAWLiteOS — KiCad netlist + symbol generator (pure stdlib).

Emits, into this directory:
  guitardawliteos.net        KiCad netlist (Pcbnew "Import Netlist")
  guitardawliteos.kicad_sym  custom symbols: PCM1808, PCM5102A
  bom.csv                    bill of materials

Runs an ERC-lite (every pin on exactly one net, every net >=2 nodes, unique
refs, footprints present) and self-validates that the emitted s-expressions
parse (balanced).

Design = bare-chip carrier board: discrete OPA1662 guitar front-end +
bare PCM1808 ADC + bare PCM5102A DAC (+ AP2112 3.3V LDO) + Pi 5 40-pin header.
Datasheet-verified pinouts; matches docs/schematic.md (front-end SPICE-verified).
"""
import csv, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))

FP = dict(
    soic8="Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    tssop14="Package_SO:TSSOP-14_4.4x5mm_P0.65mm",
    tssop20="Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm",
    sot235="Package_TO_SOT_SMD:SOT-23-5",
    r0805="Resistor_SMD:R_0805_2012Metric",
    c0805="Capacitor_SMD:C_0805_2012Metric",
    c1206="Capacitor_SMD:C_1206_3216Metric",
    l0805="Inductor_SMD:L_0805_2012Metric",
    trim="Potentiometer_THT:Trimmer_Bourns_3296W_Vertical",
    hdr1x3="Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
    hdr2x20="Connector_PinHeader_2.54mm:PinHeader_2x20_P2.54mm_Vertical",
)

def Rr(v): return (v, FP["r0805"], {1: "1", 2: "2"})
def Cc(v, big=False): return (v, FP["c1206"] if big else FP["c0805"], {1: "1", 2: "2"})

# ref -> (value, footprint, {pin#: pinname})
COMP = {
    # --- guitar front-end (discrete) ---
    "U1": ("OPA1662", FP["soic8"], {1:"OUTA",2:"-INA",3:"+INA",4:"V-",5:"+INB",6:"-INB",7:"OUTB",8:"V+"}),
    "J1": ("Guitar_6.35mm", FP["hdr1x3"], {1:"TIP",2:"RING",3:"SLEEVE"}),
    "Cin": Cc("0.1uF"), "Rbias": Rr("1M"),
    "Rg": Rr("10k"), "Rf": Rr("10k"), "RV1": ("100k", FP["trim"], {1:"1",2:"W",3:"3"}),
    "Cout": Cc("1uF"), "Rs": Rr("1k"), "Ca": Cc("1nF"), "Cr": Cc("1uF"),
    "R1": Rr("10k"), "R2": Rr("10k"), "C1": Cc("10uF", True), "C8": Cc("0.1uF"),
    # --- ADC: PCM1808 (bare, TSSOP-14) ---
    "U2": ("PCM1808", FP["tssop14"], {1:"VREF",2:"AGND",3:"VCC",4:"VDD",5:"DGND",6:"SCKI",
            7:"LRCK",8:"BCK",9:"DOUT",10:"MD0",11:"MD1",12:"FMT",13:"VINL",14:"VINR"}),
    "FB1": ("FerriteBead", FP["l0805"], {1:"1",2:"2"}),
    "C2": Cc("0.1uF"), "C3": Cc("10uF", True),   # VCC
    "C4": Cc("0.1uF"), "C5": Cc("10uF", True),   # VDD
    "C6": Cc("0.1uF"), "C7": Cc("10uF", True),   # VREF
    # --- DAC: PCM5102A (bare, TSSOP-20) ---
    "U3": ("PCM5102A", FP["tssop20"], {1:"CPVDD",2:"CAPP",3:"CPGND",4:"CAPM",5:"VNEG",6:"OUTL",
            7:"OUTR",8:"AVDD",9:"AGND",10:"DEMP",11:"FLT",12:"SCK",13:"BCK",14:"DIN",15:"LRCK",
            16:"FMT",17:"XSMT",18:"LDOO",19:"DGND",20:"DVDD"}),
    "C9": Cc("0.1uF"),                # CPVDD
    "C10": Cc("2.2uF"),               # CAPP-CAPM flying cap
    "C11": Cc("2.2uF"),               # VNEG
    "C12": Cc("0.1uF"), "C13": Cc("10uF", True),  # AVDD
    "C14": Cc("0.1uF"),               # LDOO
    "C15": Cc("0.1uF"), "C16": Cc("10uF", True),  # DVDD
    "Rol": Rr("470"), "Col": Cc("2.2nF"),   # OUTL filter
    "Ror": Rr("470"), "Cor": Cc("2.2nF"),   # OUTR filter
    "J2": ("Line_out", FP["hdr1x3"], {1:"L",2:"R",3:"G"}),
    # --- power ---
    "U4": ("AP2112K-3.3", FP["sot235"], {1:"VIN",2:"GND",3:"EN",4:"NC",5:"VOUT"}),
    "Cli": Cc("1uF"), "Clo": Cc("1uF"),
    # --- Pi 5 header (only used pins listed) ---
    # Pi 3V3 pin is NOT used: this board derives its own 3.3V via U4 (LDO off 5V).
    "J3": ("RPi5_GPIO", FP["hdr2x20"], {2:"5V",6:"GND",7:"GPIO4",12:"GPIO18",
            35:"GPIO19",38:"GPIO20",40:"GPIO21"}),
}

# net name -> list of (ref, pin#)
NETS = {
    "+5V":   [("J3",2),("U1",8),("C8",1),("U4",1),("U4",3),("FB1",1),("Cli",1),("R1",1)],
    "VCC_A": [("FB1",2),("U2",3),("C2",1),("C3",1)],            # PCM1808 analog 5V (post-ferrite)
    "+3V3":  [("U4",5),("Clo",1),("U2",4),("C4",1),("C5",1),
              ("U3",1),("C9",1),("U3",8),("C12",1),("C13",1),
              ("U3",20),("C15",1),("C16",1),("U3",17)],          # XSMT=H -> unmute
    "GND":   [("J3",6),("U1",4),("U4",2),("U4",4),("Cli",2),("Clo",2),
              ("J1",2),("J1",3),                                  # ring(NC) + sleeve
              ("U2",2),("U2",5),("U2",10),("U2",11),("U2",12),    # AGND,DGND,MD0,MD1,FMT
              ("C2",2),("C3",2),("C4",2),("C5",2),("C6",2),("C7",2),
              ("R2",2),("C1",2),("C8",2),("Ca",2),("Cr",2),
              ("U3",3),("U3",9),("U3",10),("U3",11),("U3",12),("U3",16),("U3",19),  # CPGND,AGND,DEMP,FLT,SCK,FMT,DGND
              ("C9",2),("C11",2),("C12",2),("C13",2),("C14",2),("C15",2),("C16",2),
              ("Col",2),("Cor",2),("J2",3)],
    "VREF":  [("U2",1),("C6",1),("C7",1)],                       # decouple-only (not driven)
    # I2S0 (RP1 alt a2) + MCLK
    "MCLK":    [("J3",7),("U2",6)],
    "BCLK":    [("J3",12),("U2",8),("U3",13)],
    "LRCLK":   [("J3",35),("U2",7),("U3",15)],
    "CAPDAT":  [("U2",9),("J3",38)],
    "PLAYDAT": [("J3",40),("U3",14)],
    # front-end signal chain
    "GIN":   [("J1",1),("Cin",1)],
    "NBI":   [("Cin",2),("Rbias",2),("U1",3)],          # +INA
    "VBIAS": [("R1",2),("R2",1),("C1",1),("Rbias",1),("Rg",2)],
    "BUF":   [("U1",1),("U1",2),("U1",5)],              # OUTA -> -INA (fb) + +INB
    "NINV":  [("U1",6),("Rg",1),("Rf",1)],              # -INB, gain set
    "MIDF":  [("Rf",2),("RV1",1)],                      # Rf 10k in series with RV1 (rheostat)
    "FEO":   [("U1",7),("RV1",2),("RV1",3),("Cout",1)], # OUTB + feedback + output
    "AAO":   [("Cout",2),("Rs",1)],
    "VINL":  [("Rs",2),("Ca",1),("U2",13)],
    "VINR":  [("U2",14),("Cr",1)],                      # Cr 1uF -> GND (AC-ground)
    # charge pump + DAC analog out
    "CPP":   [("U3",2),("C10",1)],
    "CPM":   [("U3",4),("C10",2)],
    "VNEG":  [("U3",5),("C11",1)],
    "LDOO":  [("U3",18),("C14",1)],
    "OUTL":  [("U3",6),("Rol",1)],
    "OUTR":  [("U3",7),("Ror",1)],
    "LOUT":  [("Rol",2),("Col",1),("J2",1)],
    "ROUT":  [("Ror",2),("Cor",1),("J2",2)],
}

# ---------------------------------------------------------------------------
# REV-B SECTION  (retro-deck-design.md §5: rev A stays unchanged; rev-B changes
# live behind a clearly-marked section.)  Select with GDAW_REV=B.
#
# Rev B is the 2026-07-10 pivot, sourced from docs/adc-hookup.md,
# docs/hardware-review.md and docs/claim-verification.md:
#   * PCM1808 is the I2S BUS MASTER (MD0/MD1 = H); the Pi is I2S slave.
#   * MCLK comes from an Arduino Nano ESP32 (D2 = ESP32-S3 GPIO5), NOT from the
#     Pi: clk_i2s is claimed by BCLK, so GPCLK0-on-GPIO4 is a dead route
#     (ruled out 2026-07-09).  A ~33R series resistor damps the clock line
#     (M2 bench review B2/A3).
#   * The 3.3 V rail is taken from Pi J8 pin 1 (user decision 2026-07-28,
#     matching the bench); the onboard AP2112K LDO is therefore not populated.
#
# NOT encoded here (this is still a BARE-CHIP carrier board, not the module
# bench build): the bench drops the FB1/C2/C3 VCC filter and leaves VINR open
# because the CJMCU-1808 module AC-terminates it internally.  A real PCB wants
# both, so they stay.  See docs/pinmap-plan-revB.md §1 item 4.
#
# Rev A remains the learning-vehicle board and regenerates byte-identically.
# ---------------------------------------------------------------------------
REV = os.environ.get("GDAW_REV", "A").upper()

def apply_rev_b():
    """Mutate COMP/NETS in place into the rev-B topology."""
    # -- new parts: Nano MCLK header + series damping resistor --
    COMP["J4"] = ("Nano_ESP32_MCLK", FP["hdr1x3"], {1: "D2", 2: "GND", 3: "GND"})
    COMP["R_mclk"] = Rr("33")

    def drop(net, *nodes):
        NETS[net] = [n for n in NETS[net] if n not in nodes]

    # -- 3.3 V now arrives on J8 pin 1; the LDO is not populated --
    COMP["J3"][2][1] = "3V3"
    for ref in ("U4", "Cli", "Clo"):
        del COMP[ref]
    drop("+5V", ("U4", 1), ("U4", 3), ("Cli", 1))
    drop("GND", ("U4", 2), ("U4", 4), ("Cli", 2), ("Clo", 2))
    drop("+3V3", ("U4", 5), ("Clo", 1))
    NETS["+3V3"].insert(0, ("J3", 1))

    # -- ADC becomes bus master: MD0/MD1 pulled high (were strapped to GND) --
    drop("GND", ("U2", 10), ("U2", 11))
    NETS["+3V3"] += [("U2", 10), ("U2", 11)]

    # -- MCLK: Nano D2 -> 33R -> SCKI.  The Pi no longer sources it. --
    NETS["MCLK"] = [("R_mclk", 2), ("U2", 6)]
    NETS["MCLK_SRC"] = [("J4", 1), ("R_mclk", 1)]
    del COMP["J3"][2][7]                      # GPIO4 no longer used
    NETS["GND"] += [("J4", 2), ("J4", 3)]     # both Nano grounds to the star

if REV == "B":
    apply_rev_b()
elif REV != "A":
    sys.exit(f"GDAW_REV must be A or B, got {REV!r}")

def erc():
    errs, warns = [], []
    for ref,(val,fp,pins) in COMP.items():
        if not fp: errs.append(f"{ref}: missing footprint")
    seen = {}
    for net,nodes in NETS.items():
        if len(nodes) < 2: warns.append(f"net {net}: only {len(nodes)} node(s)")
        for ref,pin in nodes:
            if ref not in COMP: errs.append(f"net {net}: unknown ref {ref}"); continue
            if pin not in COMP[ref][2]: errs.append(f"net {net}: {ref} has no pin {pin}")
            seen.setdefault((ref,pin),[]).append(net)
    for (ref,pin),nl in seen.items():
        if len(nl) > 1: errs.append(f"{ref} pin {pin} on multiple nets: {nl}")
    for ref,(val,fp,pins) in COMP.items():
        for pin in pins:
            if (ref,pin) not in seen: errs.append(f"{ref} pin {pin} ({pins[pin]}) UNCONNECTED")
    return errs, warns

def sexpr_ok(text):
    depth=0; instr=False; esc=False
    for ch in text:
        if esc: esc=False; continue
        if ch=='\\' and instr: esc=True; continue
        if ch=='"': instr=not instr; continue
        if instr: continue
        if ch=='(': depth+=1
        elif ch==')':
            depth-=1
            if depth<0: return False
    return depth==0 and not instr

def emit_netlist():
    # rev A must regenerate byte-identically: no rev suffix, original date
    date = "2026-06-29" if REV == "A" else "2026-07-28"
    tool = "GuitarDAWLiteOS gen.py" if REV == "A" else f"GuitarDAWLiteOS gen.py rev {REV}"
    L=['(export (version "E")',
       f'  (design (source "guitardawliteos/kicad/gen.py") (date "{date}")'
       f' (tool "{tool}"))',
       '  (components']
    for ref in sorted(COMP):
        val,fp,_=COMP[ref]
        L.append(f'    (comp (ref "{ref}") (value "{val}") (footprint "{fp}"))')
    L.append('  )')
    L.append('  (nets')
    for i,(net,nodes) in enumerate(NETS.items(),1):
        line=f'    (net (code "{i}") (name "{net}")'
        for ref,pin in nodes:
            line+=f' (node (ref "{ref}") (pin "{pin}"))'
        L.append(line+')')
    L.append('  ))')
    return "\n".join(L)+"\n"

def emit_symbols():
    def sym(name, pins, fp):
        n=len(pins); half=(n+1)//2
        left=pins[:half]; right=pins[half:]
        h=max(len(left),len(right))*2.54+5.08; w=25.4
        s=[f'  (symbol "{name}" (in_bom yes) (on_board yes)',
           f'    (property "Reference" "U" (at 0 {h/2+2.54:.2f} 0))',
           f'    (property "Value" "{name}" (at 0 {-h/2-2.54:.2f} 0))',
           f'    (property "Footprint" "{fp}" (at 0 0 0) (effects (hide yes)))',
           f'    (symbol "{name}_0_1"',
           f'      (rectangle (start {-w/2:.2f} {h/2:.2f}) (end {w/2:.2f} {-h/2:.2f})'
           f' (stroke (width 0.254) (type default)) (fill (type background))))',
           f'    (symbol "{name}_1_1"']
        for i,(num,pname) in enumerate(left):
            y=h/2-2.54-i*2.54
            s.append(f'      (pin passive line (at {-w/2-2.54:.2f} {y:.2f} 0) (length 2.54)'
                     f' (name "{pname}" (effects (font (size 1.27 1.27))))'
                     f' (number "{num}" (effects (font (size 1.27 1.27)))))')
        for i,(num,pname) in enumerate(right):
            y=h/2-2.54-i*2.54
            s.append(f'      (pin passive line (at {w/2+2.54:.2f} {y:.2f} 180) (length 2.54)'
                     f' (name "{pname}" (effects (font (size 1.27 1.27))))'
                     f' (number "{num}" (effects (font (size 1.27 1.27)))))')
        s.append('    ))')
        return "\n".join(s)
    def P(comp): return [(str(n),nm) for n,nm in sorted(COMP[comp][2].items())]
    out=['(kicad_symbol_lib (version 20211014) (generator guitardawliteos_gen)']
    out.append(sym("PCM1808", P("U2"), FP["tssop14"]))
    out.append(sym("PCM5102A", P("U3"), FP["tssop20"]))
    out.append(')')
    return "\n".join(out)+"\n"

def main():
    errs,warns=erc()
    for w in warns: print("WARN:",w)
    if errs:
        print("ERC FAILED:")
        for e in errs: print("  -",e)
        sys.exit(1)
    net=emit_netlist(); syms=emit_symbols()
    assert sexpr_ok(net), "netlist s-expr unbalanced"
    assert sexpr_ok(syms), "symbol s-expr unbalanced"
    tag = "" if REV == "A" else "-revB"
    open(os.path.join(HERE,f"guitardawliteos{tag}.net"),"w").write(net)
    open(os.path.join(HERE,f"guitardawliteos{tag}.kicad_sym"),"w").write(syms)
    with open(os.path.join(HERE,f"bom{tag}.csv"),"w",newline="") as f:
        w=csv.writer(f); w.writerow(("Ref","Value","Footprint"))
        for ref in sorted(COMP): w.writerow((ref,COMP[ref][0],COMP[ref][1]))
    print(f"OK: rev {REV} — {len(COMP)} components, {len(NETS)} nets. ERC clean. s-expr valid.")
    print(f"Wrote guitardawliteos{tag}.net, guitardawliteos{tag}.kicad_sym, bom{tag}.csv")

if __name__=="__main__":
    main()
