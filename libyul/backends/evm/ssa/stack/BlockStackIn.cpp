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

#include <libyul/backends/evm/ssa/stack/Shuffler.h>

#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackUtils.h>

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/count.hpp>
#include <range/v3/algorithm/replace.hpp>

#include <tuple>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::stack;

namespace
{
/// Turns a predecessor's exit stack into a candidate stack-in: an input written into a shadow on the edge becomes the
/// shadow slot in place if nothing else needs it on entry, and is copied otherwise
void applyEdgeWrites(StackData& _stackData, ShadowLiveness::Writes const& _writes, LivenessAnalysis::LivenessData const& _liveness, SSACFG const& _cfg)
{
	// add the shadow slots written on the edge that are not already contained in the stack
	for (auto const& [phi, input]: _writes)
	{
		auto const shadowSlot = StackSlot::makeShadow(_cfg, phi);
		auto reversedStackData = _stackData | ranges::views::reverse;
		auto const inputSlot = StackSlot::makeValue(_cfg, input);
		auto it = ranges::find(reversedStackData, inputSlot);
		if (_liveness.contains(input))
		{
			// The input is part of the live-in set as well.
			// If the input occurs more than once on the stack, one occurrence is
			// symbolically replaced by the shadow slot; otherwise, we push the shadow slot.
			if (ranges::count(_stackData, inputSlot) > 1)
				*it = shadowSlot;
			else
				_stackData.emplace_back(shadowSlot);
		}
		else
		{
			// replace all occurrences of the input with the shadow slot
			ranges::replace(_stackData, inputSlot, shadowSlot);
			// if it's not contained, push it (could be derived from a literal)
			if (it == ranges::end(reversedStackData))
				_stackData.emplace_back(shadowSlot);
		}
	}
}

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

}

StackData stack::chooseBlockStackIn(
	LivenessAnalysis const& _liveness,
	SSACFG::BlockId const _block,
	std::vector<IncomingEdge> const& _edges,
	spill::SpillSet& _spillSet,
	bool const _spillingAllowed
)
{
	yulAssert(!_edges.empty());
	SSACFG const& cfg = _liveness.cfg();
	auto const& liveIn = _liveness.liveIn(_block);
	auto const& liveInShadows = _liveness.liveInShadows(_block);
	StackData stackIn;
	// The chosen stack-in only affects code quality since every incoming edge is shuffled to it
	if (cfg.block(_block).entries.size() == 1)
	{
		// pass through
		yulAssert(_edges.size() == 1);
		stackIn = _edges[0].stack;
		applyEdgeWrites(stackIn, _liveness.liveWrites(_edges[0].predecessor), liveIn, cfg);
		Stack stack(stackIn);
		declareJunk(stack, liveIn, liveInShadows);
	}
	else
	{
		// The shadow slots written on an incoming edge, back edges included: every candidate stack-in carries them,
		// even if the predecessor it comes from dropped a spilled copy
		std::vector<StackSlot> edgeWrittenShadows;
		for (SSACFG::BlockId const entry: cfg.block(_block).entries)
			for (auto const& [phi, input]: _liveness.liveWrites(entry))
				if (auto const shadowSlot = StackSlot::makeShadow(cfg, phi); !ranges::contains(edgeWrittenShadows, shadowSlot))
					edgeWrittenShadows.push_back(shadowSlot);

		// Pre-compute each parent's proposal and the writes on its edge
		std::vector<StackData> proposals(_edges.size());
		std::vector<ShadowLiveness::Writes> edgeWrites(_edges.size());
		for (std::size_t i = 0; i < _edges.size(); ++i)
		{
			proposals[i] = _edges[i].stack;
			edgeWrites[i] = _liveness.liveWrites(_edges[i].predecessor);
			applyEdgeWrites(proposals[i], edgeWrites[i], liveIn, cfg);
			{
				Stack stack(proposals[i]);
				declareJunk(stack, liveIn, liveInShadows);
			}
			for (StackSlot const& shadowSlot: edgeWrittenShadows)
				if (!ranges::contains(proposals[i], shadowSlot))
					proposals[i].push_back(shadowSlot);
		}
		// For each candidate stack-in layout (one parent's proposal), reconcile every parent to it,
		// discovering the spills needed to make each reconciliation realizable
		std::vector<std::size_t> cumulativeGas(_edges.size(), 0);
		std::vector<spill::SpillSet> candidateSpillSets(_edges.size());
		for (std::size_t i = 0; i < _edges.size(); ++i)
		{
			spill::SpillSet candidateSpillSet = _spillSet;
			for (std::size_t j = 0; j < _edges.size(); ++j)
			{
				ShuffleResult const result = shuffleAcrossEdge(
					cfg,
					_edges[j].stack,
					proposals[i],
					edgeWrites[j],
					candidateSpillSet,
					_spillingAllowed
				);
				yulAssert(result.status == ShuffleResult::Status::Admissible);
				cumulativeGas[i] += stackOpsGas(cfg, result.trace);
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
	SSACFG const& _cfg,
	StackData _exitStack,
	StackData const& _stackIn,
	ShadowLiveness::Writes const& _writes,
	spill::SpillSet& _spillSet,
	bool const _spillingAllowed
)
{
	auto const isWritten = [&](StackSlot const& _slot) { return _slot.isShadow() && _writes.contains(_slot.shadowPhi()); };
	StackData target = _stackIn;
	for (auto& slot: target)
		if (isWritten(slot))
			slot = StackSlot::makeValue(_cfg, _writes.at(slot.shadowPhi()));
	ShuffleResult result = stack::shuffle(_exitStack, target, _spillSet, _spillingAllowed);
	if (result.status != ShuffleResult::Status::Admissible)
		return result;
	Stack stack(_exitStack, &result.trace);
	for (StackOffset offset{0}; offset < _stackIn.size(); ++offset.value)
		if (isWritten(_stackIn[offset.value]))
			stack.rename(stack.offsetToDepth(offset), _stackIn[offset.value]);
	return result;
}
