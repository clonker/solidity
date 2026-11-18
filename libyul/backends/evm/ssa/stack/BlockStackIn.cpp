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

#include <libyul/backends/evm/ssa/stack/BlockStackIn.h>

#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackUtils.h>

#include <range/v3/algorithm/replace.hpp>

#include <tuple>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::stack;

namespace
{
void declareJunk(
	Stack& _stack,
	LivenessAnalysis::LivenessData const& _live,
	ShadowLiveness::ShadowSet const& _liveShadows
)
{
	for (StackOffset offset{0}; offset < _stack.size(); ++offset.value)
	{
		auto const& slot = _stack[offset];
		if (
			(slot.isValue() && !_live.contains(slot.value())) ||
			(slot.isShadow() && !_liveShadows.contains(slot.shadowPhi()))
		)
			_stack.declareJunk(offset);
	}
}

/// The stack-in that `_edge` offers: its stack and the shadow slots of its pending copies, dead slots declared junk
StackData candidateOf(
	IncomingEdge const& _edge,
	LivenessAnalysis::LivenessData const& _liveIn,
	ShadowLiveness::ShadowSet const& _liveInShadows
)
{
	StackData candidate = _edge.stack;
	for (PendingCopy const& copy: _edge.pendingCopies)
		candidate.push_back(copy.shadowSlot);
	Stack stack(candidate);
	declareJunk(stack, _liveIn, _liveInShadows);
	return candidate;
}

}

StackData stack::chooseBlockStackIn(
	SSACFG const& _cfg,
	std::vector<IncomingEdge> const& _edges,
	LivenessAnalysis::LivenessData const& _liveIn,
	ShadowLiveness::ShadowSet const& _liveInShadows,
	spill::SpillSet& _spillSet,
	bool const _spillingAllowed
)
{
	yulAssert(!_edges.empty());
	StackData stackIn;
	// The chosen stack-in only affects code quality since every incoming edge is shuffled to it
	if (_edges.size() == 1)
		// pass through
		stackIn = candidateOf(_edges[0], _liveIn, _liveInShadows);
	else
	{
		// Pre-compute each parent's proposal
		std::vector<StackData> proposals(_edges.size());
		for (std::size_t i = 0; i < _edges.size(); ++i)
			proposals[i] = candidateOf(_edges[i], _liveIn, _liveInShadows);
		// For each candidate stack-in layout (one parent's proposal), reconcile every parent to it,
		// discovering the spills needed to make each reconciliation realizable
		std::vector<std::size_t> cumulativeGas(_edges.size(), 0);
		std::vector<spill::SpillSet> candidateSpillSets(_edges.size());
		for (std::size_t i = 0; i < _edges.size(); ++i)
		{
			spill::SpillSet candidateSpillSet = _spillSet;
			for (std::size_t j = 0; j < _edges.size(); ++j)
			{
				stack::ShuffleResult const result = stack::shuffleAcrossEdge(
					_edges[j],
					proposals[i],
					candidateSpillSet,
					_spillingAllowed
				);
				yulAssert(result.status == stack::ShuffleResult::Status::Admissible);
				cumulativeGas[i] += stackOpsGas(_cfg, result.trace);
			}
			candidateSpillSets[i] = std::move(candidateSpillSet);
		}
		// Pick the candidate that spills the least; break ties by gas, then by preferring smaller stacks.
		auto const candidateCost = [&](std::size_t const _i) {
			return std::make_tuple(candidateSpillSets[_i].numSpilled(), cumulativeGas[_i], proposals[_i].size());
		};
		std::size_t best = 0;
		for (std::size_t i = 1; i < _edges.size(); ++i)
			if (candidateCost(i) < candidateCost(best))
				best = i;
		yulAssert(
			_spillingAllowed || candidateSpillSets[best].numSpilled() == _spillSet.numSpilled(),
			"Stack too deep, but spilling is disabled because the function is part of a recursive call chain."
		);
		_spillSet = std::move(candidateSpillSets[best]);
		stackIn = std::move(proposals[best]);
	}
	return stackIn;
}

ShuffleResult stack::shuffleAcrossEdge(
	IncomingEdge const& _edge,
	StackData const& _stackIn,
	spill::SpillSet& _spillSet,
	bool const _spillingAllowed
)
{
	StackData target = _stackIn;
	for (PendingCopy const& copy: _edge.pendingCopies)
		ranges::replace(target, copy.shadowSlot, copy.input);
	StackData edgeStack = _edge.stack;
	ShuffleResult result = shuffle(edgeStack, target, _spillSet, _spillingAllowed);
	if (result.status != ShuffleResult::Status::Admissible)
		return result;
	Stack stack(edgeStack, &result.trace);
	for (StackOffset offset{0}; offset < _stackIn.size(); ++offset.value)
		if (_stackIn[offset.value] != target[offset.value])
		{
			yulAssert(edgeStack[offset.value] == target[offset.value]);
			stack.rename(stack.offsetToDepth(offset), _stackIn[offset.value]);
		}
	return result;
}
