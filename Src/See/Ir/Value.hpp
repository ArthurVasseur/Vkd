/**
 * @file Value.hpp
 * @brief IR value-level data: constants and per-id decorations
 * @date 2026-08-27
 */

#pragma once

#include <vector>

#include <Concerto/Core/Types/Types.hpp>

namespace see::ir
{
	struct Constant
	{
		cct::UInt32 m_type = 0;
		cct::UInt32 m_bits = 0; // scalar constant (OpConstant/True/False): raw bit pattern, reinterpret via m_type's scalar kind. Unused when m_words is non-empty.
		std::vector<cct::UInt32> m_words; // OpConstantComposite: constituents flattened in order (m_bits of each, recursively for nested composites).
	};

	struct Decoration
	{
		cct::UInt32 m_kind = 0; // raw SpvDecoration value
		std::vector<cct::UInt32> m_literals;
	};

	struct MemberDecoration
	{
		cct::UInt32 m_member = 0;
		cct::UInt32 m_kind = 0; // raw SpvDecoration value
		std::vector<cct::UInt32> m_literals;
	};
} // namespace see::ir
