/**
 * @file Module.cpp
 * @brief Implementation of the SPIR-V -> IR conversion
 * @date 2026-08-24
 */

#include "See/Ir/Module.hpp"

#include "See/Spirv/Module.hpp"
#include <spirv/unified1/spirv.h>

namespace see::ir
{
	namespace
	{
		std::vector<cct::UInt32> RealOperands(const spirv::Instruction& instruction)
		{
			const std::size_t skip = (instruction.m_typeId != 0 ? 1 : 0) + (instruction.m_resultId != 0 ? 1 : 0);
			if (skip >= instruction.m_operands.size())
				return {};

			return std::vector<cct::UInt32>(instruction.m_operands.begin() + static_cast<std::ptrdiff_t>(skip), instruction.m_operands.end());
		}

		bool IsTypeOpcode(SpvOp opcode)
		{
			switch (opcode)
			{
				case SpvOpTypeVoid:
				case SpvOpTypeBool:
				case SpvOpTypeInt:
				case SpvOpTypeFloat:
				case SpvOpTypeVector:
				case SpvOpTypeMatrix:
				case SpvOpTypePointer:
				case SpvOpTypeStruct:
					return true;
				default:
					return false;
			}
		}

		bool IsConstantOpcode(SpvOp opcode)
		{
			switch (opcode)
			{
				case SpvOpConstantTrue:
				case SpvOpConstantFalse:
				case SpvOpConstant:
					return true;
				default:
					return false;
			}
		}

		bool IsIgnorableOpcode(SpvOp opcode)
		{
			switch (opcode)
			{
				case SpvOpCapability:
				case SpvOpExtension:
				case SpvOpExtInstImport:
				case SpvOpMemoryModel:
				case SpvOpExecutionMode:
				case SpvOpExecutionModeId:
				case SpvOpSource:
				case SpvOpSourceExtension:
				case SpvOpSourceContinued:
				case SpvOpName:
				case SpvOpMemberName:
				case SpvOpMemberDecorate:
				case SpvOpModuleProcessed:
				case SpvOpString:
				case SpvOpLine:
				case SpvOpNoLine:
				case SpvOpTypeFunction:
				case SpvOpNop:
				case SpvOpSelectionMerge:
				case SpvOpLoopMerge:
					return true;
				default:
					return false;
			}
		}

		Op ToOp(SpvOp opcode)
		{
			switch (opcode)
			{
				case SpvOpVariable:
					return Op::Variable;
				case SpvOpLoad:
					return Op::Load;
				case SpvOpStore:
					return Op::Store;
				case SpvOpCopyMemory:
					return Op::CopyMemory;
				case SpvOpAccessChain:
					return Op::AccessChain;
				case SpvOpCompositeConstruct:
					return Op::CompositeConstruct;
				case SpvOpCompositeExtract:
					return Op::CompositeExtract;
				case SpvOpFAdd:
				case SpvOpIAdd:
					return Op::Add;
				case SpvOpFSub:
				case SpvOpISub:
					return Op::Sub;
				case SpvOpFMul:
				case SpvOpIMul:
					return Op::Mul;
				case SpvOpFDiv:
				case SpvOpSDiv:
				case SpvOpUDiv:
					return Op::Div;
				case SpvOpDot:
					return Op::Dot;
				case SpvOpMatrixTimesVector:
					return Op::MatrixTimesVector;
				case SpvOpVectorTimesMatrix:
					return Op::VectorTimesMatrix;
				case SpvOpMatrixTimesMatrix:
					return Op::MatrixTimesMatrix;
				case SpvOpFOrdEqual:
				case SpvOpIEqual:
					return Op::CompareEqual;
				case SpvOpFOrdNotEqual:
				case SpvOpINotEqual:
					return Op::CompareNotEqual;
				case SpvOpFOrdLessThan:
				case SpvOpSLessThan:
				case SpvOpULessThan:
					return Op::CompareLess;
				case SpvOpFOrdLessThanEqual:
				case SpvOpSLessThanEqual:
				case SpvOpULessThanEqual:
					return Op::CompareLessEqual;
				case SpvOpFOrdGreaterThan:
				case SpvOpSGreaterThan:
				case SpvOpUGreaterThan:
					return Op::CompareGreater;
				case SpvOpFOrdGreaterThanEqual:
				case SpvOpSGreaterThanEqual:
				case SpvOpUGreaterThanEqual:
					return Op::CompareGreaterEqual;
				case SpvOpSelect:
					return Op::Select;
				default:
					return Op::Unsupported;
			}
		}

		Terminator ToTerminator(spirv::Terminator terminator)
		{
			switch (terminator)
			{
				case spirv::Terminator::Branch:
					return Terminator::Branch;
				case spirv::Terminator::BranchConditional:
					return Terminator::BranchConditional;
				case spirv::Terminator::Return:
					return Terminator::Return;
				case spirv::Terminator::ReturnValue:
					return Terminator::ReturnValue;
				default:
					return Terminator::None;
			}
		}

		void ConvertType(Module& module, const spirv::Instruction& instruction, SpvOp opcode)
		{
			const cct::UInt32 id = instruction.m_resultId;
			const std::vector<cct::UInt32> operands = RealOperands(instruction);

			switch (opcode)
			{
				case SpvOpTypeVoid:
					module.m_types[id] = Type{.m_kind = TypeKind::Void};
					break;
				case SpvOpTypeBool:
					module.m_types[id] = Type{.m_kind = TypeKind::Scalar, .m_scalar = ScalarKind::Bool};
					break;
				case SpvOpTypeInt:
					if (!operands.empty() && operands[0] == 32)
						module.m_types[id] = Type{.m_kind = TypeKind::Scalar, .m_scalar = ScalarKind::Int32};
					break;
				case SpvOpTypeFloat:
					if (!operands.empty() && operands[0] == 32)
						module.m_types[id] = Type{.m_kind = TypeKind::Scalar, .m_scalar = ScalarKind::Float32};
					break;
				case SpvOpTypeVector:
				{
					if (operands.size() < 2)
						break;
					auto componentIt = module.m_types.find(operands[0]);
					if (componentIt == module.m_types.end() || componentIt->second.m_kind != TypeKind::Scalar)
						break;
					module.m_types[id] = Type{.m_kind = TypeKind::Vector, .m_scalar = componentIt->second.m_scalar, .m_componentCount = operands[1]};
					break;
				}
				case SpvOpTypeMatrix:
				{
					if (operands.size() < 2)
						break;
					auto columnIt = module.m_types.find(operands[0]);
					if (columnIt == module.m_types.end() || columnIt->second.m_kind != TypeKind::Vector)
						break;
					module.m_types[id] = Type{.m_kind = TypeKind::Matrix, .m_scalar = columnIt->second.m_scalar, .m_componentCount = operands[1], .m_pointee = operands[0]};
					break;
				}
				case SpvOpTypePointer:
					if (operands.size() >= 2)
						module.m_types[id] = Type{.m_kind = TypeKind::Pointer, .m_pointee = operands[1]};
					break;
				case SpvOpTypeStruct:
					module.m_types[id] = Type{.m_kind = TypeKind::Struct, .m_memberTypes = operands};
					break;
				default:
					break;
			}
		}

		void ConvertConstant(Module& module, const spirv::Instruction& instruction, SpvOp opcode)
		{
			const std::vector<cct::UInt32> operands = RealOperands(instruction);
			switch (opcode)
			{
				case SpvOpConstantTrue:
					module.m_constants[instruction.m_resultId] = Constant{.m_type = instruction.m_typeId, .m_bits = 1};
					break;
				case SpvOpConstantFalse:
					module.m_constants[instruction.m_resultId] = Constant{.m_type = instruction.m_typeId, .m_bits = 0};
					break;
				case SpvOpConstant:
					if (!operands.empty())
						module.m_constants[instruction.m_resultId] = Constant{.m_type = instruction.m_typeId, .m_bits = operands[0]};
					break;
				default:
					break;
			}
		}

		Instruction ConvertInstruction(const spirv::Instruction& instruction)
		{
			Instruction result;
			result.m_id = instruction.m_resultId;
			result.m_type = instruction.m_typeId;
			result.m_spirvOpcode = instruction.m_opcode;
			result.m_op = ToOp(static_cast<SpvOp>(instruction.m_opcode));
			result.m_args = RealOperands(instruction);
			return result;
		}
	} // namespace

	std::optional<Module> Convert(const see::spirv::Module& spirvModule)
	{
		Module module;

		for (const auto& [id, spirvDecorations] : spirvModule.m_decorations)
		{
			std::vector<Decoration>& decorations = module.m_decorations[id];
			decorations.reserve(spirvDecorations.size());
			for (const spirv::Decoration& spirvDecoration : spirvDecorations)
				decorations.push_back(Decoration{.m_kind = spirvDecoration.m_kind, .m_literals = spirvDecoration.m_literals});
		}

		for (const spirv::Instruction& instruction : spirvModule.m_globalInstructions)
		{
			const SpvOp opcode = static_cast<SpvOp>(instruction.m_opcode);
			if (IsTypeOpcode(opcode))
				ConvertType(module, instruction, opcode);
			else if (IsConstantOpcode(opcode))
				ConvertConstant(module, instruction, opcode);
			else if (!IsIgnorableOpcode(opcode))
				module.m_globalInstructions.push_back(ConvertInstruction(instruction));
		}

		module.m_functions.reserve(spirvModule.m_functions.size());
		for (const spirv::Function& spirvFunction : spirvModule.m_functions)
		{
			Function function;
			function.m_id = spirvFunction.m_id;
			function.m_blocks.reserve(spirvFunction.m_blocks.size());

			for (const spirv::BasicBlock& spirvBlock : spirvFunction.m_blocks)
			{
				BasicBlock block;
				block.m_label = spirvBlock.m_label;
				block.m_terminator = ToTerminator(spirvBlock.m_terminator);
				block.m_branchTargets = spirvBlock.m_branchTargets;
				block.m_terminatorOperand = spirvBlock.m_terminatorOperand;

				block.m_instructions.reserve(spirvBlock.m_instructions.size());
				for (const spirv::Instruction& instruction : spirvBlock.m_instructions)
					if (!IsIgnorableOpcode(static_cast<SpvOp>(instruction.m_opcode)))
						block.m_instructions.push_back(ConvertInstruction(instruction));

				function.m_blocks.push_back(std::move(block));
			}

			module.m_functions.push_back(std::move(function));
		}

		return module;
	}
} // namespace see::ir
