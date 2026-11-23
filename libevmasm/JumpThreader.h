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
#pragma once

#include <vector>

namespace solidity::evmasm
{
class AssemblyItem;
using AssemblyItems = std::vector<AssemblyItem>;

/**
 * Replaces every push of a local tag whose block consists only of `PUSH tag JUMP` by the
 * tag the chain of such blocks ends in, so that the jump-only blocks can be removed by
 * the JumpdestRemover and the PeepholeOptimiser.
 * Only ordinary jumps are threaded, so that no jump into or out of a function is lost.
 * No tag or block is removed, so tags referenced from outside the assembly stay valid.
 */
class JumpThreader
{
public:
	explicit JumpThreader(AssemblyItems& _items): m_items(_items) {}

	/// @returns true if a tag reference was replaced.
	bool optimise();

private:
	AssemblyItems& m_items;
};

}
