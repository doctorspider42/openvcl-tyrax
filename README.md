# OpenVCL — TyraX fork

OpenVCL is a free VCL preprocessor for PlayStation 2 VU programs. It reads
VCL-style source, performs register allocation and scheduling, and emits
standard VSM/DSM-style output that can be assembled by the PS2 toolchain.

**This is a modified copy of OpenVCL**, forked from
[ps2dev/openvcl](https://github.com/ps2dev/openvcl) at commit
`a5867c3daf03828806ee966aca4116622da3f671` (v0.4.0) and maintained for
[TyraX](https://github.com/doctorspider42/tyra-editor), a PlayStation 2 game
editor. It adds seventeen options to upstream, all of them off by default:
with none of them passed the output is byte-identical to upstream's, and with
all of them the fork assembles TyraX's entire VU corpus in less micro memory
than Sony's `vcl` needs for the same programs.

Upstream has not seen or accepted these changes. Report problems here, not to
ps2dev. Licence is unchanged: **Academic Free License v2.0**, see
[`LICENSE`](LICENSE).

## Why it exists

TyraX compiles a PS2 engine's VU1 and VU0 microprograms, and the standard tool
for that job is Sony's `vcl` — VCL 1.4beta7, Sony Computer Entertainment
America © 2001. It ships with no licence at all, which makes a publishable
toolchain container image impossible: distributing the image distributes the
binary. Every other tool in that image has a licence that permits it.

OpenVCL is the only free assembler for this job, and at the fork point it could
not do it: it miscompiled one program silently, ran out of registers on
twenty-two others, and the programs it did compile were too big to fit VU1's
micro memory. Closing that gap is what this fork is.

## What it buys

Measured over TyraX's two corpora — the engine's 25 hand-written microprograms
and the 45 its VU authoring layer generates — with Sony's `vcl` as the
reference:

| | Sony `vcl` | this fork | |
|---|---:|---:|---|
| engine resident VU1 set | 2028 words | **1988** | ceiling is 2042; upstream did not fit |
| generated corpus, 45 programs | 9264 words | **9308** | +0.47%; smaller in 17, larger in 24, equal in 4 |
| real (non-`nop`) pipe slots, 45 programs | 10910 | **10864** | fewer |
| programs that compile | 45 / 45 | **45 / 45** | upstream at the fork point: 23 |

The rendered frame is pixel-identical to a Sony-`vcl` build in PCSX2, and the
GIF packet VU1 stages on a sampled flush is identical across the whole dump bar
the microprogram entry address. Upstream's own test suite — 419 tests, 4465
assertions — passes unmodified.

## The seventeen options

Every one is off by default and each was added to close a measured gap, in this
order.

**Correctness (no flag, always on)**

`CLIPw` with an implied `w` component is accepted instead of rejected. Upstream
rejects the operand *and still writes a complete output file*, with the `clipw`
missing and the `fcand` that reads its clip flags kept — silently emitting a
program that tests clip flags nobody set.

**Density — ten flags**

| flag | effect |
|---|---|
| `--schedule-flag-readers` | MAC/CLIP flag readers take part in list scheduling instead of ending the segment |
| `--fmac-interlock` | a VF-to-VF wait costs cycles, not emitted `nop` words — the FMAC pipeline interlocks |
| `--sce-latencies` | flag visibility 4 → 1 cycle; an integer load's result readable at issue+3 |
| `--emit-delay-fillers` | offer the instruction scheduled before a branch as its delay-slot filler |
| `--branch-interlock` | no padding word before a branch whose operand came from a load or a flag reader |
| `--branch-bubble-on-dependency` | emit the pre-branch bubble only when the row above actually produces one of the branch's operands |
| `--loop-liveness-always` | never skip extending a live range across a loop's back edge |
| `--upper-move-with-w` | promote a full-width `move` into the upper pipe, where it can pair |
| `--pair-best-of-two` | schedule each segment both ways and keep the shorter |
| `--pair-best-of-many` | schedule each segment under seven ready-list heuristics and keep the shortest |

Every latency constant is calibrated against the minimum distances Sony's `vcl`
demonstrates in its own output — black-box observation of a known-good
reference. No binary was disassembled.

**Register allocation — six flags**

The density flags make the code smaller; these make it fit. TyraX's authoring
layer generates 45 microprograms; at the fork point this compiler managed 23,
every failure being *Register allocation ran out of registers*.

| flag | effect |
|---|---|
| `--trim-uncarried-ranges` | rebuild a live range from the alias's own accesses, when the value provably dies at its last use inside one iteration |
| `--coalesce-float-writes` | give a float write the register its own previous value already sits in, when that value is dead from the write on |
| `--sink-loads` | move a load down the token list to just before the value it loads is first read, and re-derive the allocator's timeline from list position |
| `--sink-loads-across-stores` | let a sinking load pass a store through a *different* base register — an aliasing assumption, and the one Sony's `vcl` makes in its own output |
| `--sink-loads-into-loops` | let a sinking load pass one loop header, so a preamble load whose only readers are inside the batch loop stops pinning a register across the whole program |
| `--sink-loads-past-branches` | let a sinking load cross a branch to a point every path out of its old position reaches |

`--sink-loads-past-branches` is not applied speculatively. Carrying a load over
a branch reorders the block it lands in, and over the 45 programs that was worth
about two words in either direction where registers were not scarce. So
allocation runs first exactly as it would with the flag off, and the flag only
gets its turn when that runs out of registers. All 44 programs that compiled
without it are byte-identical with it on.

**Dead code — one flag**

| flag | effect |
|---|---|
| `--drop-dead-writes` | delete a token whose register destination is read nowhere in the program, field by field, and whose MAC/CLIP/I/Q/P/R/ACC writes nothing observes either |

The twelve flags above made the generated programs fit and made them dense, and
they were still 242 words bigger than Sony's. Counting rows said nothing — both
compilers fit 1.18 real operations in a row — but counting opcodes did: this
copy emitted 312 instructions Sony's `vcl` never emits, led by 93 `loi` and 65
`lq`. They are dead. A VU authoring layer emits a whole four-component constant
vector when the body reads two components, and an `lq` for every quadword a
description names whether the body touches it or not. Sony's `vcl` deletes both.

The liveness in it is the weakest one that finds them: a value is live if *any*
token anywhere reads that component, with no control flow in the analysis at
all. Nothing here deletes a write that is dead on one path and live on another.
Only aliases are candidates — a literal `VF05` an author named by number may be
an interface with something this compiler cannot see. Names any
`in_vf`/`out_vf`/`--exitm` directive mentions are live by declaration, as are
hardware resources any `out_hw_*` names. Stores, `xgkick`, auto-incrementing
loads, branches, anything with a delay slot, and any token carrying a label are
never candidates. The pass iterates to a fixed point: deleting
`add.z k0, vf00, i` is what makes the `loi` above it dead.

## Known limits

**The remaining 44 words are one shape.** With the dead code gone this fork
emits fewer real operations than Sony's (10864 against 10910) in more rows (9283
against 9240), and every one of the 43 extra rows is `nop nop` padding in front
of a CLIP reader. Sony runs two or three `clip`s ahead of their readers; this
fork issues one, waits out its window, reads it, then issues the next. The cause
is that neither the scheduler nor the emitter models the CLIP register as the
24-bit shift register it is — four entries of six bits. They model "a `clip`
happened N rows ago", under which a second `clip` in flight is indistinguishable
from clobbering the first, so refusing it is the only safe answer. Overlapping
the chains means threading which entry each mask selects, and how many pushes
have happened since, through the dependency graph, the pair test, the latency
tracker and `emittedRowsSinceClipWrite`.

**The loop-liveness bail-out has no minimal reproducer.** Without
`--loop-liveness-always`, three of five `as_is_*` programs clobber a register
carried across a loop's back edge. The guard responsible is visible in
`extendLoopDirectiveRange` — the range extension is all-or-nothing and returns
early when the set would not fit — but four hand-built reproducers failed to
make it fire: pool pressure refuses cleanly instead. Every program that does
trip it has an inner loop, so the extension running twice over nested ranges is
the untried hypothesis. Worth closing, because a fix nobody can demonstrate in
ten lines is not one upstream can take.

## Upstream and attribution

* Project: **OpenVCL** — <https://github.com/ps2dev/openvcl>
* Forked at `a5867c3daf03828806ee966aca4116622da3f671` (v0.4.0)
* Originally written by **Jesper Svennevid** and **Daniel Collin**
* Current upstream maintainer: **Francisco Javier Trujillo Mata**
* Licence: **Academic Free License v2.0** — see [`LICENSE`](LICENSE), unchanged

Those names are recorded as attribution, which section 6 (*Attribution Rights*)
of the AFL v2.0 requires along with the notice above that the Original Work has
been modified. Per section 4 (*Exclusions From License Grant*) they do not
endorse this copy.

OpenVCL has been built from public VCL documentation and VCL source examples.
No proprietary binary has been reverse engineered. VU Command Line is a
trademark of Sony Computer Entertainment; VCL is the abbreviated name for VU
Command Line.

---

# Using it

## Build

```sh
make openvcl
```

Install into `$PS2DEV/bin` when `PS2DEV` is set:

```sh
make install
```

Or install into another prefix:

```sh
PREFIX=/usr/local make install
```

BSD users may need to use `gmake`.

## Basic Usage

Compile VCL to VSM:

```sh
./openvcl input.vcl -o output.vsm
```

Read from stdin and write to stdout:

```sh
./openvcl < input.vcl > output.vsm
```

Run MASP as the `gasp` replacement:

```sh
./openvcl -g --gasp masp input.vcl -o output.vsm
```

Show command-line help:

```sh
./openvcl -h
```

Useful options:

| option | purpose |
|---|---|
| `-c` | emit nearly original source as comments |
| `-C` | disable code reduction |
| `-d` | emit dumb/unscheduled-style code |
| `-e` | disable generated `[E]` bits |
| `-f` | disable generated `.align` directives |
| `-g` | run `gasp` or `--gasp` before VCL processing |
| `-G` | run the C preprocessor before VCL processing |
| `-I<path>` | include path for gasp/MASP |
| `-K` | keep preprocessor temporary files |
| `-L` | globally disable loop code generation |
| `-m` | generate `.mpg` and DMA tags automatically |
| `-n` | enable new syntax |
| `-o <file>` | output filename |
| `-t <n>` | optimizer timeout |
| `-u <text>` | unique label-generation string |
| `--gasp <name>` | run a specific gasp-compatible preprocessor |
| `--cpp <name>` | run a specific C preprocessor |
| `--bthres <n>` | dynamic branch visit threshold |
| `--show-reg-alloc` | print register allocation information |
| `--cost` | analyze scheduled `.vsm` cost |
| `--cost-json` | analyze scheduled `.vsm` cost as JSON |
| `--cost-loop <label>=<n>` | weight a block by expected iterations |
| `--cost-loop-preset ps2gl` | apply known ps2gl hot-loop weights |
| `--cost-compare <baseline>` | compare scheduled `.vsm` cost against a baseline |
| `--cost-compare-json <baseline>` | compare scheduled `.vsm` cost as JSON |
| `--cost-compare-markdown <baseline>` | compare scheduled `.vsm` cost as a Markdown table |
| `--cost-compare-list-markdown` | read baseline/candidate VSM pairs and emit one Markdown table |
| `--cost-compare-list-check <metric>` | fail if any listed candidate is slower than its baseline |
| `--dump-instruction-info` | print the VU instruction metadata table |
| `--dump-instruction-info-json` | print the VU instruction metadata table as JSON |
| `--dump-schedule-info` | print generic ready-scheduler issue slots |
| `--dump-schedule-info-json` | print generic ready-scheduler issue slots as JSON |
| `--enable-generic-software-pipelining` | enable safe generic software-pipeline rewrites, currently the default |
| `--disable-generic-software-pipelining` | disable generic software-pipeline rewrites for comparison/debugging |
| `--strict-schedule-slots` | emit from the typed scheduler slot model without legacy lookahead pairing |

`-M`, `-P`, and `-Z` are accepted for VCL command-line compatibility.

The seventeen density, register-allocation and dead-code options this fork adds
are listed above, under *The seventeen options*.

## VSM Cost Analysis

OpenVCL can also analyze already scheduled `.vsm` files. This works for both
OpenVCL-generated VSM and SCE/reference VSM files.

Human-readable report:

```sh
./openvcl --cost shader.vsm
```

JSON report:

```sh
./openvcl --cost-json shader.vsm
```

The JSON report includes `label_order` and `cost_by_label` so tools can read a
shader by its VSM labels instead of reverse-engineering the raw block list:

```json
{
  "label_order": ["init_lid", "xform_loop_lid", "done_lid"],
  "cost_by_label": {
    "xform_loop_lid": {
      "affine_role": "loop",
      "static_cycles": 25,
      "estimated_cycles": 25,
      "weighted_estimated_cycles": 2500
    }
  }
}
```

Weight hot blocks by expected loop iterations:

```sh
./openvcl --cost --cost-loop xform_loop_lid=100 shader.vsm
```

Apply the ps2gl 100-vertex hot-loop preset:

```sh
./openvcl --cost --cost-loop-preset ps2gl shader.vsm
```

When loop labels are configured, reports also include an affine cost expression:

```text
affine_estimated_cycles: 120 + 25n
```

The base term is the one-time cost for setup plus teardown. The `n` term is
the cost of the selected loop block(s) for each vertex. Static affine cost uses
the scheduled instruction cycles as emitted; estimated affine cost also adds the
modeled FDIV/EFU issue stalls and explicit `waitq`/`waitp` stalls.

The preset recognizes both OpenVCL labels such as `xform_loop_lid` and SCE
optimized main-loop labels such as `EXPL_..._xform_loop_lid__MAIN_LOOP`. It
also maps SCE fast-family `adcLoop_done_lid__MAIN_LOOP` labels onto
`xform_loop_lid`, so it can be used for side-by-side reference comparisons.

Compare a candidate shader against a reference shader:

```sh
./openvcl --cost-compare sce_reference.vsm openvcl_candidate.vsm
```

Emit comparison output for scripts or Markdown reports:

```sh
./openvcl --cost-compare-json sce_reference.vsm openvcl_candidate.vsm
./openvcl --cost-compare-markdown sce_reference.vsm openvcl_candidate.vsm
```

The comparison JSON also includes `label_comparisons`, keyed by canonical label
matching where possible, so SCE optimized loop labels such as
`EXPL_...__MAIN_LOOP` can be compared with their OpenVCL source labels.

Emit one Markdown table for a set of VSM pairs:

```sh
./openvcl --cost-compare-list-markdown --cost-loop-preset ps2gl pairs.txt
```

`pairs.txt` contains whitespace-separated `baseline.vsm candidate.vsm` rows.
Blank lines and `#` comments are ignored.

Fail when any listed candidate is slower than its own baseline:

```sh
./openvcl --cost-compare-list-check weighted-estimated --cost-loop-preset ps2gl pairs.txt
```

Supported check metrics are `static`, `estimated`, `weighted-static`,
`weighted-estimated`, `affine-static-base`, `affine-static-loop`,
`affine-estimated-base`, and `affine-estimated-loop`. This check is per row: an
OpenVCL shader only passes when that specific shader is equal to or faster than
its matching SCE/reference VSM.

The report includes:

- static scheduled cycles;
- estimated cycles including modeled FDIV/EFU producer issue stalls and
  explicit `waitq`/`waitp` stalls;
- loop-weighted totals when `--cost-loop` or `--cost-loop-preset` is used;
- affine `base + loop*n` static and estimated costs for selected loop labels;
- upper/lower slot usage, paired cycles, NOP slots, and nop-only cycles;
- per-label block costs;
- weighted hot-block, idle-slot, estimated-cost, and wait-stall rankings;
- unknown-instruction and slot-mismatch checks.

## Instruction Metadata

OpenVCL exposes its shared VU instruction table for scheduling and tooling
work. The text form is useful while inspecting opcodes:

```sh
./openvcl --dump-instruction-info
```

The JSON form is intended for scripts and regression tests:

```sh
./openvcl --dump-instruction-info-json
```

Each row includes the mnemonic, pipe, execution unit, throughput, latency,
parser operand pattern, readable parameter summary, short description, implicit
resources, memory flags, branch-delay slots, and special bypass notes. This is
the canonical table to inspect before adding new parser, cost-analysis, or
scheduler rules.

Generated-code helpers should use `VuInstructionOpcode`/`vuInstructionMnemonic`
instead of spelling raw mnemonics directly in `CodeGenerator.cpp`. Hand-written
software-pipeline paths can then share the same names as the parser, cost
analyzer, and metadata dumps.

## Scheduler Status

OpenVCL now performs conservative VU scheduling rather than only emitting
VCL `-d`-style output. Current work includes:

- bounded upper/lower pairing lookahead;
- latency-gap filling with ready independent instructions;
- deferred `waitq`/`waitp` emission;
- Q/P, I, MAC, CLIP, ACC, VF/VI, and per-field VF dependency checks;
- safe movement around selected plain loads/stores;
- branch padding reuse when an existing pure `nop/nop` cycle is available;
- adjacent upper/direct-branch pairing while preserving branch delay slots;
- deterministic alias allocation for reproducible VSM output;
- `--LoopCS`-marked loop temporaries get conservative VF lifetime expansion
  when register pressure allows, giving the scheduler room to overlap loads;
- static cost reporting used to compare OpenVCL output with SCE/reference VSM.

The current refactor is consolidating instruction facts into one canonical VU
instruction metadata table. `src/VuInstructionInfo.*` now feeds parser operand
construction and cost-analysis opcode classification. The scheduler should
migrate onto the same table so resource and barrier rules are not duplicated
across the codebase.

Current ps2gl pure-OpenVCL aggregate cost baseline:

| metric | SCE/reference | OpenVCL | delta |
|---|---:|---:|---:|
| static scheduled cycles | 6308 | 5268 | -1040 |
| estimated cycles | 6820 | 5838 | -982 |
| ps2gl-loop weighted static cycles | 100358 | 334938 | +234580 |
| ps2gl-loop weighted estimated cycles | 100870 | 373227 | +272357 |

`estimated cycles` includes modeled FDIV/EFU producer issue stalls and
explicit `waitq`/`waitp` stalls. These are static VSM estimates, not measured
runtime per draw call. The loop-weighted rows apply `--cost-loop-preset ps2gl`
to the 13 matched ps2gl renderer pairs; they better expose the remaining
hot-loop gap caused by SCE/reference prolog/main/epilog software-pipelined
loops versus OpenVCL's current generic scheduling and limited generic
software-pipeline coverage.

This baseline uses corrected ACC dependencies for multiply-add/subtract
instructions plus safe generic software-pipeline rewrites where loop analysis
can prove the cloned prolog/main/drain structure. Conservative branch-delay
filling handles independent integer instructions immediately before direct
branches. Standalone branches no longer emit an extra pre-branch bubble once
normal read-hazard padding is satisfied.
Eligible pre-increment stores can also move into branch delay slots by
adjusting their offsets against the incremented base register. Dead VI-only
fallthrough integer instructions can fill forward conditional branch delay
slots when the taken path overwrites the same VI value before reading it.
Independent plain stores immediately before loop-counter increments can fill
the following branch delay slot when the store does not depend on the updated
counter value. Load results can feed `ftoi*` conversions with the same narrow
bypass behavior used by the SCE-generated ps2gl ADC setup, while normal
load-to-VF consumers still keep the conservative load-use padding.
The ACC rule is intentionally more conservative than older reports that let the
scheduler move `madd*`/`msub*` instructions across ACC producer chains.

The performance target is per shader, not aggregate. A scheduler change is only
complete when every matched ps2gl OpenVCL VSM is equal to or faster than its
matching SCE/reference VSM for the selected static and estimated metrics.

## Roadmap

The generic scheduler is now a descriptor-backed ready-set scheduler over typed
basic blocks. The remaining work is to keep moving code emission and loop
optimization decisions onto that explicit schedule model:

- keep `VuInstructionInfo` as the canonical instruction table for parser,
  cost analysis, resource descriptors, and scheduler tooling;
- move remaining default emission-time cycle state into the typed scheduler
  plan already used by `--strict-schedule-slots`;
- improve exact memory aliasing beyond base register and constant offset;
- extend typed branch-delay-slot metadata before attempting broader branch NOP
  removal or delay-slot filling in the default path;
- optimize hot ps2gl loops for estimated block cost and dual-pipe occupancy,
  guided by `--cost-compare` and `--cost-loop-preset ps2gl`;
- retire the remaining bounded textual lookahead once the typed scheduler and
  software-pipeline planner match the legacy output on correctness and cost.

Scheduling changes should preserve Q/P, I, MAC, CLIP, ACC, VF/VI, broadcast
field, branch-delay, and memory-ordering correctness. Each new scheduling rule
should have a focused unit or integration test, full OpenVCL test coverage,
regenerated ps2gl pure-OpenVCL VSMs, and PCSX2 smoke coverage when generated
output can affect visible examples.

## Expression Solver

OpenVCL extends `loi` expressions with math functions. These extensions make
source incompatible with standard VCL when used, but are useful for standalone
OpenVCL projects.

| function | result |
|---|---|
| `abs(x)` | absolute value |
| `exp(x)` | exponential |
| `sin(x)`, `cos(x)`, `tan(x)` | trigonometry from radians |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | hyperbolic trigonometry |
| `asin(x)`, `acos(x)`, `atan(x)`, `atan2(x, y)` | inverse trigonometry |
| `pow(x, y)` | `x` raised to `y` |
| `log(x)`, `log10(x)` | logarithms |
| `sqrt(x)` | square root |
| `pi()` | pi |

Example:

```vcl
loi sin(45 * (pi()/1.8e2))
```

Function names are case-sensitive.

## Differences From SCE VCL

- If an alias contains a register-field declaration and the argument slot does
  not support fields, OpenVCL rejects it.
- `I` may not be used as an alias for integer registers.
- Float expressions are evaluated for `loi`; GAS does not handle
  float-expression immediates.
- Old-syntax field access by suffixing aliases, for example `srcx`, is not
  supported. Prefer new syntax such as `src[x]`.
- Selecting a specific simplification branch, for example `mula d,s,t`, limits
  simplification to that operand and does not expand back to the full VCL
  simplification set.

## MASP / GASP

GNU `gasp` has been removed from newer binutils. Use MASP as a compatible
replacement by installing `masp` on your `PATH` and passing:

```sh
./openvcl -g --gasp masp input.vcl -o output.vsm
```

You may also pass a full path to `--gasp`.

## Tests

Build and run the unit/integration suite:

```sh
cmake --build test/build --target openvcl_unit_tests -j8
ctest --test-dir test/build --output-on-failure
```
