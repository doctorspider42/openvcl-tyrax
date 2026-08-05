#ifndef __OPENVCL_VULATENCYTRACKER_H__
#define __OPENVCL_VULATENCYTRACKER_H__

/*
 * VuLatencyTracker.h
 *
 * Shared VU readiness model used by code emission and scheduler analysis.
 */

#include "Token.h"

#include <map>
#include <string>

namespace vcl
{

class VuLatencyTracker
{
public:
	VuLatencyTracker();

	void reset();
	int readHazardDelay( const Token& token, const Token* partner, int currentCycle ) const;

	// The part of readHazardDelay() that the hardware will NOT resolve on its own,
	// i.e. what has to be spent as emitted instruction words rather than left to
	// the VU's own stall.
	//
	// The FMAC pipeline interlocks: an instruction whose source is still in flight
	// simply stalls, so padding a VF-to-VF latency costs micro memory and buys
	// nothing. Everything else here does need the words - the integer pipeline
	// feeding a branch or another integer op, the MAC/CLIP flags (not interlocked)
	// and Q/P. That split is exactly what SCE's vcl does: with its code-size
	// reduction pass disabled (-C) stapip_clip_c grows from 257 to 327
	// instructions, the extra 70 being nop/nop rows (88 vs 18), and the 18 it
	// keeps sit in front of branches, fcand and Q readers.
	int manualReadHazardDelay( const Token& token, const Token* partner, int currentCycle ) const;
	void recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite = false );

	int qReadyCycle() const;
	int pReadyCycle() const;

private:
	int readHazardDelayImpl( const Token& token,
	                         const Token* partner,
	                         int currentCycle,
	                         bool skipInterlockedRegisters ) const;

	int m_qReadyCycle;
	int m_pReadyCycle;
	int m_lastFMACCycle;
	int m_lastClipwCycle;
	std::map<std::string, int> m_registerReadyCycle;
	std::map<std::string, std::string> m_registerProducerMnemonic;
};

}

#endif
