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

	enum class ValueKind : cct::UInt32
	{
		None = 0,
		Ssa = 1,
		Constant = 2,
		Pointer = 3
	};

	struct ValueRef
	{
		cct::UInt32 m_encoded = 0; // kind in bits 0-1, slot in bits 2-31
	};

	static constexpr cct::UInt32 InvalidSlot = 0xFFFFFFFFu;

	[[nodiscard]] constexpr ValueKind GetKind(ValueRef ref) noexcept
	{
		return static_cast<ValueKind>(ref.m_encoded & 3u);
	}

	[[nodiscard]] constexpr cct::UInt32 GetSlot(ValueRef ref) noexcept
	{
		return ref.m_encoded >> 2;
	}

	[[nodiscard]] constexpr ValueRef MakeValueRef(ValueKind kind, cct::UInt32 slot) noexcept
	{
		return ValueRef{.m_encoded = (slot << 2) | static_cast<cct::UInt32>(kind)};
	}
} // namespace see::ir
