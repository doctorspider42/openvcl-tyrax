/*
 * RegisterAllocator.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */


#include "RegisterAllocator.h"
#include "VuSchedulingRules.h"
#include "BranchState.h"
#include "Error.h"
#include "VuTokenResourceAccess.h"

#include <iostream>
#include <iomanip>
#include <set>
#include <vector>
#include <stdlib.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::RegisterAllocator()
{
	unsigned int i;

	for( i = 0; i < sizeof(m_floats)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VF" << std::setw(2) << std::setfill('0') << i;
		m_floats[i].setName( s.str() );
	}

	for( i = 0; i < sizeof(m_integers)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VI" << std::setw(2) << std::setfill('0') << i;
		m_integers[i].setName( s.str() );
	}

	m_dynamicThreshold = 16;
	m_showRegisterInfo = false;
	m_currState = OUTSIDE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::~RegisterAllocator()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableFloats( unsigned int floats )
{
	for( unsigned int i = 1; i < 32; i++ )
		m_floats[i].setAvailable( ( floats & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableIntegers( unsigned int integers  )
{
	for( unsigned int i = 1; i < 16; i++ )
		m_integers[i].setAvailable( ( integers & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::process( std::list<Token>& tokens )
{
	BranchState* branchState = NULL;

	if( !collectLabels( tokens.begin(), tokens.end() ) )
		return false;

	for( std::list<Token>::iterator i = tokens.begin(), next = tokens.begin(); i != tokens.end(); i = next )
	{
		next++;

		switch( state() )
		{
			case OUTSIDE:
			{
				if( !(*i).operand() )
					continue;

				if( !((*i).operand()->flags() & Operand::PREPROCESSOR) )
				{
					Error::Display( Error( "Code not allowed outside entry point", *i ) );
					return false;
				}

				if( "--enter" == (*i).operand()->name() )
				{
					next = i;

					branchState = new BranchState( *this );
					assert( branchState );

					setState( ENTER );
				}
				if( ("--exit" == (*i).operand()->name()) || ("--exitm" == (*i).operand()->name()) )
				{
					next = i;
					setState( EXIT );
				}
				else
				{
				}
			}
			break;

			case ENTER:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::ENTER )
				{
					Error::Display( Error( "Invalid operand inside --enter/--endenter block", *i ) );
					delete branchState;
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "in_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setFloatInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setIntegerInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_hw_acc" == (*i).operand()->name() )
					branchState->writeAccumulator( Token::X|Token::Y|Token::Z|Token::W );
				else if( "in_hw_i" == (*i).operand()->name() )
					branchState->writeI();
				else if( "in_hw_p" == (*i).operand()->name() )
					branchState->writeP();
				else if( "in_hw_q" == (*i).operand()->name() )
					branchState->writeQ();
				else if( "in_hw_r" == (*i).operand()->name() )
					branchState->writeR();
				else if( "--endenter" == (*i).operand()->name() )
				{
					m_states.push_back( branchState );
					setState( CODE );
				}
			}
			break;

			case CODE:
			{
				if( !branchState )
				{
					Error::Display( Error( "Internal error: missing branch state before CODE block" ) );
					return false;
				}
				// locate end of codeblock
				for( ; next != tokens.end(); next++ )
				{
					if( !(*next).operand() )
						continue;

					if( !((*next).operand()->flags() & Operand::PREPROCESSOR) )
						continue;

					if( ((*next).operand()->unit() == Operand::EXIT) || ((*next).operand()->unit() == Operand::ENTER) )
						break;

					if( !processCommonDirective( (*next) ) )
						return false;
				}

				branchState->setCurrent( i );
				branchState->setExitPoint( next );
				branchState->pushTraces( &*i, true );

				setState( EXIT );
			}
			break;

			case EXIT:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::EXIT )
				{
					Error::Display( Error( "Invalid operand inside --exit/--endexit block", *i ) );
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "out_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());

					if( branchState )
						branchState->setFloatOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "out_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					if( branchState )
						branchState->setIntegerOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "--endexit" == (*i).operand()->name() )
				{
					setState( OUTSIDE );
				}
			}
			break;
		}

		if( !processCommonDirective( (*i) ) )
			return false;
	}

	std::list<BranchState*>::iterator j;
	for( j = m_states.begin(); j != m_states.end(); j++ )
	{
		if( !(*j) )
		{
			Error::Display( Error( "Internal error: null branch state before processing" ) );
			return false;
		}
		if( !processBranchState( *j, tokens.end() ) )
			return false;
	}

	// Before the extension passes, not after: trimming is allowed to undo the
	// branch-state analysis's blanket stretch, but it must not be able to undo a
	// liveness requirement one of the extensions below discovers.
	if( vuTrimUncarriedRangesEnabled() )
		trimUncarriedLoopLocalRanges( tokens );

	collectLiteralRegisterUsage( tokens );
	extendContinuationLiveRanges( tokens );
	extendLoopDirectiveLiveRanges( tokens );
	extendMultiQStageLiveRanges( tokens );

	// After every extension: the deadness test below reads final ranges.
	if( vuCoalesceFloatWritesEnabled() )
		coalesceSameNameFloatWrites( tokens );

	if( m_aliases.size() > 0 )
	{
		// Coalescing is a preference, not a requirement, and the chain pre-pass
		// enforces it as a requirement: a chain must find ONE register free over
		// the union of every member's range, and a long chain placed early can
		// leave a later one with nothing. That turned vu0_rt_kernel - which
		// allocates fine without the flag - into a failure. So allocate with the
		// coalescing edges, and if that runs out, throw the edges away and
		// allocate again. The flag can then only ever add programs that compile.
		std::map<Alias*, const Register*> preallocated;
		if( !m_coalescedWrites.empty() )
		{
			for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
				preallocated[ i->first ] = i->first->allocatedRegister();
		}

		bool allocated = processAliases();

		if( !allocated && !m_coalescedWrites.empty() )
		{
			for( std::map<Alias*, const Register*>::iterator i = preallocated.begin();
			     i != preallocated.end(); ++i )
				i->first->setAllocatedRegister( i->second );
			for( unsigned int i = 0; i < m_coalescedWrites.size(); ++i )
				m_coalescedWrites[i]->setSameNamePredecessor( NULL );
			m_coalescedWrites.clear();

			if( m_showRegisterInfo )
				std::cerr << "Retrying allocation without --coalesce-float-writes" << std::endl;

			allocated = processAliases();
		}

		if( !allocated )
		{
			Error::Display( Error( "Register allocation ran out of registers" ) );
			return false;
		}
	}

	while( !m_states.empty() )
	{
		delete m_states.back();
		m_states.pop_back();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processBranchState( BranchState* state, std::list<Token>::iterator end )
{
	std::list<Token>::iterator curr, next;
	for( ; state->current() != end; state->setCurrent( curr ) )
	{
		curr = state->current();
		Token& token = *curr;
		curr++;

		if( !token.operand() )
			continue;

		// if we've reached a new entry or exit-point, abort branch
		if( (token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER) )
			break;

		token.setFlags( token.flags() | Token::PROCESSED );

		for( std::list<Token::Argument>::reverse_iterator i = token.arguments().rbegin(); i != token.arguments().rend(); i++ )
		{
			switch( (*i).type() )
			{
				case Token::Argument::FLOAT_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeFloat( (*i) );
					else
					{
						if( !state->readFloat( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized float register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::INTEGER_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeInteger( (*i) );
					else
					{
						if( !state->readInteger( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized integer register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::ACCUMULATOR:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeAccumulator( (*i).fields() );
					else
					{
						if( !state->readAccumulator( (*i).fields() ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized accumulator", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::Q:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeQ();
					else
					{
						if( !state->readQ() )
						{
							Error::Display( Error( "Read-attempt from uninitialized Q register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::P:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeP();
					else
					{
						if( !state->readP() )
						{
							Error::Display( Error( "Read-attempt from uninitialized P register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::R:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeR();
					else
					{
						if( !state->readR() )
						{
							Error::Display( Error( "Read-attempt from uninitialized R register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::I:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeI();
					else
					{
						if( !state->readI() )
						{
							Error::Display( Error( "Read-attempt from uninitialized I register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::IMMEDIATE:
				{
					if( token.operand()->flags() & Operand::IWRITE )
					{
						// special case for LOI
						state->writeI();
					}
				}
				break;

				default: break;
			}
		}

		if( token.operand()->unit() == Operand::BRU )
		{
			// branch instruction

			std::list<Token::Argument>::const_iterator branchDest = token.arguments().end();
			std::list<Token::Argument>::const_iterator branchStore = token.arguments().end();

			std::list<Token::Argument>::const_iterator i;
			for( i = token.arguments().begin(); i != token.arguments().end(); i++ )
			{
				if( (*i).flags() & Token::Argument::BRANCH )
					branchDest = i;

				if( (*i).flags() & Token::Argument::ADDRESS )
					branchStore = i;
			}

			if( token.arguments().end() == branchDest )
			{
				Error::Display( Error( "Invalid branch", token ) );
				return false;
			}

			if( token.arguments().end() != branchStore )
				state->storeAddress( *branchStore );

			std::list<Token>::iterator target;

			if( token.operand()->flags() & Operand::DYNAMIC )
			{
				if( !state->address( *branchDest, target, m_labels ) )
					continue;

				unsigned int targetLine = (*target).lineNumber();
				unsigned int currentLine = (*curr).lineNumber();
				bool isBackwardBranch = targetLine < currentLine;

				if( state->isBranchTaken( &*target ) )
				{
					if( isBackwardBranch )
					{
						// Backward branch (loop back-edge) already processed - extend ranges again and stop
						state->extendLiveRanges( targetLine, currentLine );
					}
					break;
				}

				// First time seeing this branch - extend ranges now if it's a backward branch
				if( isBackwardBranch )
				{
					state->extendLiveRanges( targetLine, currentLine );
				}

				if( !updateDynamicTracker( &*state->current() ) )
					break;

				state->storeBranch( &*target );

				BranchState* newState = new BranchState( *state );
				assert( newState );

				newState->pushTraces( &*curr, false );
				newState->pushTraces( &*target, true );
				newState->setCurrent( target );

				m_states.push_back( newState );
			}
			else
			{
				if( !state->address( *branchDest, target, m_labels ) )
					break;

				if( state->isBranchTaken( &*target ) )
				{
					// Backward branch (loop back-edge) - extend live ranges to cover loop body
					unsigned int targetLine = (*target).lineNumber();
					unsigned int currentLine = (*curr).lineNumber();
					if( targetLine < currentLine )
						state->extendLiveRanges( targetLine, currentLine );
					break;
				}

				state->storeBranch( &*target );

				state->pushTraces( &*curr, false );
				state->pushTraces( &*target, true );

				curr = target;
			}
		}
	}

	if( state->current() == state->exitPoint() )
	{
		if( !state->applyRegisterOutputs() )
		{
			Error::Display( Error( "Output register conflict", *(state->exitPoint()) ) );
			return false;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::collectLabels( std::list<Token>::iterator start, std::list<Token>::iterator end )
{
	m_labels.clear();

	for( std::list<Token>::iterator i = start; i != end; i++ )
	{
		if( (*i).label().length() )
		{
			std::map<std::string,std::list<Token>::iterator>::iterator j = m_labels.find( (*i).label() );

			if( j != m_labels.end() )
			{
				Error::Display( Error( "Duplicate label '" + (*i).label() + "'", *i ) );
				return false;
			}

			m_labels[ (*i).label() ] = i;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processAliases()
{
	// allocate registers that do not overlap usage

	// Count total aliases by type
	int totalFloatAliases = 0;
	int totalIntAliases = 0;
	int preallocatedFloats = 0;
	int preallocatedInts = 0;

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		if( i->first->type() == Alias::FLOAT )
		{
			totalFloatAliases++;
			if( i->first->allocatedRegister() )
				preallocatedFloats++;
		}
		else
		{
			totalIntAliases++;
			if( i->first->allocatedRegister() )
				preallocatedInts++;
		}
	}

	// Count available registers
	int availableFloats = 0;
	int availableInts = 0;
	for( int i = 0; i < 32; i++ )
		if( m_floats[i].available() )
			availableFloats++;
	for( int i = 0; i < 16; i++ )
		if( m_integers[i].available() )
			availableInts++;

	if( m_showRegisterInfo )
	{
		std::cerr << "\n=== Register Allocation Debug ===" << std::endl;
		std::cerr << "Float registers: " << availableFloats << " available, "
		          << (totalFloatAliases - preallocatedFloats) << " needed (total aliases: "
		          << totalFloatAliases << ", preallocated: " << preallocatedFloats << ")" << std::endl;
		std::cerr << "Integer registers: " << availableInts << " available, "
		          << (totalIntAliases - preallocatedInts) << " needed (total aliases: "
		          << totalIntAliases << ", preallocated: " << preallocatedInts << ")" << std::endl;

		// Print all alias ranges for debugging
		std::cerr << "\n=== Alias Ranges ===" << std::endl;
		for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
		{
			Alias* alias = i->first;
			std::cerr << (alias->type() == Alias::FLOAT ? "FLOAT" : "INT") << " "
			          << (alias->debugName().empty() ? "?" : alias->debugName())
			          << " #" << alias->id() << ": ";
			if( alias->allocatedRegister() )
				std::cerr << "[prealloc: " << alias->allocatedRegister()->name() << "] ";
			alias->printRanges( std::cerr );
			std::cerr << std::endl;
		}
		std::cerr << "====================\n" << std::endl;
	}

	// Pre-pass: allocate every two-address chain ATOMICALLY (root +
	// all its known successors) before any singleton aliases run the
	// main loop.  Treating chain members one-at-a-time isn't enough,
	// because an unrelated singleton alias whose live range fits in
	// the same register can snipe it before the successor reaches the
	// main loop — leaving the successor with no choice but a different
	// register, which is exactly the bug we're trying to prevent (the
	// `isubiu` dest then disagrees with its source, the loop counter
	// never decrements, and xgkick never fires).
	//
	// For each unallocated root (no sameNamePredecessor), find every
	// alias whose chain root is this root, then pick a register that
	// no non-chain allocated alias intersects on ANY chain member's
	// range.  Assign that register to all chain members at once.
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* root = i->first;
		if( root->allocatedRegister() )
			continue;
		if( root->sameNamePredecessor() )
			continue;

		// Collect chain members: walk every alias's predecessor chain
		// (with a hard hop cap as a cycle defence) and gather the ones
		// that terminate at `root`.  This is O(N²) but N is small.
		std::vector<Alias*> chain;
		chain.push_back( root );
		for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
		{
			Alias* other = k->first;
			if( other == root ) continue;
			if( other->type() != root->type() ) continue;
			Alias* p = other;
			for( int hop = 0; hop < 32 && p->sameNamePredecessor(); ++hop )
				p = p->sameNamePredecessor();
			if( p == root )
				chain.push_back( other );
		}

		unsigned int maxLimit = root->type() == Alias::FLOAT ? 32 : 16;
		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (root->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];
			if( !candidate->available() )
				continue;

			// `candidate` must not collide with any already-allocated
			// non-chain alias on ANY chain member's live range.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); !conflict && k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != root->type() ) continue;
				if( src->allocatedRegister() != candidate ) continue;
				// Chain members don't conflict with each other on the
				// shared register — that's the whole point.
				bool inChain = false;
				for( unsigned int m = 0; m < chain.size(); ++m )
					if( chain[m] == src ) { inChain = true; break; }
				if( inChain ) continue;
				for( unsigned int m = 0; m < chain.size(); ++m )
				{
					if( chain[m]->intersects( src ) )
					{
						conflict = true;
						break;
					}
				}
			}

			if( !conflict )
			{
				for( unsigned int m = 0; m < chain.size(); ++m )
					chain[m]->setAllocatedRegister( candidate );
				break;
			}
		}
	}

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* dest = i->first;

		// preallocated register
		if( dest->allocatedRegister() )
			continue;

		unsigned int maxLimit = dest->type() == Alias::FLOAT ? 32 : 16;

		// Two-address coalescing: if `dest` was created by a write that
		// reuses an existing source-level alias name (e.g. the dest of
		// `isubiu x, x, 1`), try the predecessor's register FIRST.  The
		// predecessor may itself be unallocated yet; walk the chain to
		// find the earliest allocated ancestor.  Falls through to the
		// regular j=0..maxLimit scan if no predecessor / preferred reg
		// is available or it conflicts.
		const Register* preferred = NULL;
		// Walk to the root, with a hard cap to defeat any residual cycle
		// the branch-state analysis might have introduced via loop re-
		// processing.  16 is well above any plausible chain depth in
		// real shaders.
		Alias* chainRoot = dest;
		for( int hop = 0; hop < 16 && chainRoot->sameNamePredecessor(); ++hop )
			chainRoot = chainRoot->sameNamePredecessor();
		if( chainRoot != dest && chainRoot->allocatedRegister() )
			preferred = chainRoot->allocatedRegister();
		if( preferred )
		{
			// Run the normal conflict check against `preferred`.  If it
			// holds, assign immediately and skip the j-loop.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != dest->type() ) continue;
				if( src->allocatedRegister() != preferred ) continue;
				if( src == dest ) continue;
				// Same-name predecessor on the chain doesn't count as a
				// conflict: that's the whole point of coalescing.
				bool isAncestor = false;
				for( Alias* p = dest->sameNamePredecessor(); p; p = p->sameNamePredecessor() )
				{
					if( p == src ) { isAncestor = true; break; }
				}
				if( isAncestor ) continue;
				if( !dest->intersects( src ) ) continue;
				conflict = true;
				break;
			}
			if( !conflict )
			{
				dest->setAllocatedRegister( preferred );
				continue;
			}
		}

		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (dest->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];

			if( !candidate->available() )
				continue;

			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;

				if( !src->allocatedRegister() )
					continue;

				if( src->type() != dest->type() )
					continue;

				if( src->allocatedRegister() != candidate )
					continue;

				if( !dest->intersects( src ) )
					continue;

				candidate = NULL;
				break;
			}

			if( candidate )
			{
				dest->setAllocatedRegister( candidate );
				break;
			}
		}

		if( !dest->allocatedRegister() )
		{
			if( m_showRegisterInfo )
			{
				std::cerr << "Failed to allocate " << (dest->type() == Alias::FLOAT ? "FLOAT" : "INTEGER")
				          << " register for alias " << (dest->debugName().empty() ? "?" : dest->debugName())
				          << " #" << dest->id() << std::endl;
			}
			return false;
		}
		//assert( dest->allocatedRegister() );
	}

	if( m_showRegisterInfo )
	{
		std::cerr << std::endl << "=== Final assignment ===" << std::endl;
		for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
		{
			Alias* alias = i->first;
			std::cerr << alias->allocatedRegister()->name() << " <- "
			          << (alias->debugName().empty() ? "?" : alias->debugName())
			          << " #" << alias->id() << "  ";
			alias->printRanges( std::cerr );
			std::cerr << std::endl;
		}
		std::cerr << "====================" << std::endl << std::endl;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	void extendContinuationBlockLiveRanges( std::list<Token>::iterator blockStart, std::list<Token>::iterator blockEnd )
	{
		for( std::list<Token>::iterator cont = blockStart; cont != blockEnd; ++cont )
		{
			if( !cont->operand() || cont->operand()->name() != "--cont" )
				continue;

			const unsigned int contLine = cont->lineNumber();
			unsigned int blockEndLine = contLine;
			std::set<Alias*> writtenBeforeCont;
			std::set<Alias*> liveAcrossCont;

			for( std::list<Token>::iterator t = blockStart; t != blockEnd; ++t )
			{
				if( t->lineNumber() > blockEndLine )
					blockEndLine = t->lineNumber();

				for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
				{
					if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
						continue;

					Alias* alias = a->dependency()->alias();
					if( t->lineNumber() < contLine && (a->flags() & Token::Argument::WRITE) )
						writtenBeforeCont.insert( alias );
					else if( t->lineNumber() > contLine
					         && !(a->flags() & Token::Argument::WRITE)
					         && writtenBeforeCont.find( alias ) != writtenBeforeCont.end() )
						liveAcrossCont.insert( alias );
				}
			}

			for( std::set<Alias*>::iterator a = liveAcrossCont.begin(); a != liveAcrossCont.end(); ++a )
				(*a)->addRange( contLine, blockEndLine );
		}
	}
}

void RegisterAllocator::extendContinuationLiveRanges( std::list<Token>& tokens )
{
	std::list<Token>::iterator blockStart = tokens.end();
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( !i->operand() )
			continue;

		if( i->operand()->name() == "--endenter" )
		{
			blockStart = i;
			++blockStart;
			continue;
		}

		const bool startsNewBlock = (i->operand()->unit() == Operand::ENTER);
		const bool startsExitBlock = (i->operand()->unit() == Operand::EXIT);
		if( blockStart == tokens.end() || (!startsNewBlock && !startsExitBlock) )
			continue;

		std::list<Token>::iterator blockEnd = i;
		extendContinuationBlockLiveRanges( blockStart, blockEnd );

		blockStart = tokens.end();
		if( startsNewBlock )
		{
			blockStart = i;
			++blockStart;
		}
	}

	if( blockStart != tokens.end() )
		extendContinuationBlockLiveRanges( blockStart, tokens.end() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::loopTargetHasLoopDirective( std::list<Token>::iterator target, std::list<Token>::iterator end ) const
{
	if( target == end )
		return false;

	if( target->operand() && target->operand()->name() == "--LoopCS" )
		return true;

	std::list<Token>::iterator next = target;
	++next;
	if( next != end && next->operand() && next->operand()->name() == "--LoopCS" )
		return true;

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendLoopDirectiveRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd )
{
	// Which aliases does this loop body touch, and is the FIRST touch a read?
	// Only a read-first alias is live across the back edge - the next iteration
	// depends on what the previous one left in it. An alias written before it is read
	// inside the body is a temporary and its own range already covers it; stretching
	// those over the whole loop is what runs the allocator out of registers.
	//
	// Integers are collected on the same terms as floats. Tying their carried writes is
	// not enough on its own: an integer whose live range stops at its last use inside
	// the body has its register handed to another name for the rest of the loop, so the
	// next iteration reads whatever that name left. The range has to cover the back edge
	// too - the file is only 16 registers, so this is where a program that is already
	// tight will fail loudly instead of quietly computing the wrong thing.
	std::set<Alias*> aliases;
	std::set<Alias*> liveInAliases;
	std::set<Alias*> touched;
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;

		std::set<Alias*> readHere;
		std::set<Alias*> tokenAliases;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* alias = a->dependency()->alias();
			aliases.insert( alias );
			tokenAliases.insert( alias );
			if( !(a->flags() & Token::Argument::WRITE) )
				readHere.insert( alias );
		}

		// One token can both read and write the same alias (`add a, a, b`), and that
		// still reads what the previous iteration left, so the read wins.
		for( std::set<Alias*>::iterator i = tokenAliases.begin(); i != tokenAliases.end(); ++i )
		{
			if( touched.find( *i ) != touched.end() )
				continue;
			touched.insert( *i );
			if( readHere.find( *i ) != readHere.end() )
				liveInAliases.insert( *i );
		}
	}
	if( vuLoopLivenessAlwaysEnabled() )
		aliases = liveInAliases;

	unsigned int availableFloats = 0;
	unsigned int availableInts = 0;
	for( unsigned int i = 0; i < 32; ++i )
	{
		if( m_floats[i].available() )
			++availableFloats;
	}
	for( unsigned int i = 0; i < 16; ++i )
	{
		if( m_integers[i].available() )
			++availableInts;
	}

	std::set<Alias*> overlappingFloats;
	std::set<Alias*> overlappingInts;
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* alias = i->first;
		if( aliases.find( alias ) == aliases.end()
		    && !alias->hasRangeOverlapping( loopStart, loopEnd ) )
			continue;
		if( alias->type() == Alias::FLOAT )
			overlappingFloats.insert( alias );
		else
			overlappingInts.insert( alias );
	}

	// Skipping the extension is not a licence to emit wrong code: a value live
	// across the back edge whose range was not extended gets its register handed
	// to another name. --loop-liveness-always keeps the extension and lets
	// allocation fail loudly instead.
	if( !vuLoopLivenessAlwaysEnabled()
	    && ( overlappingFloats.size() > availableFloats
	         || overlappingInts.size() > availableInts ) )
		return;

	for( std::set<Alias*>::iterator a = aliases.begin(); a != aliases.end(); ++a )
		(*a)->addRange( loopStart, loopEnd );

	tieCarriedWritesToLiveInAliases( tokens, loopStart, loopEnd, liveInAliases );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::tieCarriedWritesToLiveInAliases( std::list<Token>& tokens,
                                                         unsigned int loopStart, unsigned int loopEnd,
                                                         const std::set<Alias*>& liveInAliases )
{
	// A name read before it is written inside the body is carried across the back
	// edge, and the readers at the top of the loop hold ONE alias for it. But a
	// fresh Alias is spawned for every write, so the update at the bottom of the
	// loop is a different alias - free to land in a different register. The write
	// then goes nowhere the reader looks: the next iteration sees what the previous
	// one saw, forever.
	//
	// Worse, those tail aliases get a live range one line long - the line of the
	// write itself - because nothing downstream reads them. Two such ranges do not
	// intersect, so the allocator may legally put two different carried names in one
	// register, where the second write destroys the first.
	//
	// Both follow from the same omission, so both have one fix: tie every in-loop
	// write of a carried name to the alias its readers use. openvcl already has the
	// machinery - the two-address chain, which exists so `isubiu x, x, 1` writes the
	// register it read - and the chain pre-pass then hands the whole chain a single
	// register.
	std::map<std::string, Alias*> carriedByName;
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			if( a->alias().empty() )
				continue;
			Alias* alias = a->dependency()->alias();
			if( liveInAliases.find( alias ) == liveInAliases.end() )
				continue;
			if( carriedByName.find( a->alias() ) == carriedByName.end() )
				carriedByName[ a->alias() ] = alias;
		}
	}

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( !(a->flags() & Token::Argument::WRITE) )
				continue;
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* written = a->dependency()->alias();
			std::map<std::string, Alias*>::iterator carried = carriedByName.find( a->alias() );
			if( carried == carriedByName.end() )
				continue;
			if( written == carried->second )
				continue;
			// An existing chain already says where this write must live; a carried
			// name cannot claim it too without contradicting the earlier link.
			if( written->sameNamePredecessor() )
				continue;
			written->setSameNamePredecessor( carried->second );
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendLoopDirectiveLiveRanges( std::list<Token>& tokens )
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
			continue;

		std::list<Token::Argument>::const_iterator branchDest = branch->arguments().end();
		for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin(); a != branch->arguments().end(); ++a )
		{
			if( a->flags() & Token::Argument::BRANCH )
			{
				branchDest = a;
				break;
			}
		}
		if( branchDest == branch->arguments().end() || branchDest->type() != Token::Argument::IMMEDIATE )
			continue;

		std::map< std::string, std::list<Token>::iterator >::iterator label = m_labels.find( branchDest->immediate() );
		if( label == m_labels.end() )
			continue;

		std::list<Token>::iterator target = label->second;
		if( target->lineNumber() >= branch->lineNumber() )
			continue;
		// A back edge is a back edge whether or not the source marked it with a --loop
		// directive: a value written at the bottom and read at the top is live across
		// it either way. Requiring the directive means a plain `label: ... ibne label`
		// loop - which is what hand-written VU code looks like - gets no extension at
		// all, and the allocator then hands one register to two live names.
		// See --loop-liveness-always.
		if( !vuLoopLivenessAlwaysEnabled()
		    && !loopTargetHasLoopDirective( target, tokens.end() ) )
			continue;

		extendLoopDirectiveRange( tokens, target->lineNumber(), branch->lineNumber() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	struct MultiQStage
	{
		MultiQStage()
		{
			producerLine = 0;
		}

		unsigned int producerLine;
		std::vector<unsigned int> consumerLines;
	};

	bool tokenTouchesQ( const Token& token, bool& readsQ, bool& writesQ )
	{
		readsQ = false;
		writesQ = false;
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::Q )
				continue;
			if( a->flags() & Token::Argument::WRITE )
				writesQ = true;
			else
				readsQ = true;
		}
		return readsQ || writesQ;
	}

	void collectFloatAliasesFromToken( const Token& token, std::set<Alias*>& aliases )
	{
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* alias = a->dependency()->alias();
			if( alias->type() == Alias::FLOAT )
				aliases.insert( alias );
		}
	}

	bool stringListContains( const std::list<std::string>& values, const std::string& value )
	{
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( *i == value )
				return true;
		}
		return false;
	}

	bool stringListsIntersect( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( stringListContains( b, *i ) )
				return true;
		}
		return false;
	}

	void addUniqueStrings( std::list<std::string>& dest, const std::list<std::string>& src )
	{
		for( std::list<std::string>::const_iterator i = src.begin(); i != src.end(); ++i )
		{
			if( !stringListContains( dest, *i ) )
				dest.push_back( *i );
		}
	}

	void addAliasRange( const std::set<Alias*>& aliases, unsigned int beginLine, unsigned int endLine )
	{
		if( beginLine > endLine )
			return;
		for( std::set<Alias*>::const_iterator a = aliases.begin(); a != aliases.end(); ++a )
			(*a)->addRange( beginLine, endLine );
	}

	void collectMultiQStages( std::list<Token>& tokens,
	                          unsigned int loopStart,
	                          unsigned int loopEnd,
	                          std::vector<MultiQStage>& stages,
	                          unsigned int& qReads,
	                          unsigned int& qWrites )
	{
		qReads = 0;
		qWrites = 0;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
		{
			if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
				continue;

			bool readsQ = false;
			bool writesQ = false;
			if( !tokenTouchesQ( *t, readsQ, writesQ ) )
				continue;

			if( writesQ )
			{
				MultiQStage stage;
				stage.producerLine = t->lineNumber();
				stages.push_back( stage );
				++qWrites;
			}

			if( readsQ )
			{
				++qReads;
				if( !stages.empty() )
					stages.back().consumerLines.push_back( t->lineNumber() );
			}
		}
	}

	void collectMultiQProducerDependencySliceAliases( std::list<Token>& tokens,
	                                                  unsigned int beginLine,
	                                                  unsigned int producerLine,
	                                                  std::set<Alias*>& aliases )
	{
		std::vector<Token*> rangeTokens;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
		{
			if( t->lineNumber() < beginLine || t->lineNumber() > producerLine )
				continue;
			rangeTokens.push_back( &*t );
		}
		if( rangeTokens.empty() )
			return;

		VuTokenResourceAccess producerAccess;
		if( !buildVuTokenResourceAccess( *rangeTokens.back(), producerAccess ) )
			return;

		std::list<std::string> neededRegisters = producerAccess.registerReads;
		unsigned int neededResources = producerAccess.implicitReads;
		collectFloatAliasesFromToken( *rangeTokens.back(), aliases );

		for( unsigned int reverse = static_cast<unsigned int>( rangeTokens.size() ); reverse > 0; --reverse )
		{
			Token& token = *rangeTokens[reverse - 1];
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( token, access ) )
				continue;

			const bool feedsNeededRegister = stringListsIntersect( access.registerWrites, neededRegisters );
			const bool feedsNeededResource = (access.implicitWrites & neededResources) != 0;
			if( !feedsNeededRegister && !feedsNeededResource )
				continue;

			collectFloatAliasesFromToken( token, aliases );
			addUniqueStrings( neededRegisters, access.registerReads );
			neededResources |= access.implicitReads;
		}
	}

	void extendAdjacentMultiQStageAliases( std::list<Token>& tokens,
	                                       const std::vector<MultiQStage>& stages,
	                                       unsigned int loopEnd )
	{
		if( stages.size() < 2 )
			return;

		for( unsigned int stage = 1; stage < stages.size(); ++stage )
		{
			if( stages[stage - 1].consumerLines.empty() )
				continue;

			const unsigned int previousProducerLine = stages[stage - 1].producerLine;
			const unsigned int previousLastConsumerLine = stages[stage - 1].consumerLines.back();
			const unsigned int currentProducerLine = stages[stage].producerLine;
			if( previousProducerLine == 0
			    || currentProducerLine == 0
			    || previousLastConsumerLine >= currentProducerLine )
				continue;

			std::set<Alias*> aliases;
			collectMultiQProducerDependencySliceAliases( tokens,
			                                             previousLastConsumerLine + 1,
			                                             currentProducerLine,
			                                             aliases );
			addAliasRange( aliases, previousProducerLine, currentProducerLine );
		}
		(void)loopEnd;
	}
}

void RegisterAllocator::extendMultiQStageRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd )
{
	std::set<Alias*> qStageAliases;
	std::vector<MultiQStage> stages;
	unsigned int qReads = 0;
	unsigned int qWrites = 0;

	collectMultiQStages( tokens, loopStart, loopEnd, stages, qReads, qWrites );
	if( qWrites > 1 && qReads > 0 )
		extendAdjacentMultiQStageAliases( tokens, stages, loopEnd );

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;

		bool readsQ = false;
		bool writesQ = false;
		if( !tokenTouchesQ( *t, readsQ, writesQ ) )
			continue;

		collectFloatAliasesFromToken( *t, qStageAliases );
	}

	if( qWrites <= 1 || qReads == 0 || qStageAliases.empty() )
		return;

	unsigned int availableFloats = 0;
	for( unsigned int i = 0; i < 32; ++i )
	{
		if( m_floats[i].available() )
			++availableFloats;
	}

	std::set<Alias*> overlappingAliases;
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* alias = i->first;
		if( alias->type() != Alias::FLOAT )
			continue;
		if( qStageAliases.find( alias ) != qStageAliases.end() || alias->hasRangeOverlapping( loopStart, loopEnd ) )
			overlappingAliases.insert( alias );
	}

	// Skipping the extension is not a licence to emit wrong code: a value live
	// across the back edge whose range was not extended gets its register handed
	// to another name. --loop-liveness-always keeps the extension and lets
	// allocation fail loudly instead.
	if( !vuLoopLivenessAlwaysEnabled()
	    && overlappingAliases.size() > availableFloats )
		return;

	for( std::set<Alias*>::iterator a = qStageAliases.begin(); a != qStageAliases.end(); ++a )
		(*a)->addRange( loopStart, loopEnd );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendMultiQStageLiveRanges( std::list<Token>& tokens )
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
			continue;

		std::list<Token::Argument>::const_iterator branchDest = branch->arguments().end();
		for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin(); a != branch->arguments().end(); ++a )
		{
			if( a->flags() & Token::Argument::BRANCH )
			{
				branchDest = a;
				break;
			}
		}
		if( branchDest == branch->arguments().end() || branchDest->type() != Token::Argument::IMMEDIATE )
			continue;

		std::map< std::string, std::list<Token>::iterator >::iterator label = m_labels.find( branchDest->immediate() );
		if( label == m_labels.end() )
			continue;

		std::list<Token>::iterator target = label->second;
		if( target->lineNumber() >= branch->lineNumber() )
			continue;
		if( !loopTargetHasLoopDirective( target, tokens.end() ) )
			continue;

		extendMultiQStageRange( tokens, target->lineNumber(), branch->lineNumber() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::collectLiteralRegisterUsage( std::list<Token>& tokens )
{
	// One synthetic alias per (type, register) pair, with one range per
	// usage line.  Inserted into m_aliases pre-allocated to its physical
	// register so the existing conflict check in processAliases naturally
	// keeps user aliases off it.
	Alias* floats[32];
	Alias* integers[16];
	for( unsigned int i = 0; i < 32; i++ ) floats[i]   = NULL;
	for( unsigned int i = 0; i < 16; i++ ) integers[i] = NULL;

	for( std::list<Token>::iterator it = tokens.begin(); it != tokens.end(); ++it )
	{
		if( !it->operand() )
			continue;
		// Preprocessor directives (--enter, in_vf, .name, etc.) don't emit
		// hardware ops — their register references shouldn't pin physical
		// registers across the whole program.
		if( it->operand()->flags() & Operand::PREPROCESSOR )
			continue;
		if( it->flags() & Token::IGNORED )
			continue;

		const unsigned int line = it->lineNumber();

		for( std::list<Token::Argument>::const_iterator a = it->arguments().begin(); a != it->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::REGISTER )
				continue;

			if( a->type() == Token::Argument::FLOAT_REGISTER )
			{
				// Float side intentionally disabled — adding synthetic
				// VF aliases (even just for vf00, the zero register)
				// shifts the allocator's choices and ends up splitting
				// `vert_xform` between two VF sets across the [E]
				// halt boundary (load into VF05..VF08, use in
				// xform_loop as VF01..VF04 = garbage).  The integer-
				// side fix below is the one that matters for the
				// originally-motivating `do_clipping` / VI01 bug.
				(void)a;
				continue;
			}
			else if( a->type() == Token::Argument::INTEGER_REGISTER )
			{
				int r = a->regNumber();
				if( r < 0 || r >= 16 )
					continue;
				if( !integers[r] )
				{
					integers[r] = new Alias( Alias::INTEGER );
					integers[r]->setAllocatedRegister( &m_integers[r] );
					m_aliases[ integers[r] ] = integers[r];
				}
				integers[r]->addRange( line, line );
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// One reference to an alias: which line, which components, and whether it
	// was the destination.  Sources are recorded before the destination of the
	// same token, which is the order the hardware sees them in.
	struct AliasAccess
	{
		AliasAccess() : m_line(0), m_fields(0), m_write(false) {}
		AliasAccess( unsigned int line, unsigned int fields, bool write )
			: m_line(line), m_fields(fields), m_write(write) {}

		unsigned int m_line;
		unsigned int m_fields;
		bool m_write;
	};

	// Which components an operand really touches.  A float source written
	// without a selector is read under the instruction's destination mask - a
	// `mini.xyz d,s,t` never looks at s.w - and openvcl leaves such an operand's
	// field mask at zero, meaning "whatever the instruction says". Reading that
	// zero as "all four" would make every masked read look like a read of an
	// undefined w. An integer register has no components at all, so it is always
	// whole.
	unsigned int accessFields( const Token& token, const Token::Argument& argument )
	{
		const unsigned int all = Token::X | Token::Y | Token::Z | Token::W;
		if( argument.type() == Token::Argument::INTEGER_REGISTER )
			return all;
		if( argument.fields() )
			return argument.fields();
		return token.fields() ? token.fields() : all;
	}

	bool tokenIsEmittable( const Token& token )
	{
		if( !token.operand() )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.flags() & Token::IGNORED )
			return false;
		return true;
	}

	// Every backward branch in the program, as [target line, branch line].
	void collectBackEdges( std::list<Token>& tokens,
	                       const std::map< std::string, std::list<Token>::iterator >& labels,
	                       std::vector< std::pair<unsigned int, unsigned int> >& loops )
	{
		for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
		{
			if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
				continue;

			std::list<Token::Argument>::const_iterator dest = branch->arguments().end();
			for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin();
			     a != branch->arguments().end(); ++a )
			{
				if( a->flags() & Token::Argument::BRANCH )
				{
					dest = a;
					break;
				}
			}
			if( dest == branch->arguments().end() || dest->type() != Token::Argument::IMMEDIATE )
				continue;

			std::map< std::string, std::list<Token>::iterator >::const_iterator label =
				labels.find( dest->immediate() );
			if( label == labels.end() )
				continue;

			if( label->second->lineNumber() >= branch->lineNumber() )
				continue;

			loops.push_back( std::make_pair( label->second->lineNumber(), branch->lineNumber() ) );
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::trimUncarriedLoopLocalRanges( std::list<Token>& tokens )
{
	// The branch-state analysis merges every alias a source-level name ever had
	// into one, and the trace machinery then stretches the survivor from the
	// enclosing loop's entry point to the last line it walked. For a value that
	// is recomputed from scratch every iteration - a transformed vertex, a
	// per-vertex colour - that turns a range twenty lines long into one covering
	// the whole loop body. Three of those cost three registers for nothing, and
	// on a three-vertex batch that is the difference between fitting in 31 VF
	// registers and not.
	//
	// A range may be pulled back to its own accesses only when the value dies at
	// its last use, and the three conditions that establish that are all
	// necessary:
	//
	//   * every component read is written by an EARLIER access, so the iteration
	//     does not depend on what the previous one left behind;
	//   * every access sits inside the same loops, so nothing outside a loop
	//     defines a value read inside it (which WOULD have to survive the back
	//     edge, and is what the extension passes exist to cover);
	//   * no branch lies between the first and last access, so the access list
	//     read in line order is the order the value actually sees. Without this
	//     a definition on one arm of a conditional reads as covering both.
	std::vector< std::pair<unsigned int, unsigned int> > loops;
	collectBackEdges( tokens, m_labels, loops );

	std::map< Alias*, std::vector<AliasAccess> > accesses;
	std::vector<unsigned int> branchLines;

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !tokenIsEmittable( *t ) )
			continue;

		const unsigned int line = t->lineNumber();

		if( t->operand()->unit() == Operand::BRU )
			branchLines.push_back( line );

		// Sources first, destination last - a token that reads and writes the
		// same name reads the previous value.
		for( int pass = 0; pass < 2; ++pass )
		{
			for( std::list<Token::Argument>::const_iterator a = t->arguments().begin();
			     a != t->arguments().end(); ++a )
			{
				const bool write = (a->flags() & Token::Argument::WRITE) != 0;
				if( write != (pass == 1) )
					continue;
				if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
					continue;
				accesses[ a->dependency()->alias() ].push_back(
					AliasAccess( line, accessFields( *t, *a ), write ) );
			}
		}
	}

	for( std::map< Alias*, std::vector<AliasAccess> >::iterator i = accesses.begin();
	     i != accesses.end(); ++i )
	{
		Alias* alias = i->first;
		const std::vector<AliasAccess>& acc = i->second;

		// An input or output register is pinned to a physical register by the
		// --enter/--exit contract; its liveness is not ours to shorten.
		if( alias->allocatedRegister() )
			continue;
		if( acc.empty() )
			continue;

		unsigned int lo = 0;
		unsigned int hi = 0;
		if( !alias->rangeExtent( lo, hi ) )
			continue;

		const unsigned int firstLine = acc.front().m_line;
		const unsigned int lastLine = acc.back().m_line;
		if( firstLine > lastLine )
			continue;
		(void)lo;
		(void)hi;

		// Condition 1: no component is read before it is written.
		unsigned int defined = 0;
		bool carried = false;
		for( std::vector<AliasAccess>::const_iterator a = acc.begin(); a != acc.end(); ++a )
		{
			if( a->m_write )
			{
				defined |= a->m_fields;
				continue;
			}
			if( a->m_fields & ~defined )
			{
				carried = true;
				break;
			}
		}
		if( carried )
			continue;

		// Condition 2: every loop either contains the whole span or misses it.
		bool crossesLoop = false;
		for( unsigned int l = 0; !crossesLoop && l < loops.size(); ++l )
		{
			const unsigned int start = loops[l].first;
			const unsigned int stop = loops[l].second;
			const bool overlaps = !(lastLine < start || stop < firstLine);
			const bool contained = firstLine >= start && lastLine <= stop;
			if( overlaps && !contained )
				crossesLoop = true;
		}
		if( crossesLoop )
			continue;

		// Condition 3: straight-line control flow between the two ends.
		bool branchInside = false;
		for( unsigned int b = 0; !branchInside && b < branchLines.size(); ++b )
		{
			if( branchLines[b] > firstLine && branchLines[b] < lastLine )
				branchInside = true;
		}
		if( branchInside )
			continue;

		// The precise thing: one interval per definition, from the defining line
		// to that definition's last reader, per component. A scalar scratch name
		// reused once per vertex is a merged alias with ONE range spanning the
		// whole batch (vu_script3_d's `vuS1` is [86-229]); its three generations
		// are independent, and the register is free in the holes between them.
		// Nothing here invents liveness: every interval starts at a write this
		// alias performs and ends at a read of that write.
		std::vector< std::pair<unsigned int, unsigned int> > intervals;
		for( unsigned int component = 0; component < 4; ++component )
		{
			const unsigned int mask = 1u << component;
			bool open = false;
			unsigned int defLine = 0;
			unsigned int lastUse = 0;

			for( std::vector<AliasAccess>::const_iterator a = acc.begin(); a != acc.end(); ++a )
			{
				if( !(a->m_fields & mask) )
					continue;

				if( a->m_write )
				{
					if( open )
						intervals.push_back( std::make_pair( defLine, lastUse ) );
					open = true;
					defLine = a->m_line;
					lastUse = a->m_line;
				}
				else if( open )
					lastUse = a->m_line;
			}

			if( open )
				intervals.push_back( std::make_pair( defLine, lastUse ) );
		}

		if( intervals.empty() )
		{
			alias->clipRanges( firstLine, lastLine );
			continue;
		}

		alias->clearRanges();
		for( unsigned int n = 0; n < intervals.size(); ++n )
			alias->addRange( intervals[n].first, intervals[n].second );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::coalesceSameNameFloatWrites( std::list<Token>& tokens )
{
	// `mul.x fogAccum, fogAccum, fogParams[z]` is one value, but openvcl gives
	// the read one Alias and the write another, and the two ranges touch on the
	// shared line, so they interfere and the chain alternates between two
	// registers. The integer side already fixes this - it had to, or `isubiu x,
	// x, 1` decremented a register nobody read - via setSameNamePredecessor and
	// the atomic chain pre-pass in processAliases. This is the same edge for
	// floats, with one extra precondition the integer path gets for free: the
	// predecessor must be dead from the write on. If it is still live afterwards
	// the two values coexist, and sharing a register would destroy one of them.
	std::map<std::string, Alias*> lastByName;

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !tokenIsEmittable( *t ) )
			continue;

		const unsigned int line = t->lineNumber();

		for( int pass = 0; pass < 2; ++pass )
		{
			for( std::list<Token::Argument>::iterator a = t->arguments().begin();
			     a != t->arguments().end(); ++a )
			{
				const bool write = (a->flags() & Token::Argument::WRITE) != 0;
				if( write != (pass == 1) )
					continue;
				if( a->type() != Token::Argument::FLOAT_REGISTER )
					continue;
				if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
					continue;

				Alias* alias = a->dependency()->alias();
				if( alias->type() != Alias::FLOAT )
					continue;

				if( !write )
				{
					lastByName[ a->alias() ] = alias;
					continue;
				}

				std::map<std::string, Alias*>::iterator previous = lastByName.find( a->alias() );
				if( previous != lastByName.end()
				    && previous->second != alias
				    && !alias->sameNamePredecessor()
				    && !alias->allocatedRegister()
				    && !previous->second->allocatedRegister() )
				{
					unsigned int lo = 0;
					unsigned int hi = 0;
					if( previous->second->rangeExtent( lo, hi ) && hi <= line )
					{
						// A chain must stay a chain: refuse an edge that would
						// close a cycle, the same defence BranchState uses.
						bool wouldCycle = false;
						Alias* p = previous->second;
						for( int hop = 0; hop < 32 && p; ++hop, p = p->sameNamePredecessor() )
						{
							if( p == alias ) { wouldCycle = true; break; }
						}
						if( !wouldCycle )
						{
							alias->setSameNamePredecessor( previous->second );
							m_coalescedWrites.push_back( alias );
						}
					}
				}

				lastByName[ a->alias() ] = alias;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Alias* RegisterAllocator::obtainAlias( Alias::Type type )
{
	Alias* alias = new Alias( type );

	m_aliases[ alias ] = alias;

	return alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::releaseAlias( Alias* alias )
{
	AliasMap::iterator i = m_aliases.find( alias );
	assert( i != m_aliases.end() );

	// Any alias whose same-name predecessor chain points at `alias` is
	// about to hold a dangling pointer; clear those edges so processAliases
	// doesn't walk into freed memory.  (Without this guard openvcl segfaults
	// when branch-state merges release one half of a previously-recorded
	// two-address pair.)
	for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
	{
		Alias* other = k->first;
		if( other == alias )
			continue;
		if( other->sameNamePredecessor() == alias )
			other->setSameNamePredecessor( NULL );
	}

	m_aliases.erase( i );

	delete alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processCommonDirective( Token& token )
{
	if( !token.operand() )
		return true;

	if( token.operand()->name() == ".name" )
	{
		m_name = token.arguments().begin()->immediate();
		token.setFlags( token.flags() | Token::IGNORED );
		return true;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::updateDynamicTracker( const Token* src )
{
	std::map< const Token*, unsigned int >::iterator i = m_dynamicTracker.find( src );

	if( m_dynamicTracker.end() == i )
	{
		m_dynamicTracker[ src ] = 1;
		return true;
	}

	if( i->second > m_dynamicThreshold )
		return false;

	m_dynamicTracker[ src ] = i->second+1;
	return true;
}


}
