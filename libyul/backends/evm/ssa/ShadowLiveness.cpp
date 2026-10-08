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

#include <libyul/backends/evm/ssa/ShadowLiveness.h>

#include <libyul/backends/evm/ssa/analysis/DepthFirstSpanningTree.h>

#include <libyul/Exceptions.h>

#include <fmt/format.h>

#include <cstdint>

using namespace solidity::yul::ssa;

ShadowLiveness::ShadowLiveness(SSACFG const& _cfg):
	m_cfg(_cfg),
	m_liveIns(_cfg.numBlocks()),
	m_liveOuts(_cfg.numBlocks())
{
	analysis::DepthFirstSpanningTree const dfsTree(_cfg);
	std::vector<std::uint8_t> reachable(_cfg.numBlocks(), false);
	for (SSACFG::BlockId const blockId: dfsTree.preOrder())
		reachable[blockId.value] = true;

	// the phis whose shadows each block writes
	std::vector<ShadowSet> written(_cfg.numBlocks());
	for (SSACFG::BlockId const blockId: dfsTree.preOrder())
		_cfg.forEachUpsilon(_cfg.block(blockId), [&](InstId const _upsilon, SSACFG::Inst const&) {
			written[blockId.value].insert(_cfg.upsilonPhi(_upsilon));
		});

	std::vector<SSACFG::BlockId> toVisit;
	for (SSACFG::BlockId const blockId: dfsTree.preOrder())
	{
		// the phis whose shadows the block writes ahead of the current Inst
		ShadowSet writtenAhead;
		for (InstId const id: _cfg.block(blockId).instructions)
		{
			if (_cfg.isUpsilon(id))
				writtenAhead.insert(_cfg.upsilonPhi(id));
			if (!_cfg.isPhi(id))
				continue;
			yulAssert(
				!writtenAhead.contains(id),
				fmt::format("phi {} is written ahead of itself in its block, which the trivial phi eliminator removes", id)
			);
			// the phi reads its shadow: it is live on entry, and backwards from there up to the last upsilon for
			// the phi on every path
			yulAssert(blockId != _cfg.entry, fmt::format("phi {} reads its shadow before any upsilon writes it", id));
			m_liveIns[blockId.value].insert(id);
			toVisit.assign(_cfg.block(blockId).entries.begin(), _cfg.block(blockId).entries.end());
			while (!toVisit.empty())
			{
				SSACFG::BlockId const predecessor = toVisit.back();
				toVisit.pop_back();
				if (!reachable[predecessor.value] || !m_liveOuts[predecessor.value].insert(id).second)
					continue;
				if (written[predecessor.value].contains(id) || !m_liveIns[predecessor.value].insert(id).second)
					continue;
				yulAssert(predecessor != _cfg.entry, fmt::format("phi {} reads its shadow before any upsilon writes it", id));
				auto const& entries = _cfg.block(predecessor).entries;
				toVisit.insert(toVisit.end(), entries.begin(), entries.end());
			}
		}
	}
}

ShadowLiveness::Writes ShadowLiveness::liveWrites(SSACFG::BlockId const _blockId) const
{
	Writes writes;
	// in schedule order, so that the last upsilon for a phi wins
	m_cfg.forEachUpsilon(m_cfg.block(_blockId), [&](InstId const _upsilon, SSACFG::Inst const& _inst) {
		writes[m_cfg.upsilonPhi(_upsilon)] = _inst.inputs.at(0);
	});
	std::erase_if(writes, [&](auto const& _write) { return !liveOut(_blockId, _write.first); });
	return writes;
}

bool ShadowLiveness::liveBehind(InstId const _phi) const
{
	SSACFG::BlockId const blockId = m_cfg.inst(_phi).block;
	// An upsilon for the phi in its block comes after it and writes on the block's out-edge: the shadow is dead from
	// the phi up to there. Otherwise, the shadow keeps the value the phi read up to the block's exit.
	bool writtenInBlock = false;
	m_cfg.forEachUpsilon(m_cfg.block(blockId), [&](InstId const _upsilon, SSACFG::Inst const&) {
		if (m_cfg.upsilonPhi(_upsilon) == _phi)
			writtenInBlock = true;
	});
	return !writtenInBlock && liveOut(blockId, _phi);
}
