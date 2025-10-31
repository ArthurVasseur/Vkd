/**
 * @file CommandPool.cpp
 * @brief Implementation of Vulkan command pool
 * @date 2025-10-27
 */

#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd
{
	DispatchableObjectResult<CommandBuffer> CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		return CreateCommandBuffer(level);
	}
}
