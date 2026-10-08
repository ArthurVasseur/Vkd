#include "VkdAmd/CommandPool/CommandPool.hpp"

#include "Vkd/Memory/Memory.hpp"

#include "VkdAmd/CommandBuffer/CommandBuffer.hpp"

namespace vkd::amd
{
	DispatchableObjectResult<vkd::CommandBuffer> CommandPool::CreateCommandBuffer(VkCommandBufferLevel level)
	{
		(void)level;
		auto* buffer = mem::NewObjectDispatchable<CommandBuffer>(GetAllocationCallbacks());
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandBuffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		return reinterpret_cast<DispatchableObject<vkd::CommandBuffer>*>(buffer);
	}
} // namespace vkd::amd
