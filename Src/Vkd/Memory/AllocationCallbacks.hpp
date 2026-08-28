/**
 * @file AllocationCallbacks.hpp
 * @brief Driver provided host allocation callbacks
 * @date 2026-08-27
 */

#pragma once
#include "Vkd/Defines.hpp"

namespace vkd::mem
{
	const VkAllocationCallbacks& GetDefaultAllocationCallbacks() noexcept;
} // namespace vkd::mem
