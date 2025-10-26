//
// Created by arthur on 25/10/2025.
//

#include "VkdSoftware/CommandPool/CommandPool.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd::software
{
	VkResult CommandPool::Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE;

		return vkd::CommandPool::Create(owner, createInfo, pAllocator);
	}

	VkResult CommandPool::Reset(VkCommandPoolResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE;

		// TODO: implement CPU execution - reset all command buffers in pool
		return VK_SUCCESS;
	}

	DispatchableObjectResult<vkd::CommandBuffer> CommandPool::DoCreateCommandBuffer(VkCommandBufferLevel level)
	{
		VKD_AUTO_PROFILER_SCOPE;

		auto* buffer = mem::NewDispatchable<CommandBuffer>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandBuffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::CommandBuffer>*>(buffer);
	}
}
