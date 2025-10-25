#include "CommandBuffer.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	VkResult CommandBuffer::AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
	{
		VKD_FROM_HANDLE(Device, deviceObj, device);
		if (!deviceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkDevice handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pAllocateInfo || !pCommandBuffers)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		VKD_FROM_HANDLE(CommandPool, poolObj, pAllocateInfo->commandPool);
		if (!poolObj)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandPool handle");
			return VK_ERROR_DEVICE_LOST;
		}

		// Allocate command buffers from the pool
		for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i)
		{
			auto bufferResult = poolObj->AllocateCommandBuffer(pAllocateInfo->level);
			if (bufferResult.IsError())
			{
				// Clean up any buffers allocated so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(CommandBuffer, bufferObj, pCommandBuffers[j]);
					if (bufferObj)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<CommandBuffer>*>(pCommandBuffers[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return bufferResult.GetError();
			}

			auto* buffer = std::move(bufferResult).GetValue();
			VkResult result = buffer->Object->Create(*poolObj, pAllocateInfo->level);
			if (result != VK_SUCCESS)
			{
				mem::DeleteDispatchable(buffer);
				// Clean up any buffers allocated so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(CommandBuffer, bufferObj, pCommandBuffers[j]);
					if (bufferObj)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<CommandBuffer>*>(pCommandBuffers[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return result;
			}

			pCommandBuffers[i] = VKD_TO_HANDLE(VkCommandBuffer, buffer);
		}

		return VK_SUCCESS;
	}

	void CommandBuffer::FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
	{
		if (!pCommandBuffers || commandBufferCount == 0)
			return;

		// TODO: implement - free command buffers back to pool
		for (uint32_t i = 0; i < commandBufferCount; ++i)
		{
			VKD_FROM_HANDLE(CommandBuffer, cmdBuffer, pCommandBuffers[i]);
			if (!cmdBuffer)
				continue;

			auto* dispatchable = reinterpret_cast<DispatchableObject<CommandBuffer>*>(pCommandBuffers[i]);
			mem::DeleteDispatchable(dispatchable);
		}
	}

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
