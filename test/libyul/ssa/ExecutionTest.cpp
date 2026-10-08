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

#include <test/libyul/ssa/ExecutionTest.h>

#include <test/EVMHost.h>

#include <libyul/backends/evm/ssa/io/Parser.h>
#include <libyul/backends/evm/ssa/transform/OptimizationPipeline.h>
#include <libyul/backends/evm/ssa/CallGraph.h>
#include <libyul/backends/evm/ssa/CodeTransform.h>
#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/StackLayoutGenerator.h>
#include <libyul/backends/evm/ssa/StackUtils.h>

#include <libyul/backends/evm/EthAssemblyAdapter.h>
#include <libyul/backends/evm/EVMBuiltins.h>
#include <libyul/backends/evm/EVMDialect.h>

#include <libevmasm/Assembly.h>

#include <libsolutil/AnsiColorized.h>
#include <libsolutil/CommonData.h>
#include <libsolutil/StringUtils.h>
#include <libsolutil/Visitor.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <range/v3/view/map.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <map>
#include <optional>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::test::ssa;
using namespace solidity::frontend::test;
using solidity::test::EVMHost;

namespace
{

/// The observable outcome of a run: whether it reverted, and the storage it leaves
struct Outcome
{
	bool reverted = false;
	std::map<u256, u256> storage;

	bool operator==(Outcome const&) const = default;

	std::string str() const
	{
		if (reverted)
			return "revert";
		std::vector<std::string> entries;
		for (auto const& [key, value]: storage)
			entries.push_back(fmt::format("{}: {}", toCompactHexWithPrefix(key), toCompactHexWithPrefix(value)));
		return entries.empty() ? "{}" : fmt::format("{{{}}}", joinHumanReadable(entries));
	}
};

/// Executes SSA CFGs in Pizlo form: an upsilon writes the shadow of its phi at its position, a phi reads its shadow
/// at its position. Shadows are per function invocation. Supports the builtins the tests use.
class ReferenceInterpreter
{
public:
	ReferenceInterpreter(ControlFlowGraphs const& _cfgs, u256 const& _calldataWord):
		m_cfgs(_cfgs), m_calldataWord(_calldataWord) {}

	Outcome run()
	{
		Outcome outcome;
		try
		{
			call(ControlFlowGraphs::mainGraphID(), {});
		}
		catch (Revert const&)
		{
			outcome.reverted = true;
			return outcome;
		}
		catch (Stop const&)
		{
		}
		for (auto const& [key, value]: m_storage)
			if (value != 0)
				outcome.storage[key] = value;
		return outcome;
	}

private:
	struct Stop {};
	struct Revert {};

	std::vector<u256> call(FunctionGraphID const _graphID, std::vector<u256> const& _arguments)
	{
		SSACFG const& cfg = *m_cfgs.functionGraph(_graphID);
		std::map<InstId, u256> values;
		std::map<InstId, std::vector<u256>> tuples;
		std::map<InstId, u256> shadows;
		soltestAssert(cfg.arguments.size() == _arguments.size());
		for (std::size_t i = 0; i < _arguments.size(); ++i)
			values[cfg.arguments[i]] = _arguments[i];

		auto const value = [&](InstId const _id) -> u256 {
			if (cfg.isLiteral(_id))
				return cfg.literalPayload(_id);
			auto const it = values.find(_id);
			soltestAssert(it != values.end(), fmt::format("{} is read before it is defined", _id));
			return it->second;
		};

		BlockId blockId = cfg.entry;
		while (true)
		{
			soltestAssert(++m_steps < 1'000'000, "step limit exceeded");
			auto const& block = cfg.block(blockId);
			for (InstId const id: block.instructions)
			{
				auto const& inst = cfg.inst(id);
				switch (inst.opcode)
				{
				case InstOpcode::Phi:
				{
					auto const it = shadows.find(id);
					soltestAssert(it != shadows.end(), fmt::format("phi {} reads its shadow before any upsilon writes it", id));
					values[id] = it->second;
					break;
				}
				case InstOpcode::Upsilon:
					shadows[cfg.upsilonPhi(id)] = value(inst.inputs.at(0));
					break;
				case InstOpcode::BuiltinCall:
				{
					std::vector<u256> arguments;
					for (InstId const input: inst.inputs)
						arguments.push_back(value(input));
					if (std::optional<u256> const result = builtin(cfg, id, arguments))
						values[id] = *result;
					break;
				}
				case InstOpcode::Call:
				{
					std::vector<u256> arguments;
					for (InstId const input: inst.inputs)
						arguments.push_back(value(input));
					std::vector<u256> results = call(cfg.callPayload(id).graphID, arguments);
					if (results.size() == 1)
						values[id] = results.front();
					else
						tuples[id] = std::move(results);
					break;
				}
				case InstOpcode::Projection:
					values[id] = tuples.at(inst.inputs.at(0)).at(cfg.projectionIndex(id));
					break;
				case InstOpcode::Identity:
					values[id] = value(inst.inputs.at(0));
					break;
				case InstOpcode::MemoryGuard:
					soltestAssert(m_cfgs.memoryGuard.has_value());
					values[id] = *m_cfgs.memoryGuard;
					break;
				case InstOpcode::Const:
				case InstOpcode::FunctionArg:
				case InstOpcode::Nop:
					break;
				case InstOpcode::Unreachable:
				case InstOpcode::Tombstone:
					soltestAssert(false, fmt::format("{} executed", id));
				}
			}

			if (auto const* jump = std::get_if<SSACFG::BasicBlock::Jump>(&block.exit))
				blockId = jump->target;
			else if (auto const* conditionalJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&block.exit))
				blockId = value(conditionalJump->condition) != 0 ? conditionalJump->nonZero : conditionalJump->zero;
			else if (auto const* functionReturn = std::get_if<SSACFG::BasicBlock::FunctionReturn>(&block.exit))
			{
				std::vector<u256> results;
				for (InstId const returnValue: functionReturn->returnValues)
					results.push_back(value(returnValue));
				return results;
			}
			else if (block.isMainExitBlock())
				throw Stop{};
			else
				soltestAssert(false, fmt::format("block {} terminates without a terminating builtin", blockId));
		}
	}

	std::optional<u256> builtin(SSACFG const& _cfg, InstId const _id, std::vector<u256> const& _args)
	{
		std::string const& name = _cfg.evmDialect.builtin(_cfg.builtinPayload(_id).builtin).name;
		auto const arg = [&](std::size_t _index) -> u256 const& { return _args.at(_index); };
		if (name == "add") return arg(0) + arg(1);
		if (name == "sub") return arg(0) - arg(1);
		if (name == "mul") return arg(0) * arg(1);
		if (name == "div") return arg(1) == 0 ? u256(0) : arg(0) / arg(1);
		if (name == "mod") return arg(1) == 0 ? u256(0) : arg(0) % arg(1);
		if (name == "lt") return u256(arg(0) < arg(1));
		if (name == "gt") return u256(arg(0) > arg(1));
		if (name == "eq") return u256(arg(0) == arg(1));
		if (name == "iszero") return u256(arg(0) == 0);
		if (name == "and") return arg(0) & arg(1);
		if (name == "or") return arg(0) | arg(1);
		if (name == "xor") return arg(0) ^ arg(1);
		if (name == "not") return ~arg(0);
		if (name == "shl") return arg(0) >= 256 ? u256(0) : u256(arg(1) << static_cast<unsigned>(arg(0)));
		if (name == "shr") return arg(0) >= 256 ? u256(0) : u256(arg(1) >> static_cast<unsigned>(arg(0)));
		if (name == "calldataload") return arg(0) == 0 ? m_calldataWord : u256(0);
		if (name == "calldatasize") return u256(32);
		if (name == "sload") return m_storage[arg(0)];
		if (name == "mload") return m_memory[arg(0)];
		if (name == "sstore")
		{
			m_storage[arg(0)] = arg(1);
			return std::nullopt;
		}
		if (name == "mstore")
		{
			soltestAssert(arg(0) % 32 == 0, "the reference interpreter only supports word-aligned memory");
			m_memory[arg(0)] = arg(1);
			return std::nullopt;
		}
		if (name == "stop" || name == "return")
			throw Stop{};
		if (name == "revert" || name == "invalid")
			throw Revert{};
		soltestAssert(false, fmt::format("builtin {} is not supported by the reference interpreter", name));
		solidity::util::unreachable();
	}

	ControlFlowGraphs const& m_cfgs;
	u256 m_calldataWord;
	std::map<u256, u256> m_storage;
	std::map<u256, u256> m_memory;
	std::size_t m_steps = 0;
};

Outcome execute(langutil::EVMVersion const _evmVersion, evmc::VM& _vm, bytes const& _code, u256 const& _calldataWord)
{
	EVMHost host(_evmVersion, _vm);
	evmc::address const address = EVMHost::convertToEVMC(h160("0x1000000000000000000000000000000000000000"));
	host.accounts[address].code = evmc::bytes(_code.begin(), _code.end());

	bytes const calldata = toBigEndian(_calldataWord);
	evmc_message message{};
	message.kind = EVMC_CALL;
	message.gas = 100'000'000;
	message.recipient = address;
	message.code_address = address;
	message.input_data = calldata.data();
	message.input_size = calldata.size();
	evmc::Result const result = host.call(message);

	Outcome outcome;
	if (result.status_code == EVMC_REVERT)
	{
		outcome.reverted = true;
		return outcome;
	}
	soltestAssert(result.status_code == EVMC_SUCCESS, fmt::format("execution failed with status {}", static_cast<int>(result.status_code)));
	for (auto const& [key, value]: host.accounts[address].storage)
		if (u256 const word = u256(EVMHost::convertFromEVMC(value.current)); word != 0)
			outcome.storage[u256(EVMHost::convertFromEVMC(key))] = word;
	return outcome;
}

}

std::unique_ptr<TestCase> ExecutionTest::create(Config const& _config)
{
	return std::make_unique<ExecutionTest>(_config.filename, _config.evmVersion, _config.vmPaths);
}

ExecutionTest::ExecutionTest(
	std::string const& _filename,
	langutil::EVMVersion const _evmVersion,
	std::vector<boost::filesystem::path> _vmPaths
):
	TestCase(_filename),
	m_evmVersion(_evmVersion),
	m_vmPaths(std::move(_vmPaths))
{
	m_source = m_reader.source();
	std::string calldata = m_reader.stringSetting("calldata", "0");
	boost::algorithm::trim(calldata);
	boost::algorithm::split(m_calldata, calldata, boost::is_any_of(" "), boost::token_compress_on);
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult ExecutionTest::run(std::ostream& _stream, std::string const& _linePrefix, bool const _formatted)
{
	EVMDialect const& dialect = EVMDialect::strictAssemblyForEVMObjects(m_evmVersion);
	auto parsed = io::parse(m_source, dialect);
	if (!parsed)
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) <<
			_linePrefix << "Parse error: " << parsed.error().message << std::endl;
		return TestResult::FatalError;
	}
	// the reference interprets the graphs as written, the bytecode is compiled from the graphs after the passes
	auto const reference = io::parse(m_source, dialect);
	ControlFlowGraphs& cfgs = **parsed;

	transform::optimize(cfgs);
	m_obtainedResult = cfgs.print();

	evmasm::Assembly assembly{m_evmVersion, false, {}};
	{
		EthAssemblyAdapter adapter(assembly);
		BuiltinContext context;
		ControlFlowGraphsLiveness const liveness(cfgs);
		// the slots the code transform spills, recomputed the way it computes them
		CallGraph const callGraph(cfgs);
		for (std::size_t functionIndex = 0; functionIndex < cfgs.functionGraphs.size(); ++functionIndex)
		{
			SSACFG const& cfg = *cfgs.functionGraphs[functionIndex];
			auto const graphID = static_cast<ControlFlowGraphs::FunctionGraphID>(functionIndex);
			auto result = StackLayoutGenerator::generate(
				*liveness.cfgLiveness[functionIndex],
				gatherCallSites(cfg),
				graphID,
				!callGraph.isRecursive(graphID)
			);
			spill::SpillStoreTraces storeTraces;
			result.spillSet.closeUnderReachabilityConstraints(cfg, result.layout, &storeTraces);
			std::vector<std::string> stores;
			for (auto const& [site, slots]: storeTraces)
				for (StackSlot const& slot: slots | ranges::views::keys)
					stores.push_back(std::visit(GenericVisitor{
						[&](BlockId const _block) { return fmt::format("{} on entry of #{}", slot, _block.value); },
						[&](InstId const _inst) { return fmt::format("{} behind {}", slot, _inst); }
					}, site));
			if (!stores.empty())
				m_obtainedResult += fmt::format(
					"spilled in {}: {}\n",
					cfg.isMainGraph() ? "<main>" : "@" + cfg.name,
					fmt::join(stores, ", ")
				);
		}
		CodeTransform::run(adapter, cfgs, liveness, context);
	}
	bytes const code = assembly.assemble().bytecode;

	evmc::VM* vm = nullptr;
	for (auto const& path: m_vmPaths)
		if (evmc::VM& candidate = EVMHost::getVM(path.string()); candidate.has_capability(EVMC_CAPABILITY_EVM1))
		{
			vm = &candidate;
			break;
		}
	soltestAssert(vm, "no EVM1 capable VM available");

	bool agree = true;

	for (std::string const& word: m_calldata)
	{
		u256 const calldataWord(word);
		Outcome const expected = ReferenceInterpreter(**reference, calldataWord).run();
		Outcome const actual = execute(m_evmVersion, *vm, code, calldataWord);
		m_obtainedResult += fmt::format("calldata {}: {}\n", toCompactHexWithPrefix(calldataWord), expected.str());
		if (actual != expected)
		{
			agree = false;
			AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) <<
				_linePrefix << "Bytecode disagrees with the reference interpreter for calldata " <<
				toCompactHexWithPrefix(calldataWord) << ":" << std::endl <<
				_linePrefix << "  reference: " << expected.str() << std::endl <<
				_linePrefix << "  bytecode:  " << actual.str() << std::endl;
		}
	}
	if (!agree)
		return TestResult::FatalError;
	return checkResult(_stream, _linePrefix, _formatted);
}
