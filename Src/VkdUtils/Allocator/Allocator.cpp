/**
 * @file Allocator.cpp
 * @brief Implementation of TLSF (Two-Level Segregate Fit) allocator
 * @date 2025-10-30
 */

#include "VkdUtils/Allocator/Allocator.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <ostream>

#include <Concerto/Core/EnumFlags/EnumFlags.hpp>

#if defined(CCT_PLATFORM_WINDOWS)
#include <intrin.h>
#endif

namespace vkd
{

	/**
	 * @enum BlockFlag
	 * @brief Flags for tracking block state
	 */
	enum class BlockFlag : UInt32
	{
		None = 0, ///< No flags set
		IsFree = 0x1, ///< Block is free (not allocated)
		PrevIsFree = 0x2 ///< Previous physical block is free
	};

	/**
	 * @struct Block
	 * @brief Memory block header containing metadata for allocation tracking
	 *
	 * Each block (free or allocated) has this header structure.
	 * For free blocks, additional pointers are stored in the payload area.
	 *
	 * Aligned to ensure payload can be aligned to common boundaries.
	 */
	struct VKD_ALIGN(Allocator::BlockAlignment) Allocator::Block
	{
		/// Size of this block (not including header)
		std::size_t size;

		/// Previous physical block size (for backward coalescing)
		std::size_t prevPhysicalSize;

		/// Block state flags (IsFree, PrevIsFree)
		cct::EnumFlags<BlockFlag> flags;

		/// Padding for alignment
		UInt32 padding;

		/// For free blocks: next in free list
		Block* nextFree;

		/// For free blocks: previous in free list
		Block* prevFree;

		bool IsFree() const noexcept
		{
			return flags.Contains(BlockFlag::IsFree);
		}

		void SetFree(bool free) noexcept
		{
			if (free)
				flags.Set(BlockFlag::IsFree);
			else
				flags.Reset(BlockFlag::IsFree);
		}

		bool PrevIsFree() const noexcept
		{
			return flags.Contains(BlockFlag::PrevIsFree);
		}

		void SetPrevFree(bool free) noexcept
		{
			if (free)
				flags.Set(BlockFlag::PrevIsFree);
			else
				flags.Reset(BlockFlag::PrevIsFree);
		}
	};

	Allocator::Allocator(std::size_t poolSizeBytes) noexcept
		:
		m_TotalSize(poolSizeBytes),
		m_UsedSize(0),
		m_Pool(),
		m_FirstLevelIndexBits(DefaultFirstLevelIndexBits),
		m_SecondLevelIndexBits(DefaultSecondLevelIndexBits),
		m_FirstLevelCount(1u << DefaultFirstLevelIndexBits),
		m_SecondLevelCount(1u << DefaultSecondLevelIndexBits),
		m_FirstLevelBitmap(0),
		m_SecondLevelBitmaps(),
		m_FreeLists(),
		m_Initialized(false)
	{
	}

	bool Allocator::Init() noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (m_Initialized)
			return false;

		if (m_TotalSize < MinBlockSize + sizeof(Block))
			return false;

		try
		{
			m_Pool.resize(m_TotalSize);
		}
		catch (...)
		{
			return false;
		}

		try
		{
			m_SecondLevelBitmaps.resize(m_FirstLevelCount);
		}
		catch (...)
		{
			m_Pool.clear();
			return false;
		}

		const std::size_t totalLists = m_FirstLevelCount * m_SecondLevelCount;
		try
		{
			m_FreeLists.resize(totalLists);
		}
		catch (...)
		{
			m_Pool.clear();
			m_SecondLevelBitmaps.clear();
			return false;
		}

		m_FirstLevelBitmap = 0;
		std::memset(m_SecondLevelBitmaps.data(), 0, sizeof(UInt64) * m_FirstLevelCount);
		std::memset(m_FreeLists.data(), 0, sizeof(Block*) * totalLists);

		Block* initialBlock = reinterpret_cast<Block*>(m_Pool.data());
		initialBlock->size = m_TotalSize - sizeof(Block);
		initialBlock->prevPhysicalSize = 0;
		initialBlock->flags.Clear();
		initialBlock->SetFree(true);
		initialBlock->SetPrevFree(false);
		initialBlock->nextFree = nullptr;
		initialBlock->prevFree = nullptr;

		InsertFree(initialBlock);

		m_UsedSize = 0;
		m_Initialized = true;

		return true;
	}

	UInt32 Allocator::FindLastSet64(UInt64 x) noexcept
	{
		if (x == 0)
			return 0;

#if defined(CCT_PLATFORM_WINDOWS)
#if defined(CCT_ARCH_X86)
		unsigned long index;
		_BitScanReverse(&index, static_cast<unsigned long>(x));
		return static_cast<UInt32>(index);
#else
		unsigned long index;
		_BitScanReverse64(&index, x);
		return static_cast<UInt32>(index);
#endif
#elif defined(CCT_PLATFORM_POSIX)
		return 63 - __builtin_clzll(x);
#else
		// Portable fallback
		UInt32 bit = 0;
		while (x >>= 1)
		{
			++bit;
		}
		return bit;
#endif
	}

	UInt32 Allocator::CountLeadingZeros64(UInt64 x) noexcept
	{
		if (x == 0)
			return 64;

#if defined(CCT_PLATFORM_WINDOWS)
#if defined(CCT_ARCH_X86)
		if (x >> 32)
		{
			unsigned long index;
			_BitScanReverse(&index, static_cast<unsigned long>(x >> 32));
			return static_cast<UInt32>(31 - index);
		}
		else
		{
			unsigned long index;
			_BitScanReverse(&index, static_cast<unsigned long>(x));
			return static_cast<UInt32>(63 - index);
		}
#else
		unsigned long index;
		_BitScanReverse64(&index, x);
		return static_cast<UInt32>(63 - index);
#endif
#elif defined(CCT_PLATFORM_POSIX)
		return __builtin_clzll(x);
#else
		// Portable fallback
		UInt32 count = 0;
		UInt64 mask = UInt64{1} << 63;
		while ((x & mask) == 0)
		{
			++count;
			mask >>= 1;
			if (count >= 64)
			{
				break;
			}
		}
		return count;
#endif
	}

	std::size_t Allocator::GetBlockOffset(const Block* b) const noexcept
	{
		return reinterpret_cast<const UInt8*>(b) - m_Pool.data();
	}

	Allocator::Block* Allocator::GetBlockFromOffset(std::size_t offset) noexcept
	{
		return reinterpret_cast<Block*>(m_Pool.data() + offset);
	}

	const Allocator::Block* Allocator::GetBlockFromOffset(std::size_t offset) const noexcept
	{
		return reinterpret_cast<const Block*>(m_Pool.data() + offset);
	}

	std::size_t Allocator::GetPayloadOffset(const Block* b) const noexcept
	{
		return GetBlockOffset(b) + sizeof(Block);
	}

	void Allocator::Mapping(std::size_t size, UInt32& outFirstLevelIndex, UInt32& outSecondLevelIndex) const noexcept
	{
		if (size < MinBlockSize)
			size = MinBlockSize;

		outFirstLevelIndex = FindLastSet64(size);

		if (outFirstLevelIndex < m_SecondLevelIndexBits)
		{
			outFirstLevelIndex = 0;
			outSecondLevelIndex = static_cast<UInt32>(size >> (outFirstLevelIndex > 0 ? outFirstLevelIndex - 1 : 0)) & ((1u << m_SecondLevelIndexBits) - 1);
		}
		else
		{
			const UInt32 shift = outFirstLevelIndex - m_SecondLevelIndexBits;
			outSecondLevelIndex = static_cast<UInt32>((size >> shift) - (1u << m_SecondLevelIndexBits));
			outSecondLevelIndex = std::min(outSecondLevelIndex, (1u << m_SecondLevelIndexBits) - 1);
		}

		outFirstLevelIndex = std::min(outFirstLevelIndex, m_FirstLevelCount - 1);
		outSecondLevelIndex = std::min(outSecondLevelIndex, m_SecondLevelCount - 1);
	}

	void Allocator::InsertFree(Block* b) noexcept
	{
		UInt32 firstLevelIndex, secondLevelIndex;
		Mapping(b->size, firstLevelIndex, secondLevelIndex);

		const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);

		b->nextFree = m_FreeLists[index];
		b->prevFree = nullptr;

		if (m_FreeLists[index] != nullptr)
			m_FreeLists[index]->prevFree = b;

		m_FreeLists[index] = b;

		SetFirstLevelBit(firstLevelIndex);
		SetSecondLevelBit(firstLevelIndex, secondLevelIndex);
	}

	void Allocator::RemoveFree(Block* b) noexcept
	{
		if (b == nullptr)
			return;

		if (!b->IsFree())
			return;

		UInt32 firstLevelIndex, secondLevelIndex;
		Mapping(b->size, firstLevelIndex, secondLevelIndex);

		if (b->prevFree != nullptr)
			b->prevFree->nextFree = b->nextFree;
		else
		{
			const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
			m_FreeLists[index] = b->nextFree;

			if (m_FreeLists[index] == nullptr)
			{
				ClearSecondLevelBit(firstLevelIndex, secondLevelIndex);

				if (m_SecondLevelBitmaps[firstLevelIndex] == 0)
					ClearFirstLevelBit(firstLevelIndex);
			}
		}

		if (b->nextFree != nullptr)
			b->nextFree->prevFree = b->prevFree;

		b->nextFree = nullptr;
		b->prevFree = nullptr;
	}

	Allocator::Block* Allocator::GetNextPhysicalBlock(Block* b) noexcept
	{
		const std::size_t offset = GetBlockOffset(b) + sizeof(Block) + b->size;
		if (offset >= m_TotalSize)
			return nullptr;
		return GetBlockFromOffset(offset);
	}

	const Allocator::Block* Allocator::GetNextPhysicalBlock(const Block* b) const noexcept
	{
		const std::size_t offset = GetBlockOffset(b) + sizeof(Block) + b->size;
		if (offset >= m_TotalSize)
			return nullptr;
		return GetBlockFromOffset(offset);
	}

	Allocator::Block* Allocator::GetPrevPhysicalBlock(Block* b) noexcept
	{
		if (!b->PrevIsFree() || b->prevPhysicalSize == 0)
			return nullptr;

		const std::size_t currentOffset = GetBlockOffset(b);
		if (currentOffset < sizeof(Block) + b->prevPhysicalSize)
			return nullptr;

		const std::size_t prevOffset = currentOffset - sizeof(Block) - b->prevPhysicalSize;
		return GetBlockFromOffset(prevOffset);
	}

	const Allocator::Block* Allocator::GetPrevPhysicalBlock(const Block* b) const noexcept
	{
		if (!b->PrevIsFree() || b->prevPhysicalSize == 0)
			return nullptr;

		const std::size_t currentOffset = GetBlockOffset(b);
		if (currentOffset < sizeof(Block) + b->prevPhysicalSize)
			return nullptr;

		const std::size_t prevOffset = currentOffset - sizeof(Block) - b->prevPhysicalSize;
		return GetBlockFromOffset(prevOffset);
	}

	bool Allocator::IsLastBlock(const Block* b) const noexcept
	{
		const std::size_t offset = GetBlockOffset(b) + sizeof(Block) + b->size;
		return offset >= m_TotalSize;
	}

	void Allocator::MarkAllocated(Block* b) noexcept
	{
		b->SetFree(false);

		Block* next = GetNextPhysicalBlock(b);
		if (next != nullptr)
			next->SetPrevFree(false);
	}

	void Allocator::MarkFree(Block* b) noexcept
	{
		b->SetFree(true);

		Block* next = GetNextPhysicalBlock(b);
		if (next != nullptr)
		{
			next->SetPrevFree(true);
			next->prevPhysicalSize = b->size;
		}
	}

	bool Allocator::IsFree(const Block* b) const noexcept
	{
		return b->IsFree();
	}

	bool Allocator::FindNextFreeList(UInt32& firstLevelIndex, UInt32& secondLevelIndex) const noexcept
	{
		UInt64 secondLevelBitmap = m_SecondLevelBitmaps[firstLevelIndex] & (~UInt64{0} << secondLevelIndex);

		if (secondLevelBitmap != 0)
		{
			secondLevelIndex = 63 - CountLeadingZeros64(secondLevelBitmap & -static_cast<Int64>(secondLevelBitmap));
			return true;
		}

		UInt64 firstLevelBitmap = m_FirstLevelBitmap & (~UInt64{0} << (firstLevelIndex + 1));

		if (firstLevelBitmap == 0)
			return false;

		firstLevelIndex = 63 - CountLeadingZeros64(firstLevelBitmap & -static_cast<Int64>(firstLevelBitmap));

		secondLevelBitmap = m_SecondLevelBitmaps[firstLevelIndex];
		if (secondLevelBitmap == 0)
		{
			return false;
		}

		secondLevelIndex = 63 - CountLeadingZeros64(secondLevelBitmap & -static_cast<Int64>(secondLevelBitmap));
		return true;
	}

	Allocator::Block* Allocator::FindSuitable(std::size_t size, std::size_t alignment) noexcept
	{
		if (size < MinBlockSize)
			size = MinBlockSize;

		const std::size_t searchSize = size + alignment + sizeof(Block);

		UInt32 firstLevelIndex, secondLevelIndex;
		Mapping(searchSize, firstLevelIndex, secondLevelIndex);

		if (!FindNextFreeList(firstLevelIndex, secondLevelIndex))
			return nullptr;

		const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
		Block* candidate = m_FreeLists[index];

		while (candidate != nullptr)
		{
			const std::size_t blockStart = GetPayloadOffset(candidate);
			const std::size_t alignedStart = AlignUp(blockStart, alignment);
			const std::size_t alignmentPadding = alignedStart - blockStart;
			const std::size_t totalNeeded = alignmentPadding + size;

			if (candidate->size >= totalNeeded)
				return candidate;

			candidate = candidate->nextFree;
		}

		firstLevelIndex++;
		secondLevelIndex = 0;

		while (firstLevelIndex < m_FirstLevelCount)
		{
			if (!FindNextFreeList(firstLevelIndex, secondLevelIndex))
				break;

			const std::size_t idx = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
			candidate = m_FreeLists[idx];

			while (candidate != nullptr)
			{
				const std::size_t blockStart = GetPayloadOffset(candidate);
				const std::size_t alignedStart = AlignUp(blockStart, alignment);
				const std::size_t alignmentPadding = alignedStart - blockStart;
				const std::size_t totalNeeded = alignmentPadding + size;

				if (candidate->size >= totalNeeded)
					return candidate;

				candidate = candidate->nextFree;
			}

			firstLevelIndex++;
		}

		return nullptr;
	}

	void Allocator::SplitBlock(Block* b, std::size_t needed, Block*& remainder) noexcept
	{
		remainder = nullptr;

		if (b->size < needed + sizeof(Block) + MinBlockSize)
			return;

		const std::size_t remainderSize = b->size - needed - sizeof(Block);
		const std::size_t remainderOffset = GetBlockOffset(b) + sizeof(Block) + needed;

		remainder = GetBlockFromOffset(remainderOffset);
		remainder->size = remainderSize;
		remainder->prevPhysicalSize = needed;
		remainder->flags.Clear();
		remainder->SetFree(true);
		remainder->SetPrevFree(false);
		remainder->nextFree = nullptr;
		remainder->prevFree = nullptr;

		b->size = needed;

		Block* next = GetNextPhysicalBlock(remainder);
		if (next != nullptr)
		{
			next->prevPhysicalSize = remainderSize;
			next->SetPrevFree(true);
		}
	}

	void Allocator::Coalesce(Block* b) noexcept
	{
		Block* next = GetNextPhysicalBlock(b);
		if (next != nullptr && next->IsFree())
		{
			RemoveFree(next);

			b->size += sizeof(Block) + next->size;

			Block* afterNext = GetNextPhysicalBlock(b);
			if (afterNext != nullptr)
			{
				afterNext->prevPhysicalSize = b->size;
				afterNext->SetPrevFree(true);
			}
		}

		Block* prev = GetPrevPhysicalBlock(b);
		if (prev != nullptr && prev->IsFree())
		{
			RemoveFree(prev);

			prev->size += sizeof(Block) + b->size;

			Block* nextAfterMerge = GetNextPhysicalBlock(prev);
			if (nextAfterMerge != nullptr)
			{
				nextAfterMerge->prevPhysicalSize = prev->size;
				nextAfterMerge->SetPrevFree(true);
			}

			b = prev;
		}

		InsertFree(b);
	}

	bool Allocator::Allocate(std::size_t size, std::size_t alignment, Allocation& out) noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (!m_Initialized || size == 0)
			return false;

		if (!IsPow2(alignment))
			return false;

		if (alignment > MaxAlignment)
			return false;

		Block* block = FindSuitable(size, alignment);
		if (block == nullptr)
			return false;

		RemoveFree(block);

		const std::size_t blockStart = GetPayloadOffset(block);
		const std::size_t alignedStart = AlignUp(blockStart, alignment);
		const std::size_t alignmentPadding = alignedStart - blockStart;

		if (alignmentPadding > 0)
		{
			if (alignmentPadding >= sizeof(Block) + MinBlockSize)
			{
				const std::size_t originalSize = block->size;

				Block* paddingBlock = block;
				const std::size_t paddingSize = alignmentPadding - sizeof(Block);

				paddingBlock->size = paddingSize;
				MarkFree(paddingBlock);
				InsertFree(paddingBlock);

				const std::size_t alignedBlockOffset = GetBlockOffset(paddingBlock) + sizeof(Block) + paddingSize;
				block = GetBlockFromOffset(alignedBlockOffset);
				block->size = originalSize - sizeof(Block) - paddingSize;
				block->prevPhysicalSize = paddingSize;
				block->flags.Clear();
				block->SetPrevFree(true);

				Block* next = GetNextPhysicalBlock(block);
				if (next != nullptr)
					next->prevPhysicalSize = block->size;
			}
			else
			{
				InsertFree(block);
				return false;
			}
		}

		Block* remainder = nullptr;
		SplitBlock(block, size, remainder);

		if (remainder != nullptr)
			InsertFree(remainder);

		MarkAllocated(block);

		m_UsedSize += sizeof(Block) + block->size;

		out.offset = GetPayloadOffset(block);
		out.size = block->size;

		return true;
	}

	void Allocator::Free(const Allocation& alloc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (!m_Initialized || alloc.offset == 0)
			return;

		const std::size_t blockOffset = alloc.offset - sizeof(Block);
		Block* block = GetBlockFromOffset(blockOffset);

		if (block->IsFree())
			return;

		m_UsedSize -= sizeof(Block) + block->size;

		MarkFree(block);

		Coalesce(block);
	}

	bool Allocator::ReallocateInPlace(Allocation& inOut, std::size_t newSize) noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (!m_Initialized || inOut.offset == 0 || newSize == 0)
			return false;

		const std::size_t blockOffset = inOut.offset - sizeof(Block);
		Block* block = GetBlockFromOffset(blockOffset);

		if (block->IsFree())
			return false;

		const std::size_t currentSize = block->size;

		if (newSize == currentSize)
			return true;

		if (newSize < currentSize)
		{
			const std::size_t shrinkAmount = currentSize - newSize;

			if (shrinkAmount >= sizeof(Block) + MinBlockSize)
			{
				Block* remainder = nullptr;
				SplitBlock(block, newSize, remainder);

				if (remainder != nullptr)
				{
					m_UsedSize -= sizeof(Block) + remainder->size;
					MarkFree(remainder);
					Coalesce(remainder);
				}

				inOut.size = block->size;
				return true;
			}

			return true;
		}

		Block* next = GetNextPhysicalBlock(block);
		if (next == nullptr || !next->IsFree())
			return false;

		const std::size_t availableSize = currentSize + sizeof(Block) + next->size;
		if (availableSize < newSize)
			return false;

		RemoveFree(next);

		block->size += sizeof(Block) + next->size;

		Block* afterNext = GetNextPhysicalBlock(block);
		if (afterNext != nullptr)
		{
			afterNext->prevPhysicalSize = block->size;
			afterNext->SetPrevFree(false);
		}

		Block* remainder = nullptr;
		SplitBlock(block, newSize, remainder);

		if (remainder != nullptr)
		{
			MarkFree(remainder);
			InsertFree(remainder);
		}
		else
			m_UsedSize += sizeof(Block) + next->size;

		inOut.size = block->size;
		return true;
	}

	std::size_t Allocator::GetLargestFreeBlock() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (!m_Initialized || m_FirstLevelBitmap == 0)
			return 0;

		UInt32 firstLevelIndex = FindLastSet64(m_FirstLevelBitmap);

		if (m_SecondLevelBitmaps[firstLevelIndex] == 0)
			return 0;

		UInt32 secondLevelIndex = FindLastSet64(m_SecondLevelBitmaps[firstLevelIndex]);

		const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
		const Block* block = m_FreeLists[index];

		std::size_t largest = 0;
		while (block != nullptr)
		{
			if (block->size > largest)
				largest = block->size;
			block = block->nextFree;
		}

		return largest;
	}

	double Allocator::GetExternalFragmentation() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (!m_Initialized || m_UsedSize >= m_TotalSize)
			return 0.0;

		const std::size_t freeSpace = m_TotalSize - m_UsedSize;
		if (freeSpace == 0)
			return 0.0;

		if (!m_Initialized || m_FirstLevelBitmap == 0)
			return 0.0;

		UInt32 firstLevelIndex = FindLastSet64(m_FirstLevelBitmap);

		if (m_SecondLevelBitmaps[firstLevelIndex] == 0)
			return 0.0;

		UInt32 secondLevelIndex = FindLastSet64(m_SecondLevelBitmaps[firstLevelIndex]);

		const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
		const Block* block = m_FreeLists[index];

		std::size_t largestFree = 0;
		while (block != nullptr)
		{
			if (block->size > largestFree)
				largestFree = block->size;
			block = block->nextFree;
		}

		if (largestFree == 0)
			return 1.0;

		return 1.0 - (static_cast<double>(largestFree) / static_cast<double>(freeSpace));
	}

	void Allocator::DumpState(std::ostream& os) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		std::size_t largestFree = 0;
		if (m_Initialized && m_FirstLevelBitmap != 0)
		{
			UInt32 firstLevelIndex = FindLastSet64(m_FirstLevelBitmap);
			if (m_SecondLevelBitmaps[firstLevelIndex] != 0)
			{
				UInt32 secondLevelIndex = FindLastSet64(m_SecondLevelBitmaps[firstLevelIndex]);
				const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
				const Block* block = m_FreeLists[index];
				while (block != nullptr)
				{
					if (block->size > largestFree)
						largestFree = block->size;
					block = block->nextFree;
				}
			}
		}

		double fragmentation = 0.0;
		if (m_Initialized && m_UsedSize < m_TotalSize)
		{
			const std::size_t freeSpace = m_TotalSize - m_UsedSize;
			if (freeSpace > 0 && largestFree > 0)
				fragmentation = 1.0 - (static_cast<double>(largestFree) / static_cast<double>(freeSpace));
			else if (freeSpace > 0 && largestFree == 0)
				fragmentation = 1.0;
		}

		os << "=== Two-Level Segregate Fit Allocator State ===\n";
		os << "Total Size: " << m_TotalSize << " bytes\n";
		os << "Used Size: " << m_UsedSize << " bytes\n";
		os << "Free Size: " << (m_TotalSize - m_UsedSize) << " bytes\n";
		os << "Largest Free Block: " << largestFree << " bytes\n";
		os << "External Fragmentation: " << (fragmentation * 100.0) << "%\n";
		os << "\n";

		os << "First Level Bitmap: 0x" << std::hex << m_FirstLevelBitmap << std::dec << "\n";
		os << "\n";

		for (UInt32 firstLevelIndex = 0; firstLevelIndex < m_FirstLevelCount; ++firstLevelIndex)
		{
			if ((m_FirstLevelBitmap & (UInt64{1} << firstLevelIndex)) == 0)
				continue;

			os << "FirstLevel[" << firstLevelIndex << "] (SecondLevel Bitmap: 0x" << std::hex << m_SecondLevelBitmaps[firstLevelIndex] << std::dec << ")\n";

			for (UInt32 secondLevelIndex = 0; secondLevelIndex < m_SecondLevelCount; ++secondLevelIndex)
			{
				if ((m_SecondLevelBitmaps[firstLevelIndex] & (UInt64{1} << secondLevelIndex)) == 0)
					continue;

				const std::size_t index = GetFreeListIndex(firstLevelIndex, secondLevelIndex);
				const Block* block = m_FreeLists[index];

				os << "  SecondLevel[" << secondLevelIndex << "]: ";

				std::size_t count = 0;
				while (block != nullptr)
				{
					if (count > 0)
						os << " -> ";
					os << "[offset=" << GetBlockOffset(block) << ", size=" << block->size << "]";
					block = block->nextFree;
					++count;
				}

				os << " (count: " << count << ")\n";
			}
		}

		os << "\n=== End State ===\n";
	}

} // namespace vkd

CCT_ENABLE_ENUM_FLAGS(vkd::BlockFlag)
