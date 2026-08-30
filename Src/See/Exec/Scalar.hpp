/**
 * @file Scalar.hpp
 * @brief Single-lane scalar interpreter for the IR (see::ir)
 * @date 2026-08-24
 */

#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Concerto/Core/Assert.hpp>
#include <Concerto/Core/Types/Types.hpp>

#include "See/Ir/Module.hpp"

namespace see::exec
{
	class ExecutionStorage;

	struct Value
	{
		// Largest type the interpreter will ever materialize whole (a mat4[4] is 64 words). Anything
		// larger lives in an ExternalBuffer and is addressed member-by-member.
		static constexpr cct::UInt32 MaxWords = 64;
		// Words kept in the Value itself; 99.27% of the values a CTS run manipulates fit here.
		static constexpr cct::UInt32 InlineWords = 4;

		// m_overflow is either NoOverflow, or a domain bit plus an offset into that domain's arena.
		static constexpr cct::UInt32 NoOverflow = 0xFFFFFFFFu;
		static constexpr cct::UInt32 ModuleDomain = 0x80000000u;
		static constexpr cct::UInt32 OffsetMask = 0x7FFFFFFFu;

		ir::ScalarKind m_scalar = ir::ScalarKind::Float32;
		cct::UInt32 m_rows = 1; // vector length, or total word count when m_structType != 0. 1 for a plain scalar.
		cct::UInt32 m_columns = 1; // matrix column count. 1 for scalar/vector/struct.
		cct::UInt32 m_structType = 0; // non-zero: this is a struct (ir type id), the words are its flat layout.
		cct::UInt32 m_imageVariable = 0; // non-zero: this is an (optionally sampled) image handle, naming the UniformConstant variable it was loaded from.
		std::array<cct::UInt32, InlineWords> m_inline{};
		cct::UInt32 m_overflow = NoOverflow;

		Value& operator=(const Value&) = delete;

		[[nodiscard]] constexpr cct::UInt32 WordCount() const noexcept
		{
			return m_rows * m_columns;
		}

		[[nodiscard]] constexpr bool IsInline() const noexcept
		{
			return WordCount() <= InlineWords;
		}

		[[nodiscard]] std::span<const cct::UInt32> Words(const ExecutionStorage& storage) const noexcept;
		[[nodiscard]] std::span<cct::UInt32> Words(ExecutionStorage& storage) noexcept;

		[[nodiscard]] float GetFloat(const ExecutionStorage& storage, cct::UInt32 index = 0) const;
		void SetFloat(ExecutionStorage& storage, cct::UInt32 index, float value);
		[[nodiscard]] cct::Int32 GetInt32(const ExecutionStorage& storage, cct::UInt32 index = 0) const;
		void SetInt32(ExecutionStorage& storage, cct::UInt32 index, cct::Int32 value);
		[[nodiscard]] bool GetBool(const ExecutionStorage& storage, cct::UInt32 index = 0) const;
		void SetBool(ExecutionStorage& storage, cct::UInt32 index, bool value);
	};

	// A word-addressed view over externally-owned memory (e.g. a mapped uniform/storage buffer). Unlike a
	// Value, which caps its storage at Value::MaxWords, this lets a pointer variable address memory of any
	// size without copying it into the interpreter's register file.
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

	// Where a pointer id currently points: a base variable plus a word offset into its storage.
	struct PointerInfo
	{
		cct::UInt32 m_baseVariable = 0;
		cct::UInt32 m_wordOffset = 0;
		cct::UInt32 m_pointeeType = 0; // ir type id of what this pointer currently points to
		cct::UInt32 m_matrixStride = 0; // words between matrix columns in memory, 0 when naturally packed
	};

	struct MemberLayout
	{
		cct::UInt32 m_offset = 0;
		cct::UInt32 m_wordCount = 0;
		cct::UInt32 m_matrixStride = 0;
	};

	// Fixed-size blocks, so a span handed out by Allocate stays valid across every later allocation.
	// Rewind reuses the blocks instead of releasing them.
	class WordArena
	{
	public:
		static constexpr cct::UInt32 BlockWords = 4096;

		[[nodiscard]] cct::UInt32 Allocate(cct::UInt32 wordCount);

		void Rewind() noexcept
		{
			m_cursor = 0;
		}

		[[nodiscard]] std::span<const cct::UInt32> Resolve(cct::UInt32 offset, cct::UInt32 wordCount) const noexcept
		{
			return std::span<const cct::UInt32>(m_blocks[offset / BlockWords].get() + offset % BlockWords, wordCount);
		}

		[[nodiscard]] std::span<cct::UInt32> Resolve(cct::UInt32 offset, cct::UInt32 wordCount) noexcept
		{
			return std::span<cct::UInt32>(m_blocks[offset / BlockWords].get() + offset % BlockWords, wordCount);
		}

	private:
		std::vector<std::unique_ptr<cct::UInt32[]>> m_blocks;
		cct::UInt32 m_cursor = 0;
	};

	class PreparedModule
	{
	public:
		[[nodiscard]] const Value* GetConstant(cct::UInt32 slot) const noexcept;
		[[nodiscard]] const Value* GetZeroValue(cct::UInt32 typeId) const noexcept;
		[[nodiscard]] const std::vector<MemberLayout>* GetMemberLayout(cct::UInt32 typeId) const noexcept;
		[[nodiscard]] cct::UInt32 GetArrayStride(cct::UInt32 typeId) const noexcept;

		[[nodiscard]] const WordArena& Arena() const noexcept
		{
			return m_arena;
		}

		[[nodiscard]] cct::UInt32 SsaSlotCount() const noexcept
		{
			return m_ssaSlotCount;
		}

		[[nodiscard]] cct::UInt32 VariableSlotCount() const noexcept
		{
			return m_variableSlotCount;
		}

		[[nodiscard]] cct::UInt32 PointerSlotCount() const noexcept
		{
			return m_pointerSlotCount;
		}

	private:
		friend PreparedModule BuildPreparedModule(const ir::Module& module);

		std::vector<Value> m_constants;
		std::vector<cct::UInt8> m_constantValid;
		std::vector<Value> m_zeroValues;
		std::vector<cct::UInt8> m_zeroValid;
		std::vector<std::vector<MemberLayout>> m_memberLayouts;
		std::vector<cct::UInt8> m_memberLayoutValid;
		std::vector<cct::UInt32> m_arrayStrides;
		WordArena m_arena;
		cct::UInt32 m_ssaSlotCount = 0;
		cct::UInt32 m_variableSlotCount = 0;
		cct::UInt32 m_pointerSlotCount = 0;
	};

	[[nodiscard]] PreparedModule BuildPreparedModule(const ir::Module& module);

	// A table indexed by slot and reused for the whole life of the storage. Reset bumps an epoch rather
	// than clearing, so what the previous invocation left behind reads as absent at no cost, and no
	// invocation ever allocates a node or frees one.
	template<typename T>
	class SlotFile
	{
	public:
		void Reset(cct::UInt32 slotCount)
		{
			if (m_slotEpoch.size() < slotCount)
			{
				m_entries.resize(slotCount);
				m_slotEpoch.resize(slotCount, 0);
			}

			if (m_epoch == 0xFFFFFFFFu)
			{
				std::fill(m_slotEpoch.begin(), m_slotEpoch.end(), 0u);
				m_epoch = 0;
			}

			++m_epoch;
		}

		[[nodiscard]] const T* Find(cct::UInt32 slot) const noexcept
		{
			return m_slotEpoch[slot] == m_epoch ? &m_entries[slot] : nullptr;
		}

		[[nodiscard]] T* Find(cct::UInt32 slot) noexcept
		{
			return m_slotEpoch[slot] == m_epoch ? &m_entries[slot] : nullptr;
		}

		[[nodiscard]] T& Emplace(cct::UInt32 slot) noexcept
		{
			m_slotEpoch[slot] = m_epoch;
			return m_entries[slot];
		}

	private:
		std::vector<T> m_entries;
		std::vector<cct::UInt32> m_slotEpoch;
		cct::UInt32 m_epoch = 0;
	};

	// Everything one invocation owns: its registers, its variables and pointers, and the arena backing
	// values too large to live inline. An overflow offset only means anything relative to the storage
	// that handed it out, so this is neither copyable nor movable.
	class ExecutionStorage
	{
	public:
		ExecutionStorage() = default;
		ExecutionStorage(const ExecutionStorage&) = delete;
		ExecutionStorage& operator=(const ExecutionStorage&) = delete;

		void Reset(const PreparedModule& prepared)
		{
			m_prepared = &prepared;
			m_registers.Reset(prepared.SsaSlotCount());
			m_variables.Reset(prepared.VariableSlotCount());
			m_pointers.Reset(prepared.PointerSlotCount());
			m_arena.Rewind();
		}

		[[nodiscard]] const PreparedModule& Prepared() const noexcept
		{
			return *m_prepared;
		}

		[[nodiscard]] SlotFile<Value>& Registers() noexcept
		{
			return m_registers;
		}

		[[nodiscard]] const SlotFile<Value>& Registers() const noexcept
		{
			return m_registers;
		}

		[[nodiscard]] SlotFile<Value>& Variables() noexcept
		{
			return m_variables;
		}

		[[nodiscard]] const SlotFile<Value>& Variables() const noexcept
		{
			return m_variables;
		}

		[[nodiscard]] SlotFile<PointerInfo>& Pointers() noexcept
		{
			return m_pointers;
		}

		[[nodiscard]] cct::UInt32 Allocate(cct::UInt32 wordCount)
		{
			return m_arena.Allocate(wordCount);
		}

		[[nodiscard]] std::span<const cct::UInt32> Resolve(cct::UInt32 overflow, cct::UInt32 wordCount) const noexcept
		{
			if ((overflow & Value::ModuleDomain) != 0)
				return m_prepared->Arena().Resolve(overflow & Value::OffsetMask, wordCount);
			return m_arena.Resolve(overflow, wordCount);
		}

		[[nodiscard]] std::span<cct::UInt32> Resolve(cct::UInt32 overflow, cct::UInt32 wordCount) noexcept
		{
			return m_arena.Resolve(overflow & Value::OffsetMask, wordCount);
		}

	private:
		SlotFile<Value> m_registers;
		SlotFile<Value> m_variables;
		SlotFile<PointerInfo> m_pointers;
		WordArena m_arena;
		const PreparedModule* m_prepared = nullptr;
	};

	inline std::span<const cct::UInt32> Value::Words(const ExecutionStorage& storage) const noexcept
	{
		const cct::UInt32 wordCount = WordCount();
		if (wordCount <= InlineWords)
			return std::span<const cct::UInt32>(m_inline.data(), wordCount);
		CCT_ASSERT(m_overflow != NoOverflow, "See: a {}-word value was never given an overflow region", wordCount);
		return storage.Resolve(m_overflow, wordCount);
	}

	inline std::span<cct::UInt32> Value::Words(ExecutionStorage& storage) noexcept
	{
		const cct::UInt32 wordCount = WordCount();
		if (wordCount <= InlineWords)
			return std::span<cct::UInt32>(m_inline.data(), wordCount);
		CCT_ASSERT(m_overflow != NoOverflow, "See: a {}-word value was never given an overflow region", wordCount);
		return storage.Resolve(m_overflow, wordCount);
	}

	// Reserves an overflow region for `value` when its shape no longer fits inline. Call after setting
	// the shape and before writing through Words().
	void ReserveWords(Value& value, ExecutionStorage& storage);

	// Gives `destination` the shape of `source` plus its own copy of the words, reserving a fresh
	// overflow region rather than sharing the source's.
	void CopyValue(Value& destination, const Value& source, ExecutionStorage& storage);

	[[nodiscard]] std::optional<Value> ExtractStructMember(const ir::Module& module, const Value& composite, cct::UInt32 memberIndex, ExecutionStorage& storage);

	// A variable is named by its ir id but stored by slot; these translate. Emplace returns nullptr for
	// an id that owns no storage.
	[[nodiscard]] const Value* FindVariable(const ir::Module& module, const ExecutionStorage& storage, cct::UInt32 variableId) noexcept;
	[[nodiscard]] Value* EmplaceVariable(const ir::Module& module, ExecutionStorage& storage, cct::UInt32 variableId) noexcept;

	// Inputs are the variables the caller emplaced in `storage` after resetting it, and the outputs are
	// the variables left there afterwards - nothing is handed back by value.
	[[nodiscard]] bool Run(const ir::Module& module, const ir::Function& function,
						   const std::unordered_map<cct::UInt32, ExternalBuffer>& externalBuffers,
						   const std::unordered_map<cct::UInt32, ExternalImage>& externalImages, const PreparedModule& prepared, ExecutionStorage& storage);
} // namespace see::exec
