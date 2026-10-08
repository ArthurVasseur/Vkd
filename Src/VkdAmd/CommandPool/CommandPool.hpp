#pragma once

#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd::amd
{
	class CommandPool : public vkd::CommandPool
	{
	public:
		using vkd::CommandPool::CommandPool;

	protected:
		DispatchableObjectResult<vkd::CommandBuffer> CreateCommandBuffer(VkCommandBufferLevel level) override;
	};
} // namespace vkd::amd
