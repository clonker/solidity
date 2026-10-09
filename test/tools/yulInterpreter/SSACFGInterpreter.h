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
 * Interpreter for SSA CFGs in Phi/Upsilon form.
 *
 * Runs a `ControlFlowGraphs` instance on an `EVMState`, delegating every builtin to `EVMInstructionInterpreter`, as
 * the Yul interpreter does. The only semantics implemented here are control flow, user function calls, and the
 * Phi/Upsilon shadow-variable protocol: an upsilon writes the shadow of its phi when it executes and a phi reads its
 * shadow when it executes, wherever in the graph either of them sits. A differential run against the Yul interpreter
 * thus isolates exactly the part of the pipeline that `SSACFGBuilder` and the `transform/` passes are responsible for.
 */
#pragma once

#include <test/tools/yulInterpreter/EVMInstructionInterpreter.h>
#include <test/tools/yulInterpreter/EVMState.h>

#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <libsolutil/Numeric.h>

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace solidity::yul::test
{

class SSACFGInterpreter
{
public:
	struct Result
	{
		/// How execution ended: by a terminating builtin, or by a stop at the exit of the main graph, as the EVM stops
		/// at the end of the code. Unset if the step, trace or call depth limit was hit, which leaves the state partial.
		std::optional<Termination> termination;
		/// The state after the run: effect trace, memory, storage, transient storage, returndata.
		EVMState state;
	};

	/// Executes the main graph of @a _controlFlow on @a _state (calldata, environment) and returns how execution ended
	/// together with the final state. At most @a _maxSteps Insts and blocks are executed, any number if it is zero.
	/// Violations of the CFG's own invariants (executing `Unreachable`, reading a phi before any upsilon wrote its
	/// shadow, a non-continuing call returning, reaching a `Terminated` exit without termination) are reported via
	/// `yulAssert`, i.e. they throw `YulAssertion`.
	static Result run(
		yul::ssa::ControlFlowGraphs const& _controlFlow,
		EVMState _state,
		std::size_t _maxSteps,
		bool _disableMemoryTrace
	);

private:
	/// Per-activation storage: SSA values, phi shadows, and the result vectors of multi-return operations.
	struct Frame
	{
		std::vector<std::optional<u256>> values;
		std::map<yul::ssa::InstId, u256> shadows;
		std::map<yul::ssa::InstId, std::vector<u256>> multiValues;
	};

	SSACFGInterpreter(
		EVMState& _state,
		yul::ssa::ControlFlowGraphs const& _controlFlow,
		std::size_t _maxSteps,
		bool _disableMemoryTrace
	);

	std::vector<u256> runFunction(yul::ssa::SSACFG const& _cfg, std::vector<u256> const& _arguments);
	void execute(yul::ssa::SSACFG const& _cfg, Frame& _frame, yul::ssa::InstId _id);
	void executeBuiltinCall(yul::ssa::SSACFG const& _cfg, Frame& _frame, yul::ssa::InstId _id);
	void executeCall(yul::ssa::SSACFG const& _cfg, Frame& _frame, yul::ssa::InstId _id);

	static u256 const& valueOf(Frame const& _frame, yul::ssa::InstId _id);
	static void setValue(Frame& _frame, yul::ssa::InstId _id, u256 _value);
	void incrementStep();

	EVMState& m_state;
	yul::ssa::ControlFlowGraphs const& m_controlFlow;
	EVMInstructionInterpreter m_builtins;
	std::size_t const m_maxSteps;
	std::size_t m_numSteps = 0;
	std::size_t m_callDepth = 0;
};

}
