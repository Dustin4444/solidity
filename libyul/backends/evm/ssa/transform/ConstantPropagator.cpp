#include <libyul/backends/evm/ssa/transform/ConstantPropagator.h>

#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <libsolutil/Numeric.h>

#include "libyul/backends/evm/ssa/SSACFG.h"

#include <queue>


using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{
enum struct Knowledge {UNKNOWN, CONSTANT, NOT_CONSTANT};
struct Value
{
	Knowledge knowledge;
	u256 value{};
};

u256 eval(evmasm::Instruction const _instruction, std::vector<u256> const & _values)
{
	switch (_instruction)
	{
		case evmasm::Instruction::ADD:
			yulAssert(_values.size() == 2);
			return _values[0] + _values[1];
		case evmasm::Instruction::MUL:
			yulAssert(_values.size() == 2);
			return _values[0] * _values[1];
		case evmasm::Instruction::SUB:
			yulAssert(_values.size() == 2);
			return _values[0] - _values[1];
		case evmasm::Instruction::DIV:
			yulAssert(_values.size() == 2);
			return _values[1] == 0 ? 0 : _values[0] / _values[1];
		case evmasm::Instruction::SDIV:
			yulAssert(_values.size() == 2);
			return _values[1] == 0 ? 0 : s2u(u2s(_values[0]) / u2s(_values[1]));
		case evmasm::Instruction::MOD:
			yulAssert(_values.size() == 2);
			return _values[1] == 0 ? 0 : _values[0] % _values[1];
		case evmasm::Instruction::SMOD:
			yulAssert(_values.size() == 2);
			return _values[1] == 0 ? 0 : s2u(u2s(_values[0]) % u2s(_values[1]));
		case evmasm::Instruction::ADDMOD:
			yulAssert(_values.size() == 3);
			if (_values[2] == 0)
				return 0;
			return u256{(u512{_values[0]} + u512{_values[1]}) / u512{_values[2]}};
		case evmasm::Instruction::MULMOD:
			yulAssert(_values.size() == 3);
			return u256{(u512{_values[0]} * u512{_values[1]}) / u512{_values[2]}};
		case evmasm::Instruction::EXP:
			yulAssert(_values.size() == 2);
			return u256{boost::multiprecision::powm(bigint(_values[0]), bigint(_values[1]), bigint(1) << 256)};
		case evmasm::Instruction::SIGNEXTEND:
		{
			yulAssert(_values.size() == 2);
			if (_values[0] >= 31)
				return _values[1];
			auto const testBit = static_cast<unsigned>(_values[0]) * 8 + 7;
			u256 ret = _values[1];
			u256 const mask = ((u256(1) << testBit) - 1);
			if (boost::multiprecision::bit_test(ret, testBit))
				ret |= ~mask;
			else
				ret &= mask;
			return ret;
		}
		case evmasm::Instruction::LT:
			yulAssert(_values.size() == 2);
			return _values[0] < _values[1] ? 1 : 0;
		case evmasm::Instruction::GT:
			yulAssert(_values.size() == 2);
			return _values[0] > _values[1] ? 1 : 0;
		case evmasm::Instruction::SLT:
			yulAssert(_values.size() == 2);
			return u2s(_values[0]) < u2s(_values[1]) ? 1 : 0;
		case evmasm::Instruction::SGT:
			yulAssert(_values.size() == 2);
			return u2s(_values[0]) > u2s(_values[1]) ? 1 : 0;
		case evmasm::Instruction::EQ:
			yulAssert(_values.size() == 2);
			return _values[0] == _values[1] ? 1 : 0;
		case evmasm::Instruction::ISZERO:
			yulAssert(_values.size() == 1);
			return _values[0] == 0 ? 1 : 0;
		case evmasm::Instruction::AND:
			yulAssert(_values.size() == 2);
			return _values[0] & _values[1];
		case evmasm::Instruction::OR:
			yulAssert(_values.size() == 2);
			return _values[0] | _values[1];
		case evmasm::Instruction::XOR:
			yulAssert(_values.size() == 2);
			return _values[0] ^ _values[1];
		case evmasm::Instruction::NOT:
			yulAssert(_values.size() == 1);
			return ~_values[0];
		case evmasm::Instruction::BYTE:
			yulAssert(_values.size() == 2);
			return _values[0] >= 32 ? 0 : (_values[1] >> static_cast<unsigned>(8 * (31 - _values[0]))) & 0xff;
		case evmasm::Instruction::SHL:
			yulAssert(_values.size() == 2);
			return _values[0] >= 256 ? 0 : u256((bigint(_values[1]) << static_cast<unsigned>(_values[0])) & u256(-1));
		case evmasm::Instruction::SHR:
			yulAssert(_values.size() == 2);
			return _values[0] >= 256 ? 0 : _values[1] >> static_cast<unsigned>(_values[0]);
		case evmasm::Instruction::SAR:
		{
			yulAssert(_values.size() == 2);
			constexpr u256 hibit = u256(1) << 255;
			if (_values[0] >= 256)
				return _values[1] & hibit ? u256(-1) : 0;
			auto const amount = static_cast<unsigned>(_values[0]);
			u256 v = _values[1] >> amount;
			if (_values[1] & hibit)
				v |= u256(-1) << (256 - amount);
			return v;
		}
		case evmasm::Instruction::CLZ:
			yulAssert(_values.size() == 1);
			return _values[0] == 0 ? 256 : 255 - msb(_values[0]);
		default:
			yulAssert(false, "Unexpected instruction to be const-evaluated");
	}
}
}

void transform::propagateConstants(SSACFG& _cfg)
{
	std::map<InstId, Value> knowledgeBase;
	std::map<InstId, std::vector<InstId>> phiToUpsilons;
	for (auto instId: _cfg.instructionIds())
	{
		knowledgeBase.insert({instId, {Knowledge::UNKNOWN, {}}});
		if (_cfg.isUpsilon(instId))
			phiToUpsilons[_cfg.upsilonPhi(instId)].push_back(instId);
	}
	bool newInformation = true;

	while (newInformation)
	{
		newInformation = false;
		auto makeNotConstant = [&](Value& value) {
			newInformation |= value.knowledge == Knowledge::UNKNOWN;
			value.knowledge = Knowledge::NOT_CONSTANT;
		};
		for (auto const bid: _cfg.liveBlocks())
		{
			auto const& block = _cfg.block(bid);
			for (auto instId: block.instructions)
			{
				auto const & instruction = _cfg.inst(instId);
				switch (instruction.opcode)
				{
					case InstOpcode::Const:
					{
						auto& currentKnowledge = knowledgeBase.at(instId);
						newInformation |= currentKnowledge.knowledge == Knowledge::UNKNOWN;
						currentKnowledge.knowledge = Knowledge::CONSTANT;
						currentKnowledge.value = _cfg.literalPayload(instId);
						break;
					}
					case InstOpcode::Phi:
					{
						auto const& upsilons = phiToUpsilons.at(instId);
						auto const currentKnowledge = knowledgeBase.at(instId);
						auto const newKnowledge = std::accumulate(upsilons.begin(), upsilons.end(), currentKnowledge, [&](Value const& v, InstId otherId)
						{
							auto const& other = knowledgeBase.at(otherId);
							if (v.knowledge == Knowledge::UNKNOWN || other.knowledge == Knowledge::UNKNOWN)
								return Value{Knowledge::UNKNOWN, {}};
							if (v.knowledge == Knowledge::NOT_CONSTANT || other.knowledge == Knowledge::NOT_CONSTANT)
								return Value{Knowledge::NOT_CONSTANT, {}};
							yulAssert(v.knowledge == Knowledge::CONSTANT && other.knowledge == Knowledge::CONSTANT);
							if (v.value == other.value)
								return v;
							return Value{Knowledge::NOT_CONSTANT, {}};
						});
						if (currentKnowledge.knowledge != newKnowledge.knowledge)
						{
							knowledgeBase.at(instId) = newKnowledge;
							newInformation = true;
						}
						break;
					}
					case InstOpcode::BuiltinCall:
					{
						auto handle = _cfg.builtinPayload(instId).builtin;
						auto maybeEVMInstruction = _cfg.evmDialect.builtin(handle).instruction;
						auto& currentKnowledge = knowledgeBase.at(instId);
						if (currentKnowledge.knowledge != Knowledge::UNKNOWN)
							break;
						if (not maybeEVMInstruction)
						{
							makeNotConstant(currentKnowledge);
							break;
						}
						auto const& evmInstruction = maybeEVMInstruction.value();
						if (evmInstruction < evmasm::Instruction::ADD || evmInstruction > evmasm::Instruction::CLZ)
						{
							makeNotConstant(currentKnowledge);
							break;
						}
						auto const newKnowledge = [&]() -> Knowledge
						{
							for (auto const arg: instruction.inputs)
							{
								auto const argKnowledge = knowledgeBase.at(arg).knowledge;
								if (argKnowledge == Knowledge::NOT_CONSTANT)
									return Knowledge::NOT_CONSTANT;
								if (argKnowledge == Knowledge::UNKNOWN)
									return Knowledge::UNKNOWN;
							}
							return Knowledge::CONSTANT;
						}();
						if (newKnowledge == Knowledge::NOT_CONSTANT)
							makeNotConstant(currentKnowledge);
						else if (newKnowledge == Knowledge::CONSTANT)
						{
							auto const newConstantValue = eval(evmInstruction, ranges::views::transform(instruction.inputs, [&](InstId const arg)
							{
								auto const& argValue = knowledgeBase.at(arg);
								yulAssert(argValue.knowledge == Knowledge::CONSTANT);
								return argValue.value;
							}) | ranges::to_vector);
							currentKnowledge = Value{newKnowledge, newConstantValue};
							newInformation = true;
						}
						break;
					}
					case InstOpcode::Upsilon:
					case InstOpcode::Identity:
					{
						// Both Upsilon and identity instructions just propagate information from their argument
						yulAssert(instruction.inputs.size() == 1);
						auto& currentKnowledge = knowledgeBase.at(instId);
						if (currentKnowledge.knowledge == Knowledge::UNKNOWN)
						{
							currentKnowledge = knowledgeBase.at(instruction.inputs[0]);
							newInformation |= currentKnowledge.knowledge != Knowledge::UNKNOWN;
						}
						break;
					}
					case InstOpcode::Unreachable:
					case InstOpcode::FunctionArg:
					case InstOpcode::Nop:
					case InstOpcode::MemoryGuard:
					case InstOpcode::Tombstone:
					case InstOpcode::Call:
					case InstOpcode::Projection:
					{
						auto& currentKnowledge = knowledgeBase.at(instId);
						makeNotConstant(currentKnowledge);
						break;
					}
				}
			}
		}
	}
	for (auto const& [instId, value]: knowledgeBase)
	{
		if (value.knowledge != Knowledge::CONSTANT)
			continue;
		if (_cfg.isLiteral(instId) || _cfg.isUpsilon(instId))
			continue;
		auto const constInstId = _cfg.newLiteral({}, value.value);
		_cfg.replaceWithIdentity(instId, constInstId);
	}
}

void transform::pruneConstantConditionBranches(SSACFG& _cfg)
{
	for (auto const bid : _cfg.liveBlocks())
	{
		auto& block =_cfg.block(bid);
		if (auto const* conditionalJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&block.exit))
		{
			auto const iid = conditionalJump->condition;
			auto const& inst = _cfg.inst(iid);
			if (!inst.isLiteral())
				continue;
			bool const isZero = _cfg.literalPayload(iid).is_zero();
			auto& prunedBlock = _cfg.block(isZero ? conditionalJump->nonZero : conditionalJump->zero);
			auto it = std::find(prunedBlock.entries.begin(), prunedBlock.entries.end(), bid);
			yulAssert(it != prunedBlock.entries.end());
			prunedBlock.entries.erase(it);
			block.exit = SSACFG::BasicBlock::Jump{.target = isZero ? conditionalJump->zero : conditionalJump->nonZero};

		}
	}
}
