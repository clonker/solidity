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
#include <test/tools/yulInterpreter/SSACFGInterpreter.h>

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

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::test::ssa;
using namespace solidity::frontend::test;
using solidity::test::EVMHost;
using solidity::yul::test::InterpreterState;
using solidity::yul::test::SSACFGInterpreter;

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

/// The outcome of interpreting the graphs: an upsilon writes the shadow of its phi when it executes, a phi reads
/// its shadow when it executes
Outcome interpret(ControlFlowGraphs const& _cfgs, u256 const& _calldataWord)
{
	InterpreterState state;
	state.calldata = toBigEndian(_calldataWord);
	state.maxSteps = 1'000'000;
	auto const [outcome, finalState] = SSACFGInterpreter::run(std::move(state), _cfgs, true);
	soltestAssert(outcome != SSACFGInterpreter::Outcome::Limit, "the SSA CFG interpreter hit a limit");

	Outcome result;
	result.reverted =
		outcome == SSACFGInterpreter::Outcome::Terminated &&
		!finalState.trace.empty() &&
		finalState.trace.back().starts_with("REVERT(");
	if (!result.reverted)
		for (auto const& [key, value]: finalState.storage)
			if (value != h256{})
				result.storage[u256(key)] = u256(value);
	return result;
}

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
		Outcome const expected = interpret(**reference, calldataWord);
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
