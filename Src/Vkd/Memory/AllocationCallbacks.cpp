/**
 * @file AllocationCallbacks.cpp
 * @brief Implementation of the driver provided host allocation callbacks
 * @date 2026-08-27
 */

#include "Vkd/Memory/AllocationCallbacks.hpp"

#include <mimalloc.h>

namespace vkd::mem
{
	namespace
	{
		void* VKAPI_PTR DefaultAllocation(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
		{
			VKD_AUTO_PROFILER_SCOPE();

			void* allocation = mi_malloc_aligned(size, alignment);
			if (!allocation)
			{
				CCT_ASSERT_FALSE("Could not allocate memory: size={}, alignment={}", size, alignment);
				return nullptr;
			}

			return allocation;
		}

		void* VKAPI_PTR DefaultReallocation(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
		{
			VKD_AUTO_PROFILER_SCOPE();

			void* allocation = mi_realloc_aligned(pOriginal, size, alignment);
			if (!allocation)
			{
				CCT_ASSERT_FALSE("Could not allocate memory: size={}, alignment={}", size, alignment);
				return nullptr;
			}

			return allocation;
		}

		void VKAPI_PTR DefaultFree(void* pUserData, void* pMemory)
		{
			VKD_AUTO_PROFILER_SCOPE();

			mi_free(pMemory);
		}

		constexpr VkAllocationCallbacks DefaultAllocationCallbacks = {
			.pUserData = nullptr,
			.pfnAllocation = DefaultAllocation,
			.pfnReallocation = DefaultReallocation,
			.pfnFree = DefaultFree,
			.pfnInternalAllocation = nullptr,
			.pfnInternalFree = nullptr};
	} // namespace

	const VkAllocationCallbacks& GetDefaultAllocationCallbacks() noexcept
	{
		return DefaultAllocationCallbacks;
	}
} // namespace vkd::mem
