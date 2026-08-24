/**
 * @file Module.hpp
 * @brief Minimal SSA IR converted from a parsed SPIR-V module (see::spirv::Module)
 * @date 2026-08-24
 *
 * Reuses SPIR-V ids as SSA value names; unrecognized ops become Op::Unsupported.
 */

#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <Concerto/Core/Types/Types.hpp>

namespace see::spirv
{
	struct Module;
}

namespace see::ir
{
	enum class ScalarKind
	{
		Bool,
		Int32,
		Float32
	};

	enum class TypeKind
	{
		Void,
		Scalar,
		Vector,
		Matrix,
		Pointer
	};

	struct Type
	{
		TypeKind m_kind = TypeKind::Void;
		ScalarKind m_scalar = ScalarKind::Float32; // meaningful for Scalar/Vector/Matrix
		cct::UInt32 m_componentCount = 0; // Vector: component count. Matrix: column count.
		cct::UInt32 m_pointee = 0; // Pointer: pointee type id. Matrix: column (Vector) type id.
	};

	enum class Op
	{
		Unsupported,
		Variable,
		Constant,
		Load,
		Store,
		AccessChain,
		CompositeConstruct,
		CompositeExtract,
		Add,
		Sub,
		Mul,
		Div,
		Dot,
		MatrixTimesVector,
		VectorTimesMatrix,
		MatrixTimesMatrix,
		CompareEqual,
		CompareNotEqual,
		CompareLess,
		CompareLessEqual,
		CompareGreater,
		CompareGreaterEqual,
		Select
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
		std::vector<BasicBlock> m_blocks;
	};

	struct Constant
	{
		cct::UInt32 m_type = 0;
		cct::UInt32 m_bits = 0; // raw bit pattern, reinterpret via m_type's scalar kind
	};

	struct Decoration
	{
		cct::UInt32 m_kind = 0; // raw SpvDecoration value
		std::vector<cct::UInt32> m_literals;
	};

	struct Module
	{
		std::unordered_map<cct::UInt32, Type> m_types;
		std::unordered_map<cct::UInt32, Constant> m_constants;
		std::unordered_map<cct::UInt32, std::vector<Decoration>> m_decorations;
		std::vector<Instruction> m_globalInstructions; // mostly OpVariable
		std::vector<Function> m_functions;
	};

	[[nodiscard]] std::optional<Module> Convert(const see::spirv::Module& spirvModule);
} // namespace see::ir
