/**
 * @file Scalar.hpp
 * @brief Single-lane scalar interpreter for the IR (see::ir)
 * @date 2026-08-24
 */

#pragma once

#include <array>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Concerto/Core/Types/Types.hpp>

#include "See/Ir/Module.hpp"

namespace see::exec
{
	struct Value
	{
		// Sized for composite locals (arrays of matrices, arrays of structs): a mat4[4] is 64 words.
		// Anything larger (big uniform blocks) lives in an ExternalBuffer and is addressed
		// member-by-member instead of ever being materialized whole in a Value.
		static constexpr cct::UInt32 MaxWords = 64;

		ir::ScalarKind m_scalar = ir::ScalarKind::Float32;
		cct::UInt32 m_rows = 1; // vector length, or total word count when m_structType != 0. 1 for a plain scalar.
		cct::UInt32 m_columns = 1; // matrix column count. 1 for scalar/vector/struct.
		cct::UInt32 m_structType = 0; // non-zero: this is a struct (ir type id), m_bits is its flat word layout.
		cct::UInt32 m_imageVariable = 0; // non-zero: this is an (optionally sampled) image handle, naming the UniformConstant variable it was loaded from.
		std::array<cct::UInt32, MaxWords> m_bits{};

		[[nodiscard]] constexpr cct::UInt32 WordCount() const noexcept
		{
			return m_rows * m_columns;
		}

		// A register slot outlives the value it held, so only the first WordCount() words are meaningful;
		// the rest are whatever the previous occupant left behind. Iterate through Words(), never m_bits.
		[[nodiscard]] std::span<const cct::UInt32> Words() const noexcept
		{
			return std::span<const cct::UInt32>(m_bits.data(), WordCount());
		}

		[[nodiscard]] std::span<cct::UInt32> Words() noexcept
		{
			return std::span<cct::UInt32>(m_bits.data(), WordCount());
		}

		[[nodiscard]] float GetFloat(cct::UInt32 index = 0) const;
		void SetFloat(cct::UInt32 index, float value);
		[[nodiscard]] cct::Int32 GetInt32(cct::UInt32 index = 0) const;
		void SetInt32(cct::UInt32 index, cct::Int32 value);
		[[nodiscard]] bool GetBool(cct::UInt32 index = 0) const;
		void SetBool(cct::UInt32 index, bool value);
	};

	// A word-addressed view over externally-owned memory (e.g. a mapped uniform/storage buffer). Unlike a
	// Value, which caps a variable's storage at Value::MaxWords, this lets a pointer variable address
	// memory of any size without copying it into the interpreter's register file.
	struct ExternalBuffer
	{
		cct::UInt32* m_words = nullptr;
		cct::UInt32 m_wordCount = 0;
	};

	// A 2D RGBA8 texture the interpreter can sample from (nearest-neighbor, no mip/LOD). Backs a
	// UniformConstant sampled-image variable, addressed the same way ExternalBuffer addresses a buffer.
	struct ExternalImage
	{
		const cct::UInt8* m_texels = nullptr; // row-major, 4 bytes/texel
		cct::UInt32 m_width = 0;
		cct::UInt32 m_height = 0;
	};

	[[nodiscard]] std::optional<Value> ExtractStructMember(const ir::Module& module, const Value& composite, cct::UInt32 memberIndex);

	struct MemberLayout
	{
		cct::UInt32 m_offset = 0;
		cct::UInt32 m_wordCount = 0;
		cct::UInt32 m_matrixStride = 0;
	};

	class PreparedModule
	{
	public:
		[[nodiscard]] const Value* GetConstant(cct::UInt32 slot) const noexcept;
		[[nodiscard]] const Value* GetZeroValue(cct::UInt32 typeId) const noexcept;
		[[nodiscard]] const std::vector<MemberLayout>* GetMemberLayout(cct::UInt32 typeId) const noexcept;
		[[nodiscard]] cct::UInt32 GetArrayStride(cct::UInt32 typeId) const noexcept;

	private:
		friend PreparedModule BuildPreparedModule(const ir::Module& module);

		std::vector<Value> m_constants;
		std::vector<cct::UInt8> m_constantValid;
		std::vector<Value> m_zeroValues;
		std::vector<cct::UInt8> m_zeroValid;
		std::vector<std::vector<MemberLayout>> m_memberLayouts;
		std::vector<cct::UInt8> m_memberLayoutValid;
		std::vector<cct::UInt32> m_arrayStrides;
	};

	[[nodiscard]] PreparedModule BuildPreparedModule(const ir::Module& module);

	class RegisterFile
	{
	public:
		void Reset(cct::UInt32 slotCount);

		[[nodiscard]] const Value* Find(cct::UInt32 slot) const noexcept
		{
			return m_slotEpoch[slot] == m_epoch ? &m_values[slot] : nullptr;
		}

		[[nodiscard]] Value& Emplace(cct::UInt32 slot) noexcept
		{
			m_slotEpoch[slot] = m_epoch;
			return m_values[slot];
		}

	private:
		std::vector<Value> m_values;
		std::vector<cct::UInt32> m_slotEpoch;
		cct::UInt32 m_epoch = 0;
	};

	[[nodiscard]] std::optional<std::unordered_map<cct::UInt32, Value>> Run(
		const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers = {},
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages = {});

	[[nodiscard]] std::optional<std::unordered_map<cct::UInt32, Value>> Run(
		const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared);

	[[nodiscard]] std::optional<std::unordered_map<cct::UInt32, Value>> Run(
		const ir::Module& module, const ir::Function& function, std::unordered_map<cct::UInt32, Value> inputs,
		const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
		const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared, RegisterFile& registers);
} // namespace see::exec
