/**
 * @file Module.hpp
 * @brief Parsed SPIR-V module: functions, basic blocks (CFG) and decorations
 * @date 2026-08-24
 *
 * Built on SPIRV-Tools' spvBinaryParse rather than a hand-written decoder.
 */

#pragma once

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Concerto/Core/Types/Types.hpp>

namespace see::spirv
{
	struct Instruction
	{
		cct::UInt32 m_opcode;
		cct::UInt32 m_resultId;
		cct::UInt32 m_typeId;
		std::vector<cct::UInt32> m_operands;
	};

	enum class Terminator
	{
		None,
		Branch,
		BranchConditional,
		Switch,
		Return,
		ReturnValue,
		Kill,
		Unreachable
	};

	struct BasicBlock
	{
		cct::UInt32 m_label;
		std::vector<Instruction> m_instructions;
		Terminator m_terminator = Terminator::None;
		std::vector<cct::UInt32> m_branchTargets;
		cct::UInt32 m_terminatorOperand = 0; // BranchConditional: condition id. Switch: selector id. ReturnValue: returned value id.
		std::vector<cct::UInt64> m_switchLiterals;
	};

	struct Function
	{
		cct::UInt32 m_id;
		cct::UInt32 m_resultType;
		std::vector<cct::UInt32> m_parameters;
		std::vector<cct::UInt32> m_parameterTypes;
		std::vector<BasicBlock> m_blocks;
	};

	struct Decoration
	{
		cct::UInt32 m_kind;
		std::vector<cct::UInt32> m_literals;
	};

	struct MemberDecoration
	{
		cct::UInt32 m_member;
		cct::UInt32 m_kind;
		std::vector<cct::UInt32> m_literals;
	};

	struct EntryPoint
	{
		cct::UInt32 m_executionModel;
		cct::UInt32 m_functionId;
		std::string m_name;
		std::vector<cct::UInt32> m_interfaceIds;
	};

	struct Module
	{
		std::vector<Instruction> m_globalInstructions;
		std::vector<Function> m_functions;
		std::vector<EntryPoint> m_entryPoints;
		std::unordered_map<cct::UInt32, std::vector<Decoration>> m_decorations;
		std::unordered_map<cct::UInt32, std::vector<MemberDecoration>> m_memberDecorations; // keyed by struct type id
	};

	// Returns nullopt if the binary fails to parse (invalid SPIR-V).
	[[nodiscard]] std::optional<Module> Parse(std::span<const cct::UInt32> code);
} // namespace see::spirv
