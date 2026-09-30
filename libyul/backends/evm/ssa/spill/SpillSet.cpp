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
#include <libyul/backends/evm/ssa/StackUtils.h>

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/algorithm/find.hpp>

#include <deque>

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

/// The symbolic stack the Emitter faces behind `_site`, where the `mstore` of a value stored there fires. Two cases:
/// - a function argument: it has no producer operation and lives on the function entry stack, where CodeTransform emits `mstore` while the args are still laid out;
/// - any other Inst: right behind it.
StackData defStackFor(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	InstId const _site
)
{
	if (_cfg.isFunctionArg(_site))
	{
		auto const& entryLayout = _layout[_cfg.entry];
		yulAssert(entryLayout, "entry block has no layout for function-arg def-site");
		return entryLayout->stackIn;
	}
	return replayInsts(_cfg, _layout, _cfg.inst(_site).block, positionInBlock(_cfg, _site) + 1);
}

/// The Inst behind which the value `_value` is stored: a projection behind its operation, a phi, which takes its value
/// out of its shadow slot at its position, behind the last phi of the run of phis it belongs to
InstId storeSiteOf(SSACFG const& _cfg, InstId const _value)
{
	if (_cfg.isPhi(_value))
	{
		SSACFG::BlockId const blockId = _cfg.inst(_value).block;
		yulAssert(blockId.hasValue(), fmt::format("phi {} has no defining block", _value));
		SSACFG::BasicBlock const& block = _cfg.block(blockId);
		return block.instructions[phiRunEnd(_cfg, block, positionInBlock(_cfg, _value)) - 1];
	}
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
	std::map<InstId, std::vector<InstId>> const& _upsilonsByPhi,
	SpillKey const _key
)
{
	bool const sharedWithShadow = _key.isPhiValue() && _spillSet.sharesKeyWithShadow(_key.value());
	if (_key.isValue() && !sharedWithShadow)
	{
		InstId const site = storeSiteOf(_cfg, _key.value());
		return {{site, _key, defStackFor(_cfg, _layout, site)}};
	}

	InstId const phi = _key.isValue() ? _key.value() : _key.shadowPhi();
	StackSlot const shadowSlot = StackSlot::makeShadow(_cfg, phi);
	std::vector<DefSite> sites;
	for (SSACFG::BlockId const blockId: _cfg.liveBlocks())
		if (auto const& blockLayout = _layout[blockId]; blockLayout && ranges::contains(blockLayout->stackIn, shadowSlot))
			sites.push_back({blockId, shadowSlot, blockLayout->stackIn});
	if (auto const it = _upsilonsByPhi.find(phi); it != _upsilonsByPhi.end())
		for (InstId const upsilon: it->second)
		{
			SSACFG::BlockId const blockId = _cfg.inst(upsilon).block;
			std::size_t const position = positionInBlock(_cfg, upsilon);
			if (writesShadowSlot(_layout[blockId]->operationShuffles[position]))
				sites.push_back({upsilon, shadowSlot, replayInsts(_cfg, _layout, blockId, position + 1)});
		}
	return sites;
}

}

void SpillSet::closeUnderReachabilityConstraints(SSACFG const& _cfg, SSACFGStackLayout const& _layout, SpillStoreTraces* _storeTraces)
{
	if (_storeTraces)
		_storeTraces->clear();

	// the upsilons of the laid out blocks
	std::map<InstId, std::vector<InstId>> upsilonsByPhi;
	for (SSACFG::BlockId const blockId: _cfg.liveBlocks())
		if (_layout[blockId])
			_cfg.forEachUpsilon(_cfg.block(blockId), [&](InstId const _upsilon, SSACFG::Inst const&) {
				upsilonsByPhi[_cfg.upsilonPhi(_upsilon)].push_back(_upsilon);
			});

	// work queue over variables that are marked for spillage
	std::deque<SpillKey> queue;
	for (SpillKey const key: spilledValues())
		queue.push_back(key);

	while (!queue.empty())
	{
		SpillKey const key = queue.front();
		queue.pop_front();

		for (auto const& [site, slot, defStack]: defSitesFor(_cfg, _layout, *this, upsilonsByPhi, key))
			ensureDefSiteFeasible(slot, site, defStack, queue, _storeTraces);
	}
}

void SpillSet::ensureDefSiteFeasible(
	StackSlot const _slot,
	SpillStoreSite const _site,
	StackData const& _defStack,
	std::deque<SpillKey>& _workQueue,
	SpillStoreTraces* _storeTraces)
{
	// predicate = spill set minus the owner; the shuffle accumulates discovered culprits here.
	SpillSet spillSetWithoutOwner = without(_slot);
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

	if (_storeTraces)
	{
		// the `mstore` consuming the variable from the top concludes the def-site trace
		result.trace.push_back(ShuffleOp::store(_slot));
		(*_storeTraces)[_site][_slot] = std::move(result.trace);
	}

	for (SpillKey const culprit: spillSetWithoutOwner.spilledValues())
	{
		if (isSpilled(culprit))
			continue;
		add(culprit);
		_workQueue.push_back(culprit);
	}
}

SpillSet SpillSet::without(StackSlot const _slot) const
{
	SpillSet result = *this;
	result.m_values.erase(keyOf(_slot));
	return result;
}
