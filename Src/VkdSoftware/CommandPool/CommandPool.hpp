//
// Created by arthur on 25/10/2025.
//

#pragma once

#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd::software
{
	class CommandPool : public vkd::CommandPool
	{
	public:
		CommandPool() = default;
		~CommandPool() override = default;

		VkResult Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator) override;

	protected:
		VkResult Reset(VkCommandPoolResetFlags flags) override;
		DispatchableObjectResult<vkd::CommandBuffer> DoCreateCommandBuffer(VkCommandBufferLevel level) override;

	private:
		// TODO: implement CPU command buffer pool management
	};
}
