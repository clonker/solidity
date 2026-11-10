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

#include <vector>

namespace solidity::yul::ssa::stack
{

/// What a predecessor hands to an edge into a block
struct IncomingEdge
{
	SSACFG::BlockId predecessor;
	/// The stack the predecessor leaves, for a conditional jump after popping the condition
	StackData stack;
};

/// Chooses the stack-in of the block `_block` from its incoming forward edges. There must be at least one incoming edge.
StackData chooseBlockStackIn(
	LivenessAnalysis const& _liveness,
	SSACFG::BlockId _block,
	std::vector<IncomingEdge> const& _edges,
	spill::SpillSet& _spillSet,
	bool _spillingAllowed
);

/// Shuffles `_exitStack` across an edge into a block with stack-in `_stackIn`: towards `_stackIn` with the shadow slots
/// written on the edge (`_writes`, the live writes of the edge's source block) replaced by the inputs of their
/// upsilons, then renaming those inputs to the shadow slots.
/// All inputs are in place before the first rename, so the writes of an edge are a parallel copy.
ShuffleResult shuffleAcrossEdge(
	SSACFG const& _cfg,
	StackData _exitStack,
	StackData const& _stackIn,
	ShadowLiveness::Writes const& _writes,
	spill::SpillSet& _spillSet,
	bool _spillingAllowed
);

}
