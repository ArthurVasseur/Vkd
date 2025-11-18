/**
 * @file ImageView.cpp
 * @brief Implementation of software image view
 * @date 2025-11-18
 */

#include "VkdSoftware/ImageView/ImageView.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	VkResult ImageView::Create(Device& owner, const VkImageViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		return vkd::ImageView::Create(owner, info, allocationCallbacks);
	}
} // namespace vkd::software
