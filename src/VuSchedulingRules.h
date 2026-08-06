#ifndef __OPENVCL_VUSCHEDULINGRULES_H__
#define __OPENVCL_VUSCHEDULINGRULES_H__

/*
 * VuSchedulingRules.h
 *
 * Shared scheduling predicates for VU tokens.  These helpers keep the
 * stateless resource, memory, branch, and pairing rules out of the code
 * emitter so future scheduling passes can reuse the same contract.
 */

#include "Token.h"

#include <list>
#include <string>

namespace vcl
{

bool isVuEmittableInstruction( const Token& token );

std::string lowerVuTokenName( const Token& token );
bool isVuMtir( const Token& token );
bool isVuFtoiConversion( const std::string& name );
bool isVuLoadToFtoiBypassProducer( const std::string& name );
bool isVuMinii( const std::string& name );
bool isVuLoadToMiniiBypassProducer( const std::string& name );

bool isVuMacReader( const std::string& name );
bool isVuClipReader( const std::string& name );
bool isVuClipw( const std::string& name );

bool vuTokenHasInstructionFlag( const Token& token, unsigned int flag );
unsigned int vuTokenBranchDelaySlots( const Token& token );
bool isVuTerminalUnconditionalBranch( const Token& token );

bool vuTokenReadsQ( const Token& token );
bool vuTokenWritesQ( const Token& token );
bool vuTokenReadsP( const Token& token );
bool vuTokenWritesP( const Token& token );
bool vuTokenReadsRegister( const Token& token, const std::string& key );

void collectVuRegisterReadKeys( const Token& token, std::list<std::string>& reads );
void collectVuRegisterWriteKeys( const Token& token, std::list<std::string>& writes );

bool isVuZeroMoveFromVf00( const Token& token );
bool isVuMoveAsUpperMaxCandidate( const Token& token );
bool vuTokenListReadsMac( const std::list<Token>& tokens );
bool vuTokenListReadsClip( const std::list<Token>& tokens );

bool isVuPlainMemoryStore( const Token& token );
bool isVuPlainMemoryLoad( const Token& token );
bool isVuXgkick( const Token& token );
bool isVuMemoryOrderingAccess( const Token& token );
bool isVuBoundaryOperand( const Token& token );
bool isVuSchedulingBarrier( const Token& token );
bool isVuReadyScheduleCandidate( const Token& token );

// --schedule-flag-readers: let instructions that implicitly read the MAC/CLIP
// flags take part in list scheduling instead of ending the scheduling segment.
void setVuScheduleFlagReadersEnabled( bool enabled );
bool vuScheduleFlagReadersEnabled();

// --fmac-interlock: spend a VF-to-VF wait as a hardware stall rather than as
// emitted nop words. Same cycles, smaller program.
void setVuFmacInterlockEnabled( bool enabled );
bool vuFmacInterlockEnabled();

// How many cycles after its producer the MAC/CLIP flags may be read. Default 4.
// --sce-latencies sets 1, which is what SCE's vcl emits: over the 25
// microprograms of a real engine its output contains five clipw -> fcand pairs
// exactly one row apart, e.g.
//     clipw.xyz VF15xyz,VF15 | ior   VI09,VI09,VI08
//     addw.x    VF05,VF05,.. | fcand VI01,63
// and 0x3FFFF/0x3F masks mean that fcand is testing the clipw right above it.
void setVuFlagVisibilityLatency( unsigned int cycles );
unsigned int vuFlagVisibilityLatency();
void setVuClipFlagVisibilityLatency( unsigned int cycles );
unsigned int vuClipFlagVisibilityLatency();
void setVuClipFlagSchedulingLatency( unsigned int cycles );
unsigned int vuClipFlagSchedulingLatency();

// Cycles after which a memory load's destination may be read, or 0 to keep the
// instruction table's latency+1. --sce-latencies sets 3 (see VuLatencyTracker).
void setVuIntegerLoadReadyCycles( unsigned int cycles );
unsigned int vuIntegerLoadReadyCycles();

// --emit-delay-fillers: offer the instruction scheduled just before a branch as
// its delay-slot filler, instead of leaving the slot as a nop word.
void setVuEmitDelayFillersEnabled( bool enabled );
bool vuEmitDelayFillersEnabled();

// --branch-interlock: a branch reading a register produced by a LOAD or by a
// flag reader waits in hardware, so that wait must not be paid in instruction
// words. SCE's vcl emits exactly that, and annotates the latency it is leaving
// to the hardware:
//     ilw.x  VI01,8(VI00)
//     iblez  VI01,multiColor      ;  STALL_LATENCY ?3
// Five of the engine's programs do it, and five more put `fcand VI01,8`
// immediately before `ibne ...,VI01,...`. An ordinary integer op is NOT in this
// class - SCE never comes closer than two instructions there (24 cases at
// exactly two), which is what openvcl already emits.
// --branch-bubble-on-dependency: emit the pre-branch bubble only when the row
// above the branch actually produces one of its operands. The emitter's own rule
// is unconditional - one nop in front of every conditional branch - which is
// where openvcl's remaining stall rows are: 80 sites over the resident program
// set against SCE's 36, for the same hazard and the same one-word cost where it
// is real.
void setVuBranchBubbleOnDependencyEnabled( bool enabled );
bool vuBranchBubbleOnDependencyEnabled();

// --loop-liveness-always: never skip extending a live range across a loop's back
// edge. The allocator's guard trades that correctness requirement for a compile
// that succeeds, and the result is a value handed to two names at once.
void setVuLoopLivenessAlwaysEnabled( bool enabled );
void setVuUpperMoveWithWEnabled( bool enabled );
bool vuUpperMoveWithWEnabled();

// --coalesce-float-writes: give a float write the register its own previous
// value already sits in, when that previous value is dead from the write on.
// openvcl spawns a fresh Alias per write, so a two-address self-update chain
// (`mul.x a,a,b` - 3673 of them across the generated programs, against 57 plain
// `move`s) burns a second register for a value that never needed one. The
// integer side has done this since the isubiu loop-counter fix; this is the
// float half. Off by default: it changes which register a program lands in, and
// upstream's fixtures pin those names.
void setVuCoalesceFloatWritesEnabled( bool enabled );
bool vuCoalesceFloatWritesEnabled();

// --trim-uncarried-ranges: a value defined and consumed inside one iteration of
// one loop does not have to hold its register for the whole loop. openvcl's
// branch-state analysis merges the aliases of a name across the back edge and
// stretches the survivor from the loop's entry point to its last line, so a
// per-vertex temporary looks exactly like a loop-carried accumulator. Trim such
// a range back to [first access, last access], but ONLY when every component
// read is written earlier in the same iteration, every access sits inside the
// same loop, and no branch lies between the two ends - i.e. only when the value
// provably dies at its last use. Off by default: it removes liveness, so it is
// the one change here that could hide a real carry if the test were wrong.
void setVuTrimUncarriedRangesEnabled( bool enabled );
bool vuTrimUncarriedRangesEnabled();

// --sink-loads: move a load down to just before the value it loads is first
// read, which is what SCE's vcl achieves by scheduling and openvcl never could,
// because the allocator only ever saw source order. The generated programs load
// vertex1/2/3 and normal1/2/3 at the top of the loop body and then transform
// them one at a time, so six registers are pinned where two are in use; Sony's
// output for the same source holds one vertex at a time and reloads into the
// register the previous one just freed. The load moves for real - the token is
// spliced in the list and the allocator's timeline re-derived from list
// position - because a range computed from the first read while the load is
// still emitted early is unsound: the register is unreserved between the two,
// and something else can take it. Off by default: it reorders the emitted
// program, so register names and instruction placement change everywhere.
void setVuSinkLoadsEnabled( bool enabled );
bool vuSinkLoadsEnabled();

// --sink-loads-across-stores: let a sinking load pass a store that writes
// through a DIFFERENT base register. Nothing in the source proves the two
// quadwords are distinct, so this is an aliasing assumption and it gets its own
// flag - but it is the assumption SCE's vcl makes. Its own output for
// vu_script3_d puts `lq.xyz VF25,2(VI05)` at row 199, after three stores through
// VI07 at rows 162, 181 and 198, from a source line that sits above all three.
// Without it the generated loop bodies stop dead at the isw that writes the
// previous vertex's ADC bit and every later load piles up behind it. Only has an
// effect together with --sink-loads.
void setVuSinkLoadsAcrossStoresEnabled( bool enabled );
bool vuSinkLoadsAcrossStoresEnabled();

// --sink-loads-into-loops: let a sinking load pass ONE label, so a value loaded
// in the preamble and read inside the batch loop is loaded inside that loop
// instead. The four GIF-tag quadwords are the reason: gifSetTag, lodGifTag,
// testsTag and alphaGifTag are read only by the seven `sq`s that build the
// packet header, but because the load sits above `begin:` their ranges span the
// whole program, and they are 4 of the 32 values live at the vertex loop's
// pressure peak. This is rematerialization, and it is paid for per loop
// iteration, so the motion is only taken when it reaches the value's first
// reader - stopping halfway would pay the instruction and free nothing - and
// only one LOOP HEADER may be crossed, which keeps a preamble load out of the
// inner per-vertex loop (a label nothing branches back to costs nothing and does
// not count). Four conditions make it sound over the back edge: the address
// register is written nowhere in the program, no store can reach the quadword
// (same base and same constant offset - a different offset on the same base is a
// different quadword and is allowed), the load is the only thing that ever
// writes the value, and the reader it lands in front of carries no label of its
// own for a branch to jump straight past it to. Only has an effect together
// with --sink-loads.
void setVuSinkLoadsIntoLoopsEnabled( bool enabled );
bool vuSinkLoadsIntoLoopsEnabled();

// --sink-loads-past-branches: let a sinking load cross a branch, landing at a
// point every path out of the load's old position reaches. vu_script3_tce_cl is
// the program that needs it: five GIF-tag quadwords are loaded in the preamble
// and read only by the `sq`s that build the packet header, but a two-arm branch
// picking the destination address sits in between, and the pass walls off at any
// branch because the allocator has no dominance information. Landing on
// `setDestAddr:` is correct - both arms rejoin there - and it takes the peak from
// 33 simultaneously live float ranges to 28, against 31 registers.
//
// The condition is post-dominance restricted to a forward span, tested during the
// walk itself rather than from a built CFG, which the allocator would otherwise
// need and does not have. Two counts do it. No path may leave the span past the
// landing point: a branch's target label has to have been walked over before the
// load may land after it, so a jump over the current position always blocks it,
// and a branch back to a label already walked over is refused outright. And no
// path may enter the span past the load: a label is only transparent once every
// branch in the WHOLE program that names it has been walked over, so a join of
// arms that all started at the load is free while a way in from anywhere else is
// a wall. One `jr` - a branch whose target is not a label this can read - and the
// program is given up on, since then any label might be entered from outside.
// Counting branches per label rather than positions is what survives the pass
// splicing tokens as it goes: a load is never a branch, so no move it makes can
// change a count. Only has an effect together with --sink-loads.
void setVuSinkLoadsPastBranchesEnabled( bool enabled );
bool vuSinkLoadsPastBranchesEnabled();
void setVuShowPairMissesEnabled( bool enabled );
bool vuShowPairMissesEnabled();
void setVuPairBestOfTwoEnabled( bool enabled );
bool vuPairBestOfTwoEnabled();
void setVuPairBestOfManyEnabled( bool enabled );
bool vuPairBestOfManyEnabled();
bool vuLoopLivenessAlwaysEnabled();

void setVuBranchInterlockEnabled( bool enabled );
bool vuBranchInterlockEnabled();

bool isVuLowerPipe( const Token& token );
bool isVuLongLatencyProducer( const Token& token );
bool isVuLatencyLoad( const Token& token );

bool vuTokensHaveDataDependency( const Token& a, const Token& b );
bool vuTokenCanMoveBefore( const Token& moved,
                           const Token& crossed,
                           unsigned int ignoredImplicitWawResources = 0 );
bool vuTokenRangeCanBeCrossed( const Token& first, const Token& last );
bool vuTokenPairResourcesAreIndependent( const Token& a,
                                         const Token& b,
                                         bool aWritesMac,
                                         bool bWritesMac );

void coalesceAdjacentVuIntegerAdds( std::list<Token>& tokens );

}

#endif
