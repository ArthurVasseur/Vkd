/**
 * @file Scalar.hpp
 * @brief Single-lane scalar interpreter for the IR (see::ir)
 * @date 2026-08-24
 */

#pragma once

#include <array>
#include <optional>
#include <unordered_map>

#include <Concerto/Core/Types/Types.hpp>

#include "See/Ir/Module.hpp"

namespace see::exec
{
	struct Value
	{
		ir::ScalarKind m_scalar = ir::ScalarKind::Float32;
		cct::UInt32 m_rows = 1; // vector length, or total word count when m_structType != 0. 1 for a plain scalar.
		cct::UInt32 m_columns = 1; // matrix column count. 1 for scalar/vector/struct.
		cct::UInt32 m_structType = 0; // non-zero: this is a struct (ir type id), m_bits is its flat word layout.
		std::array<cct::UInt32, 16> m_bits{};

		[[nodiscard]] float GetFloat(cct::UInt32 index = 0) const;
		void SetFloat(cct::UInt32 index, float value);
		[[nodiscard]] cct::Int32 GetInt32(cct::UInt32 index = 0) const;
		void SetInt32(cct::UInt32 index, cct::Int32 value);
		[[nodiscard]] bool GetBool(cct::UInt32 index = 0) const;
		void SetBool(cct::UInt32 index, bool value);
	};

	// A word-addressed view over externally-owned memory (e.g. a mapped uniform/storage buffer). Unlike a
	// Value, which caps a variable's storage at 16 words, this lets a pointer variable address memory of
	// any size without copying it into the interpreter's register file.
	struct ExternalBuffer
	{
		cct::UInt32* m_words = nullptr;
		cct::UInt32 m_wordCount = 0;
	};

	[[nodiscard]] std::optional<std::unordered_map<cct::UInt32, Value>> Run(
		const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers = {});
} // namespace see::exec
