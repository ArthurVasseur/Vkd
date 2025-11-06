/**
 * @file CommandBuffer.cpp
 * @brief Implementation of Vulkan command buffer
 * @date 2025-10-27
 */

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
		commandBufferObj->PushFillBuffer(dstBuffer, dstOffset, size, data);
	}

	void CommandBuffer::CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopyBuffer(srcBuffer, dstBuffer, regionCount, pRegions);
	}

	void CommandBuffer::CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2* pCopyBufferInfo)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		VKD_CHECK(pCopyBufferInfo);

		commandBufferObj->PushCopyBuffer2(pCopyBufferInfo->srcBuffer, pCopyBufferInfo->dstBuffer, pCopyBufferInfo->regionCount, pCopyBufferInfo->pRegions);
	}

	void CommandBuffer::CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushUpdateBuffer(dstBuffer, dstOffset, dataSize, pData);
	}

	void CommandBuffer::CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
	}

	void CommandBuffer::CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopyBufferToImage(srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
	}

	void CommandBuffer::CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushCopyImageToBuffer(srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
	}

	void CommandBuffer::CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		commandBufferObj->PushClearColorImage(image, imageLayout, pColor, rangeCount, pRanges);
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
