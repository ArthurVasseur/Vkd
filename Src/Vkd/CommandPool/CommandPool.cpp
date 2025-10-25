#include "CommandPool.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	DispatchableObjectResult<CommandBuffer> CommandPool::AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		return DoCreateCommandBuffer(level);
	}

	VkResult CommandPool::CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
	{
		VKD_FROM_HANDLE(Device, deviceObj, device);
		if (!deviceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkDevice handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pCommandPool)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto poolResult = deviceObj->CreateCommandPool();
		if (poolResult.IsError())
			return poolResult.GetError();

		auto* pool = std::move(poolResult).GetValue();
		VkResult result = pool->Object->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(pool);
			return result;
		}

		*pCommandPool = VKD_TO_HANDLE(VkCommandPool, pool);
		return VK_SUCCESS;
	}

	void CommandPool::DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);
		if (!poolObj)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<CommandPool>*>(commandPool);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult CommandPool::ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
	{
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);
		if (!poolObj)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandPool handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return poolObj->Reset(flags);
	}
}
