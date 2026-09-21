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

#include <libyul/backends/evm/ssa/io/Parser.h>

#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/CommonData.h>
#include <libsolutil/StringUtils.h>
#include <libsolutil/Visitor.h>

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::io;

namespace
{

using Id = InstId::ValueType;
using Exit = decltype(SSACFG::BasicBlock::exit);

/// `@name` or `@name#id`: a function by name, or by its position among the graphs where names repeat
struct FunctionRef
{
	std::string name;
	std::optional<FunctionGraphID> id;
};

struct ParsedInst
{
	enum class Kind { Const, Builtin, Call, Projection, Phi, Upsilon, Arg, MemoryGuard, Unreachable };
	Kind kind = Kind::Unreachable;
	/// the value named in the text; upsilons have none
	std::optional<Id> id;
	std::vector<Id> inputs;
	u256 constant;
	std::string builtin;
	FunctionRef callee;
	std::size_t index = 0;
	/// the `{ nocontinue = true }` marker on a call, checked against the callee
	bool markedNoContinue = false;
	/// the id the instruction got in the store
	InstId allocated;
};

struct ParsedBlock
{
	BlockId::ValueType id = 0;
	std::optional<std::vector<BlockId::ValueType>> preds;
	std::vector<ParsedInst> instructions;
	std::optional<Exit> exit;
};

struct ParsedGraph
{
	FunctionRef ref;
	std::vector<Id> arguments;
	std::size_t numReturns = 0;
	/// the `nocontinue` marker, checked against what the blocks say
	bool markedNoContinue = false;
	std::vector<ParsedBlock> blocks;
	/// whether a return is reachable from the entry, derived from the blocks
	bool canContinue = true;
};

/// The text as a stream of tokens: names (`v3`, `builtin`, `nocontinue`), numbers, the punctuation the printer
/// uses, and line ends, which delimit instructions. Comments run to the end of the line.
class Tokenizer
{
public:
	enum class Kind { Name, Number, Punct, Newline, End };
	struct Token
	{
		Kind kind;
		std::string text;
		std::size_t line;
	};

	explicit Tokenizer(std::string const& _text)
	{
		std::size_t line = 1;
		auto const emit = [&](Kind const _kind, std::string _text) { m_tokens.push_back({_kind, std::move(_text), line}); };
		for (std::size_t i = 0; i < _text.size();)
		{
			char const c = _text[i];
			if (c == '\n')
			{
				emit(Kind::Newline, "\n");
				++line;
				++i;
			}
			else if (std::isspace(static_cast<unsigned char>(c)))
				++i;
			else if (_text.compare(i, 2, "//") == 0)
				while (i < _text.size() && _text[i] != '\n')
					++i;
			else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
			{
				std::size_t const begin = i;
				while (i < _text.size() && (std::isalnum(static_cast<unsigned char>(_text[i])) || _text[i] == '_'))
					++i;
				emit(Kind::Name, _text.substr(begin, i - begin));
			}
			else if (std::isdigit(static_cast<unsigned char>(c)))
			{
				std::size_t const begin = i;
				while (i < _text.size() && std::isalnum(static_cast<unsigned char>(_text[i])))
					++i;
				emit(Kind::Number, _text.substr(begin, i - begin));
			}
			else if (_text.compare(i, 2, "->") == 0)
			{
				emit(Kind::Punct, "->");
				i += 2;
			}
			else if (std::string_view("#:,=@^(){}").find(c) != std::string_view::npos)
			{
				emit(Kind::Punct, std::string(1, c));
				++i;
			}
			else
				throw ParserError(fmt::format("line {}: unexpected character '{}'", line, c));
		}
		emit(Kind::Newline, "\n");
		emit(Kind::End, "");
	}

	Token const& peek() const { return m_tokens[m_position]; }
	Token const& next() { return m_tokens[m_position++]; }
	bool atEnd() const { return peek().kind == Kind::End; }

private:
	std::vector<Token> m_tokens;
	std::size_t m_position = 0;
};

struct ParsedModule
{
	std::optional<u256> memoryGuard;
	std::vector<ParsedGraph> graphs;
};

/// Recursive descent over the tokens:
///   module      := ['memoryguard' '=' number NL] graph*
///   graph       := block+                                    (the main graph, first and without a header)
///                | 'func' function '(' 'args' ':' '(' values ')' ')' '->' number ['nocontinue'] '{' NL block+ '}' NL
///   function    := '@' name ['#' number]                     (the number is the graph's position, for repeated names)
///   block       := '#' number ':' ['preds' ':' blocks] NL (instruction NL)* exit NL
///   instruction := 'upsilon' value '->' '^' value
///                | [value '='] ('const' number | 'builtin' '@' name values | 'call' function values ['{' 'nocontinue' '=' 'true' '}']
///                               | 'proj' value ',' number | 'phi' | 'arg' number | 'memoryguard' | 'unreachable')
///   exit        := 'main_exit' | 'terminated' | 'jump' block | 'branch' value ',' block ',' block | 'return' values
/// Whether a function can continue is not taken from the text but derived from its blocks: it can if a return is
/// reachable from its entry. The `nocontinue` markers only have to agree with that.
class Parser
{
public:
	explicit Parser(std::string const& _text): m_tokens(_text) {}

	ParsedModule parseModule()
	{
		ParsedModule module;
		skipNewlines();
		if (acceptName("memoryguard"))
		{
			expectPunct("=");
			module.memoryGuard = u256(expect(Tokenizer::Kind::Number).text);
			expectNewline();
			skipNewlines();
		}
		while (!m_tokens.atEnd())
		{
			if (peekName("func"))
				module.graphs.push_back(parseFunction());
			else if (peekPunct("#"))
			{
				if (!module.graphs.empty())
					fail("a block outside of a function has to belong to the main graph, which comes first");
				module.graphs.emplace_back();
				while (peekPunct("#"))
					module.graphs.back().blocks.push_back(parseBlock());
			}
			else
				fail("expected a function or a block");
			skipNewlines();
		}
		if (module.graphs.empty())
			fail("no main graph");
		return module;
	}

private:
	[[noreturn]] void fail(std::string const& _message) const
	{
		throw ParserError(fmt::format("line {}: {}", m_tokens.peek().line, _message));
	}

	Tokenizer::Token const& expect(Tokenizer::Kind const _kind, std::string_view const _what = {})
	{
		if (m_tokens.peek().kind != _kind)
			fail(fmt::format(
				"expected {}, got '{}'",
				_what.empty() ? (_kind == Tokenizer::Kind::Name ? "a name" : _kind == Tokenizer::Kind::Number ? "a number" : "a line end") : _what,
				m_tokens.peek().text
			));
		return m_tokens.next();
	}
	bool peekName(std::string_view const _name) const { return m_tokens.peek().kind == Tokenizer::Kind::Name && m_tokens.peek().text == _name; }
	bool peekPunct(std::string_view const _punct) const { return m_tokens.peek().kind == Tokenizer::Kind::Punct && m_tokens.peek().text == _punct; }
	bool acceptName(std::string_view const _name)
	{
		if (!peekName(_name))
			return false;
		m_tokens.next();
		return true;
	}
	bool acceptPunct(std::string_view const _punct)
	{
		if (!peekPunct(_punct))
			return false;
		m_tokens.next();
		return true;
	}
	void expectName(std::string_view const _name)
	{
		if (!acceptName(_name))
			fail(fmt::format("expected '{}', got '{}'", _name, m_tokens.peek().text));
	}
	void expectPunct(std::string_view const _punct)
	{
		if (!acceptPunct(_punct))
			fail(fmt::format("expected '{}', got '{}'", _punct, m_tokens.peek().text));
	}
	void expectNewline() { expect(Tokenizer::Kind::Newline); }
	void skipNewlines()
	{
		while (m_tokens.peek().kind == Tokenizer::Kind::Newline)
			m_tokens.next();
	}

	template<typename T>
	T number()
	{
		auto const& token = expect(Tokenizer::Kind::Number);
		if (auto const value = parseArithmetic<T>(token.text))
			return *value;
		fail(fmt::format("'{}' is out of range", token.text));
	}

	/// `vN`
	Id value()
	{
		auto const& token = expect(Tokenizer::Kind::Name, "a value like v3");
		if (token.text.size() < 2 || token.text[0] != 'v')
			fail(fmt::format("expected a value like v3, got '{}'", token.text));
		if (auto const id = parseArithmetic<Id>(std::string_view(token.text).substr(1)))
			return *id;
		fail(fmt::format("expected a value like v3, got '{}'", token.text));
	}
	/// `@name` or `@name#id`
	FunctionRef function()
	{
		expectPunct("@");
		FunctionRef ref;
		ref.name = expect(Tokenizer::Kind::Name, "a function name").text;
		if (acceptPunct("#"))
			ref.id = number<FunctionGraphID>();
		return ref;
	}
	/// `#N`
	BlockId::ValueType block()
	{
		expectPunct("#");
		return number<BlockId::ValueType>();
	}
	/// comma-separated values up to the end of the line or the next non-value token, possibly none
	std::vector<Id> values()
	{
		std::vector<Id> result;
		if (m_tokens.peek().kind != Tokenizer::Kind::Name || !m_tokens.peek().text.starts_with('v'))
			return result;
		result.push_back(value());
		while (acceptPunct(","))
			result.push_back(value());
		return result;
	}
	std::vector<BlockId::ValueType> blocks()
	{
		std::vector<BlockId::ValueType> result{block()};
		while (acceptPunct(","))
			result.push_back(block());
		return result;
	}

	ParsedGraph parseFunction()
	{
		ParsedGraph function;
		expectName("func");
		function.ref = this->function();
		expectPunct("(");
		expectName("args");
		expectPunct(":");
		expectPunct("(");
		function.arguments = values();
		expectPunct(")");
		expectPunct(")");
		expectPunct("->");
		function.numReturns = number<std::size_t>();
		function.markedNoContinue = acceptName("nocontinue");
		expectPunct("{");
		expectNewline();
		skipNewlines();
		while (peekPunct("#"))
			function.blocks.push_back(parseBlock());
		expectPunct("}");
		expectNewline();
		return function;
	}

	ParsedBlock parseBlock()
	{
		ParsedBlock parsed;
		parsed.id = block();
		expectPunct(":");
		if (acceptName("preds"))
		{
			expectPunct(":");
			parsed.preds = blocks();
		}
		expectNewline();
		skipNewlines();
		while (!parsed.exit)
		{
			if (auto exit = parseExit())
				parsed.exit = std::move(exit);
			else
				parsed.instructions.push_back(parseInstruction());
			expectNewline();
			skipNewlines();
		}
		return parsed;
	}

	std::optional<Exit> parseExit()
	{
		if (acceptName("main_exit"))
			return SSACFG::BasicBlock::MainExit{};
		if (acceptName("terminated"))
			return SSACFG::BasicBlock::Terminated{};
		if (acceptName("jump"))
			return SSACFG::BasicBlock::Jump{BlockId{block()}};
		if (acceptName("branch"))
		{
			InstId const condition{value()};
			expectPunct(",");
			BlockId const nonZero{block()};
			expectPunct(",");
			BlockId const zero{block()};
			return SSACFG::BasicBlock::ConditionalJump{condition, nonZero, zero};
		}
		if (acceptName("return"))
		{
			SSACFG::BasicBlock::FunctionReturn ret;
			for (Id const id: values())
				ret.returnValues.emplace_back(id);
			return ret;
		}
		return std::nullopt;
	}

	ParsedInst parseInstruction()
	{
		ParsedInst inst{};
		inst.kind = ParsedInst::Kind::Unreachable;
		if (acceptName("upsilon"))
		{
			inst.kind = ParsedInst::Kind::Upsilon;
			Id const input = value();
			expectPunct("->");
			expectPunct("^");
			inst.inputs = {input, value()};
			return inst;
		}
		if (m_tokens.peek().kind == Tokenizer::Kind::Name && m_tokens.peek().text.starts_with('v') && m_tokens.peek().text.size() > 1)
		{
			inst.id = value();
			expectPunct("=");
		}
		auto const& keyword = expect(Tokenizer::Kind::Name, "an instruction");
		if (keyword.text == "const")
		{
			inst.kind = ParsedInst::Kind::Const;
			inst.constant = u256(expect(Tokenizer::Kind::Number).text);
		}
		else if (keyword.text == "builtin")
		{
			inst.kind = ParsedInst::Kind::Builtin;
			expectPunct("@");
			inst.builtin = expect(Tokenizer::Kind::Name, "a builtin name").text;
			inst.inputs = values();
		}
		else if (keyword.text == "call")
		{
			inst.kind = ParsedInst::Kind::Call;
			inst.callee = function();
			inst.inputs = values();
			if (acceptPunct("{"))
			{
				expectName("nocontinue");
				expectPunct("=");
				expectName("true");
				expectPunct("}");
				inst.markedNoContinue = true;
			}
		}
		else if (keyword.text == "proj")
		{
			inst.kind = ParsedInst::Kind::Projection;
			inst.inputs = {value()};
			expectPunct(",");
			inst.index = number<std::size_t>();
		}
		else if (keyword.text == "phi")
			inst.kind = ParsedInst::Kind::Phi;
		else if (keyword.text == "arg")
		{
			inst.kind = ParsedInst::Kind::Arg;
			inst.index = number<std::size_t>();
		}
		else if (keyword.text == "memoryguard")
			inst.kind = ParsedInst::Kind::MemoryGuard;
		else if (keyword.text == "unreachable")
			inst.kind = ParsedInst::Kind::Unreachable;
		else
			fail(fmt::format("unknown instruction '{}'", keyword.text));
		if (inst.kind != ParsedInst::Kind::Builtin && inst.kind != ParsedInst::Kind::Call && !inst.id)
			fail("instruction needs a value name");
		return inst;
	}

	Tokenizer m_tokens;
};

class GraphBuilder
{
public:
	GraphBuilder(ParsedModule& _module, ControlFlowGraphs& _cfgs, EVMDialect const& _dialect):
		m_module(_module), m_cfgs(_cfgs), m_dialect(_dialect)
	{}

	void build()
	{
		// every graph first, so calls can refer to functions defined later in the text
		for (std::size_t index = 0; index < m_module.graphs.size(); ++index)
		{
			ParsedGraph& parsed = m_module.graphs[index];
			auto const id = static_cast<FunctionGraphID>(index);
			if (parsed.ref.id && *parsed.ref.id != id)
				throw ParserError(fmt::format("function @{}#{} is graph {}", parsed.ref.name, *parsed.ref.id, id));
			if (!parsed.ref.name.empty())
				m_graphsByName[parsed.ref.name].push_back(id);
			parsed.canContinue = index == 0 || returnReachable(parsed);
			if (parsed.markedNoContinue == parsed.canContinue)
				throw ParserError(fmt::format(
					"function @{} {} continue, its 'nocontinue' marker says otherwise", parsed.ref.name, parsed.canContinue ? "can" : "cannot"
				));
			m_cfgs.functionGraphs.emplace_back(std::make_unique<SSACFG>(m_dialect));
			SSACFG& cfg = *m_cfgs.functionGraphs.back();
			cfg.name = parsed.ref.name;
			cfg.numReturns = parsed.numReturns;
			cfg.canContinue = parsed.canContinue;
		}
		for (std::size_t index = 0; index < m_module.graphs.size(); ++index)
			buildGraph(m_module.graphs[index], *m_cfgs.functionGraphs[index]);
	}

private:
	/// Whether a block with a return exit is reachable from the entry
	static bool returnReachable(ParsedGraph const& _graph)
	{
		std::map<BlockId::ValueType, ParsedBlock const*> byId;
		for (ParsedBlock const& block: _graph.blocks)
			byId.emplace(block.id, &block);
		std::set<BlockId::ValueType> seen{0};
		std::vector<BlockId::ValueType> queue{0};
		while (!queue.empty())
		{
			auto const it = byId.find(queue.back());
			queue.pop_back();
			if (it == byId.end() || !it->second->exit)
				continue;
			bool found = false;
			auto const visit = [&](BlockId const _next) {
				if (seen.insert(_next.value).second)
					queue.push_back(_next.value);
			};
			std::visit(GenericVisitor{
				[&](SSACFG::BasicBlock::FunctionReturn const&) { found = true; },
				[&](SSACFG::BasicBlock::Jump const& _jump) { visit(_jump.target); },
				[&](SSACFG::BasicBlock::ConditionalJump const& _cjump) { visit(_cjump.nonZero); visit(_cjump.zero); },
				[](auto const&) {}
			}, *it->second->exit);
			if (found)
				return true;
		}
		return false;
	}

	FunctionGraphID resolve(FunctionRef const& _ref) const
	{
		auto const it = m_graphsByName.find(_ref.name);
		if (it == m_graphsByName.end())
			throw ParserError(fmt::format("unknown function @{}", _ref.name));
		if (_ref.id)
		{
			if (std::find(it->second.begin(), it->second.end(), *_ref.id) == it->second.end())
				throw ParserError(fmt::format("no function @{}#{}", _ref.name, *_ref.id));
			return *_ref.id;
		}
		if (it->second.size() > 1)
			throw ParserError(fmt::format("@{} names {} functions, say which one by @{}#id", _ref.name, it->second.size(), _ref.name));
		return it->second.front();
	}

	void buildGraph(ParsedGraph const& _parsed, SSACFG& _cfg)
	{
		// makeBlock numbers densely; ids the text skips (blocks a transform freed) are made and freed again
		std::size_t numBlocks = 0;
		std::set<BlockId::ValueType> used;
		for (ParsedBlock const& block: _parsed.blocks)
		{
			numBlocks = std::max(numBlocks, static_cast<std::size_t>(block.id) + 1);
			if (!used.insert(block.id).second)
				throw ParserError(fmt::format("#{} defined twice", block.id));
		}
		if (!used.contains(0))
			throw ParserError(fmt::format("graph @{} has no entry block #0", _parsed.ref.name));
		for (std::size_t i = 0; i < numBlocks; ++i)
			_cfg.makeBlock(nullptr);
		for (std::size_t i = 0; i < numBlocks; ++i)
			if (!used.contains(static_cast<BlockId::ValueType>(i)))
				_cfg.resetBlock(BlockId{static_cast<BlockId::ValueType>(i)});
		_cfg.entry = BlockId{0};

		// Named values are allocated in ascending order, so every one gets the id the text gives it. Instructions
		// without a name (upsilons, calls without a result) take ids too, but no visible ones: they come last.
		std::map<Id, std::pair<ParsedBlock const*, ParsedInst*>> byId;
		std::vector<std::pair<ParsedBlock const*, ParsedInst*>> unnamed;
		std::vector<std::unique_ptr<ParsedBlock>> blocks;  // mutable copies, ids get filled in
		for (ParsedBlock const& block: _parsed.blocks)
			blocks.push_back(std::make_unique<ParsedBlock>(block));
		for (auto const& block: blocks)
			for (ParsedInst& inst: block->instructions)
				if (!inst.id)
					unnamed.emplace_back(block.get(), &inst);
				else if (!byId.emplace(*inst.id, std::pair{block.get(), &inst}).second)
					throw ParserError(fmt::format("v{} defined twice", *inst.id));

		InstructionStore& store = _cfg.instructionStore();
		std::vector<InstId> arguments(_parsed.arguments.size());
		auto const allocate = [&](ParsedBlock const& block, ParsedInst& inst) -> InstId
		{
			BlockId const blockId{block.id};
			auto const inputs = [&] {
				std::vector<InstId> result;
				for (Id const input: inst.inputs)
					result.emplace_back(input);
				return result;
			};
			std::string const name = inst.id ? fmt::format("v{}", *inst.id) : "an unnamed instruction";
			switch (inst.kind)
			{
			case ParsedInst::Kind::Const:
			{
				auto const [constId, fresh] = store.appendLiteral(_cfg.entry, inst.constant);
				if (!fresh)
					throw ParserError(fmt::format("{}: the constant {} is already v{}", name, toCompactHexWithPrefix(inst.constant), constId.value));
				return constId;
			}
			case ParsedInst::Kind::Builtin:
			{
				auto const handle = m_dialect.findBuiltin(inst.builtin);
				if (!handle)
					throw ParserError(fmt::format("{}: unknown builtin @{}", name, inst.builtin));
				BuiltinFunction const& builtin = m_dialect.builtin(*handle);
				for (std::size_t i = 0; i < builtin.numParameters; ++i)
					if (builtin.literalArgument(i))
						throw ParserError(fmt::format("builtin @{} takes literal arguments, which are not supported", inst.builtin));
				if (inst.inputs.size() != builtin.numParameters)
					throw ParserError(fmt::format("builtin @{} takes {} arguments", inst.builtin, builtin.numParameters));
				if ((builtin.numReturns >= 1) != inst.id.has_value())
					throw ParserError(fmt::format("builtin @{} returns {} values", inst.builtin, builtin.numReturns));
				auto const numReturns = static_cast<InstructionStore::NumReturnsSizeType>(builtin.numReturns);
				return numReturns >= 2 ?
					store.appendBuiltinCallWithProjections(blockId, SSACFG::BuiltinCall{*handle, {}}, inputs(), numReturns) :
					store.appendBuiltinCall(blockId, SSACFG::BuiltinCall{*handle, {}}, inputs());
			}
			case ParsedInst::Kind::Call:
			{
				FunctionGraphID const callee = resolve(inst.callee);
				SSACFG const& calleeGraph = *m_cfgs.functionGraphs[callee];
				std::size_t const numArguments = m_module.graphs[callee].arguments.size();
				if (inst.inputs.size() != numArguments)
					throw ParserError(fmt::format("function @{} takes {} arguments", inst.callee.name, numArguments));
				if ((calleeGraph.numReturns >= 1) != inst.id.has_value())
					throw ParserError(fmt::format("function @{} returns {} values", inst.callee.name, calleeGraph.numReturns));
				if (inst.markedNoContinue == calleeGraph.canContinue)
					throw ParserError(fmt::format("{}: function @{} {} continue, the call's marker says otherwise", name, inst.callee.name, calleeGraph.canContinue ? "can" : "cannot"));
				SSACFG::Call const payload{callee, calleeGraph.canContinue, calleeGraph.numReturns};
				return calleeGraph.numReturns >= 2 ?
					store.appendCallWithProjections(blockId, payload, inputs()) :
					store.appendCall(blockId, payload, inputs());
			}
			case ParsedInst::Kind::Phi:
				return store.appendPhi(blockId);
			case ParsedInst::Kind::Arg:
			{
				if (inst.index >= arguments.size() || _parsed.arguments[inst.index] != *inst.id)
					throw ParserError(fmt::format("{} is not argument {} of the function header", name, inst.index));
				InstId const allocated = store.appendFunctionArg(_cfg.entry);
				arguments[inst.index] = allocated;
				return allocated;
			}
			case ParsedInst::Kind::MemoryGuard:
				return store.appendMemoryGuard(blockId);
			case ParsedInst::Kind::Unreachable:
				return store.appendUnreachable();
			case ParsedInst::Kind::Upsilon:
			{
				InstId const phi{inst.inputs[1]};
				if (phi.value >= store.numInsts() || !store.inst(phi).isPhi())
					throw ParserError(fmt::format("upsilon targets v{}, which is not a phi", phi.value));
				return store.appendUpsilon(blockId, InstId{inst.inputs[0]}, phi);
			}
			case ParsedInst::Kind::Projection:
				yulAssert(false);
			}
			solidity::util::unreachable();
		};

		for (auto& [id, entry]: byId)
		{
			auto& [block, inst] = entry;
			if (inst->kind == ParsedInst::Kind::Projection)
			{
				// allocated along with its producer
				inst->allocated = InstId{id};
				continue;
			}
			while (store.numInsts() < id)
				store.appendUnreachable();  // a gap in the numbering, left unscheduled
			InstId const allocated = allocate(*block, *inst);
			if (allocated.value != id)
				throw ParserError(fmt::format("v{} cannot get its id, the store handed out v{} (does a multi-return producer's numbering leave room for its projections?)", id, allocated.value));
			inst->allocated = allocated;
		}
		for (auto const& [id, entry]: byId)
			if (entry.second->kind == ParsedInst::Kind::Projection)
			{
				ParsedInst const& inst = *entry.second;
				InstId const producer{inst.inputs.front()};
				if (id >= store.numInsts() || !store.inst(InstId{id}).isProjection() || store.inst(InstId{id}).inputs.front() != producer || store.projectionIndex(InstId{id}) != inst.index)
					throw ParserError(fmt::format("v{} is not projection {} of v{}", id, inst.index, producer.value));
			}
		for (auto& [block, inst]: unnamed)
			inst->allocated = allocate(*block, *inst);
		for (std::size_t i = 0; i < arguments.size(); ++i)
			if (!arguments[i].hasValue())
				throw ParserError(fmt::format("argument {} of @{} has no 'arg' instruction", i, _parsed.ref.name));
		_cfg.arguments = std::move(arguments);

		// schedule in text order and wire up the blocks
		for (auto const& block: blocks)
		{
			BlockId const blockId{block->id};
			SSACFG::BasicBlock& basicBlock = _cfg.block(blockId);
			for (ParsedInst const& inst: block->instructions)
			{
				store.inst(inst.allocated).block = blockId;
				basicBlock.instructions.push_back(inst.allocated);
			}
			if (!block->exit)
				throw ParserError(fmt::format("#{} of @{} has no exit", block->id, _parsed.ref.name));
			basicBlock.exit = *block->exit;
			if (std::holds_alternative<SSACFG::BasicBlock::FunctionReturn>(*block->exit))
				_cfg.exits.insert(blockId);
		}
		for (auto const& block: blocks)
			_cfg.block(BlockId{block->id}).forEachExit([&](BlockId const _successor) {
				if (!used.contains(_successor.value))
					throw ParserError(fmt::format("#{} jumps to the undefined block #{}", block->id, _successor.value));
				_cfg.block(_successor).entries.emplace_back(block->id);
			});
		for (auto const& block: blocks)
			if (block->preds)
			{
				auto& entries = _cfg.block(BlockId{block->id}).entries;
				std::vector<BlockId> declared;
				for (auto const pred: *block->preds)
					declared.emplace_back(pred);
				if (!std::is_permutation(declared.begin(), declared.end(), entries.begin(), entries.end()))
					throw ParserError(fmt::format("#{} declares predecessors that do not match the jumps to it", block->id));
				entries = std::move(declared);
			}
	}

	ParsedModule& m_module;
	ControlFlowGraphs& m_cfgs;
	EVMDialect const& m_dialect;
	std::map<std::string, std::vector<FunctionGraphID>> m_graphsByName;
};

}

std::unique_ptr<ControlFlowGraphs> io::parse(std::string const& _text, EVMDialect const& _dialect)
{
	ParsedModule module = Parser(_text).parseModule();
	auto cfgs = std::make_unique<ControlFlowGraphs>();
	cfgs->memoryGuard = module.memoryGuard;
	GraphBuilder(module, *cfgs, _dialect).build();
	return cfgs;
}
