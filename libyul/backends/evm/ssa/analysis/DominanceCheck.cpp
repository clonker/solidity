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

#include <libyul/backends/evm/ssa/analysis/DominanceCheck.h>

#include <libyul/backends/evm/ssa/analysis/DepthFirstSpanningTree.h>
#include <libyul/backends/evm/ssa/analysis/DominatorTree.h>

#include <libyul/Exceptions.h>

#include <libsolutil/Visitor.h>

#include <fmt/format.h>

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

using namespace solidity::yul::ssa;

void analysis::assertDefinitionsDominateUses(DepthFirstSpanningTree const& _dfsTree)
{
	SSACFG const& cfg = _dfsTree.cfg();
	DominatorTree const dominatorTree(_dfsTree);

	// the position of every Inst in the schedule of its block
	std::vector<std::size_t> position(cfg.numInsts(), 0);
	for (SSACFG::BlockId const blockId: _dfsTree.preOrder())
	{
		auto const& instructions = cfg.block(blockId).instructions;
		for (std::size_t index = 0; index < instructions.size(); ++index)
		{
			InstId const id = instructions[index];
			yulAssert(
				cfg.inst(id).block == blockId,
				fmt::format("{} is scheduled in block {} but defined in block {}", id, blockId, cfg.inst(id).block)
			);
			position[id.value] = index;
		}
	}

	// `_value` is used in `_block` at position `_position`, or at the block's exit if that is unset
	auto const checkUse = [&](InstId const _value, SSACFG::BlockId const _block, std::optional<std::size_t> const _position) {
		// an Unreachable has no definition point
		if (cfg.isUnreachable(_value))
			return;
		SSACFG::BlockId const definingBlock = cfg.inst(_value).block;
		if (definingBlock == _block)
			yulAssert(
				!_position || position[_value.value] < *_position,
				fmt::format("{} is used ahead of its definition in block {}", _value, _block)
			);
		else
			yulAssert(
				definingBlock.hasValue() &&
				_dfsTree.reachable(definingBlock) &&
				dominatorTree.dominates(definingBlock, _block),
				fmt::format(
					"{} is used in block {}, which its definition in block {} does not dominate",
					_value,
					_block,
					definingBlock
				)
			);
	};

	for (SSACFG::BlockId const blockId: _dfsTree.preOrder())
	{
		auto const& block = cfg.block(blockId);
		for (std::size_t index = 0; index < block.instructions.size(); ++index)
			for (InstId const input: cfg.inst(block.instructions[index]).inputs)
				checkUse(input, blockId, index);
		std::visit(solidity::util::GenericVisitor{
			[&](SSACFG::BasicBlock::ConditionalJump const& _jump) { checkUse(_jump.condition, blockId, std::nullopt); },
			[&](SSACFG::BasicBlock::FunctionReturn const& _return)
			{
				for (InstId const value: _return.returnValues)
					checkUse(value, blockId, std::nullopt);
			},
			[](SSACFG::BasicBlock::Jump const&) {},
			[](SSACFG::BasicBlock::MainExit const&) {},
			[](SSACFG::BasicBlock::Terminated const&) {}
		}, block.exit);
	}
}
