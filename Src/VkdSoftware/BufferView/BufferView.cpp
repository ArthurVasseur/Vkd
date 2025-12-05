/**
 * @file BufferView.cpp
 * @brief Implementation of software buffer view
 * @date 2025-12-05
 */

#include "VkdSoftware/BufferView/BufferView.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult BufferView::Create(vkd::Device& owner, const VkBufferViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		return vkd::BufferView::Create(owner, info, allocationCallbacks);
	}
} // namespace vkd::software
