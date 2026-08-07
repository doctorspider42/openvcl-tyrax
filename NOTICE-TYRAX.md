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

**Dead code — one more flag, off by default**

The twelve above made the generated programs *fit* and made them *dense*, and they
were still bigger than Sony's: 9506 instructions against 9264 over the 45, larger
in 34 of them. The first thing measured was whether the six allocator flags had
caused it. They had not. Over the 23 programs that compile with and without them,
the ten density flags alone come to 4064 and all sixteen come to 4064 — the same
number. (The four sink flags cost 30 on their own and `--trim-uncarried-ranges`
with `--coalesce-float-writes` buys 8 back, so the six net to zero.) The gap was
there before any of that work.

Counting rows and slots says where it was. Over the 45, Sony's output is 9240
instruction rows holding 10910 non-`nop` slots; this copy's was 9486 rows holding
**11222**. Both fit 1.18 real operations in a row. This copy was not
scheduling worse — it was scheduling 312 instructions Sony's `vcl` never emitted,
and the histogram named them: 93 more `loi`, 65 more `lq`, and the `muli`/`addi`/
`mulw`/`addw` that consume them.

They are dead. TyraX's VU authoring layer emits a whole four-component constant
vector when the body reads two of its components, and an `lq` for every quadword
the description names whether the body touches it or not. Sony's `vcl` deletes
both. `--drop-dead-writes` does the same:

| flag | effect |
|---|---|
| `--drop-dead-writes` | delete a token whose register destination is read nowhere in the program, field by field, and whose MAC/CLIP/I/Q/P/R/ACC writes nothing observes either |

The liveness in it is the weakest one that finds them: a value is live if *any*
token anywhere reads that component, with no control flow in the analysis at all.
Nothing here deletes a write that is dead on one path and live on another — that
needs dominance the register allocator does not build. Only aliases are
candidates; a literal `VF05` an author named by number may be an interface with
something this compiler cannot see. A name any `in_vf`/`out_vf`/`--exitm`
directive mentions is live by declaration, as is a hardware resource any
`out_hw_*` names. Stores, `xgkick`, the auto-incrementing loads, branches,
anything with a delay slot, and any token carrying a label are never candidates.

The flag writes are what make it worth having. Every FMAC instruction writes the
MAC flags, so a dead `add.z` is only deletable if that write is unobserved; the
test is global first (a program with no flag reader at all cannot observe any of
them) and positional second (walk forward: a reader says live, another writer says
dead, and a label, a branch, a directive or the end of the program says live,
because a wrong answer here is a miscompile). It iterates to a fixed point:
deleting `add.z k0, vf00, i` is what makes the `loi` above it dead.

With it on top of the twelve, over the 45 generated programs:

| | Sony `vcl` | this copy, sixteen flags | plus `--drop-dead-writes` |
|---|---|---|---|
| instructions | 9264 | 9506 (+242) | **9308** (+44) |
| smaller / larger / equal | — | 4 / 34 / 7 | 17 / 24 / 4 |

and the engine's resident VU1 set drops from 2008 to **1988** against Sony's 2028.
Not one of the 70 programs gets bigger: 42 of the 45 shrink and 3 are unchanged,
6 of the 10 resident ones shrink and 4 are unchanged. All 45 still compile and
assemble, all 25 engine programs still compile, both corpora are clean under
TyraX's loop-carry checker, the multiset of stores and `xgkick`s each of the 70
emits is unchanged to the offset, and with the flag off every one of them is
byte-identical to what this copy emitted before it — no flags, the ten, and all
sixteen.

**What is left, and its shape.** With the dead code gone this copy emits *fewer*
real operations than Sony's — 10864 non-`nop` slots against 10910 — in *more*
rows: 9283 against 9240. None of the remaining 43 rows is work. All of it is `nop nop` in
front of a CLIP reader: 91 words of it here against Sony's 48, and +43 is exactly
the row gap. Every other padding bucket nets out.

It is one shape, in the `_cl` programs' clip chain, and it is not a tuning
constant. Sony pays the window exactly once in each of the sixteen `_cl` programs
— three words, in the fan block where there is genuinely nothing else to do — and
this copy pays it three to eleven times. The difference is how many `clip`s are
allowed to be in flight at once. Over the 45:

| | Sony `vcl` | this copy |
|---|---|---|
| `clip` sharing a row with a CLIP reader | 12 | **0** |
| `clip`-to-`clip` distance 2, 3 or 4 rows | 10 / 10 / 13 | **0 / 0 / 0** |
| `clip`-to-`clip` distance exactly 5 rows | 3 | 37 |

Sony runs two or three `clip`s ahead of their readers and puts a reader in the
lower slot of a row whose upper is the *next* `clip`; this copy issues one `clip`,
waits out its window, reads it, and only then issues the next — five rows per
vertex where Sony takes one or two. Sony's same-row reader is not reading the
`clip` beside it; it is reading one from several rows back, which is the only way
that output and the four-word window are both true. Two rules keep this copy from
doing the same. The pair-legality test refuses a `clip` and a CLIP reader on one
row outright, in both directions, and the window is enforced against the *nearest
preceding* `clip` rather than against the one whose bits the mask selects.

Both are about the same thing: the CLIP register is a 24-bit shift register, four
entries of six bits, and neither the scheduler nor the emitter models the shift.
They model "a `clip` happened N rows ago". Under that model a second `clip` in
flight is indistinguishable from clobbering the first, so refusing it is the only
safe answer, and the serialisation follows. Overlapping the chains needs the
positions modelled — which entry each mask selects, and how many pushes have
happened since — through the dependency graph, the pair test, the latency tracker
and `emittedRowsSinceClipWrite`. That is the redesign, and it is the whole 43.

It is also the part of this compiler with the worst track record. Three commits
here — `ff1bc4a`, `09f8557`, `68d27df` — are all corrections to the positional
CLIP window, and `stapip_clip_c` is still on the defect list below. Forty-three
words out of 9264 do not justify reopening it without hardware to check against.

## Known defects in this copy

* `stapip_as_is_c`-style programs: a value loaded above a batch loop can be reused
  inside it (TyraX works around it in its own sources). The obvious explanation —
  liveness ignoring the back edge — is **wrong**: narrow the register pool and this
  copy refuses cleanly instead of clobbering. No minimal reproducer yet.
* One TyraX microprogram, `stapip_clip_c`, is still miscompiled by this copy.

Upstream's own test suite (419 tests) passes unmodified.
