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

#include <test/tools/yulInterpreter/EVMState.h>

#include <iomanip>

using namespace solidity;
using namespace solidity::yul::test;

using solidity::util::h256;

void EVMState::dumpStorage(std::ostream& _out) const
{
	for (auto const& [slot, value]: storage)
		if (value != h256{})
			_out << "  " << slot.hex() << ": " << value.hex() << std::endl;
}

void EVMState::dumpTransientStorage(std::ostream& _out) const
{
	for (auto const& [slot, value]: transientStorage)
		if (value != h256{})
			_out << "  " << slot.hex() << ": " << value.hex() << std::endl;
}

void EVMState::dumpTraceAndState(std::ostream& _out, bool _disableMemoryTrace) const
{
	_out << "Trace:" << std::endl;
	for (auto const& line: trace)
		_out << "  " << line << std::endl;
	if (!_disableMemoryTrace)
	{
		_out << "Memory dump:\n";
		std::map<u256, u256> words;
		for (auto const& [offset, value]: memory)
			words[(offset / 0x20) * 0x20] |= u256(uint32_t(value)) << (256 - 8 - 8 * static_cast<size_t>(offset % 0x20));
		for (auto const& [offset, value]: words)
			if (value != 0)
				_out << "  " << std::uppercase << std::hex << std::setw(4) << offset << ": " << h256(value).hex() << std::endl;
	}
	_out << "Storage dump:" << std::endl;
	dumpStorage(_out);

	_out << "Transient storage dump:" << std::endl;
	dumpTransientStorage(_out);

	if (!calldata.empty())
	{
		_out << "Calldata dump:";

		for (size_t offset = 0; offset < calldata.size(); ++offset)
			if (calldata[offset] != 0)
			{
				if (offset % 32 == 0)
					_out <<
						std::endl <<
						"  " <<
						std::uppercase <<
						std::hex <<
						std::setfill(' ') <<
						std::setw(4) <<
						offset <<
						": ";

				_out <<
					std::hex <<
					std::setw(2) <<
					std::setfill('0') <<
					static_cast<int>(calldata[offset]);
			}

		_out << std::endl;
	}
}
