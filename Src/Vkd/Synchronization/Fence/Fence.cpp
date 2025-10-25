#include "Fence.hpp"

#include "Vkd/Defines.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	VkResult Fence::CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
	{
		VKD_FROM_HANDLE(Device, deviceObj, device);
		if (!deviceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkDevice handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pCreateInfo || !pFence)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		auto fenceResult = deviceObj->CreateFence();
		if (fenceResult.IsError())
			return fenceResult.GetError();

		auto* fenceObj = std::move(fenceResult).GetValue();
		VkResult result = fenceObj->Object->Create(*deviceObj, *pCreateInfo);
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(fenceObj);
			return result;
		}

		*pFence = VKD_TO_HANDLE(VkFence, fenceObj);
		return VK_SUCCESS;
	}

	void Fence::DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(Fence, fenceObj, fence);
		if (!fenceObj)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<Fence>*>(fence);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Fence::WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
	{
		VKD_FROM_HANDLE(Device, deviceObj, device);
		if (!deviceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkDevice handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pFences || fenceCount == 0)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// TODO: implement - wait for multiple fences with timeout
		// For now, iterate and wait on each fence individually
		for (uint32_t i = 0; i < fenceCount; ++i)
		{
			VKD_FROM_HANDLE(Fence, fenceObj, pFences[i]);
			if (!fenceObj)
			{
				CCT_ASSERT_FALSE("Invalid VkFence handle at index {}", i);
				return VK_ERROR_DEVICE_LOST;
			}

			VkResult result = fenceObj->Wait(timeout);
			if (result != VK_SUCCESS)
				return result;

			// If waitAll is false, return on first signaled fence
			if (!waitAll)
				return VK_SUCCESS;
		}

		return VK_SUCCESS;
	}

	VkResult Fence::ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
	{
		VKD_FROM_HANDLE(Device, deviceObj, device);
		if (!deviceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkDevice handle");
			return VK_ERROR_DEVICE_LOST;
		}

		if (!pFences || fenceCount == 0)
		{
			CCT_ASSERT_FALSE("Invalid parameters");
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// TODO: implement - reset multiple fences
		for (uint32_t i = 0; i < fenceCount; ++i)
		{
			VKD_FROM_HANDLE(Fence, fenceObj, pFences[i]);
			if (!fenceObj)
			{
				CCT_ASSERT_FALSE("Invalid VkFence handle at index {}", i);
				return VK_ERROR_DEVICE_LOST;
			}

			VkResult result = fenceObj->Reset();
			if (result != VK_SUCCESS)
				return result;
		}

		return VK_SUCCESS;
	}

	VkResult Fence::GetFenceStatus(VkDevice device, VkFence fence)
	{
		VKD_FROM_HANDLE(Fence, fenceObj, fence);
		if (!fenceObj)
		{
			CCT_ASSERT_FALSE("Invalid VkFence handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return fenceObj->GetStatus();
	}
}
