/**
 * @file CommandPool.cpp
 * @brief Implementation of Vulkan command pool
 * @date 2025-10-27
 */

#include "Vkd/CommandPool/CommandPool.hpp"

#include <algorithm>

#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	CommandPool::~CommandPool()
	{
		for (auto* commandBuffer : m_commandBuffers)
		{
			if (commandBuffer)
				mem::DeleteDispatchable(commandBuffer);
		}
		m_commandBuffers.clear();
	}

	DispatchableObjectResult<CommandBuffer> CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		return CreateCommandBuffer(level);
	}

	void CommandPool::AddCommandBuffer(DispatchableObject<CommandBuffer>* commandBuffer)
	{
		m_commandBuffers.push_back(commandBuffer);
	}

	void CommandPool::RemoveCommandBuffer(DispatchableObject<CommandBuffer>* commandBuffer)
	{
		auto it = std::find(m_commandBuffers.begin(), m_commandBuffers.end(), commandBuffer);
		if (it != m_commandBuffers.end())
			m_commandBuffers.erase(it);
	}

	VkResult CommandPool::Reset(VkCommandPoolResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		(void)flags;

		for (auto* commandBuffer : m_commandBuffers)
		{
			if (!commandBuffer || !commandBuffer->Object)
				continue;

			const VkResult result = commandBuffer->Object->Reset(0);
			if (result != VK_SUCCESS)
				return result;
		}
		return VK_SUCCESS;
	}
} // namespace vkd
