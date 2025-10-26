#include "CommandBuffer.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Memory/Memory.hpp"
#include "VkdSoftware/CommandBuffer/CommandBuffer.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"

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

	void CommandBuffer::CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);
		VKD_FROM_HANDLE(Pipeline, pipelineObj, pipeline);

		commandBufferObj->BindPipeline(pipelineObj);
	}

	void CommandBuffer::CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		if (bindingCount > 0 && pBuffers && pOffsets)
			commandBufferObj->PushBindVertexBuffer(std::span(pBuffers, bindingCount), std::span(pOffsets, bindingCount), firstBinding);
	}

	void CommandBuffer::CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, commandBuffer);

		commandBufferObj->PushDraw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	VkResult CommandBuffer::Begin(const VkCommandBufferBeginInfo& beginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Recording, { State::Initial });

		return VK_SUCCESS;
	}

	VkResult CommandBuffer::End()
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Executable, { State::Recording });

		return VK_SUCCESS;
	}

	VkResult CommandBuffer::Reset(VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE;
		Transition(State::Initial, { State::Executable, State::Pending });

		return VK_SUCCESS;
	}
}
