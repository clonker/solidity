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

namespace solidity::yul::ssa::analysis
{

class DepthFirstSpanningTree;

/// Asserts that every use of a value is dominated by the value's definition. An Inst defines its value at its
/// position in its block, a phi included; an input is used at the position of its Inst, and a block's exit uses its
/// condition or return values behind every Inst of the block. Only reachable blocks are visited, and an Unreachable
/// value has no definition point to check.
void assertDefinitionsDominateUses(DepthFirstSpanningTree const& _dfsTree);

}
