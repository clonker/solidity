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

#include <test/TestCase.h>

#include <liblangutil/EVMVersion.h>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <string>
#include <vector>

namespace solidity::yul::test::ssa
{

/// Parses a textual SSA CFG, compiles it with the SSA CFG backend (the SSA CFG passes, stack layout generation and
/// code transform) and executes the bytecode once per calldata word of the `calldata` setting. The resulting storage
/// is compared with the storage that a reference interpreter of the SSA CFG produces, which implements Pizlo form
/// directly: an upsilon writes the shadow of its phi when it executes and a phi reads its shadow when it executes.
/// A disagreement is a fatal error. Otherwise the expectation is the optimized SSA CFG followed by the storage per run.
class ExecutionTest: public frontend::test::TestCase
{
public:
	static std::unique_ptr<TestCase> create(Config const& _config);
	ExecutionTest(std::string const& _filename, langutil::EVMVersion _evmVersion, std::vector<boost::filesystem::path> _vmPaths);
	TestResult run(std::ostream& _stream, std::string const& _linePrefix = "", bool _formatted = false) override;

private:
	langutil::EVMVersion m_evmVersion;
	std::vector<boost::filesystem::path> m_vmPaths;
	std::vector<std::string> m_calldata;
};

}
