#include "CommandBuffer.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	VkResult CommandBuffer::BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_VALIDATION_FAILED_EXT;
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
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		return cmdBuffer->End();
	}

	VkResult CommandBuffer::ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		if (!cmdBuffer)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandBuffer handle");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		return cmdBuffer->Reset(flags);
	}

	void CommandBuffer::CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushFill(dstBuffer, dstOffset, size, data);
	}

	void CommandBuffer::CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopy(srcBuffer, dstBuffer, regionCount, pRegions);
	}
}
