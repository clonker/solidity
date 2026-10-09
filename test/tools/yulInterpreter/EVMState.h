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
 * The state of the EVM that interpreters evaluate instructions on, and the ways in which their execution ends.
 */

#pragma once

#include <libyul/Exceptions.h>

#include <libsolutil/CommonData.h>
#include <libsolutil/Exceptions.h>
#include <libsolutil/FixedHash.h>
#include <libsolutil/Numeric.h>

#include <array>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace solidity::yul::test
{

class InterpreterTerminatedGeneric: public util::Exception
{
};

class ExplicitlyTerminated: public InterpreterTerminatedGeneric
{
};

class ExplicitlyTerminatedWithReturn: public ExplicitlyTerminated
{
};

class StepLimitReached: public InterpreterTerminatedGeneric
{
};

class TraceLimitReached: public InterpreterTerminatedGeneric
{
};

/// The machine state and the environment of a single contract, and the log of the effects of the instructions executed
/// on it (see `EVMInstructionInterpreter`)
struct EVMState
{
	bytes calldata;
	bytes returndata;
	std::map<u256, uint8_t> memory;
	/// This is different than memory.size() because we ignore gas.
	u256 msize;
	std::map<util::h256, util::h256> storage;
	std::map<util::h256, util::h256> transientStorage;
	util::h160 address = util::h160("0x0000000000000000000000000000000011111111");
	u256 balance = 0x22222222;
	u256 selfbalance = 0x22223333;
	util::h160 origin = util::h160("0x0000000000000000000000000000000033333333");
	util::h160 caller = util::h160("0x0000000000000000000000000000000044444444");
	u256 callvalue = 0x55555555;
	/// Deployed code
	bytes code = util::asBytes("codecodecodecodecode");
	u256 gasprice = 0x66666666;
	util::h160 coinbase = util::h160("0x0000000000000000000000000000000077777777");
	u256 timestamp = 0x88888888;
	u256 blockNumber = 1024;
	u256 difficulty = 0x9999999;
	u256 prevrandao = (u256(1) << 64) + 1;
	u256 gaslimit = 4000000;
	u256 chainid = 0x01;
	/// The minimum value of basefee: 7 wei.
	u256 basefee = 0x07;
	/// The minimum value of blobbasefee: 1 wei.
	u256 blobbasefee = 0x01;
	u256 slotnum = 0xaaaaaaaa;
	/// Log of changes / effects. Sholud be structured data in the future.
	std::vector<std::string> trace;
	/// This is actually an input parameter that more or less limits the runtime.
	size_t maxTraceSize = 0;

	// Blob commitment hash version
	util::FixedHash<1> const blobHashVersion = util::FixedHash<1>(1);
	// Blob commitments
	std::array<u256, 2> const blobCommitments = {0x01, 0x02};

	/// Prints execution trace and non-zero storage to @param _out.
	/// Flag @param _disableMemoryTrace, if set, does not produce a memory dump. This
	/// avoids false positives reports by the fuzzer when certain optimizer steps are
	/// activated e.g., Redundant store eliminator, Equal store eliminator.
	void dumpTraceAndState(std::ostream& _out, bool _disableMemoryTrace) const;
	/// Prints non-zero storage to @param _out.
	void dumpStorage(std::ostream& _out) const;
	/// Prints non-zero transient storage to @param _out.
	void dumpTransientStorage(std::ostream& _out) const;

	bytes readMemory(u256 const& _offset, u256 const& _size)
	{
		yulAssert(_size <= 0xffff, "Too large read.");
		bytes data(size_t(_size), uint8_t(0));
		for (size_t i = 0; i < data.size(); ++i)
			data[i] = memory[_offset + i];
		return data;
	}
};

}
