# This is a MODIFIED copy of OpenVCL

**This repository is not OpenVCL.** It is a derivative work, maintained for
[TyraX](https://github.com/doctorspider42/tyra-editor), and it carries changes the
upstream project has neither seen nor accepted. Do not report problems with it to
upstream, and do not assume its behaviour matches theirs.

This notice exists to satisfy section 6 (*Attribution Rights*) of the Academic Free
License v2.0, under which OpenVCL is licensed: every copyright, patent and trademark
notice from the original source is retained, and this file is the prominent notice
that the Original Work has been modified.

## Upstream

* Project: **OpenVCL** — <https://github.com/ps2dev/openvcl>
* Forked at commit `a5867c3daf03828806ee966aca4116622da3f671` (v0.4.0)
* Originally written by **Jesper Svennevid** and **Daniel Collin**
* Current upstream maintainer: **Francisco Javier Trujillo Mata**
* Licence: **Academic Free License v2.0** — see [`LICENSE`](LICENSE), unchanged

The names above are recorded as attribution, as the licence requires. Per section 4
(*Exclusions From License Grant*) they do **not** endorse this copy, and nothing here
should be read as their approval.

## What was changed, and why

TyraX compiles a PS2 engine's 25 VU1/VU0 microprograms. It needs an assembler it can
ship, because the alternative — Sony's `vcl` — comes with no licence at all, which is
what makes a publishable toolchain image impossible. Getting there took one
correctness fix and a set of density flags; the full measurements, including the ones
that failed, live in the TyraX repo under `docs/toolchain-image.md`.

**Correctness**

* `CLIPw` with an implied `w` component is accepted instead of rejected. Upstream
  rejects the operand *and still writes a complete output file* with the `clipw`
  missing and the `fcand` that reads its clip flags kept — silently emitting a program
  that tests clip flags nobody set.

**Density — six flags, every one off by default**

| flag | effect |
|---|---|
| `--schedule-flag-readers` | MAC/CLIP flag readers take part in list scheduling instead of ending the segment |
| `--fmac-interlock` | a VF-to-VF wait costs cycles, not emitted `nop` words — the FMAC pipeline interlocks |
| `--sce-latencies` | flag visibility 4 → 1 cycle; an integer load's result readable at issue+3 |
| `--emit-delay-fillers` | offer the instruction scheduled before a branch as its delay-slot filler |
| `--branch-interlock` | no padding word before a branch whose operand came from a load or a flag reader |
| `--branch-bubble-on-dependency` | emit the pre-branch bubble only when the row above actually produces one of the branch's operands |

Every latency constant is calibrated against the minimum distances Sony's `vcl`
demonstrates in its own output over those 25 programs — black-box observation of a
known-good reference, no disassembly of anything. With all six, the engine's resident
VU1 program set needs **2040** instructions against Sony's 2042, and the rendered
frame is pixel-identical in PCSX2.

**Register allocation — six more flags, every one off by default**

The six above make the code denser; these make it fit. TyraX's VU authoring layer
generates 45 microprograms and Sony's `vcl` compiles all 45; this copy compiled 23,
every failure being *Register allocation ran out of registers*.

| flag | effect |
|---|---|
| `--trim-uncarried-ranges` | rebuild a live range from the alias's own accesses, when the value provably dies at its last use inside one iteration |
| `--coalesce-float-writes` | give a float write the register its own previous value already sits in, when that value is dead from the write on |
| `--sink-loads` | move a load down the token list to just before the value it loads is first read, and re-derive the allocator's timeline from list position |
| `--sink-loads-across-stores` | let a sinking load pass a store through a *different* base register — an aliasing assumption, and the one SCE's `vcl` makes in its own output |
| `--sink-loads-into-loops` | let a sinking load pass one loop header, so a preamble load whose only readers are inside the batch loop stops pinning a register across the whole program |
| `--sink-loads-past-branches` | let a sinking load cross a branch to a point every path out of its old position reaches, so a value read only past a two-arm block stops being live across it |

With all six on top of the six above, **all 45** compile and assemble, all clean
under TyraX's loop-carry checker, and the engine's resident VU1 set needs **2008**
instructions against Sony's 2028 for the same ten programs.

The last of the 45, `vu_script3_tce_cl`, is what the sixth flag exists for. Five
GIF-tag quadwords are loaded in its preamble and read only by the `sq`s that build
the packet header, but a two-arm branch picking the destination address sits in
between, so their live ranges spanned the whole program and the peak was 33
simultaneously live float ranges against 31 registers. Both arms rejoin at
`setDestAddr:` before the first of those `sq`s, which is a point every path
reaches, and landing the loads there takes the peak to 28. The post-dominance that
licenses the move is tested during the sink pass's own forward walk, from one
count of how many branches name each label, rather than from a control-flow graph
the allocator does not build.

Carrying a load over a branch reorders the block it lands in, and over the 45 it
was worth about two words in either direction in programs that had registers to
spare and gained nothing for it. So the flag is not applied speculatively: the
allocation runs first exactly as it would with the flag off, and the flag only
gets its turn when that runs out of registers. All 44 programs that compiled
without it are byte-identical with it on, as are the engine's 25.

## Known defects in this copy

* `stapip_as_is_c`-style programs: a value loaded above a batch loop can be reused
  inside it (TyraX works around it in its own sources). The obvious explanation —
  liveness ignoring the back edge — is **wrong**: narrow the register pool and this
  copy refuses cleanly instead of clobbering. No minimal reproducer yet.
* One TyraX microprogram, `stapip_clip_c`, is still miscompiled by this copy.

Upstream's own test suite (419 tests) passes unmodified.
