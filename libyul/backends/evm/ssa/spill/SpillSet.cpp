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

#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libyul/backends/evm/ssa/stack/Shuffler.h>

#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackLayout.h>

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/reverse.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::spill;

namespace
{

/// Build the symbolic stack right behind the first `_numInsts` Insts of `_block` by replaying the recorded
/// shuffles and operation effects from the block's `stackIn`
StackData replayInsts(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	SSACFG::BlockId const _block,
	std::size_t const _numInsts
)
{
	auto const& blockLayout = _layout[_block];
	yulAssert(blockLayout, fmt::format("block {} has no layout", _block));

	auto const& instructions = _cfg.block(_block).instructions;
	yulAssert(blockLayout->operationShuffles.size() == instructions.size());
	yulAssert(_numInsts <= instructions.size());
	StackData stack = blockLayout->stackIn;
	for (std::size_t index = 0; index < _numInsts; ++index)
	{
		InstId const id = instructions[index];
		replay(stack, blockLayout->operationShuffles[index]);
		if (!_cfg.isOperation(id))
			continue;

		SSACFG::Inst const& inst = _cfg.inst(id);
		// a call that can continue also consumes its return label, which sits right below the inputs
		std::size_t consumedSlots = inst.inputs.size();
		if (inst.opcode == InstOpcode::Call && _cfg.callPayload(id).canContinue)
			++consumedSlots;
		yulAssert(stack.size() >= consumedSlots, "operation input layout smaller than consumed slot count");
		for (std::size_t i = 0; i < consumedSlots; ++i)
			stack.pop_back();
		_cfg.forEachOutput(id, [&](InstId const output) {
			stack.push_back(StackSlot::makeValue(_cfg, output));
		});
	}
	return stack;
}

/// The position of `_id` in the instructions of its block
std::size_t positionInBlock(SSACFG const& _cfg, InstId const _id)
{
	auto const& instructions = _cfg.block(_cfg.inst(_id).block).instructions;
	auto const it = ranges::find(instructions, _id);
	yulAssert(it != instructions.end(), fmt::format("{} not found in its block's instructions", _id));
	return static_cast<std::size_t>(std::distance(instructions.begin(), it));
}

/// The symbolic stack the Emitter faces right behind the Inst `_site`, where the `mstore` of a value stored there fires
StackData defStackFor(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	InstId const _site
)
{
	return replayInsts(_cfg, _layout, _cfg.inst(_site).block, positionInBlock(_cfg, _site) + 1);
}

/// The stack-in of `_block`, where the `mstore`s of the variables stored on its entry fire
StackData const& stackInOf(SSACFGStackLayout const& _layout, SSACFG::BlockId const _block)
{
	auto const& blockLayout = _layout[_block];
	yulAssert(blockLayout, fmt::format("block {} has no layout", _block));
	return blockLayout->stackIn;
}

/// The depth of the topmost copy of `_slot` on `_stack`
std::size_t depthOf(StackData const& _stack, StackSlot const& _slot)
{
	auto const reversed = _stack | ranges::views::reverse;
	auto const it = ranges::find(reversed, _slot);
	yulAssert(it != ranges::end(reversed), fmt::format("{} is not on the stack where it is stored", _slot));
	return static_cast<std::size_t>(ranges::distance(ranges::begin(reversed), it));
}

/// The Inst behind which the value `_value` is stored: a projection behind its operation, any other value, including a
/// phi, which takes its value out of its shadow slot at its position, right behind itself
InstId storeSiteOf(SSACFG const& _cfg, InstId const _value)
{
	return _cfg.isProjection(_value) ? _cfg.inst(_value).inputs.front() : _value;
}

struct DefSite
{
	SpillStoreSite site;
	/// the slot stored at the site, the variable itself or the shadow slot sharing its key
	StackSlot slot;
	/// the symbolic stack the Emitter faces at the site
	StackData stack;
};

/// The sites at which the spilled variable `_key` is stored (see `SpillStoreTraces`)
std::vector<DefSite> defSitesFor(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	SpillSet const& _spillSet,
	SpillKey const _key
)
{
	bool const sharedWithShadow = _key.isPhiValue() && _spillSet.sharesKeyWithShadow(_key.value());
	if (_key.isValue() && _cfg.isFunctionArg(_key.value()))
		// it has no producer operation and lives on the function entry stack
		return {{_cfg.entry, _key, stackInOf(_layout, _cfg.entry)}};
	if (_key.isValue() && !sharedWithShadow)
	{
		InstId const site = storeSiteOf(_cfg, _key.value());
		return {{site, _key, defStackFor(_cfg, _layout, site)}};
	}

	InstId const phi = _key.isValue() ? _key.value() : _key.shadowPhi();
	StackSlot const shadowSlot = StackSlot::makeShadow(_cfg, phi);
	std::vector<DefSite> sites;
	for (SSACFG::BlockId const blockId: _cfg.liveBlocks())
		if (auto const& blockLayout = _layout[blockId]; blockLayout && blockLayout->writesOnEntry(shadowSlot))
		{
			yulAssert(ranges::contains(blockLayout->stackIn, shadowSlot));
			sites.push_back({blockId, shadowSlot, blockLayout->stackIn});
		}
	return sites;
}

}

void SpillSet::closeUnderReachabilityConstraints(SSACFG const& _cfg, SSACFGStackLayout const& _layout, SpillStoreTraces* _storeTraces)
{
	if (_storeTraces)
		_storeTraces->clear();

	// Several variables can be stored at one site, e.g., the outputs of an operation or the shadow slots written on the
	// edges into a block, and a store must not reload a variable that is stored after it there. Each round stores the
	// variables spilled since the previous one, per site the topmost first: a store may then drop the stored variables
	// above the one it brings up, which are reloadable, and only needs to keep the ones below. The culprits a round
	// spills are stored in the next round, and the traces of a closure that spills culprits are discarded (see
	// `StackLayoutGenerator::generate`).
	std::set<SpillKey> stored;
	while (true)
	{
		std::map<SpillStoreSite, std::pair<StackData, std::vector<StackSlot>>> stores;
		for (SpillKey const key: m_values)
			if (stored.insert(key).second)
				for (auto& [site, slot, defStack]: defSitesFor(_cfg, _layout, *this, key))
				{
					auto& [siteStack, slots] = stores[site];
					siteStack = std::move(defStack);
					slots.push_back(slot);
				}
		if (stores.empty())
			return;

		for (auto& [site, siteStores]: stores)
		{
			auto& [defStack, slots] = siteStores;
			ranges::sort(slots, {}, [&](StackSlot const& _slot) { return depthOf(defStack, _slot); });
			for (std::size_t index = 0; index < slots.size(); ++index)
				ensureDefSiteFeasible(
					slots[index],
					site,
					defStack,
					std::vector<StackSlot>(slots.begin() + static_cast<std::ptrdiff_t>(index) + 1, slots.end()),
					_storeTraces
				);
		}
	}
}

void SpillSet::ensureDefSiteFeasible(
	StackSlot const _slot,
	SpillStoreSite const _site,
	StackData const& _defStack,
	std::vector<StackSlot> const& _storedAfter,
	SpillStoreTraces* _storeTraces)
{
	// predicate = spill set minus the owner and the variables stored after it at the site, i.e., the variables that
	// are in memory by the time the owner is stored; the shuffle accumulates discovered culprits here.
	SpillSet spillSetWithoutOwner = without(_slot);
	for (StackSlot const& sibling: _storedAfter)
		spillSetWithoutOwner.m_values.erase(keyOf(sibling));
	// [... defStack ..., _slot]
	StackData const target = [&]{
		StackData result;
		result.reserve(_defStack.size() + 1);
		result.insert(result.end(), _defStack.begin(), _defStack.end());
		result.push_back(_slot);
		return result;
	}();
	StackData workStack = _defStack;
	stack::ShuffleResult result = stack::shuffle(workStack, target, spillSetWithoutOwner);
	yulAssert(
		result.status == stack::ShuffleResult::Status::Admissible,
		fmt::format("def-site store for {} infeasible even after spilling siblings (status={})", _slot, static_cast<int>(result.status))
	);

	// - if `_slot` is reachable, it can be just DUPed and there shouldn't have been a stack too deep with it
	// - if `_slot` is unreachable, there are > reachable stack depth distinct slots strictly above it and the
	//   shuffler heuristics should not pick anything that is already too deep as culprit
	yulAssert(!spillSetWithoutOwner.isSpilled(_slot), "spill-aware shuffle reported the owner as its own blocker");
	// the variables stored after the owner sit below it on the stack, so they block nothing either
	for (StackSlot const& sibling: _storedAfter)
		yulAssert(
			!spillSetWithoutOwner.isSpilled(sibling),
			fmt::format("spill-aware shuffle reported {}, which is stored after {}, as a blocker", sibling, _slot)
		);

	if (_storeTraces)
	{
		// the `mstore` consuming the variable from the top concludes the def-site trace
		result.trace.push_back(ShuffleOp::store(_slot));
		(*_storeTraces)[_site].emplace_back(_slot, std::move(result.trace));
	}

	// the culprits are stored in the next round of `closeUnderReachabilityConstraints`
	for (SpillKey const culprit: spillSetWithoutOwner.spilledValues())
		if (!isSpilled(culprit))
			add(culprit);
}

SpillSet SpillSet::without(StackSlot const _slot) const
{
	SpillSet result = *this;
	result.m_values.erase(keyOf(_slot));
	return result;
}
