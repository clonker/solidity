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

#include <libyul/backends/evm/ssa/traversal/ForwardTopologicalSort.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/SSACFGLoopNestingForest.h>
#include <libyul/backends/evm/ssa/util/UseCountSet.h>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>

#include <vector>

namespace solidity::yul::ssa
{

/// Performs liveness analysis on a reducible SSA CFG following Algorithm 9.1 in [1].
///
/// Phis count as defined at the entry of their block. An upsilon reads its input only if what it writes into the
/// shadow of its phi is live: at the end of its block if it is lowered on the block's out-edges (see
/// `isLoweredOnEdge`), at its position otherwise.
///
/// The shadows of Pizlo form are not SSA values: several upsilons write a shadow and each of them may execute any
/// number of times. But a shadow has a single reader, its phi, so its liveness follows by path exploration [1, Chapter 9]:
/// backwards from the phi, through its block and the predecessors, up to the upsilons that write the shadow.
///
/// [1] Rastello, Fabrice, and Florent Bouchez Tichadou, eds. SSA-based Compiler Design. Springer, 2022.
class LivenessAnalysis
{
public:
	/// Per-program-point liveness, each value's use count is the max number of times the value will be read along
	/// all paths downstream of that point
	using LivenessData = util::UseCountSet<InstId>;

	explicit LivenessAnalysis(SSACFG const& _cfg);

	LivenessData const& liveIn(SSACFG::BlockId const _blockId) const { return m_liveIns[_blockId.value]; }
	LivenessData const& liveOut(SSACFG::BlockId const _blockId) const { return m_liveOuts[_blockId.value]; }
	LivenessData used(SSACFG::BlockId _blockId) const;
	/// the values live right behind an operation or an upsilon realized at its position
	LivenessData const& operationLiveOut(InstId const _id) const { return m_operationLiveOutByInst.at(_id.value); }

	/// Per block, the phis whose shadows are live on entry and on exit
	using ShadowSet = boost::container::flat_set<InstId>;
	bool shadowLiveIn(SSACFG::BlockId const _blockId, InstId const _phi) const { return m_shadowLiveIns[_blockId.value].contains(_phi); }
	bool shadowLiveOut(SSACFG::BlockId const _blockId, InstId const _phi) const { return m_shadowLiveOuts[_blockId.value].contains(_phi); }
	/// Whether the shadow of `_phi` is live right behind the Inst `_id`: the next access to it in the block is the
	/// read of the phi, or there is none and the shadow is live on exit
	bool shadowLiveBehind(InstId _id, InstId _phi) const;
	/// Whether the upsilon `_upsilon` is lowered on the out-edges of its block instead of at its position: neither an
	/// operation nor its phi follows it in the block, and its shadow is not live on entry of the nonZero target of a
	/// conditional exit (JUMPI jumps there directly, so that edge cannot carry code). The edges into successors on
	/// whose entry the shadow is live then realize the write.
	bool isLoweredOnEdge(InstId _upsilon) const;
	traversal::ForwardTopologicalSort const& topologicalSort() const { return m_topologicalSort; }
	SSACFG const& cfg() const { return m_cfg; }

private:
	void runShadowPathExploration();
	void runDagDfs();
	void runLoopTreeDfs(SSACFG::BlockId::ValueType _loopHeader);
	void fillOperationsLiveOut();
	LivenessData blockExitValues(SSACFG::BlockId const& _blockId) const;
	/// whether `_id` is an upsilon whose write is live and that reads its input at its position rather than on
	/// its block's out-edges
	bool isUpsilonReadingAtPosition(InstId _id) const;

	auto excludingLiteralsFilter() const
	{
		return [this](InstId _v) { return !m_cfg.isLiteral(_v); };
	}

	SSACFG const& m_cfg;
	traversal::ForwardTopologicalSort m_topologicalSort;
	SSACFGLoopNestingForest m_loopNestingForest;
	std::vector<LivenessData> m_liveIns;
	std::vector<LivenessData> m_liveOuts;
	std::vector<ShadowSet> m_shadowLiveIns;
	std::vector<ShadowSet> m_shadowLiveOuts;
	boost::container::flat_map<InstId::ValueType, LivenessData> m_operationLiveOutByInst;
};

}
