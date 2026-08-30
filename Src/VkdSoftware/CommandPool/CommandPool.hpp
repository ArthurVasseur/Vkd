/**
 * @file CommandPool.hpp
 * @brief Software renderer command pool implementation
 * @date 2025-10-25
 *
 * Command pool implementation for CPU-based command buffer allocation.
 */

#pragma once

#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd::software
{
	class CommandPool : public vkd::CommandPool
	{
	public:
		using vkd::CommandPool::CommandPool;
		~CommandPool() override = default;

		VkResult Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator) override;

	protected:
		VkResult Reset(VkCommandPoolResetFlags flags) override;
		DispatchableObjectResult<vkd::CommandBuffer> CreateCommandBuffer(VkCommandBufferLevel level) override;
	};
} // namespace vkd::software
