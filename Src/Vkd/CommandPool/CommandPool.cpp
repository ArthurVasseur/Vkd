#include "CommandPool.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	DispatchableObjectResult<CommandBuffer> CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		return DoCreateCommandBuffer(level);
	}
}
