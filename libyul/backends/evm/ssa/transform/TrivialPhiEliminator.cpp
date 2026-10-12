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

#include <libyul/backends/evm/ssa/transform/TrivialPhiEliminator.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/Exceptions.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

/// The value `_phi` reads if an upsilon for it precedes it in its own block: that upsilon writes the shadow on every
/// entry of the block, so the phi always reads its input, the last such upsilon's if there are several. Unset
/// otherwise.
InstId valueWrittenAheadOf(SSACFG const& _cfg, InstId const _phi)
{
	InstId value;
	for (InstId const id: _cfg.block(_cfg.inst(_phi).block).instructions)
	{
		if (id == _phi)
			return value;
		if (!_cfg.isUpsilon(id) || _cfg.upsilonPhi(id) != _phi)
			continue;
		InstId const input = _cfg.resolveIdentity(_cfg.inst(id).inputs[0]);
		if (input != _phi && !_cfg.isUnreachable(input))
			value = input;
	}
	yulAssert(false, "phi is not scheduled in its block");
	solidity::util::unreachable();
}

/// Turns the upsilons into nops whose writes no phi reads: on every path from such an upsilon to its phi, another
/// upsilon for the phi comes first.
void removeDeadWrites(SSACFG& _cfg)
{
	// the last upsilon for `_phi` in [_begin, _end), unset if there is none
	auto const lastWrite = [&](auto const _begin, auto _end, InstId const _phi) {
		while (_end != _begin)
			if (InstId const id = *--_end; _cfg.isUpsilon(id) && _cfg.upsilonPhi(id) == _phi)
				return id;
		return InstId{};
	};
	std::set<InstId> read;
	std::vector<std::uint8_t> visited;
	std::vector<BlockId> toVisit;
	for (BlockId const blockId: _cfg.liveBlocks())
	{
		auto const& instructions = _cfg.block(blockId).instructions;
		for (auto it = instructions.begin(); it != instructions.end(); ++it)
		{
			if (!_cfg.isPhi(*it))
				continue;
			InstId const phi = *it;
			if (InstId const write = lastWrite(instructions.begin(), it, phi); write.hasValue())
			{
				read.insert(write);
				continue;
			}
			visited.assign(_cfg.numBlocks(), false);
			toVisit.assign(_cfg.block(blockId).entries.begin(), _cfg.block(blockId).entries.end());
			while (!toVisit.empty())
			{
				BlockId const predecessor = toVisit.back();
				toVisit.pop_back();
				if (visited[predecessor.value])
					continue;
				visited[predecessor.value] = true;
				auto const& predecessorInstructions = _cfg.block(predecessor).instructions;
				if (
					InstId const write = lastWrite(predecessorInstructions.begin(), predecessorInstructions.end(), phi);
					write.hasValue()
				)
				{
					read.insert(write);
					continue;
				}
				auto const& entries = _cfg.block(predecessor).entries;
				toVisit.insert(toVisit.end(), entries.begin(), entries.end());
			}
		}
	}
	for (BlockId const blockId: _cfg.liveBlocks())
		for (InstId const id: _cfg.block(blockId).instructions)
			if (_cfg.isUpsilon(id) && !read.contains(id))
				_cfg.replaceWithNop(id);
}

}

void transform::eliminateTrivialPhis(SSACFG& _cfg)
{
	// Drop upsilons that write an Unreachable value: no path that executes reaches them
	for (BlockId const blockId: _cfg.liveBlocks())
		for (InstId const id: _cfg.block(blockId).instructions)
		{
			auto const& inst = _cfg.inst(id);
			if (inst.isUpsilon() && _cfg.isUnreachable(inst.inputs[0]))
				_cfg.replaceWithNop(id);
		}
	removeDeadWrites(_cfg);

	// Build per-phi indices
	std::map<InstId, std::vector<InstId>> upsilonsTargeting;
	std::map<InstId, std::vector<InstId>> phisReferencing;
	std::vector<InstId> worklist;
	for (std::uint32_t i = 0; i < _cfg.instructionStore().numInsts(); ++i)
	{
		InstId const id{i};
		auto const& inst = _cfg.inst(id);
		if (inst.isPhi())
			worklist.push_back(id);
		else if (inst.isUpsilon())
		{
			InstId const phi = _cfg.upsilonPhi(id);
			upsilonsTargeting[phi].push_back(id);
			InstId const value = inst.inputs[0];
			if (_cfg.kindOf(value) == InstOpcode::Phi)
				phisReferencing[value].push_back(phi);
		}
	}

	// All orphan phis can share a single Unreachable sentinel, only allocate lazily if needed
	InstId orphanUnreachable;

	while (!worklist.empty())
	{
		InstId const phi = worklist.back();
		worklist.pop_back();
		if (_cfg.kindOf(phi) != InstOpcode::Phi)
			continue;

		InstId same = valueWrittenAheadOf(_cfg, phi);
		bool nontrivial = false;
		auto const it = upsilonsTargeting.find(phi);
		if (!same.hasValue() && it != upsilonsTargeting.end())
			for (InstId const ups: it->second)
			{
				if (_cfg.kindOf(ups) == InstOpcode::Nop)
					continue;
				InstId const value = _cfg.resolveIdentity(_cfg.inst(ups).inputs[0]);
				if (value == phi || _cfg.isUnreachable(value))
					continue;
				if (same.hasValue() && value != same)
				{
					nontrivial = true;
					break;
				}
				same = value;
			}
		if (nontrivial)
			continue;
		if (!same.hasValue())
		{
			// Phi has no live, non-self upsilons — it's an orphan in dead code.
			if (!orphanUnreachable.hasValue())
				orphanUnreachable = _cfg.unreachableValue();
			same = orphanUnreachable;
		}

		_cfg.replaceWithIdentity(phi, same);
		if (it != upsilonsTargeting.end())
			for (InstId const ups: it->second)
				if (_cfg.kindOf(ups) == InstOpcode::Upsilon)
					_cfg.replaceWithNop(ups);

		// Re-queue dependents and forward `phi`'s reverse-dep entries onto `same`. If `same` is itself a Phi that
		// gets eliminated later, those dependents must be reevaluated
		auto const depsIt = phisReferencing.find(phi);
		if (depsIt != phisReferencing.end())
		{
			for (InstId const dep: depsIt->second)
				worklist.push_back(dep);
			if (_cfg.kindOf(same) == InstOpcode::Phi)
			{
				auto& sameDeps = phisReferencing[same];
				sameDeps.insert(sameDeps.end(), depsIt->second.begin(), depsIt->second.end());
			}
		}
	}
}
