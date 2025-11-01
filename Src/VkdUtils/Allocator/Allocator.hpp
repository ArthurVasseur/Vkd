/**
 * @file Allocator.hpp
 * @brief Two-Level Segregate Fit (TLSF) allocator - Generic memory allocator with O(1) operations
 * @date 2025-10-30
 *
 * TLSF allocator provides constant-time allocation and deallocation with minimal fragmentation.
 * Features:
 * - O(1) amortized allocation/deallocation
 * - Single contiguous memory pool
 * - No dynamic allocations after Init()
 * - Configurable alignment support (powers of 2 up to 4096)
 * - Split and coalesce operations in O(1)
 * - Thread-unsafe by default (single-threaded)
 */

#pragma once

#include <cstddef>
#include <iosfwd>
#include <vector>
#include <Concerto/Core/Types/Types.hpp>

// Alignment macro
#if defined(CCT_PLATFORM_WINDOWS)
	#define VKD_ALIGN(x) __declspec(align(x))
#elif defined(CCT_PLATFORM_POSIX)
	#define VKD_ALIGN(x) __attribute__((aligned(x)))
#else
	#define VKD_ALIGN(x) alignas(x)
#endif

namespace vkd
{

using namespace cct;

/**
 * @struct Allocation
 * @brief Represents a memory allocation result
 */
struct Allocation
{
	std::size_t offset;	// Offset from the beginning of the pool
	std::size_t size;	// Size of the allocation in bytes
};

/**
 * @class Allocator
 * @brief TLSF memory allocator with O(1) operations
 *
 * This allocator manages a single contiguous memory pool with constant-time
 * allocation and deallocation. It uses a two-level segregated fit approach
 * with bitmaps for fast free block lookups.
 *
 * ============================================================================
 * MEMORY LAYOUT AND BLOCK STRUCTURE
 * ============================================================================
 *
 * The allocator manages a contiguous pool of memory organized into blocks.
 * Each block has a header followed by a payload area.
 *
 * PHYSICAL LAYOUT IN MEMORY:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  Pool (contiguous memory)                                           │
 * ├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┤
 * │   Block 1   │   Block 2   │   Block 3   │   Block 4   │   Block 5   │
 * │  (Alloc)    │   (Free)    │  (Alloc)    │   (Free)    │   (Free)    │
 * └─────────────┴─────────────┴─────────────┴─────────────┴─────────────┘
 *
 * BLOCK STRUCTURE (48 bytes header, aligned to 16 bytes):
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ Block Header (48 bytes)                                          │
 * ├────────────────────────────────────────────────────────┬─────────┤
 * │ size              (8 bytes)  - Payload size            │         │
 * │ prevPhysicalSize  (8 bytes)  - Prev block payload size │         │
 * │ flags             (4 bytes)  - IsFree, PrevIsFree      │  Header │
 * │ padding           (4 bytes)  - Alignment padding       │  (48B)  │
 * │ nextFree          (8 bytes)  - Next in free list       │         │
 * │ prevFree          (8 bytes)  - Prev in free list       │         │
 * ├────────────────────────────────────────────────────────┴─────────┤
 * │ Payload (size bytes)                                             │
 * │ - For allocated blocks: user data                                │
 * │ - For free blocks: unused (nextFree/prevFree in header)          │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * ALLOCATED BLOCK:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ size = 128                                                       │
 * │ prevPhysicalSize = 64  (if previous block exists)                │
 * │ flags = 0 (IsFree=0, PrevIsFree depends on prev block)           │
 * │ nextFree = nullptr (unused)                                      │
 * │ prevFree = nullptr (unused)                                      │
 * ├──────────────────────────────────────────────────────────────────┤
 * │ User Data (128 bytes)                                            │
 * │ ← Pointer returned to user points here                           │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * FREE BLOCK:
 * ┌──────────────────────────────────────────────────────────────────┐
 * │ size = 256                                                       │
 * │ prevPhysicalSize = 128 (if previous block exists)                │
 * │ flags = 1 (IsFree=1, PrevIsFree depends on prev block)           │
 * │ nextFree = 0x1234 → (points to next free block in list)          │
 * │ prevFree = 0x5678 ← (points to prev free block in list)          │
 * ├──────────────────────────────────────────────────────────────────┤
 * │ Unused payload (256 bytes)                                       │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * PHYSICAL CHAINING (bidirectional traversal):
 * ┌─────────┐     ┌─────────┐     ┌─────────┐
 * │ Block A │ ──> │ Block B │ ──> │ Block C │
 * │         │ <── │         │ <── │         │
 * └─────────┘     └─────────┘     └─────────┘
 *   Forward: offset += sizeof(Header) + size
 *   Backward: offset -= sizeof(Header) + prevPhysicalSize
 *
 * FREE LIST CHAINING (doubly-linked lists per size class):
 * Free blocks are organized into 1024 segregated free lists (32 FLI × 32 SLI).
 *
 * Example free list for size class [128-159]:
 * ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 * │ Free Block  │ ──> │ Free Block  │ ──> │ Free Block  │
 * │ size=128    │ <── │ size=144    │ <── │ size=156    │
 * │ nextFree ───┼──>  │ nextFree ───┼──>  │ nextFree=0  │
 * │ prevFree=0  │  <──┼─ prevFree   │  <──┼─ prevFree   │
 * └─────────────┘     └─────────────┘     └─────────────┘
 *
 * COALESCING (merging adjacent free blocks):
 * Before:
 * ┌─────────┐ ┌─────────┐ ┌─────────┐
 * │ Alloc   │ │  Free   │ │  Free   │
 * │ 64B     │ │  128B   │ │  256B   │
 * └─────────┘ └─────────┘ └─────────┘
 *
 * After freeing first block and coalescing:
 * ┌───────────────────────────────────┐
 * │          Free                     │
 * │          448B (64+128+256)        │
 * └───────────────────────────────────┘
 *
 * SPLITTING (dividing a large block):
 * Before allocation of 64B from 256B free block:
 * ┌─────────────────────────────────┐
 * │          Free                   │
 * │          256B                   │
 * └─────────────────────────────────┘
 *
 * After:
 * ┌─────────────┐ ┌─────────────────┐
 * │   Alloc     │ │      Free       │
 * │   64B       │ │   144B (256-64) │
 * └─────────────┘ └─────────────────┘
 *                   (minus header overhead)
 *
 * TWO-LEVEL SEGREGATED FIT:
 * First Level Index (FLI): log2(size) - groups by power of 2
 * Second Level Index (SLI): linear subdivision within FLI range
 *
 * Example with 5-bit FLI and 5-bit SLI:
 * FLI=5 (sizes 32-63):    [32][33][34]...[63]  (32 SLI buckets)
 * FLI=6 (sizes 64-127):   [64][66][68]...[127] (32 SLI buckets)
 * FLI=7 (sizes 128-255):  [128][132][136]...[255]
 * ...
 * Total: 32 FLI × 32 SLI = 1024 free lists
 *
 * Bitmaps enable O(1) lookup of non-empty free lists:
 * - First-level bitmap (64-bit): tracks which FLI have free blocks
 * - Second-level bitmaps (32 × 64-bit): tracks which SLI have free blocks per FLI
 *
 * ============================================================================
 */
class Allocator
{
public:
	/// Minimum block size in bytes (including header)
	static constexpr std::size_t MinBlockSize = 32;

	/// First level index bits (default: 5 bits = 32 first-level classes)
	static constexpr UInt32 DefaultFirstLevelIndexBits = 5;

	/// Second level index bits (default: 5 bits = 32 second-level classes per first-level)
	static constexpr UInt32 DefaultSecondLevelIndexBits = 5;

	/// Maximum alignment supported
	static constexpr std::size_t MaxAlignment = 4096;

	/// Block header alignment
	static constexpr std::size_t BlockAlignment = 16;

	/**
	 * @brief Construct an allocator with a specified pool size
	 * @param poolSizeBytes Total size of the memory pool in bytes
	 */
	explicit Allocator(std::size_t poolSizeBytes) noexcept;
	~Allocator() noexcept = default;

	Allocator(const Allocator&) = delete;
	Allocator& operator=(const Allocator&) = delete;
	Allocator(Allocator&&) = delete;
	Allocator& operator=(Allocator&&) = delete;

	/**
	 * @brief Initialize the allocator and set up internal structures
	 * @return true if initialization succeeded, false otherwise
	 * @note Must be called before any allocation operations
	 * @note No dynamic allocations will occur after this call
	 */
	bool Init() noexcept;

	/**
	 * @brief Allocate memory with specified size and alignment
	 * @param size Size in bytes to allocate (must be > 0)
	 * @param alignment Alignment requirement (must be power of 2)
	 * @param out Output allocation result
	 * @return true if allocation succeeded, false otherwise
	 */
	bool Allocate(std::size_t size, std::size_t alignment, Allocation& out) noexcept;

	/**
	 * @brief Free a previously allocated block
	 * @param alloc The allocation to free
	 * @note The allocation must have been returned by Allocate()
	 */
	void Free(const Allocation& alloc) noexcept;

	/**
	 * @brief Attempt to resize an allocation in-place
	 * @param inOut The allocation to resize (updated on success)
	 * @param newSize The new size in bytes
	 * @return true if reallocation succeeded in-place, false otherwise
	 * @note On failure, the original allocation remains unchanged
	 */
	bool ReallocateInPlace(Allocation& inOut, std::size_t newSize) noexcept;

	std::size_t GetTotal() const noexcept;
	std::size_t GetUsed() const noexcept;
	std::size_t GetLargestFreeBlock() const noexcept;
	double GetExternalFragmentation() const noexcept;
	void DumpState(std::ostream& os) const;
	UInt8* GetPoolBase() noexcept;

private:
	struct Block;

	static bool IsPow2(std::size_t x) noexcept;
	static std::size_t AlignUp(std::size_t x, std::size_t alignment) noexcept;
	static UInt32 FindLastSet64(UInt64 x) noexcept;
	static UInt32 CountLeadingZeros64(UInt64 x) noexcept;

	void Mapping(std::size_t size, UInt32& outFirstLevelIndex, UInt32& outSecondLevelIndex) const noexcept;
	void InsertFree(Block* b) noexcept;
	void RemoveFree(Block* b) noexcept;
	Block* FindSuitable(std::size_t size, std::size_t alignment) noexcept;
	void SplitBlock(Block* b, std::size_t needed, Block*& remainder) noexcept;
	void Coalesce(Block* b) noexcept;

	std::size_t GetBlockOffset(const Block* b) const noexcept;
	Block* GetBlockFromOffset(std::size_t offset) noexcept;
	const Block* GetBlockFromOffset(std::size_t offset) const noexcept;
	std::size_t GetPayloadOffset(const Block* b) const noexcept;

	Block* GetNextPhysicalBlock(Block* b) noexcept;
	const Block* GetNextPhysicalBlock(const Block* b) const noexcept;
	Block* GetPrevPhysicalBlock(Block* b) noexcept;
	const Block* GetPrevPhysicalBlock(const Block* b) const noexcept;
	bool IsLastBlock(const Block* b) const noexcept;

	void MarkAllocated(Block* b) noexcept;
	void MarkFree(Block* b) noexcept;
	bool IsFree(const Block* b) const noexcept;

	std::size_t GetFreeListIndex(UInt32 firstLevelIndex, UInt32 secondLevelIndex) const noexcept;
	void SetFirstLevelBit(UInt32 firstLevelIndex) noexcept;
	void ClearFirstLevelBit(UInt32 firstLevelIndex) noexcept;
	void SetSecondLevelBit(UInt32 firstLevelIndex, UInt32 secondLevelIndex) noexcept;
	void ClearSecondLevelBit(UInt32 firstLevelIndex, UInt32 secondLevelIndex) noexcept;
	bool FindNextFreeList(UInt32& firstLevelIndex, UInt32& secondLevelIndex) const noexcept;

private:
	std::size_t m_TotalSize;
	std::size_t m_UsedSize;
	std::vector<UInt8> m_Pool;
	UInt32 m_FirstLevelIndexBits;
	UInt32 m_SecondLevelIndexBits;
	UInt32 m_FirstLevelCount;
	UInt32 m_SecondLevelCount;
	UInt64 m_FirstLevelBitmap;
	std::vector<UInt64> m_SecondLevelBitmaps;
	std::vector<Block*> m_FreeLists;
	bool m_Initialized;
};

} // namespace vkd

// Include inline implementations
#include "VkdUtils/Allocator/Allocator.inl"
