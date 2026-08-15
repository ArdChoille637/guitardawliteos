# Raspberry Pi 5 (2 GB) Bare-Metal Deep Dive — Hardware, PCIe/RP1 Physics, Code Review, Lowest-Level Analysis

**Date:** 2026-08-12
**Method:** 3 parallel research agents (Pi 5/BCM2712 hardware · RP1/PCIe bridge · Circle-source analysis at the project pin `22722a76`) + an independent review of `src/` + an adversarial verification pass over the load-bearing claims. Circle citations below were read from a clone checked out at **exactly `22722a76`** (the project pin). Primary sources: `raspberrypi/documentation` AsciiDoc, `raspberrypi/linux` device trees (rpi-6.12.y/6.18.y), TF-A `plat/rpi5`, a real Pi 5 `lspci -vvv` dump, Micron/JEDEC LPDDR4X data, Circle source. Anything resting on weaker evidence is flagged **[UNVERIFIED]**.

**This board:** Pi 5 **2 GB**, revision `b04170` → BCM2712 **D0 stepping** (all 2 GB boards are D0). The staged `sdcard/` already carries `bcm2712d0-rpi-5-b.dtb` + `overlays/bcm2712d0.dtbo`, which D0 requires. ✓

---

## 0. What this research changes for the project (read this first)

1. **New real-time constraint found — the DMA-restart deadline.** Circle's RP1 DMA is *software-cyclic*: every LLI is flagged `LLI_LAST|LLI_VALID`, so the DW-AXI channel **disables itself after every 333 µs block** and the IRQ handler must re-enable it (`dmachannel-rp1.cpp:786-798`). The only slack between block-complete and software restart is the I2S FIFO — ≤ 16 samples ≈ **≤ ~330 µs @ 48 kHz, less depending on fill**. Rule going forward: **nothing on core 0 may mask IRQs for more than ~100 µs** (headroom factor ~3), or the stream underruns in a way no buffer size can hide. Today's kernel honors this by design (core 0 = audio only); M4/M5 code must keep honoring it.
2. **FIQ is confirmed unavailable — stop considering it.** On Pi 5 the GIC-400's Group 0 belongs to the secure world, which is owned by the EEPROM firmware's resident BL31 (PSCI provider at `0x0–0x7FFFF`). Circle's `ConnectFIQ` panics on Pi 5 (`"FIQ not supported, ARM stub not found"`, `interruptgic.cpp:331-346`); its README lists Pi 5 as "IRQ only". Getting FIQ would mean shipping a custom `armstub8-2712.bin` (e.g. TF-A) — and per §6 it would not reduce audio latency anyway.
3. **The latency knob is chunk size, not "going lower".** The ~1.0 ms monitor latency is >99 % chunk/double-buffer structure, <1 % software. `GDAW_CHUNK_WORDS` 32→8 gives ~170–250 µs monitor latency for ~12 k IRQs/s (~1.5–6 % of core 0) *without leaving Circle or touching topology*. A from-scratch kernel (1.5–3 months) buys **zero** additional latency. See the ladder in §6.5.
4. **MMIO reads to RP1 cost ~1 µs each** (non-posted PCIe round trip; posted writes ~50 ns). Circle's INTA demux spends 2 such reads per interrupt + a handful in the DMA handler → ~5–10 µs per audio IRQ, ≈ up to ~6 % of core 0 at today's 6 kHz combined IRQ rate. Acceptable; but future hot-path code should **never poll RP1 registers when a DRAM-side flag will do**.
5. **D0-stepping deltas are real but handled.** Same peripheral bases as C1, but: debug-UART IRQ moved (GIC **SPI 121 → 120**), pinctrl register layout compacted, GPIO bank widths shrunk, some DMA40 DREQs renumbered. Circle reads the stepping register (`0x10_0150_4004`: C1=0x21, D0=0x30) and the firmware auto-applies `bcm2712d0.dtbo`. Never hard-code C1 IRQ/DREQ values in project code.
6. **Cache maintenance around RP1 DMA is mandatory, not defensive.** No BCM2712 DMA master is cache-coherent with the A76s (no `dma-coherent` anywhere in the DT). Circle's per-chunk clean/invalidate (~2–3 lines of 64 B) costs tens of ns — correct as-is. Making buffers uncached to "avoid" it would be a large net loss (§6.4).
7. **ASPM stays off.** Linux runs the RP1 link with ASPM L1 enabled (µs-class exit stalls); Circle never sets the ASPM bits — the right call for audio determinism. If `pciex4_reset=0` is ever used, explicitly clear LNKCTL ASPM on both ends (bootloader state undocumented).
8. **Code review (§5): no correctness bugs found in `src/`.** Two worthwhile improvements: a benign peak-meter stat race, and busy-wait idling on cores 1–3 that turns three 2.4 GHz A76s into heaters while the bus is dead (WFE/longer sleeps would cut heat on this D0 board).

---

## 1. BCM2712 SoC — what the kernel runs on

- **16 nm** application processor; quad **Cortex-A76** (Armv8.2 class) @ 2.4 GHz (idle floor 1.5 GHz, DVFS by firmware); 12-core VideoCore VII @ 960 MHz; official memory bandwidth "up to 17 GB/s". Foundry (TSMC 16FFC per press) [UNVERIFIED — RPi says only "16nm"].
- **Cache hierarchy** (Linux DT, from the A76/DSU TRMs): per-core L1 **64 KB I + 64 KB D**; per-core private unified **L2 512 KB**; shared DSU **L3 2 MB**. **64-byte lines at every level** — the alignment granule `ringbuffer.h` already uses (`alignas(64)`), and the correct padding unit for anything shared between the four cores.
- **ISA features present** (verified from Pi 5 `/proc/cpuinfo`): `aes pmull sha1 sha2 crc32 atomics …` — **LSE atomics are available**; `aarch64-elf-gcc 16.1` emits them for the `__atomic` builtins the ring uses (already noted in `ringbuffer.h`), and they behave far better than LL/SC under contention on A76.
- **Coherency:** all 4 cores + L3 form one **inner-shareable hardware-coherent domain**. `DMB ISH` + acquire/release atomics are sufficient for core-to-core queues — the ring's model is exactly right. **DMA masters are NOT coherent** (§3.6, §4).
- **GIC-400 (GICv2)**: GICD `0x10_7FFF_9000`, GICC `0x10_7FFF_A000`. SGIs 0–15 = IPIs, PPIs 16–31 per-core, SPIs shared. (Numbering note: "GIC SPI n" = INTID n+32 — the PCIe INTA below is *SPI 229* = INTID 261; Circle's constant is the SPI form.)
- **Timers:** ARM generic timer at **54 MHz** (crystal-direct; TF-A pins `SYS_COUNTER_FREQ_IN_TICKS = 54000000`) → 18.5 ns resolution, the right timestamp source for latency instrumentation. The legacy 1 MHz BCM2835-style system timer also exists at `0x10_7C00_3000`. Circle's `CTimer` on Pi 5 runs off the generic timer (`USE_PHYSICAL_COUNTER` mandatory; CNTP IRQ = PPI 14).
- **Core IDs live in MPIDR Aff1** on Pi 5 (cores at Aff1 = 0..3); Circle's `_start_secondary` shifts MPIDR right 8 (`startup64.S:105-107`). Classic Pi 4 → Pi 5 porting trap; relevant if any project asm ever reads MPIDR directly.
- **40-bit physical address map** (TF-A `PLAT_PHY_ADDR_SPACE_SIZE = 1<<40`):

| Arm PA | Contents |
|---|---|
| `0x0…0x7FFF_FFFF` | DRAM (2 GB board: fully populated, nothing above) — first 512 KB = **BL31 reservation, do not touch** |
| `0x10_0000_0000` + off | SoC bus: IOMMUs (`0x…5280` IOMMU5 for RP1), DMA32/40 (`0x10_0001_0000/0600`), PCIe RCs (`0x10_0010/11/12_0000`), MIP MSI translators (`0x10_0013_0000/1000`), SDIO1 **`0x10_00FF_F000`** (the SD card), SDIO2 `0x10_0110_0000` (Wi-Fi), V3D `0x10_0200_0000`, classic peripherals `0x10_7C00_0000…` (mailbox `0x10_7C01_3880`, HVS, HDMI, AON GPIO, PL011 debug UART `0x10_7D00_1000`), GIC `0x10_7FFF_9000` |
| `0x1B_8000_0000` | PCIe1 outbound window (external FPC connector) |
| `0x1F_0000_0000` | PCIe2 outbound window (**RP1** — all 40-pin-header I/O incl. our I2S at `0x1F_000A_4000`) |

Circle's `memorymap64.h` mirrors this exactly; the project's `build.sh` guard constant `MEM_KERNEL_START = 0x80000` is **confirmed** against `memorymap64.h:47`.

## 2. The D0 stepping and the 2 GB variant

- **What D0 is:** the cost-optimized respin introduced with the 2 GB board (Aug 2024). Die ~32.5 % smaller than C1; the removed "dark silicon" is unused Broadcom set-top-box logic (on-die GENET-class Ethernet MAC, extra UARTs, SDIO0, unused USB blocks — the DWC2 OTG *survives*). Functionally identical per Eben Upton; **~30 % lower idle power** (2 GB D0 ≈ 2.4 W idle) and 5–10 °C cooler under load — directly relevant to our fan-trip-at-45 °C thermal policy: the D0 board gives extra margin.
- **Address space grew, not shrank:** D0 supports >8 GB memories (the 16 GB Pi 5 is D0-only). Any claim that D0 cut addressing is wrong in direction.
- **Bare-metal-visible deltas** (authoritative source: `bcm2712d0-rpi-5-b.dts` in `raspberrypi/linux`): same peripheral base addresses; **debug-UART (UART10) IRQ GIC SPI 121→120**; main pinctrl register block compacted (size 0x30→0x20, different layout compatible string); GPIO bank widths 32+22→32+4 (main), 17+6→15+6 (AON); boot-SPI + HDMI-audio DMA DREQs renumbered; VC7 compatible string changes. The firmware auto-applies `overlays/bcm2712d0.dtbo` on D0 silicon — which is why that file is boot-critical in `sdcard/` (build.sh already verifies it's staged).
- **Runtime stepping detection:** SoC stepping register PA `0x10_0150_4004` — high 16 bits `0x2712`, low byte **C1=0x21, D0=0x30** (Circle `machineinfo.cpp` reads this).
- **2 GB memory map:** DRAM is exactly `0x0–0x7FFF_FFFF`. Every byte is 32-bit addressable and inside every DMA window — no high-memory special cases anywhere. Carve-outs: `0x0–0x7FFFF` BL31 (`atf@0`, no-map); `gpu_mem` is meaningless on Pi 5 (no start.elf memory split; only `total_mem` exists). The 4 MB capture ring + 16 MB `KERNEL_MAX_SIZE` region is a rounding error in 2 GB.
- **SDRAM:** LPDDR4X-4267, single ×32 package = **two independent 16-bit channels** (8 Gb per channel — sets refresh timing class), 17.07 GB/s theoretical peak.

## 3. The memory subsystem — physics a real-time programmer can use

- **LPDDR4X signaling:** I/O rail VDDQ **0.6 V** (vs 1.1 V LPDDR4 — the "X"), core VDD2 1.1 V/VDD1 1.8 V; interface clock 2133 MHz, data on both edges → **4267 MT/s/pin**.
- **Refresh — the worst-case stall mechanism:** per channel (8 Gb class, ≤85 °C): average refresh interval **tREFI = 3.904 µs**, all-bank refresh **tRFCab = 280 ns** (per-bank 140 ns). So a channel is dead ~280 ns every ~3.9 µs worst case; an access colliding with REFab + a row conflict (precharge+activate, tens of ns) + HVS scanout bursts (1080p60×32bpp ≈ 0.5 GB/s continuous) ⇒ **budget ~0.5–1 µs of jitter for any single DRAM access in the IRQ path**. At a 333 µs chunk cadence this is absorbed trivially; it becomes a design input only if chunk size drops below ~8 words. Above 85 °C DRAM must derate refresh (halve/quarter tREFI) — firmware thermal throttling keeps us out of that regime.
- **Measured latency (Pi 5, tinymembench):** L2-resident ~8 ns; ~L3 ~55 ns; DRAM random read **~119 ns** (64 MB block). Sustained single-core memcpy ≈ 4.8 GB/s, memset ≈ 13.7 GB/s. Our full-duplex stream (384 KB/s/direction) is ~4 orders of magnitude below saturation — **contention/jitter, never bandwidth, is the audio concern.**

## 4. Boot chain — everything that runs before `main()`

| Stage | Where | Replaceable? |
|---|---|---|
| **BootROM** (on the VideoCore VPU) | Mask ROM in BCM2712 | **No.** Enforces RPi signatures on the EEPROM stages. The absolute floor. |
| **EEPROM bootloader** (`bootcode` → `bootsys`/`bootmain`) | 2 MB Winbond W25Q16 SPI NOR; A/B partitioned, tryboot rollback | Reflashable, effectively firmware. **`start.elf` no longer exists on Pi 5** — the firmware is embedded here (verified: official boot-flow doc + no `start_2712.elf` in `raspberrypi/firmware`). Initializes clocks + LPDDR4X, parses `config.txt` itself, loads DTB (auto-applies `bcm2712d0.dtbo` on D0), patches `/chosen` + `memory@0`, loads the kernel, **brings up the RP1 PCIe link** (it needs RP1 for USB/network boot), then by default **resets the ×4 RC** before handoff (`pciex4_reset=1`). |
| **Resident EL3 / armstub** | RPi-built TF-A **BL31** in EEPROM, resident at `0x0–0x7FFFF`, PSCI provider | Replaceable via `armstub=`/`armstub8-2712.bin` (upstream TF-A `PLAT=rpi5` works) — the only route to EL3/FIQ ownership. Note: BCM2712 has no secure memory controller, so "secure" DRAM isn't actually protected. |
| **`config.txt`** | SD card | Fully ours. Project-relevant: `kernel_address=0x80000` (without it the firmware loads arm64 images at 0x20_0000 [forum-verified only]), `device_tree_address=0x2000000` (keeps the DTB clear of our 16 MB kernel region), `os_check=0` if a payload ever trips the compat check, `enable_rp1_uart=1` for firmware-initialized early UART, `pciex4_reset=0` for the no-Circle path (§6.3). |
| **`kernel_2712.img`** | SD card, entered at **EL2**, core 0 only | **Fully ours.** |

**State at kernel entry:** core 0 in **EL2** (non-secure), MMU/caches off, cores 1–3 parked behind **PSCI** (released via SMC `CPU_ON` = `0xC4000003`, target core in Aff1 — `multicore.cpp:141-205`; *not* the Pi 4 spin-table), DTB in RAM, RP1 firmware already running (loaded by the bootloader over I2C into RP1 SRAM — the endpoint side of the PCIe link is alive before our first instruction; only the RC side is ours to bring up).

**What Circle does before `CKernel` exists** (all verified at `22722a76`):
1. `_start` (`startup64.S:79-97`): reads CurrentEL → **drops EL2→EL1** (U-Boot macro: grants EL1 the generic timers, enables FP/SIMD via `cpacr_el1`, `HCR_EL2.RW=1`, `eret` to EL1t with a known-good `SCTLR_EL1` — MMU/caches still off); sets exception + kernel stacks; `vbar_el1 = VectorTable`; `b sysinit`.
2. `sysinit()` (`sysinit.cpp:327-398`): **clears BSS in C**, checks `_end` vs `MEM_KERNEL_END` and **silently halts** if `KERNEL_MAX_SIZE` is too small (the exact failure the `build.sh` post-link guard exists to catch *before* it ships), constructs `CMemorySystem` (§below) + `CMachineInfo` + `CInterruptSystem`, and — **Pi 5 only — `CSouthbridge`: the entire PCIe RC + RP1 bring-up runs here, before `main()`**. Then static ctors, then `main()`.
3. **MMU/caches** (`memory64.cpp`, `translationtable64.cpp`): 64 KB granule; all RAM **Normal WB inner-shareable** (AF=1, PXN above `_etext`); the MMIO windows (`0x10_…`, `0x1B_…`, `0x1F_…`) **Device-nGnRE outer-shareable**; the coherent/mailbox region Device-nGnRnE; `SCTLR_EL1 |= I|C|M`. So `main()` runs at EL1t, MMU+caches+NEON on. The audio ring in BSS is cached WB inner-shareable memory — correct (§6.4).
4. **Memory layout as built** (with the project's `KERNEL_MAX_SIZE=0x1000000`): kernel @ `0x8_0000`; `MEM_KERNEL_END` = `0x108_0000` → 4×128 KB core stacks, then 4×32 KB exception stacks, then the 4 MB Device-nGnRnE coherent region, heap after. This is precisely why the 4 MB BSS ring forced the KERNEL_MAX_SIZE bump: **core stacks sit immediately after the kernel image** — overflow puts stacks inside data with zero warning.

**PCIe RC bring-up Circle performs** (`bcmpciehostbridge.cpp:405-703`, condensed): RESCAL de-assert → bridge soft reset (BCM2712 reset ctrl bit 44) → SerDes un-IDDQ → **MDIO-programmed PLL for the 54 MHz xosc refclk** → `MISC_CTRL` (SCB_ACCESS_EN, MAX_BURST_SIZE=256, RCB_MPS_MODE) → **inbound RC_BAR2**: all DRAM at PCIe `0x10_0000_0000` + UBUS remap → UBUS error defang (`AXI_READ_ERROR_DATA=0xFFFFFFFF`: dead-link reads return all-1s instead of SError) → QoS → force Gen2 → outbound `0x1F_0000_0000 → PCIe 0x0` → optional MIP MSI plumbing → **PERST# de-assert + mandatory 100 ms CEM wait** → link-up poll → enumerate the single endpoint (bridge bus 1, program RP1 BARs, COMMAND.MEM|MASTER) → hook **INTA = GIC SPI 229** for the RP1 second-level interrupt demux.

## 5. Current code review (`src/`, build, tests)

**Verdict: no correctness bugs found.** The one load-bearing concurrency assumption was re-verified against Circle source at the pin, and the design decisions match the platform facts uncovered above.

**Verified correct:**
- **The monitor FIFO's non-atomic indices are safe.** Both `GetChunk` and `PutChunk` run in RP1-DMA-completion IRQ context on core 0 **under the device's `m_SpinLock`** (`i2ssoundbasedevice-rp1.cpp:565-635`: lock at 567, TX path 604, RX path 622). TX and RX complete near-simultaneously (same bit clock, both DMA channels share RP1 IRQ 40) and are serviced in one INTA pass. The `audioengine.h` context contract is accurate as written.
- **`ringbuffer.h` seqlock logic is sound.** Traced the resync arithmetic (`cursor = reserve − CAP + CAP/4`) against mid-write states (reserve ahead of commit): no path yields a bogus window; the `nHead <= cursor` guard catches resync-ahead-of-commit. Word-wise RELAXED atomics + RELEASE/ACQUIRE fences match the A76's inner-shareable coherency model; 64 B `alignas` matches the real line size at every cache level (§1).
- **`GetChunk` always returns the full count** (silence-padded) — honors the driver contract where returning short permanently stops the stream (`Cancel`+`StopI2S`).
- **`SignExtend24` is correct** for the low-24-bit wire format and well-defined on the GCC toolchain actually used.
- **`build.sh`'s post-link `_end` guard** checks the same condition `sysinit()` silently halts on — and its hardcoded `0x80000` base is confirmed against `memorymap64.h:47`. The poisoned-archive / stale-object cleaning logic addresses real Config.mk-DEFINE layout hazards (`ARM_ALLOW_MULTI_CORE` changes `CSpinLock` layout; `KERNEL_MAX_SIZE` changes the memory map).
- **Host tests** are unusually strong: deterministic overrun/resync coverage, exact value==absolute-index integrity invariant, anti-vacuous-pass floors, forced-lap napper, bounded-drain hang detection, plain-`-O2`-on-ARM64 + TSan double run.

**Findings (minor, none blocking):**
1. **Peak-meter stat race** (`cores.cpp:97-101` vs `:152`): core 1's check-then-set on `nPeakAbs24` races core 3's read-then-clear — a peak landing between core 3's `StatGet` and `StatSet(0)` is lost; a stale window's peak can also win the check. Telemetry-only today; if the meter ever drives a clip LED or gain logic, replace with `__atomic_exchange_n` (read-and-clear) on core 3 + `__atomic_compare_exchange_n` max-update on core 1.
2. **Busy-wait idling, cores 1–3** (`cores.cpp:41,77,127`, `:145`): `CTimer::SimpleMsDelay` is a calibrated busy loop → with a dead bus (today's bench state) three A76s spin at full clock doing nothing. On this D0 board that's still several watts of avoidable heat feeding the 45 °C fan trip. Cheap fix: `WaitForEvent()`/longer sleeps while `Available()==0`; core 3's 2 s cadence could be `wfe`-based outright.
3. **Dead code** (`ringbuffer.h:138-142`): the `nAvail == 0` check is unreachable after the `nHead <= cursor` return above it.
4. **Implicit invariant worth an assert** (`audioengine.cpp:42`): `nMonitorDropped` counts `nDrop/2` frames — correct only while pushes/pops are whole even-word chunks (true today: 32-word chunks). A `static_assert(GDAW_CHUNK_WORDS % 2 == 0)` in `config.h` (or a comment at the divide) pins it.
5. **Doc nit carried forward:** several docs refer to the RP1 register aperture as a "64 MB window". The RP1 BAR1 peripheral aperture is **4 MB** (inside a ~4 GB outbound window) — see §7.3. Nothing in code depends on the figure.

## 6. Lowest-possible-level analysis

### 6.1 The floor, precisely
BootROM (mask ROM, signature-enforcing) < EEPROM bootloader (reflashable ≈ firmware) < **BL31/armstub (replaceable — the EL3/FIQ gate)** < **EL2 entry (already ours — Circle voluntarily drops to EL1)** < Circle kernel. "Lowest possible level" on a Pi 5 therefore means: *own code from EL2 down, with an optional custom EL3 stub*. Nothing below the EEPROM handoff is reachable, and the EEPROM/BootROM layers do work we cannot replicate (LPDDR4X training, RP1 firmware load over I2C, PCIe EP bring-up on the RP1 side).

### 6.2 The IRQ hot path as built — counted, layer by layer
DW-AXI DMAC block-complete → RP1 INTC (`0x1F_0010_8000`) → PCIe INTA message → **GIC SPI 229** → `IRQStub` (saves GPRs **+ all 32 SIMD q-regs** — GCC ≥ 12 forces `SAVE_VFP_REGS_ON_IRQ`; ≈ 1.5 KB stack traffic round-trip) → `CInterruptSystem` (GICC_IAR read, table dispatch) → `CSouthbridge` demux (**2 × ~1 µs non-posted INTSTAT reads across PCIe**) → `CDMAChannelRP1::InterruptHandler` (static spinlock; per-channel status read + intclear write + **channel re-enable** — the §0.1 deadline) → device `m_SpinLock` → **`GetChunk`/`PutChunk`** → unwind (cache clean/invalidate of the 128 B chunk, LLI revalidate, RP1 IACK write, GICC_EOIR, full restore, `eret`).

**Total: ~8 nested calls (3+ indirect), 2 IRQ-level spinlocks, ~6–10 MMIO ops of which ~4–6 are non-posted PCIe reads → ~2–5 µs software+bus overhead per 333 µs period (~1 % of core 0)** [order-of-magnitude; instrument with `CNTPCT_EL0` deltas — 54 MHz, 18.5 ns resolution — to pin down].

### 6.3 A no-Circle kernel, concretely
Must own: startup asm (~100 lines; optionally *stay at EL2* — free, and skips the whole demotion macro), FP/SIMD enable (mandatory before any GCC-12+ C), MMU/caches (a static single-map version of Circle's ~400 lines), GIC-400 init + dispatch (~100 lines minimal), UART, generic-timer reads, and **RP1 access** — the fork:
- **`pciex4_reset=0`** (official semantics verified): the bootloader's trained link + windows are inherited; code starts poking `0x1F_…` directly. Caveats: dump-and-verify the inherited window/BAR layout matches expectations, and explicitly clear ASPM in LNKCTL on both ends (bootloader state undocumented) [UNVERIFIED which exact inbound-window config is inherited — must be checked on hardware].
- **Full own RC bring-up**: replicate §4's sequence against a publicly undocumented Broadcom RC (Circle's 1319-line port of Linux `pcie-brcmstb.c` is the de-facto reference). Highest-risk single chunk of a from-scratch build.

Plus the I2S + DMAC drivers themselves (Circle's are 677 + 837 lines, both ports of documented-IP Linux drivers; the RP1 datasheet covers the register maps). Realistic effort to feature-parity with today's kernel: **1.5–3 months** solo; the own-RC path adds 2–6 weeks and real bring-up risk. Existing non-Circle Pi 5 bare metal is embryonic (a 2-commit Rust UART/GPIO project; forum UART examples) — **Circle is effectively the mature open Pi 5 bare-metal environment**, and its RP1/PCIe code is what others port from.

### 6.4 Latency truths (the honest part)
- Structure dominates: RX chunk fill (≤333 µs) + monitor FIFO + TX double-buffer slot (≤333 µs) ⇒ **~2–3 chunks ≈ 0.67–1.0 ms** — exactly `config.h`'s budget. Software is ~2–5 µs of that.
- **FIQ would not help** (µs-scale dispatch shaved, ms-scale structure untouched) — and it's gated behind a custom EL3 stub anyway.
- **Polling instead of IRQs** (a core spinning on DMA/FIFO status) buys ~2 µs and removes the restart-deadline risk, but burns a core and adds PCIe read traffic *to the link the audio DMA shares*. Only interesting below ~32-sample chunks.
- **Cached WB ring + explicit per-chunk cache ops is the right design**: uncached buffers would slow the CPU-side copy/mix loops 10–50×, vs tens of ns for 2–3 line clean/invalidates.
- **The knob that works: `GDAW_CHUNK_WORDS` 32 → 8** (4 frames, 83 µs chunks): monitor latency ~170–250 µs at ~12 k IRQs/s ≈ 1.5–6 % of core 0. The restart deadline (§0.1) is *unchanged* by chunk size — it depends on IRQ latency only. Floor of this driver architecture: a few frames; below that, Circle's `USE_I2S_SOUND_IRQ` PIO mode (FIFO-threshold interrupts, stereo-only — fine for us) reaches sub-100 µs round trips **still inside Circle**.

### 6.5 Recommendation ladder (48 kHz looper)

| Rung | What | Effort | Monitor latency | Verdict |
|---|---|---|---|---|
| (a) | Circle as-is (32-word chunks) | 0 | ~0.7–1.0 ms | Below the ~1.5 ms of sound traveling 50 cm from an amp. **Fine — current choice.** |
| (b) | Circle + surgical: chunk→8; optionally own `RP1_IRQ_DMA` handler with true free-running LLI ring (no per-block restart → §0.1 deadline class eliminated + 1 IRQ/chunk saved) | hours → ~1 week | ~170–250 µs; ~100 µs with the handler | **Best value.** Do chunk-shrink first, measure with CNTPCT, only then consider the handler. |
| (c) | Freestanding kernel, `pciex4_reset=0`, own minimal RP1 I2S/DMAC/UART | 1.5–3 months | same as (b) | Buys understanding + a tiny image, **zero latency**. Legitimate as a learning goal — arguably on-theme for this project — but not an audio-quality argument. |
| (d) | (c) + full own PCIe RC | +2–6 weeks, high risk | same | Only removes a config.txt line. Not rational unless boot-time PCIe control is a requirement in itself. |

## 7. The PCIe bridge in depth (BCM2712 ↔ RP1)

### 7.1 Link parameters (verified against a real Pi 5 `lspci -vvv`)
**PCIe Gen 2.0 ×4, trained and running at 5 GT/s ×4** (`LnkCap`/`LnkSta` both show it). Raw 20 Gbit/s/direction; **8b/10b costs 20 %** → 4 Gbit/s payload per lane = **2 GB/s per direction, full duplex** (matches RPi's own announcement math). The BCM2712 RCs are Gen3-capable silicon (the external ×1 port overclocks to 8 GT/s), but **RP1's endpoint advertises 5 GT/s max** — the LTSSM can never negotiate higher; both Linux (`max-link-speed = <2>`) and Circle (`#define PCIE_GEN 2`) additionally pin the RC. Negotiated **MPS 256 B / MRRS 512 B**; TLP+DLLP framing brings streaming efficiency to ≈ 92–93 % → ~1.85 GB/s practical ceiling. Our audio uses ~0.05 % of it including descriptor fetches.

### 7.2 Physical layer — the actual physics
- **Topology:** 4 lanes × 2 unidirectional differential pairs + one differential 100 MHz-class REFCLK pair; no shared bus, no data clock wire.
- **Signaling:** NRZ at 5 GT/s, **UI = 200 ps**; TX differential swing 0.8–1.2 Vpp with **de-emphasis** (−3.5/−6 dB, training-selected) pre-compensating the channel's low-pass response; Gen2 RX sensitivity ~120 mV class [Base-Spec-derived figures].
- **8b/10b's three jobs:** bounded running disparity → **DC balance** → AC coupling works (spec DC-block caps 75–200 nF per TX leg); bounded run length (≤5 UI) → guaranteed edge density for **CDR**; K-code control symbols (COM alignment, STP/SDP/END framing, SKP for clock compensation). Payload additionally **scrambled** (LFSR x¹⁶+x⁵+x⁴+x³+1) to spread EMI.
- **SerDes/CDR:** TX serializes off a PLL multiplying REFCLK; RX has **no clock wire** — CDR locks onto the transition-rich stream and recovers clock+data; elastic buffers absorb clock offset via SKP insert/delete (±300 ppm budget).
- **REFCLK on this board:** **common-clock architecture** (`CommClk+` both ends), derived from the 54 MHz crystal via an MDIO-programmed PHY PLL (the `pcie_munge_pll` seven-register sequence in both Linux and Circle). **No spread-spectrum** (`CFG_QUIRK_NO_SSC` on BCM2712).
- **Gen2 has no adaptive equalization** (that's Gen3's Recovery.Equalization) — link tuning here is static de-emphasis + optional fixed RX CTLE. With BCM2712 and RP1 ~2 cm apart, insertion loss at the 2.5 GHz Nyquist is tiny; the channel is trivial by CEM standards (why the uncertified Gen3 overclock works on the external port). Board stack-up/impedance targets are not public [UNVERIFIED].
- **Training:** LTSSM Detect (receiver sensed electrically via common-mode RC) → Polling (TS1/TS2, bit/symbol lock) → Configuration (width ×4, lane deskew via COM/SKP) → L0 @ 2.5 GT/s → autonomous speed change to 5 GT/s.

### 7.3 Address translation — both directions

| Direction | Window | Meaning |
|---|---|---|
| **Outbound (CPU→RP1)** | ARM `0x1F_0000_0000` → PCIe `0x0` (~4 GB, 32-bit non-prefetch) | Inside it: **BAR1 = 4 MB** @ `0x1F_0000_0000` → RP1 internal `0x4000_0000` (**the** peripheral aperture — the "64 MB" figure in circulation is wrong); BAR2 = 64 KB @ `0x1F_0040_0000` → RP1 shared SRAM; BAR0 = 16 KB @ `0x1F_0041_0000` → MSI-X tables (unused by Circle) |
| **Inbound (RP1→DRAM)** | PCIe `0x10_0000_0000` → ARM `0x0` (64 GB, RC_BAR2 + UBUS remap) | **RP1 DMA masters address DRAM at `0x10_0000_0000 + PA`** — Circle's `PTR_TO_DMA` encodes exactly this. MSI doorbell parked at PCIe `0xFF_FFFF_F000` → MIP0 (must sit outside the RAM window). On the 2 GB board all RAM is <4 GB → no DMA-mask complications at all. |

RP1's own 40-bit fabric sees its peripherals at `0xC0_4000_0000…` (Circle `VIRT_TO_RP1_BUS`), its M3s see them at `0x4000_0000` through a 32→40-bit shim. Note the RP1 inbound path also traverses **IOMMU5** (`0x10_0000_5280`, `dma-iova-offset 0x10_00000000`) under Linux; bare metal inherits it in pass-through [UNVERIFIED whether the firmware leaves IOMMU5 in bypass — Circle works without touching it, which implies yes].

### 7.4 Protocol costs — the asymmetry that shapes driver design
- **Writes are posted** (fire-and-forget): a 32-bit store to Device-mapped RP1 space costs ~fabric-acceptance time; sustained measured rate ≈ 20 MHz (~**50 ns/write**) — wire time is only ~12 ns; the rest is RC/EP pipeline.
- **Reads are non-posted** (stall until CplD returns): A76 → fabric → RC → MRd TLP → RP1 AXI → APB peripheral → CplD back. **~1 µs order** per register read (a CM5 userspace loop measured ~3 µs incl. overhead); >95 % of it is controller/fabric traversal on both dies, not wire physics. **Driver design rule: read RP1 registers as rarely as possible; prefer DRAM-side state.**
- **Ordering guarantees we rely on:** posted writes stay ordered; a read flushes all prior posted writes (the "write config, read back once" idiom); **an MSI/INTx message cannot pass the DMA data posted before it** → "IRQ fired ⇒ samples already in DRAM" holds with no extra barriers on the RP1 side.
- **Flow control:** ACK/NAK + LCRC retry makes the link lossless (errors become latency, never loss); credit-based FC per class (P/NP/Cpl) means congestion backpressures rather than drops. BCM2712 adds a **TC→AXI-QoS elevation scheme specifically for RP1's real-time streams** (`brcm,vdm-qos-map` on the ×4 port only): RP1 raises the traffic class of urgent FIFO traffic via vendor messages (partially broken on C1, fixed with chicken bits on **D0**). At our ~0 % utilization this is belt-and-braces.

### 7.5 RP1 internals (what's on the far side)
Two Cortex-M3s (≈200 MHz [strongly corroborated, datasheet not directly readable]) + 64 KB shared SRAM running RPi-signed firmware (loaded by the bootloader **over I2C**, then it brings up the PCIe endpoint — i.e., the far side of the link is alive before our code runs); PLLs incl. **`pll_audio_core` 1.536 GHz** (the audio clock tree that Spike B established can't be tapped for MCLK while I2S runs); peripheral set: 6 PL011 UARTs, 9 SPI, 7 I2C, **3 DesignWare I2S** (I2S0/1 with DMA handshakes at `0x…A0000/A4000` — ours is I2S1 @ `0x1F_000A_4000`), PWM + ΔΣ audio-out + PDM-in, 3 GPIO banks + RIO, 12-bit ADC, Cadence GEM GbE, 2× DWC3 USB3, MIPI CSI/DSI + ISP-FE, an RP2040-style PIO block, and the **Synopsys DW-AXI DMAC: 8 channels, 64 handshake interfaces, 128-bit data path, LLI descriptors fetched from DRAM across the link**. Interrupts: **61 lines**, delivered either as **MSI-X** (posted write to `0xFF_FFFF_F000` → MIP0 → GIC SPIs 128–191, edge, with an IACK re-arm handshake for level semantics — Linux's path) or as **legacy INTA → GIC SPI 229 with a 2-read INTSTAT demux** (Circle's path — simpler, ~2 µs extra per interrupt). Datasheet rates the DMAC ≈ 2 Gb/s/channel writing (posted) but only ~500–600 Mb/s reading (non-posted round trips) — another face of §7.4's asymmetry; our playback direction uses a rounding error of it.

### 7.6 Failure modes and gotchas (bare-metal field guide)
- **Before RC init**, any access to `0x1F_…` = AXI/UBUS error → **SError-class abort** (the classic first-attempt crash). **After RC init**, Circle/Linux deliberately defang errors: dead-link reads return `0xFFFFFFFF` silently — *check for all-1s when probing*.
- **`pciex4_reset=0`** (verified official semantics): firmware skips the pre-handoff reset of the ×4 RC so bare metal inherits the trained link + windows. Companion: `enable_rp1_uart=1`. Circle needs neither (full re-init itself) — the project's "stock config.txt" rule stands.
- **ASPM:** Linux runs L1 enabled on this link (exit latency <2 µs advertised); **Circle leaves ASPM off — keep it that way** for jitter-free audio. Under `pciex4_reset=0`, clear LNKCTL ASPM explicitly on both ends.
- **Don't touch RP1's reset casually:** yanking RP1_RUN kills the link + every peripheral behind it; recovery needs the firmware-load handshake redone.
- **The `pcie-32bit-dma` overlay breaks RP1 I2S** under Linux (shrinks the inbound window) — a documented trap; irrelevant to Circle but good to know when dual-booting for debugging.
- **MPS mismatch:** if ever programming config space by hand, set DevCtl MPS 256/MRRS 512 (and RC `MAX_BURST_SIZE`) — the 128 B default halves bulk efficiency.

### 7.7 The other PCIe controller(s)
BCM2712 instantiates **three** BRCMSTB RCs: `pcie0` @ `0x10_0010_0000` (unused on Pi 5 B), `pcie1` @ `0x10_0011_0000` = the **external ×1 FPC connector** (windows at `0x1B_…`, INTA SPI 219, MSI-X via MIP1 → SPIs 247–254; Gen2 default, Gen3 via `pciex1_gen=3` officially uncertified; enabled by HAT+ EEPROM or `dtparam=pciex1`), `pcie2` @ `0x10_0012_0000` = **RP1** (everything above). Circle: `ARM_PCIE_HOST_BASE 0x1000120000` / `ARM_PCIE_EXT_HOST_BASE 0x1000110000`; reset-controller bits 44/43. Relevant later: an NVMe HAT for session storage would ride `pcie1` and is architecturally independent of the audio path — same isolation argument as `[CORR-2]`'s "SD is on the BCM2712".

## 8. Power & thermal (operational facts for the rig)

- **PMIC:** Renesas/Dialog **DA9091 "Gilmour"** — 8 SMPS incl. a quad-phase 20 A core buck; ADCs readable via mailbox (`pmic_read_adc` equivalent); drives the power button + RTC. PMIC_INT is watched by the VPU firmware (why the AON GPIO block is off-limits as an interrupt controller).
- **Supply:** official 27 W (5.1 V/5 A) PD. On a non-5 A supply the firmware caps downstream USB at 600 mA (vs 1.6 A) and disables USB boot by default — and **the fan header draws from the same budget pool**. Our bench's 45 W PD supply is comfortably above all of it; the kernel's under-voltage handler (`SystemThrottledHandler`) is the right monitor.
- **Thermal:** firmware throttles the A76s progressively **80→85 °C**, hard-limits at **85 °C** (`temp_limit`, unraisable); no Pi 3-style 60 °C soft limit exists on Pi 5. DT critical trip 110 °C. **Active Cooler:** FAN_PWM = RP1 GPIO45 (which is why `gpiofanpin=45` works), tach GPIO29; the firmware itself runs a fan curve (50 °C→30 %, 60→50 %, 67.5→70 %, 75→100 %, 5 °C hysteresis) *even with no OS cooperation* — our `socmaxtemp=45` policy simply trips earlier than the firmware would. The D0 board runs 5–10 °C cooler than C1 under identical load; measured 2 GB idle ≈ 2.4 W. (§5 finding 2 — busy-wait idle — is the main avoidable heat source in today's kernel.)
- **DVFS:** all real clock/voltage control lives in the VPU firmware, requested over the mailbox (ARM=3, CORE=4 clock IDs). `fast=true` in `cmdline.txt` (Circle's CPUSpeed) pins the ARM clock high — the right call: it removes DVFS transitions as an IRQ-latency jitter source.

## 9. Verification status of this document

The three research reports cross-agree on every overlapping fact (EL2 entry; 54 MHz generic timer; PSCI core release; non-coherent DMA; INTA = GIC SPI 229; `0x80000` load via Circle's `kernel_address=`; D0 stepping identification). Claims verified against primary sources are stated plainly; weaker evidence is marked **[UNVERIFIED]** inline. The most consequential unverified items: the firmware's default kernel load address without `kernel_address=` (forum-only; moot — we set it), the inherited PCIe window layout under `pciex4_reset=0` (moot — Circle re-inits), exact single-read PCIe RTT (bracketed ~1–3 µs; instrumentable with CNTPCT on the bench), and IOMMU5's bare-metal bypass state (implied by Circle working without touching it).

## Sources (representative; per-section citations inline)

- **Raspberry Pi official** (via `raspberrypi/documentation` AsciiDoc): BCM2712 processor doc; EEPROM boot flow ("Differences on Raspberry Pi 5"); bootloader EEPROM + A/B; config.txt boot/memory/overclocking docs (`kernel_address`, `os_check`, `enable_rp1_uart`, **`pciex4_reset`**, `temp_limit`, fan curve); revision codes (`b04170`); power supplies; PCIe FPC doc; RP1 I/O controller page; RP1 announcement (link-bandwidth math); Pi 5 product brief RP-008348; RP1 peripherals datasheet RP-008370 [not directly fetchable this session; cited via the docs repo, forum quotes and drivers].
- **Device trees / kernel** (`raspberrypi/linux`): `bcm2712.dtsi`, `bcm2712-ds.dtsi`, `bcm2712-rpi-5-b.dts`, **`bcm2712d0-rpi-5-b.dts`** (the authoritative D0 delta), `rp1.dtsi`, `dt-bindings/mfd/rp1.h`, `pcie-brcmstb.c`, `irq-bcm2712-mip.c`, `drivers/mfd/rp1.c`.
- **TF-A**: `docs/plat/rpi5.rst`, `plat/rpi/rpi5/include/platform_def.h` (EL2 entry, BL31 @ `0x1000–0x80000`, 54 MHz counter, 40-bit PA).
- **Circle @ `22722a76`**: `lib/startup64.S`, `lib/sysinit.cpp`, `include/circle/memorymap64.h`, `lib/memory64.cpp`, `lib/translationtable64.cpp`, `lib/exceptionstub64.S`, `lib/interruptgic.cpp`, `lib/multicore.cpp`, `lib/bcmpciehostbridge.cpp`, `lib/southbridge.cpp`, `lib/dmachannel-rp1.cpp`, `lib/sound/i2ssoundbasedevice-rp1.cpp`, `lib/timer.cpp`, `lib/synchronize64.cpp`, `lib/spinlock.cpp`, `include/circle/bcm2711.h`, `include/circle/bcm2712.h`, `include/circle/rp1int.h`, `boot/README`.
- **Measurements/community**: linuxhw/LsPCI Pi 5 `lspci -vvv` dump (LnkSta ×4 Gen2, MPS/MRRS, MSI-X 61, ASPM state, CommClk); geerlingguy/sbc-reviews #21 (tinymembench ~119 ns DRAM, power draws); hzeller/rpi-gpio-dma-demo (~20 MHz posted-write rate); Jeff Geerling + Tom's Hardware D0 delid coverage (~32.5 % smaller die, idle power); SkatterBencher #77 (clock tree, crystal swap); G33KatWork RP1-Reverse-Engineering (firmware load over I2C); forum threads t=368402 (RP1 bare-metal access, aborts, `pciex4_reset=0`), t=373222 (Pi 5 boot process), t=390556 (RP1 DMA read/write asymmetry, quoting the datasheet), CM5 link-latency measurement (~3 µs/read); Micron MT53E LPDDR4X datasheet (tREFI/tRFC); JEDEC JESD209-4/-4-1; TF-A rpi5 docs; rpi-eeprom repo.
