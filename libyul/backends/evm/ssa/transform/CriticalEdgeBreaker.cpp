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

#include <libyul/backends/evm/ssa/transform/CriticalEdgeBreaker.h>

#include <libyul/backends/evm/ssa/ShadowLiveness.h>
#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/Exceptions.h>

#include <range/v3/algorithm/find.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>

#include <array>
#include <variant>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

/// Inserts a block on the edge from `_predecessor` to `_successor` and redirects the predecessor's exit and the
/// successor's entry to it
BlockId splitEdge(SSACFG& _cfg, BlockId const _predecessor, BlockId const _successor)
{
	langutil::DebugData::ConstPtr const debugData = _cfg.debugInfo ? _cfg.debugInfo->exitDebugData(_predecessor) : nullptr;
	BlockId const edgeBlockId = _cfg.makeBlock(debugData);
	if (_cfg.debugInfo)
		_cfg.debugInfo->setExitDebugData(edgeBlockId, debugData);
	auto& edgeBlock = _cfg.block(edgeBlockId);
	auto& predecessor = _cfg.block(_predecessor);
	auto& successor = _cfg.block(_successor);

	edgeBlock.entries = {_predecessor};
	edgeBlock.exit = SSACFG::BasicBlock::Jump{_successor};

	auto& conditionalJump = std::get<SSACFG::BasicBlock::ConditionalJump>(predecessor.exit);
	yulAssert(conditionalJump.zero != conditionalJump.nonZero);
	(conditionalJump.zero == _successor ? conditionalJump.zero : conditionalJump.nonZero) = edgeBlockId;

	auto const entry = ranges::find(successor.entries, _predecessor);
	yulAssert(entry != successor.entries.end(), "edge target does not list the predecessor as entry");
	*entry = edgeBlockId;
	return edgeBlockId;
}

}

void transform::breakCriticalEdges(SSACFG& _cfg)
{
	ShadowLiveness const shadows(_cfg);
	auto const isUpsilon = [&](InstId const _id) { return _cfg.isUpsilon(_id); };
	// we might add new blocks, so we work on a copy of the blocks
	std::vector<BlockId> const blocks = _cfg.liveBlocks() | ranges::to<std::vector>;
	for (BlockId const blockId: blocks)
	{
		auto const* conditionalJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&_cfg.block(blockId).exit);
		if (!conditionalJump)
			continue;
		std::vector<InstId> const upsilons = _cfg.block(blockId).instructions | ranges::views::filter(isUpsilon) | ranges::to<std::vector>;
		if (upsilons.empty())
			continue;
		yulAssert(conditionalJump->zero != conditionalJump->nonZero, "upsilons in a block with a conditional jump to a single target");
		// splitting replaces the targets, so they are taken out first
		std::array<BlockId, 2> const targets{conditionalJump->zero, conditionalJump->nonZero};
		std::erase_if(_cfg.block(blockId).instructions, isUpsilon);
		// An upsilon moves onto every out-edge whose target has its shadow live on entry, copied if that is both.
		for (BlockId const successor: targets)
		{
			auto const carried = upsilons | ranges::views::filter([&](InstId const _id) {
				return shadows.liveIn(successor, _cfg.upsilonPhi(_id));
			}) | ranges::to<std::vector>;
			if (carried.empty())
				continue;
			BlockId const edgeBlockId = splitEdge(_cfg, blockId, successor);
			for (InstId const upsilon: carried)
				if (_cfg.inst(upsilon).block == blockId)
				{
					_cfg.inst(upsilon).block = edgeBlockId;
					_cfg.block(edgeBlockId).instructions.push_back(upsilon);
				}
				else
					_cfg.emitUpsilon(edgeBlockId, _cfg.inst(upsilon).inputs.at(0), _cfg.upsilonPhi(upsilon));
		}
		// a write that no phi reads on any path
		for (InstId const upsilon: upsilons)
			if (_cfg.inst(upsilon).block == blockId)
				_cfg.tombstone(upsilon);
	}
}
