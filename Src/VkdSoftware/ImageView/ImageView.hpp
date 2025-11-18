/**
 * @file ImageView.hpp
 * @brief Software renderer image view implementation
 * @date 2025-11-18
 *
 * Image view implementation for CPU rendering.
 */

#pragma once

#include "Vkd/ImageView/ImageView.hpp"

namespace vkd::software
{
	class ImageView : public vkd::ImageView
	{
	public:
		ImageView() = default;
		~ImageView() override = default;

		VkResult Create(Device& owner, const VkImageViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
} // namespace vkd::software
