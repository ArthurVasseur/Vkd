/**
 * @file BufferView.hpp
 * @brief Software renderer buffer view implementation
 * @date 2025-12-05
 *
 * Buffer view implementation for CPU rendering.
 */

#pragma once

#include "Vkd/BufferView/BufferView.hpp"

namespace vkd::software
{
	class BufferView : public vkd::BufferView
	{
	public:
		BufferView() = default;
		~BufferView() override = default;

		VkResult Create(vkd::Device& owner, const VkBufferViewCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks) override;
	};
} // namespace vkd::software
