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
 * Parser for the textual rendering of libyul SSA CFGs, the inverse of the printer.
 */

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

namespace solidity::yul
{
class EVMDialect;
}

namespace solidity::yul::ssa
{
struct ControlFlowGraphs;
}

namespace solidity::yul::ssa::io
{

struct ParserError: std::runtime_error
{
	using std::runtime_error::runtime_error;
};

/// Parses the rendering `print` produces back into control flow graphs, keeping the text's value and block
/// numbering, so that graphs can be written down directly, in shapes no builder or transform would produce.
/// Accepts exactly the printer's grammar, except for builtins with literal arguments, identities, nops and
/// tombstones. Upsilons have no name in the text and get the ids behind the last named value. Throws
/// `ParserError` on malformed input.
std::unique_ptr<ControlFlowGraphs> parse(std::string const& _text, EVMDialect const& _dialect);

}
