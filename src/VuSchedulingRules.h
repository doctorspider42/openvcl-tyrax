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
void setVuShowPairMissesEnabled( bool enabled );
bool vuShowPairMissesEnabled();
void setVuPairBestOfTwoEnabled( bool enabled );
bool vuPairBestOfTwoEnabled();
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
