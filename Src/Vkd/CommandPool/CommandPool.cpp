//
// Created by arthur on 27/10/2025.
//

#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd
{
	DispatchableObjectResult<CommandBuffer> CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		return CreateCommandBuffer(level);
	}
}
