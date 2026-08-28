/**
 * @file Instruction.hpp
 * @brief IR instructions, basic blocks and functions
 * @date 2026-08-27
 */

#pragma once

#include <vector>

#include <Concerto/Core/Types/Types.hpp>

namespace see::ir
{
	enum class Op
	{
		Unsupported,
		Variable,
		Constant,
		Load,
		Store,
		CopyMemory,
		AccessChain,
		CompositeConstruct,
		CompositeExtract,
		VectorExtractDynamic,
		VectorInsertDynamic,
		Add,
		Sub,
		Mul,
		Div,
		Rem, // OpFRem/OpSRem: sign follows the dividend
		Mod, // OpFMod/OpSMod: sign follows the divisor
		UnsignedMod,
		Negate, // OpFNegate/OpSNegate, dispatched on the operand's scalar kind
		ConvertSignedToFloat,
		ConvertUnsignedToFloat,
		ConvertFloatToSigned,
		ConvertFloatToUnsigned,
		Dot,
		MatrixTimesVector,
		VectorTimesMatrix,
		MatrixTimesMatrix,
		VectorTimesScalar, // also OpMatrixTimesScalar: every component scaled by one scalar
		CompareEqual,
		CompareNotEqual,
		CompareLess,
		CompareLessEqual,
		CompareGreater,
		CompareGreaterEqual,
		LogicalAnd,
		LogicalOr,
		LogicalNot,
		LogicalEqual,
		LogicalNotEqual,
		All,
		Any,
		Select,
		ImageSample,
		FunctionCall,
		GlslStd450 // OpExtInst from a "GLSL.std.450" import; m_args = [extended instruction number, operands...]
	};

	struct Instruction
	{
		Op m_op = Op::Unsupported;
		cct::UInt32 m_id = 0; // SSA result id (0 if the op has none, e.g. Store)
		cct::UInt32 m_type = 0; // SPIR-V id of the result type (0 if none)
		std::vector<cct::UInt32> m_args;
		cct::UInt32 m_spirvOpcode = 0; // original SpvOp, kept for Op::Unsupported
	};

	enum class Terminator
	{
		None,
		Branch,
		BranchConditional,
		Return,
		ReturnValue
	};

	struct BasicBlock
	{
		cct::UInt32 m_label = 0;
		std::vector<Instruction> m_instructions;
		Terminator m_terminator = Terminator::None;
		std::vector<cct::UInt32> m_branchTargets;
		cct::UInt32 m_terminatorOperand = 0; // BranchConditional: condition id. ReturnValue: returned value id.
	};

	struct Function
	{
		cct::UInt32 m_id = 0;
		std::vector<cct::UInt32> m_parameters;
		std::vector<BasicBlock> m_blocks;
	};
} // namespace see::ir
