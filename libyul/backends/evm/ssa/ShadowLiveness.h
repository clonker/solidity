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

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <boost/container/flat_set.hpp>

#include <map>
#include <vector>

namespace solidity::yul::ssa
{

/// The liveness of the shadows of Pizlo form: per block, the phis whose shadows are live on entry and on exit.
///
/// Shadows are not SSA values: several upsilons write a shadow and each of them may execute any number of times. But
/// a shadow has a single reader, its phi, so its liveness follows by path exploration [1, Chapter 9]: backwards from
/// the phi, through its block and the predecessors, up to the upsilons that write the shadow. Every block is visited
/// at most once per phi, and nothing depends on where the upsilons sit, as long as the graph is valid Pizlo form:
/// reaching the entry without passing an upsilon is asserted as the violation it is.
///
/// An upsilon ahead of its own phi in the phi's block is not expected, since the trivial phi eliminator replaces
/// such phis.
///
/// [1] Rastello, Fabrice, and Florent Bouchez Tichadou, eds. SSA-based Compiler Design. Springer, 2022.
class ShadowLiveness
{
public:
	using ShadowSet = boost::container::flat_set<InstId>;
	/// Writes of shadows, as phi -> input
	using Writes = std::map<InstId, InstId>;

	explicit ShadowLiveness(SSACFG const& _cfg);

	bool liveIn(SSACFG::BlockId const _blockId, InstId const _phi) const { return m_liveIns[_blockId.value].contains(_phi); }
	bool liveOut(SSACFG::BlockId const _blockId, InstId const _phi) const { return m_liveOuts[_blockId.value].contains(_phi); }
	ShadowSet const& liveIns(SSACFG::BlockId const _blockId) const { return m_liveIns[_blockId.value]; }
	ShadowSet const& liveOuts(SSACFG::BlockId const _blockId) const { return m_liveOuts[_blockId.value]; }
	/// The writes of a block's upsilons that reach a phi: per phi the input of the block's last upsilon for it, if
	/// the shadow is live on exit
	Writes liveWrites(SSACFG::BlockId _blockId) const;

private:
	SSACFG const& m_cfg;
	std::vector<ShadowSet> m_liveIns;
	std::vector<ShadowSet> m_liveOuts;
};

}
