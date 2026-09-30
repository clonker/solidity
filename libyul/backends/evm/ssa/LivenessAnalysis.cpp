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

#include <libyul/backends/evm/ssa/LivenessAnalysis.h>

#include <libsolutil/Visitor.h>

#include <boost/container/flat_map.hpp>

#include <range/v3/algorithm/find.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/reverse.hpp>

#include <utility>
#include <vector>

using namespace solidity::yul::ssa;

LivenessAnalysis::LivenessData LivenessAnalysis::blockExitValues(SSACFG::BlockId const& _blockId) const
{
	LivenessData result;
	solidity::util::GenericVisitor exitVisitor{
		[](SSACFG::BasicBlock::MainExit const&) {},
		[&](SSACFG::BasicBlock::FunctionReturn const& _functionReturn)
		{
			result.insertAll(_functionReturn.returnValues | ranges::views::filter(excludingLiteralsFilter()));
		},
		[](SSACFG::BasicBlock::Jump const&) {},
		[&](SSACFG::BasicBlock::ConditionalJump const& _conditionalJump)
		{
			if (excludingLiteralsFilter()(_conditionalJump.condition))
				result.insert(_conditionalJump.condition);
		},
		[](SSACFG::BasicBlock::Terminated const&) {}};
	std::visit(exitVisitor, m_cfg.block(_blockId).exit);
	return result;
}

bool solidity::yul::ssa::isLoweredOnEdge(SSACFG const& _cfg, InstId const _upsilon)
{
	SSACFG::Inst const& upsilon = _cfg.inst(_upsilon);
	yulAssert(upsilon.isUpsilon());
	SSACFG::BasicBlock const& block = _cfg.block(upsilon.block);
	if (!block.isJumpBlock())
		return false;

	InstId const phi = _cfg.upsilonPhi(_upsilon);
	for (InstId const id: block.instructions | ranges::views::reverse)
	{
		if (id == _upsilon)
			return true;
		if (id == phi || _cfg.isOperation(id))
			return false;
	}
	yulAssert(false, fmt::format("upsilon {} is not scheduled in its block", _upsilon));
	solidity::util::unreachable();
}

LivenessAnalysis::LivenessAnalysis(SSACFG const& _cfg):
	m_cfg(_cfg),
	m_topologicalSort(_cfg),
	m_loopNestingForest(m_topologicalSort),
	m_liveIns(_cfg.numBlocks()),
	m_liveOuts(_cfg.numBlocks()),
	m_shadowLiveIns(_cfg.numBlocks()),
	m_shadowLiveOuts(_cfg.numBlocks())
{
	runShadowPathExploration();
	runDagDfs();
	for (auto const loopRootNode: m_loopNestingForest.loopRootNodes())
		runLoopTreeDfs(loopRootNode);

	fillOperationsLiveOut();
}

bool LivenessAnalysis::isUpsilonReadingAtPosition(InstId const _id) const
{
	return m_cfg.isUpsilon(_id) && !isLoweredOnEdge(m_cfg, _id) && shadowLiveBehind(_id, m_cfg.upsilonPhi(_id));
}

bool LivenessAnalysis::shadowLiveBehind(InstId const _id, InstId const _phi) const
{
	SSACFG::BlockId const blockId = m_cfg.inst(_id).block;
	auto const& instructions = m_cfg.block(blockId).instructions;
	auto it = ranges::find(instructions, _id);
	yulAssert(it != instructions.end(), fmt::format("{} is not scheduled in its block", _id));
	for (++it; it != instructions.end(); ++it)
	{
		if (*it == _phi)
			return true;
		if (m_cfg.isUpsilon(*it) && m_cfg.upsilonPhi(*it) == _phi)
			return false;
	}
	return shadowLiveOut(blockId, _phi);
}

void LivenessAnalysis::runShadowPathExploration()
{
	std::vector<std::uint8_t> reachable(m_cfg.numBlocks(), false);
	for (auto const blockIdValue: m_topologicalSort.preOrder())
		reachable[blockIdValue] = true;

	// the phis whose shadows each block writes
	std::vector<ShadowSet> written(m_cfg.numBlocks());
	for (auto const blockIdValue: m_topologicalSort.preOrder())
		m_cfg.forEachUpsilon(m_cfg.block(SSACFG::BlockId{blockIdValue}), [&](InstId const _upsilon, SSACFG::Inst const&) {
			written[blockIdValue].insert(m_cfg.upsilonPhi(_upsilon));
		});

	std::vector<SSACFG::BlockId> toVisit;
	for (auto const blockIdValue: m_topologicalSort.preOrder())
	{
		SSACFG::BlockId const blockId{blockIdValue};
		// the phis whose shadows the block writes ahead of the current Inst
		ShadowSet writtenAhead;
		for (InstId const id: m_cfg.block(blockId).instructions)
		{
			if (m_cfg.isUpsilon(id))
				writtenAhead.insert(m_cfg.upsilonPhi(id));
			// the phi reads its shadow: unless the block wrote it ahead of the phi, it is live on entry, and
			// backwards from there up to the last upsilon for the phi on every path
			if (!m_cfg.isPhi(id) || writtenAhead.contains(id))
				continue;
			yulAssert(blockId != m_cfg.entry, fmt::format("phi {} reads its shadow before any upsilon writes it", id));
			m_shadowLiveIns[blockIdValue].insert(id);
			toVisit.assign(m_cfg.block(blockId).entries.begin(), m_cfg.block(blockId).entries.end());
			while (!toVisit.empty())
			{
				SSACFG::BlockId const predecessor = toVisit.back();
				toVisit.pop_back();
				if (!reachable[predecessor.value] || !m_shadowLiveOuts[predecessor.value].insert(id).second)
					continue;
				if (written[predecessor.value].contains(id) || !m_shadowLiveIns[predecessor.value].insert(id).second)
					continue;
				yulAssert(predecessor != m_cfg.entry, fmt::format("phi {} reads its shadow before any upsilon writes it", id));
				auto const& entries = m_cfg.block(predecessor).entries;
				toVisit.insert(toVisit.end(), entries.begin(), entries.end());
			}
		}
	}
}

LivenessAnalysis::LivenessData LivenessAnalysis::used(SSACFG::BlockId const _blockId) const
{
	auto used = liveIn(_blockId);
	for (auto const& [valueId, count]: liveOut(_blockId))
		used.remove(valueId, count);
	return used;
}

void LivenessAnalysis::runDagDfs()
{
	// SSA Book, Algorithm 9.2
	for (auto const blockIdValue: m_topologicalSort.postOrder())
	{
		// post-order traversal
		SSACFG::BlockId blockId{blockIdValue};
		auto const& block = m_cfg.block(blockId);

		// live <- PhiUses(B), the inputs of the upsilons lowered on the block's out-edge whose writes are live
		LivenessData live{};
		m_cfg.forEachUpsilon(block, [&](InstId const instId, SSACFG::Inst const& inst) {
			InstId const v = inst.inputs.at(0);
			yulAssert(!m_cfg.isUnreachable(v));
			if (!m_cfg.isLiteral(v) && isLoweredOnEdge(m_cfg, instId) && shadowLiveBehind(instId, m_cfg.upsilonPhi(instId)))
				live.insert(v);
		});

		// for each S \in succs(B) s.t. (B, S) not a back edge: live <- live \cup LiveIn(S)
		// (a phi is defined at its position, so there are no PhiDefs(S) to take out)
		block.forEachExit(
			[&](SSACFG::BlockId const& _successor) {
				if (!m_topologicalSort.backEdge(blockId, _successor))
					live.maxUnion(m_liveIns[_successor.value]);
			});

		if (std::holds_alternative<SSACFG::BasicBlock::FunctionReturn>(block.exit))
			live.insertAll(std::get<SSACFG::BasicBlock::FunctionReturn>(block.exit).returnValues | ranges::views::filter(excludingLiteralsFilter()));

		// clean out unreachables
		live.eraseIf([&](auto const& _entry) { return m_cfg.isUnreachable(_entry.first); });

		// LiveOut(B) <- live
		m_liveOuts[blockId.value] = live;

		// for each program point p in B, backwards, do:
		{
			// add value ids to the live set that are used in exit blocks
			live += blockExitValues(blockId);

			for (InstId const instId: block.instructions | ranges::views::reverse)
			{
				auto const& inst = m_cfg.inst(instId);
				if (isUpsilonReadingAtPosition(instId))
					live.insertAll(inst.inputs | ranges::views::filter(excludingLiteralsFilter()));
				if (inst.isPhi())
					live.erase(instId);
				if (!inst.isOperation())
					continue;
				// remove variables defined at p from live
				live.eraseAll(m_cfg.projectionsOf(instId));
				live.erase(instId);
				live.insertAll(inst.inputs | ranges::views::filter(excludingLiteralsFilter()));
			}
		}

		// livein(b) <- live
		m_liveIns[blockId.value] = live;
	}
}

void LivenessAnalysis::runLoopTreeDfs(SSACFG::BlockId::ValueType const _loopHeader)
{
	// SSA Book, Algorithm 9.3
	if (m_loopNestingForest.loopNodes().contains(_loopHeader))
	{
		// LiveLoop <- LiveIn(B_N) (the header's phis are defined at their position, not live on entry)
		auto liveLoop = m_liveIns[_loopHeader];
		// must be live out of header if live in of children
		m_liveOuts[_loopHeader].maxUnion(liveLoop);
		// for each blockId \in children(loopHeader)
		for (SSACFG::BlockId const blockId: m_cfg.liveBlocks())
			if (m_loopNestingForest.loopParents()[blockId.value] == _loopHeader)
			{
				// propagate loop liveness information down to the loop header's children
				m_liveIns[blockId.value].maxUnion(liveLoop);
				m_liveOuts[blockId.value].maxUnion(liveLoop);

				runLoopTreeDfs(blockId.value);
			}
	}
}

void LivenessAnalysis::fillOperationsLiveOut()
{
	for (SSACFG::BlockId const blockId: m_cfg.liveBlocks())
	{
		auto const& block = m_cfg.block(blockId);
		auto live = m_liveOuts[blockId.value];
		live += blockExitValues(blockId);
		for (InstId const instId: block.instructions | ranges::views::reverse)
		{
			auto const& inst = m_cfg.inst(instId);
			if (isUpsilonReadingAtPosition(instId))
			{
				m_operationLiveOutByInst.emplace(instId.value, live);
				live.insertAll(inst.inputs | ranges::views::filter(excludingLiteralsFilter()));
			}
			if (inst.isPhi())
				live.erase(instId);
			if (!inst.isOperation())
				continue;
			m_operationLiveOutByInst.emplace(instId.value, live);
			live.eraseAll(m_cfg.projectionsOf(instId));
			live.erase(instId);
			live.insertAll(inst.inputs | ranges::views::filter(excludingLiteralsFilter()));
		}
	}
}
