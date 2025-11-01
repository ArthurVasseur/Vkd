/**
 * @file Allocator.inl
 * @brief Inline implementations for TLSF allocator helper functions
 * @date 2025-10-30
 */

#pragma once

namespace vkd
{

inline std::size_t Allocator::GetTotal() const noexcept
{
	return m_TotalSize;
}

inline std::size_t Allocator::GetUsed() const noexcept
{
	return m_UsedSize;
}

inline UInt8* Allocator::GetPoolBase() noexcept
{
	return m_Pool.data();
}

inline bool Allocator::IsPow2(std::size_t x) noexcept
{
	return (x != 0) && ((x & (x - 1)) == 0);
}

inline std::size_t Allocator::AlignUp(std::size_t x, std::size_t alignment) noexcept
{
	return (x + alignment - 1) & ~(alignment - 1);
}

inline std::size_t Allocator::GetFreeListIndex(UInt32 firstLevelIndex, UInt32 secondLevelIndex) const noexcept
{
	return firstLevelIndex * m_SecondLevelCount + secondLevelIndex;
}

inline void Allocator::SetFirstLevelBit(UInt32 firstLevelIndex) noexcept
{
	m_FirstLevelBitmap |= (UInt64{1} << firstLevelIndex);
}

inline void Allocator::ClearFirstLevelBit(UInt32 firstLevelIndex) noexcept
{
	m_FirstLevelBitmap &= ~(UInt64{1} << firstLevelIndex);
}

inline void Allocator::SetSecondLevelBit(UInt32 firstLevelIndex, UInt32 secondLevelIndex) noexcept
{
	m_SecondLevelBitmaps[firstLevelIndex] |= (UInt64{1} << secondLevelIndex);
}

inline void Allocator::ClearSecondLevelBit(UInt32 firstLevelIndex, UInt32 secondLevelIndex) noexcept
{
	m_SecondLevelBitmaps[firstLevelIndex] &= ~(UInt64{1} << secondLevelIndex);
}

} // namespace vkd
