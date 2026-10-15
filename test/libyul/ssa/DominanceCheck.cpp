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
 * Unit tests for the SSA CFG dominance check.
 */

#include <libyul/backends/evm/ssa/analysis/DominanceCheck.h>

#include <libyul/backends/evm/ssa/analysis/DepthFirstSpanningTree.h>
#include <libyul/backends/evm/ssa/io/Parser.h>
#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/Exceptions.h>

#include <test/Common.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string_view>

using namespace solidity::yul::ssa;

namespace solidity::yul::test
{

namespace
{

std::unique_ptr<ControlFlowGraphs> parse(std::string_view const _source)
{
	auto parsed = io::parse(
		_source,
		EVMDialect::strictAssemblyForEVMObjects(solidity::test::CommonOptions::get().evmVersion())
	);
	if (!parsed)
		BOOST_FAIL(parsed.error().message);
	return std::move(*parsed);
}

void checkDominance(std::string_view const _source)
{
	auto const cfgs = parse(_source);
	for (auto const& cfg: cfgs->functionGraphs)
		analysis::assertDefinitionsDominateUses(analysis::DepthFirstSpanningTree(*cfg));
}

/// `v2` is defined in one branch and used at the join
constexpr std::string_view definitionInSiblingBranch = R"(
#0:
    v0 = const 0x00
    v1 = builtin @calldataload v0
    branch v1, #1, #2
#1: preds: #0
    v2 = builtin @calldataload v1
    jump #3
#2: preds: #0
    jump #3
#3: preds: #1, #2
    builtin @sstore v0, v2
    main_exit
)";

}

BOOST_AUTO_TEST_SUITE(SSACFGDominanceCheck)

BOOST_AUTO_TEST_CASE(definitions_dominate_uses)
{
	BOOST_CHECK_NO_THROW(checkDominance(R"(
#0:
    v0 = const 0x00
    v1 = const 0x01
    v2 = builtin @calldataload v0
    upsilon v0 -> ^v3
    jump #1
#1: preds: #0, #2
    v3 = phi
    v4 = builtin @lt v3, v2
    branch v4, #2, #3
#2: preds: #1
    v5 = builtin @add v3, v1
    upsilon v5 -> ^v3
    jump #1
#3: preds: #1
    builtin @sstore v0, v3
    main_exit
)"));
}

BOOST_AUTO_TEST_CASE(use_ahead_of_definition_in_its_block)
{
	BOOST_CHECK_THROW(checkDominance(R"(
#0:
    v0 = builtin @calldataload v1
    v1 = const 0x00
    builtin @sstore v1, v0
    main_exit
)"), YulAssertion);
}

BOOST_AUTO_TEST_CASE(use_ahead_of_phi_in_its_block)
{
	BOOST_CHECK_THROW(checkDominance(R"(
#0:
    v0 = const 0x00
    v1 = const 0x01
    v2 = builtin @calldataload v0
    upsilon v0 -> ^v3
    jump #1
#1: preds: #0, #2
    v4 = builtin @lt v3, v2
    v3 = phi
    branch v4, #2, #3
#2: preds: #1
    v5 = builtin @add v3, v1
    upsilon v5 -> ^v3
    jump #1
#3: preds: #1
    builtin @sstore v0, v3
    main_exit
)"), YulAssertion);
}

BOOST_AUTO_TEST_CASE(definition_in_sibling_branch)
{
	BOOST_CHECK_THROW(checkDominance(definitionInSiblingBranch), YulAssertion);
}

BOOST_AUTO_TEST_CASE(definition_in_loop_body_used_behind_the_loop)
{
	BOOST_CHECK_THROW(checkDominance(R"(
#0:
    v0 = const 0x00
    v1 = const 0x01
    v2 = builtin @calldataload v0
    upsilon v0 -> ^v3
    jump #1
#1: preds: #0, #2
    v3 = phi
    v4 = builtin @lt v3, v2
    branch v4, #2, #3
#2: preds: #1
    v5 = builtin @add v3, v1
    upsilon v5 -> ^v3
    jump #1
#3: preds: #1
    builtin @sstore v0, v5
    main_exit
)"), YulAssertion);
}

BOOST_AUTO_TEST_CASE(return_value_defined_in_one_branch)
{
	BOOST_CHECK_THROW(checkDominance(R"(
#0:
    main_exit

func @f(args: (v0)) -> 1 {
#0:
    v0 = arg 0
    branch v0, #1, #2
#1: preds: #0
    v1 = builtin @calldataload v0
    jump #2
#2: preds: #0, #1
    return v1
}
)"), YulAssertion);
}

BOOST_AUTO_TEST_CASE(liveness_analysis_asserts_dominance)
{
	auto const cfgs = parse(definitionInSiblingBranch);
	BOOST_CHECK_THROW((void) LivenessAnalysis(*cfgs->mainGraph()), YulAssertion);
}

BOOST_AUTO_TEST_SUITE_END()

}
