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

	void VKAPI_CALL CommandBuffer::CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void CommandBuffer::CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		commandBufferObj->PushDraw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VKAPI_CALL CommandBuffer::CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdEndRenderPass(VkCommandBuffer commandBuffer)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}

	void VKAPI_CALL CommandBuffer::CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
	{
		VKD_AUTO_PROFILER_SCOPE();
	}
} // namespace vkd
