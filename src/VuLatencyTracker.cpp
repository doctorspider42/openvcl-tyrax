#include "VuLatencyTracker.h"

#include "Operand.h"
#include "VuInstructionInfo.h"
#include "VuSchedulingRules.h"
#include "VuTokenResourceAccess.h"

#include <list>

namespace vcl
{

namespace
{
	bool tokenReadsImplicitResource( const Token& token, unsigned int resource )
	{
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( token, access )
		    && (access.implicitReads & resource) != 0;
	}

	bool tokenWritesImplicitResource( const Token& token, unsigned int resource )
	{
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( token, access )
		    && (access.implicitWrites & resource) != 0;
	}

	int bypassLatencyReduction( const std::string& mnemonic, int fallback )
	{
		const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
		return info ? static_cast<int>( info->latency ) : fallback;
	}
}

VuLatencyTracker::VuLatencyTracker()
{
	reset();
}

void VuLatencyTracker::reset()
{
	m_qReadyCycle = -10;
	m_pReadyCycle = -10;
	m_lastFMACCycle = -10;
	m_lastClipwCycle = -10;
	m_registerReadyCycle.clear();
	m_registerProducerMnemonic.clear();
}

int VuLatencyTracker::readHazardDelay( const Token& token,
                                       const Token* partner,
                                       int currentCycle ) const
{
	return readHazardDelayImpl( token, partner, currentCycle, false );
}

int VuLatencyTracker::manualReadHazardDelay( const Token& token,
                                             const Token* partner,
                                             int currentCycle ) const
{
	return readHazardDelayImpl( token, partner, currentCycle, true );
}

namespace
{
	// Register keys are the allocated register names, "VF00".."VF31" /
	// "VI00".."VI15" (see vuRegisterKey).
	// Producers whose result a BRANCH may read without a padding word: the
	// hardware stalls for these on its own. Integer loads and the flag readers
	// (which write an integer register) qualify; an ordinary integer op does not
	// - see the file comment for the SCE output this is calibrated against.
	bool isBranchInterlockedProducer( const std::string& mnemonic )
	{
		return mnemonic.compare( 0, 3, "ilw" ) == 0
		    || mnemonic.compare( 0, 2, "lq" ) == 0
		    || isVuClipReader( mnemonic )
		    || isVuMacReader( mnemonic );
	}
	bool isFloatRegisterKey( const std::string& key )
	{
		return key.length() >= 2
		    && (key[0] == 'V' || key[0] == 'v')
		    && (key[1] == 'F' || key[1] == 'f');
	}
}

int VuLatencyTracker::readHazardDelayImpl( const Token& token,
                                           const Token* partner,
                                           int currentCycle,
                                           bool skipInterlockedRegisters ) const
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys( token, reads );
	if( partner )
		collectVuRegisterReadKeys( *partner, reads );

	bool readsQ = vuTokenReadsQ( token );
	bool readsP = vuTokenReadsP( token );
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ( *partner );
		readsP = readsP || vuTokenReadsP( *partner );
	}

	int needed = 0;
	for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
	{
		// The FMAC pipeline interlocks on VF registers: the hardware stalls by
		// itself, so this wait must not be paid for in instruction words.
		if( skipInterlockedRegisters && isFloatRegisterKey( *i ) )
			continue;
		// Same distinction for the integer file, but it depends on the CONSUMER:
		// a branch reading a load result or a flag-reader result stalls in
		// hardware, so those words are not ours to spend either.
		if( skipInterlockedRegisters && vuBranchInterlockEnabled()
			&& vuTokenHasInstructionFlag( token, VU_INSTR_BRANCH ) )
		{
			std::map<std::string, std::string>::const_iterator branchProducer =
			    m_registerProducerMnemonic.find( *i );
			if( branchProducer != m_registerProducerMnemonic.end()
			    && isBranchInterlockedProducer( branchProducer->second ) )
				continue;
		}

		std::map<std::string, int>::const_iterator ready = m_registerReadyCycle.find( *i );
		if( ready == m_registerReadyCycle.end() )
			continue;

		int readyCycle = ready->second;
		std::map<std::string, std::string>::const_iterator producer =
		    m_registerProducerMnemonic.find( *i );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuFtoiConversion( producer->second )
		    && ( (isVuMtir( token ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuMtir( *partner ) && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= bypassLatencyReduction( producer->second, 4 );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadToFtoiBypassProducer( producer->second )
		    && ( (isVuFtoiConversion( lowerVuTokenName( token ) ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuFtoiConversion( lowerVuTokenName( *partner ) )
		             && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= bypassLatencyReduction( producer->second, 4 );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadToMiniiBypassProducer( producer->second )
		    && ( (isVuMinii( lowerVuTokenName( token ) ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuMinii( lowerVuTokenName( *partner ) )
		             && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= 2;

		const int gap = readyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	if( readsQ )
	{
		const int gap = m_qReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}
	if( readsP )
	{
		const int gap = m_pReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	const int flagCycle = currentCycle + needed;
	bool readsMac = tokenReadsImplicitResource( token, VU_RESOURCE_MAC );
	bool readsClip = tokenReadsImplicitResource( token, VU_RESOURCE_CLIP );
	if( partner && partner->operand() )
	{
		readsMac = readsMac || tokenReadsImplicitResource( *partner, VU_RESOURCE_MAC );
		readsClip = readsClip || tokenReadsImplicitResource( *partner, VU_RESOURCE_CLIP );
	}

	// How long after its producer a flag may be read. 4 by default;
	// --sce-flag-latency calibrates it to the 1 that SCE's vcl emits (see
	// vuFlagVisibilityLatency).
	const int flagLatency = static_cast<int>( vuFlagVisibilityLatency() );

	int flagDelay = 0;
	if( readsMac )
	{
		const int gap = flagCycle - m_lastFMACCycle;
		if( flagLatency - gap > flagDelay )
			flagDelay = flagLatency - gap;
	}
	if( readsClip )
	{
		const int gap = flagCycle - m_lastClipwCycle;
		if( flagLatency - gap > flagDelay )
			flagDelay = flagLatency - gap;
	}
	if( flagDelay > 0 )
		needed += flagDelay;

	return needed;
}

void VuLatencyTracker::recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite )
{
	if( forceMacFlagWrite || tokenWritesImplicitResource( token, VU_RESOURCE_MAC ) )
		m_lastFMACCycle = issueCycle;
	if( tokenWritesImplicitResource( token, VU_RESOURCE_CLIP ) )
		m_lastClipwCycle = issueCycle;

	if( !token.operand() || token.operand()->latency() <= 1 )
		return;

	int readyCycle = issueCycle + static_cast<int>( token.operand()->latency() ) + 1;

	// An integer load lands sooner than latency+1 says. The table gives ILW a
	// latency of 4, so this formula makes its result readable at issue+5, while
	// SCE's vcl reads one at issue+3 - measured as the minimum ilw -> integer-op
	// distance over the 25 microprograms of a real engine (85 samples), against
	// openvcl's own minimum of 5 on the same corpus. Those two extra cycles are
	// paid in nop words at every load-use pair. --sce-latencies calibrates it.
	if( vuIntegerLoadReadyCycles() > 0 )
	{
		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access )
		    && access.memoryKind == VU_MEMORY_LOAD
		    && access.memoryFlags == VU_MEMORY_FLAG_NONE )
		{
			const int calibrated = issueCycle + static_cast<int>( vuIntegerLoadReadyCycles() );
			if( calibrated < readyCycle )
				readyCycle = calibrated;
		}
	}

	std::list<std::string> writes;
	collectVuRegisterWriteKeys( token, writes );
	for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
	{
		m_registerReadyCycle[*i] = readyCycle;
		m_registerProducerMnemonic[*i] = lowerVuTokenName( token );
	}

	if( vuTokenWritesQ( token ) )
		m_qReadyCycle = readyCycle;
	if( vuTokenWritesP( token ) )
		m_pReadyCycle = readyCycle;
}

int VuLatencyTracker::qReadyCycle() const
{
	return m_qReadyCycle;
}

int VuLatencyTracker::pReadyCycle() const
{
	return m_pReadyCycle;
}

}
