/**
 * @file Allocator.hpp
 * @brief Standard library compatible allocator backed by VkAllocationCallbacks
 * @date 2026-08-27
 */

#pragma once
#include <cstddef>
#include <type_traits>

#include "Vkd/Defines.hpp"
#include "Vkd/Memory/AllocationCallbacks.hpp"

namespace vkd
{
	template<typename T>
	class allocator
	{
	public:
		using value_type = T;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using propagate_on_container_copy_assignment = std::false_type;
		using propagate_on_container_move_assignment = std::true_type;
		using propagate_on_container_swap = std::true_type;
		using is_always_equal = std::false_type;

		template<typename U>
		struct rebind
		{
			using other = allocator<U>;
		};

		allocator() = delete;
		inline allocator(const VkAllocationCallbacks& allocationCallbacks, VkSystemAllocationScope allocationScope = VK_SYSTEM_ALLOCATION_SCOPE_OBJECT) noexcept;
		template<typename U>
		inline allocator(const allocator<U>& other) noexcept;

		[[nodiscard]] inline T* allocate(std::size_t count);
		inline void deallocate(T* memory, std::size_t count) noexcept;

		[[nodiscard]] inline std::size_t max_size() const noexcept;

		[[nodiscard]] inline const VkAllocationCallbacks& GetAllocationCallbacks() const noexcept;
		[[nodiscard]] inline VkSystemAllocationScope GetAllocationScope() const noexcept;

		template<typename U>
		[[nodiscard]] inline bool operator==(const allocator<U>& other) const noexcept;

	private:
		const VkAllocationCallbacks* m_allocationCallbacks;
		VkSystemAllocationScope m_allocationScope;
	};

	// Scope pre-filled factories, so call sites state their lifetime class without spelling the enum
	template<typename T>
	[[nodiscard]] inline allocator<T> CommandAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept;

	template<typename T>
	[[nodiscard]] inline allocator<T> ObjectAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept;

	template<typename T>
	[[nodiscard]] inline allocator<T> CacheAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept;

	template<typename T>
	[[nodiscard]] inline allocator<T> DeviceAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept;

	template<typename T>
	[[nodiscard]] inline allocator<T> InstanceAllocator(const VkAllocationCallbacks& allocationCallbacks) noexcept;
} // namespace vkd

#include "Vkd/Memory/Allocator.inl"
