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

#include "See/Ir/Instruction.hpp"
#include "See/Ir/Type.hpp"
#include "See/Ir/Value.hpp"

namespace see::spirv
{
	struct Module;
}

namespace see::ir
{
	struct Module
	{
		std::unordered_map<cct::UInt32, Type> m_types;
		std::unordered_map<cct::UInt32, Constant> m_constants;
		std::unordered_map<cct::UInt32, std::vector<Decoration>> m_decorations;
		std::unordered_map<cct::UInt32, std::vector<MemberDecoration>> m_memberDecorations; // keyed by struct type id
		std::vector<Instruction> m_globalInstructions; // mostly OpVariable
		std::vector<Function> m_functions;
	};

	[[nodiscard]] std::optional<Module> Convert(const see::spirv::Module& spirvModule);
} // namespace see::ir
