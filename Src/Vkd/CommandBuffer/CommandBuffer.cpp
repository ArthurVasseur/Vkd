#include "CommandBuffer.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	VkResult CommandBuffer::BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
	{
		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pBeginInfo)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		return cmdBuffer->Begin(*pBeginInfo);
	}

	VkResult CommandBuffer::EndCommandBuffer(VkCommandBuffer commandBuffer)
	{
		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return cmdBuffer->End();
	}

	VkResult CommandBuffer::ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
	{
		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return cmdBuffer->Reset(flags);
	}
}
