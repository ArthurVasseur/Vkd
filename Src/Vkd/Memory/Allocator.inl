/**
 * @file Allocator.inl
 * @brief Inline implementations for the VkAllocationCallbacks backed allocator
 * @date 2026-08-27
 */

#pragma once
#include <limits>
#include <new>

#include "Vkd/Memory/Allocator.hpp"

namespace vkd
{
	template<typename T>
	inline allocator<T>::allocator() noexcept :
		m_allocationCallbacks(&mem::GetDefaultAllocationCallbacks()),
		m_allocationScope(VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)
	{
	}

	template<typename T>
	inline allocator<T>::allocator(const VkAllocationCallbacks& allocationCallbacks, VkSystemAllocationScope allocationScope) noexcept :
		m_allocationCallbacks(&allocationCallbacks),
		m_allocationScope(allocationScope)
	{
	}

	template<typename T>
	template<typename U>
	inline allocator<T>::allocator(const allocator<U>& other) noexcept :
		m_allocationCallbacks(&other.GetAllocationCallbacks()),
		m_allocationScope(other.GetAllocationScope())
	{
	}

	template<typename T>
	inline T* allocator<T>::allocate(std::size_t count)
	{
		if (count > max_size())
			throw std::bad_alloc();

		void* memory = m_allocationCallbacks->pfnAllocation(m_allocationCallbacks->pUserData, count * sizeof(T), alignof(T), m_allocationScope);
		if (!memory)
			throw std::bad_alloc();

		return static_cast<T*>(memory);
	}

	template<typename T>
	inline void allocator<T>::deallocate(T* memory, std::size_t count) noexcept
	{
		m_allocationCallbacks->pfnFree(m_allocationCallbacks->pUserData, memory);
	}

	template<typename T>
	inline std::size_t allocator<T>::max_size() const noexcept
	{
		return std::numeric_limits<std::size_t>::max() / sizeof(T);
	}

	template<typename T>
	inline const VkAllocationCallbacks& allocator<T>::GetAllocationCallbacks() const noexcept
	{
		return *m_allocationCallbacks;
	}

	template<typename T>
	inline VkSystemAllocationScope allocator<T>::GetAllocationScope() const noexcept
	{
		return m_allocationScope;
	}

	template<typename T>
	template<typename U>
	inline bool allocator<T>::operator==(const allocator<U>& other) const noexcept
	{
		const VkAllocationCallbacks& rhs = other.GetAllocationCallbacks();
		if (m_allocationCallbacks == &rhs)
			return true;

		return m_allocationCallbacks->pUserData == rhs.pUserData &&
			   m_allocationCallbacks->pfnAllocation == rhs.pfnAllocation &&
			   m_allocationCallbacks->pfnReallocation == rhs.pfnReallocation &&
			   m_allocationCallbacks->pfnFree == rhs.pfnFree;
	}

	template<typename T>
	inline allocator<T> CommandAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept
	{
		return allocator<T>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
	}

	template<typename T>
	inline allocator<T> ObjectAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept
	{
		return allocator<T>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	}

	template<typename T>
	inline allocator<T> CacheAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept
	{
		return allocator<T>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
	}

	template<typename T>
	inline allocator<T> DeviceAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept
	{
		return allocator<T>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
	}

	template<typename T>
	inline allocator<T> InstanceAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept
	{
		return allocator<T>(allocationCallbacks, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	}
} // namespace vkd
