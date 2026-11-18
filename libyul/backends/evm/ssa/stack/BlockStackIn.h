/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include <libyul/backends/evm/ssa/spill/SpillSet.h>
#include <libyul/backends/evm/ssa/stack/Shuffler.h>

#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>
#include <libyul/backends/evm/ssa/ShadowLiveness.h>
#include <libyul/backends/evm/ssa/StackSlot.h>

#include <vector>

namespace solidity::yul::ssa::stack
{

/// An upsilon whose copy of its input is left to the edge that its block jumps along
struct PendingCopy
{
	StackSlot shadowSlot;
	StackSlot input;
};

/// What a predecessor hands to an edge into a block
struct IncomingEdge
{
	SSACFG::BlockId predecessor;
	/// The stack the predecessor leaves, for a conditional jump after popping the condition
	StackData stack;
	/// The upsilons of the predecessor whose copy is left to the edge. No shadow slot occurs twice or on `stack`, and
	/// every input is a literal, on `stack` or spilled.
	std::vector<PendingCopy> pendingCopies{};
};

/// Chooses the stack-in of a block from its incoming forward edges, of which there is at least one. Every incoming
/// edge is shuffled to the chosen stack afterwards, so the choice only affects code quality, except for the edge of
/// a conditional jump into its nonZero target, which cannot carry code.
///
/// Each edge offers one candidate: its stack followed by the shadow slots of its pending copies, with the slots that
/// are dead on entry to the block declared junk. The candidate of a single edge is taken as it is. Of several, the
/// one wins that needs the fewest spills to shuffle every edge into it, then the least gas for these shuffles, then
/// the smallest stack. `_spillSet` grows by the spills of the winner.
StackData chooseBlockStackIn(
	SSACFG const& _cfg,
	std::vector<IncomingEdge> const& _edges,
	LivenessAnalysis::LivenessData const& _liveIn,
	ShadowLiveness::ShadowSet const& _liveInShadows,
	spill::SpillSet& _spillSet,
	bool _spillingAllowed
);

/// Shuffles the stack of `_edge` into `_stackIn`, the stack-in of the block the edge enters. `_edge` need not be one of
/// the edges the stack-in was chosen from, a back edge is not. The pending copies are made on the way: their inputs
/// are shuffled to where `_stackIn` has the shadow slots and renamed there. A pending copy whose shadow slot
/// `_stackIn` does not hold is not made, which is right only for a shadow that is dead on entry to the block.
/// A shuffle that is not admissible is returned as it is.
ShuffleResult shuffleAcrossEdge(
	IncomingEdge const& _edge,
	StackData const& _stackIn,
	spill::SpillSet& _spillSet,
	bool _spillingAllowed
);

}
