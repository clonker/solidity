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

#include <test/libyul/ssa/InterpreterTest.h>

#include <test/tools/yulInterpreter/SSACFGInterpreter.h>

#include <libyul/backends/evm/ssa/io/Parser.h>

#include <libyul/backends/evm/EVMDialect.h>

#include <libsolutil/AnsiColorized.h>

#include <sstream>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::test;
using namespace solidity::yul::test::ssa;
using namespace solidity::frontend::test;

namespace
{

std::string_view terminationName(std::optional<Termination> const _termination)
{
	if (!_termination)
		return "limit reached";
	switch (*_termination)
	{
	case Termination::Stop: return "stop";
	case Termination::Return: return "return";
	case Termination::Revert: return "revert";
	case Termination::Invalid: return "invalid";
	case Termination::SelfDestruct: return "selfdestruct";
	}
	util::unreachable();
}

}

std::unique_ptr<TestCase> InterpreterTest::create(Config const& _config)
{
	return std::make_unique<InterpreterTest>(_config.filename, _config.evmVersion);
}

InterpreterTest::InterpreterTest(std::string const& _filename, langutil::EVMVersion const _evmVersion):
	TestCase(_filename),
	m_evmVersion(_evmVersion)
{
	m_source = m_reader.source();
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult InterpreterTest::run(std::ostream& _stream, std::string const& _linePrefix, bool const _formatted)
{
	auto const parsed = yul::ssa::io::parse(m_source, EVMDialect::strictAssemblyForEVMObjects(m_evmVersion));
	if (!parsed)
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) <<
			_linePrefix << "Parse error: " << parsed.error().message << std::endl;
		return TestResult::FatalError;
	}

	EVMState state;
	state.maxTraceSize = 32;
	auto const result = SSACFGInterpreter::run(**parsed, std::move(state), 512, false);

	std::stringstream output;
	output << "Termination: " << terminationName(result.termination) << std::endl;
	result.state.dumpTraceAndState(output, false);
	m_obtainedResult = output.str();
	return checkResult(_stream, _linePrefix, _formatted);
}
