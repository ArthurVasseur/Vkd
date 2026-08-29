/**
 * @file Module.cpp
 * @brief Implementation of the SPIR-V -> IR conversion
 * @date 2026-08-24
 */

#include "See/Ir/Module.hpp"

#include <string>
#include <unordered_set>

#include "See/Defines.hpp"
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
				case SpvOpTypeArray:
				case SpvOpTypeRuntimeArray:
				case SpvOpTypeImage:
				case SpvOpTypeSampledImage:
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
				case SpvOpConstantComposite:
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
				case SpvOpVectorExtractDynamic:
					return Op::VectorExtractDynamic;
				case SpvOpVectorInsertDynamic:
					return Op::VectorInsertDynamic;
				case SpvOpVectorShuffle:
					return Op::VectorShuffle;
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
					return Op::Div;
				case SpvOpUDiv:
					return Op::UnsignedDiv;
				case SpvOpBitwiseAnd:
					return Op::BitwiseAnd;
				case SpvOpBitwiseOr:
					return Op::BitwiseOr;
				case SpvOpBitwiseXor:
					return Op::BitwiseXor;
				case SpvOpNot:
					return Op::BitwiseNot;
				case SpvOpShiftLeftLogical:
					return Op::ShiftLeftLogical;
				case SpvOpShiftRightLogical:
					return Op::ShiftRightLogical;
				case SpvOpShiftRightArithmetic:
					return Op::ShiftRightArithmetic;
				case SpvOpBitCount:
					return Op::BitCount;
				case SpvOpBitReverse:
					return Op::BitReverse;
				case SpvOpBitFieldInsert:
					return Op::BitFieldInsert;
				case SpvOpBitFieldSExtract:
					return Op::BitFieldSignedExtract;
				case SpvOpBitFieldUExtract:
					return Op::BitFieldUnsignedExtract;
				case SpvOpBitcast:
					return Op::Bitcast;
				case SpvOpFRem:
				case SpvOpSRem:
					return Op::Rem;
				case SpvOpFMod:
				case SpvOpSMod:
					return Op::Mod;
				case SpvOpUMod:
					return Op::UnsignedMod;
				case SpvOpFNegate:
				case SpvOpSNegate:
					return Op::Negate;
				case SpvOpConvertSToF:
					return Op::ConvertSignedToFloat;
				case SpvOpConvertUToF:
					return Op::ConvertUnsignedToFloat;
				case SpvOpConvertFToS:
					return Op::ConvertFloatToSigned;
				case SpvOpConvertFToU:
					return Op::ConvertFloatToUnsigned;
				case SpvOpDot:
					return Op::Dot;
				case SpvOpMatrixTimesVector:
					return Op::MatrixTimesVector;
				case SpvOpVectorTimesMatrix:
					return Op::VectorTimesMatrix;
				case SpvOpMatrixTimesMatrix:
					return Op::MatrixTimesMatrix;
				case SpvOpVectorTimesScalar:
				case SpvOpMatrixTimesScalar:
					return Op::VectorTimesScalar;
				case SpvOpFOrdEqual:
				case SpvOpFUnordEqual:
				case SpvOpIEqual:
					return Op::CompareEqual;
				case SpvOpFOrdNotEqual:
				case SpvOpFUnordNotEqual:
				case SpvOpINotEqual:
					return Op::CompareNotEqual;
				case SpvOpFOrdLessThan:
				case SpvOpFUnordLessThan:
				case SpvOpSLessThan:
					return Op::CompareLess;
				case SpvOpULessThan:
					return Op::CompareUnsignedLess;
				case SpvOpFOrdLessThanEqual:
				case SpvOpFUnordLessThanEqual:
				case SpvOpSLessThanEqual:
					return Op::CompareLessEqual;
				case SpvOpULessThanEqual:
					return Op::CompareUnsignedLessEqual;
				case SpvOpFOrdGreaterThan:
				case SpvOpFUnordGreaterThan:
				case SpvOpSGreaterThan:
					return Op::CompareGreater;
				case SpvOpUGreaterThan:
					return Op::CompareUnsignedGreater;
				case SpvOpFOrdGreaterThanEqual:
				case SpvOpFUnordGreaterThanEqual:
				case SpvOpSGreaterThanEqual:
					return Op::CompareGreaterEqual;
				case SpvOpUGreaterThanEqual:
					return Op::CompareUnsignedGreaterEqual;
				case SpvOpLogicalEqual:
					return Op::LogicalEqual;
				case SpvOpLogicalNotEqual:
					return Op::LogicalNotEqual;
				case SpvOpLogicalAnd:
					return Op::LogicalAnd;
				case SpvOpLogicalOr:
					return Op::LogicalOr;
				case SpvOpLogicalNot:
					return Op::LogicalNot;
				case SpvOpAll:
					return Op::All;
				case SpvOpAny:
					return Op::Any;
				case SpvOpSelect:
					return Op::Select;
				case SpvOpImageSampleImplicitLod:
				case SpvOpImageSampleExplicitLod:
					return Op::ImageSample;
				case SpvOpFunctionCall:
					return Op::FunctionCall;
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
					module.m_types[id] = Type{.m_kind = TypeKind::Vector, .m_scalar = componentIt->second.m_scalar, .m_componentCount = operands[1], .m_pointee = operands[0]};
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
				case SpvOpTypeArray:
				{
					if (operands.size() < 2)
						break;
					if (module.m_types.find(operands[0]) == module.m_types.end())
						break;
					auto lengthIt = module.m_constants.find(operands[1]);
					if (lengthIt == module.m_constants.end())
						break;
					module.m_types[id] = Type{.m_kind = TypeKind::Array, .m_pointee = operands[0], .m_arrayLength = lengthIt->second.m_bits};
					break;
				}
				case SpvOpTypeRuntimeArray:
					if (!operands.empty() && module.m_types.find(operands[0]) != module.m_types.end())
						module.m_types[id] = Type{.m_kind = TypeKind::Array, .m_pointee = operands[0], .m_arrayLength = 0};
					break;
				case SpvOpTypeImage:
					module.m_types[id] = Type{.m_kind = TypeKind::Image};
					break;
				case SpvOpTypeSampledImage:
					if (!operands.empty())
						module.m_types[id] = Type{.m_kind = TypeKind::SampledImage, .m_pointee = operands[0]};
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
				case SpvOpConstantComposite:
				{
					std::vector<cct::UInt32> words;
					bool resolved = true;
					for (cct::UInt32 constituent : operands)
					{
						auto constituentIt = module.m_constants.find(constituent);
						if (constituentIt == module.m_constants.end())
						{
							resolved = false;
							break;
						}

						if (constituentIt->second.m_words.empty())
							words.push_back(constituentIt->second.m_bits);
						else
							words.insert(words.end(), constituentIt->second.m_words.begin(), constituentIt->second.m_words.end());
					}

					if (resolved)
						module.m_constants[instruction.m_resultId] = Constant{.m_type = instruction.m_typeId, .m_words = std::move(words)};
					break;
				}
				default:
					break;
			}
		}

		std::string DecodeLiteralString(const std::vector<cct::UInt32>& words)
		{
			std::string result;
			for (cct::UInt32 word : words)
				for (cct::UInt32 byteIndex = 0; byteIndex < 4; ++byteIndex)
				{
					const char c = static_cast<char>((word >> (byteIndex * 8)) & 0xFF);
					if (c == '\0')
						return result;
					result.push_back(c);
				}
			return result;
		}

		Instruction ConvertInstruction(const spirv::Instruction& instruction, const std::unordered_set<cct::UInt32>& glslStd450Imports)
		{
			Instruction result;
			result.m_id = instruction.m_resultId;
			result.m_type = instruction.m_typeId;
			result.m_spirvOpcode = instruction.m_opcode;
			result.m_op = ToOp(static_cast<SpvOp>(instruction.m_opcode));
			result.m_args = RealOperands(instruction);

			if (static_cast<SpvOp>(instruction.m_opcode) == SpvOpExtInst && !result.m_args.empty())
			{
				if (glslStd450Imports.contains(result.m_args[0]))
				{
					result.m_op = Op::GlslStd450;
					result.m_args.erase(result.m_args.begin());
				}
			}

			return result;
		}

		bool ProducesSsaValue(Op op, cct::UInt32 id)
		{
			if (id == 0)
				return false;

			return op != Op::Variable && op != Op::AccessChain && op != Op::Store && op != Op::CopyMemory;
		}

		void AssignValueRefs(Module& module)
		{
			cct::UInt32 maxId = 0;
			const auto note = [&maxId](cct::UInt32 id)
			{ maxId = std::max(maxId, id); };

			for (const auto& [id, type] : module.m_types)
				note(id);
			for (const auto& [id, constant] : module.m_constants)
				note(id);
			for (const Instruction& global : module.m_globalInstructions)
				note(global.m_id);
			for (const Function& function : module.m_functions)
			{
				note(function.m_id);
				for (cct::UInt32 parameter : function.m_parameters)
					note(parameter);
				for (const BasicBlock& block : function.m_blocks)
				{
					note(block.m_label);
					for (const Instruction& instruction : block.m_instructions)
						note(instruction.m_id);
				}
			}

			module.m_valueRefs.assign(static_cast<std::size_t>(maxId) + 1, ValueRef{});

			for (const auto& [id, constant] : module.m_constants)
				module.m_valueRefs[id] = MakeValueRef(ValueKind::Constant, module.m_constantSlotCount++);

			for (const Function& function : module.m_functions)
			{
				for (cct::UInt32 parameter : function.m_parameters)
					if (GetKind(module.m_valueRefs[parameter]) == ValueKind::None)
						module.m_valueRefs[parameter] = MakeValueRef(ValueKind::Ssa, module.m_ssaSlotCount++);

				for (const BasicBlock& block : function.m_blocks)
					for (const Instruction& instruction : block.m_instructions)
						if (ProducesSsaValue(instruction.m_op, instruction.m_id) && GetKind(module.m_valueRefs[instruction.m_id]) == ValueKind::None)
							module.m_valueRefs[instruction.m_id] = MakeValueRef(ValueKind::Ssa, module.m_ssaSlotCount++);
			}
		}
	} // namespace

	std::optional<Module> Convert(const see::spirv::Module& spirvModule)
	{
		SEE_AUTO_PROFILER_SCOPE();
		Module module;

		for (const auto& [id, spirvDecorations] : spirvModule.m_decorations)
		{
			std::vector<Decoration>& decorations = module.m_decorations[id];
			decorations.reserve(spirvDecorations.size());
			for (const spirv::Decoration& spirvDecoration : spirvDecorations)
				decorations.push_back(Decoration{.m_kind = spirvDecoration.m_kind, .m_literals = spirvDecoration.m_literals});
		}

		for (const auto& [structTypeId, spirvMemberDecorations] : spirvModule.m_memberDecorations)
		{
			std::vector<MemberDecoration>& memberDecorations = module.m_memberDecorations[structTypeId];
			memberDecorations.reserve(spirvMemberDecorations.size());
			for (const spirv::MemberDecoration& spirvDecoration : spirvMemberDecorations)
				memberDecorations.push_back(
					MemberDecoration{.m_member = spirvDecoration.m_member, .m_kind = spirvDecoration.m_kind, .m_literals = spirvDecoration.m_literals});
		}

		std::unordered_set<cct::UInt32> glslStd450Imports;
		for (const spirv::Instruction& instruction : spirvModule.m_globalInstructions)
		{
			const SpvOp opcode = static_cast<SpvOp>(instruction.m_opcode);
			if (opcode == SpvOpExtInstImport)
			{
				if (DecodeLiteralString(RealOperands(instruction)) == "GLSL.std.450")
					glslStd450Imports.insert(instruction.m_resultId);
			}
			else if (IsTypeOpcode(opcode))
				ConvertType(module, instruction, opcode);
			else if (IsConstantOpcode(opcode))
				ConvertConstant(module, instruction, opcode);
			else if (!IsIgnorableOpcode(opcode))
				module.m_globalInstructions.push_back(ConvertInstruction(instruction, glslStd450Imports));
		}

		module.m_functions.reserve(spirvModule.m_functions.size());
		for (const spirv::Function& spirvFunction : spirvModule.m_functions)
		{
			Function function;
			function.m_id = spirvFunction.m_id;
			function.m_parameters = spirvFunction.m_parameters;
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
						block.m_instructions.push_back(ConvertInstruction(instruction, glslStd450Imports));

				function.m_blocks.push_back(std::move(block));
			}

			module.m_functions.push_back(std::move(function));
		}

		AssignValueRefs(module);
		return module;
	}
} // namespace see::ir
