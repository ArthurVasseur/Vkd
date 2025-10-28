//
// Created by arthur on 27/10/2025.
//

#include "Vkd/CommandBuffer/CommandBuffer.hpp"

namespace vkd
{
	VkResult CommandBuffer::BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);
		VKD_CHECK(pBeginInfo);

		return cmdBuffer->Begin(*pBeginInfo);
	}

	VkResult CommandBuffer::EndCommandBuffer(VkCommandBuffer commandBuffer)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);

		return cmdBuffer->End();
	}

	VkResult CommandBuffer::ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, commandBuffer);

		return cmdBuffer->Reset(flags);
	}

	void CommandBuffer::CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushFill(dstBuffer, dstOffset, size, data);
	}

	void CommandBuffer::CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopy(srcBuffer, dstBuffer, regionCount, pRegions);
	}

	void CommandBuffer::CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		commandBufferObj->PushBindPipeline(pipelineBindPoint, pipeline);
	}

	void CommandBuffer::CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		commandBufferObj->PushBindVertexBuffer(std::span(pBuffers, bindingCount), std::span(pOffsets, bindingCount), firstBinding);
	}

	void CommandBuffer::CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		commandBufferObj->PushDraw(vertexCount, instanceCount, firstVertex, firstInstance);
	}
}
