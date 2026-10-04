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

namespace solidity::yul::ssa
{

class SSACFG;

namespace transform
{
/// Moves the upsilons of every block with a conditional exit onto the out-edges whose targets have their shadows
/// live on entry, so that every upsilon sits in a block ending in a jump and the backend can realize it on the edge.
/// A JUMPI carries no code on its edges, so a new block is inserted on each such edge. An upsilon whose shadow is
/// live on entry of both targets is copied, and one that no target reads is dropped.
///
/// For example, with `^p := x` the upsilon of B for the phi `p` of T, which has another predecessor C:
///
///   B [..., ^p := x]                          B [...]
///    | nonZero  \ zero                         | nonZero  \ zero
///    N           T [p := phi] <- C      =>     N           E [^p := x]
///                                                          |
///                                                          T [p := phi] <- C
///
/// Requires trivial phi elimination to have run: a phi written ahead of itself in its own block would read that
/// write at its position, not on the edge.
void breakCriticalEdges(SSACFG& _cfg);
}

}
