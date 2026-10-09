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
 * Yul interpreter.
 */

#pragma once

#include <test/tools/yulInterpreter/EVMState.h>

#include <libyul/ASTForward.h>
#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/optimiser/ASTWalker.h>

#include <libevmasm/Instruction.h>

#include <libsolutil/FixedHash.h>
#include <libsolutil/CommonData.h>

#include <libsolutil/Exceptions.h>

#include <map>

namespace solidity::yul
{
class Dialect;
}

namespace solidity::yul::test
{

class ExpressionNestingLimitReached: public InterpreterTerminatedGeneric
{
};

enum class ControlFlowState
{
	Default,
	Continue,
	Break,
	Leave
};

/// The state of the Yul interpreter: the EVM state and the interpreter's own run control
struct InterpreterState: EVMState
{
	/// This is actually an input parameter that more or less limits the runtime.
	size_t maxSteps = 0;
	size_t numSteps = 0;
	size_t maxExprNesting = 0;
	ControlFlowState controlFlowState = ControlFlowState::Default;

	/// Number of the current state instance, used for recursion protection
	size_t numInstance = 0;
};

/**
 * Scope structure built and maintained during execution.
 */
struct Scope
{
	/// Used for variables and functions. Value is nullptr for variables.
	std::map<YulName, FunctionDefinition const*> names;
	std::map<Block const*, std::unique_ptr<Scope>> subScopes;
	Scope* parent = nullptr;
};

/**
 * Yul interpreter.
 */
class Interpreter: public ASTWalker
{
public:
	/// Executes the Yul interpreter. Flag @param _disableMemoryTracing if set ensures that
	/// instructions that write to memory do not affect @param _state. This
	/// avoids false positives reports by the fuzzer when certain optimizer steps are
	/// activated e.g., Redundant store eliminator, Equal store eliminator.
	static void run(
		InterpreterState& _state,
		AST const& _ast,
		bool _disableExternalCalls,
		bool _disableMemoryTracing
	);

	Interpreter(
		InterpreterState& _state,
		Dialect const& _dialect,
		Scope& _scope,
		bool _disableExternalCalls,
		bool _disableMemoryTracing,
		std::map<YulName, u256> _variables = {}
	):
		m_dialect(_dialect),
		m_state(_state),
		m_variables(std::move(_variables)),
		m_scope(&_scope),
		m_disableExternalCalls(_disableExternalCalls),
		m_disableMemoryTrace(_disableMemoryTracing)
	{
	}

	void operator()(ExpressionStatement const& _statement) override;
	void operator()(Assignment const& _assignment) override;
	void operator()(VariableDeclaration const& _varDecl) override;
	void operator()(If const& _if) override;
	void operator()(Switch const& _switch) override;
	void operator()(FunctionDefinition const&) override;
	void operator()(ForLoop const&) override;
	void operator()(Break const&) override;
	void operator()(Continue const&) override;
	void operator()(Leave const&) override;
	void operator()(Block const& _block) override;

	bytes returnData() const { return m_state.returndata; }
	std::vector<std::string> const& trace() const { return m_state.trace; }

	u256 valueOfVariable(YulName _name) const { return m_variables.at(_name); }

protected:
	/// Asserts that the expression evaluates to exactly one value and returns it.
	virtual u256 evaluate(Expression const& _expression);
	/// Evaluates the expression and returns its value.
	virtual std::vector<u256> evaluateMulti(Expression const& _expression);

	void enterScope(Block const& _block);
	void leaveScope();

	/// Increment interpreter step count, throwing exception if step limit
	/// is reached.
	void incrementStep();

	Dialect const& m_dialect;
	InterpreterState& m_state;
	/// Values of variables.
	std::map<YulName, u256> m_variables;
	Scope* m_scope;
	/// If not set, external calls (e.g. using `call()`) to the same contract
	/// are evaluated in a new parser instance.
	bool m_disableExternalCalls;
	bool m_disableMemoryTrace;
};

/**
 * Yul expression evaluator.
 */
class ExpressionEvaluator: public ASTWalker
{
public:
	ExpressionEvaluator(
		InterpreterState& _state,
		Dialect const& _dialect,
		Scope& _scope,
		std::map<YulName, u256> const& _variables,
		bool _disableExternalCalls,
		bool _disableMemoryTrace
	):
		m_state(_state),
		m_dialect(_dialect),
		m_variables(_variables),
		m_scope(_scope),
		m_disableExternalCalls(_disableExternalCalls),
		m_disableMemoryTrace(_disableMemoryTrace)
	{}

	void operator()(Literal const&) override;
	void operator()(Identifier const&) override;
	void operator()(FunctionCall const& _funCall) override;

	/// Asserts that the expression has exactly one value and returns it.
	u256 value() const;
	/// Returns the list of values of the expression.
	std::vector<u256> values() const { return m_values; }

protected:
	void runExternalCall(evmasm::Instruction _instruction);
	virtual std::unique_ptr<Interpreter> makeInterpreterCopy(std::map<YulName, u256> _variables = {}) const
	{
		return std::make_unique<Interpreter>(
			m_state,
			m_dialect,
			m_scope,
			m_disableExternalCalls,
			m_disableMemoryTrace,
			std::move(_variables)
		);
	}
	virtual std::unique_ptr<Interpreter> makeInterpreterNew(InterpreterState& _state, Scope& _scope) const
	{
		return std::make_unique<Interpreter>(
			_state,
			m_dialect,
			_scope,
			m_disableExternalCalls,
			m_disableMemoryTrace
		);
	}

	void setValue(u256 _value);

	/// Evaluates the given expression from right to left and
	/// stores it in m_value.
	void evaluateArgs(
		std::vector<Expression> const& _expr,
		std::vector<std::optional<LiteralKind>> const* _literalArguments
	);

	/// Increment evaluation count, throwing exception if the
	/// nesting level is beyond the upper bound configured in
	/// the interpreter state.
	void incrementStep();

	InterpreterState& m_state;
	Dialect const& m_dialect;
	/// Values of variables.
	std::map<YulName, u256> const& m_variables;
	Scope& m_scope;
	/// Current value of the expression
	std::vector<u256> m_values;
	/// Current expression nesting level
	unsigned m_nestingLevel = 0;
	bool m_disableExternalCalls;
	/// Flag to disable memory tracing
	bool m_disableMemoryTrace;
};

}
