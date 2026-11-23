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
/**
 * Redirects references to blocks that consist only of a jump to the final jump target.
 */

#include <libevmasm/JumpThreader.h>

#include <libevmasm/AssemblyItem.h>
#include <libevmasm/BlockDeduplicator.h>

#include <map>
#include <set>

using namespace solidity;
using namespace solidity::evmasm;

bool JumpThreader::optimise()
{
	// Tags whose block consists only of an ordinary jump to a local tag, mapped to that tag.
	std::map<u256, u256> jumpTargets;
	for (size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].type() != Tag)
			continue;
		// Further tags at the same position belong to the same block.
		size_t body = i + 1;
		while (body < m_items.size() && m_items[body].type() == Tag)
			++body;
		if (
			body + 1 < m_items.size() &&
			m_items[body].type() == PushTag &&
			m_items[body].splitForeignPushTag().first.empty() &&
			m_items[body + 1] == Instruction::JUMP &&
			m_items[body + 1].getJumpType() == AssemblyItem::JumpType::Ordinary
		)
			jumpTargets[m_items[i].data()] = m_items[body].data();
	}

	// Follow chains of jump-only blocks to their end, leaving out chains that run into a cycle.
	std::map<u256, u256> replacements;
	for (auto const& [tag, target]: jumpTargets)
	{
		std::set<u256> visited{tag};
		u256 finalTarget = target;
		while (jumpTargets.contains(finalTarget) && visited.insert(finalTarget).second)
			finalTarget = jumpTargets.at(finalTarget);
		if (!visited.contains(finalTarget))
			replacements[tag] = finalTarget;
	}

	return BlockDeduplicator::applyTagReplacement(m_items, replacements);
}
