//
// Created by arthur on 23/04/2025.
//

#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/CommandPool/CommandPool.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"

namespace vkd::software
{
	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		if (queueFamilyIndex > properties.size())
		{
			CCT_ASSERT_FALSE("Invalid queue family index '{}'", queueFamilyIndex);
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		auto* queue = mem::NewDispatchable<Queue>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!queue)
		{
			CCT_ASSERT_FALSE("Failed to allocate SoftwareQueue");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		VkResult result = queue->Object->Create(*this, queueFamilyIndex, queueIndex, flags);
		if (result != VK_SUCCESS)
			return result;

		return reinterpret_cast<DispatchableObject<vkd::Queue>*>(queue);
	}

	DispatchableObjectResult<vkd::CommandPool> SoftwareDevice::CreateCommandPool()
	{
		auto* pool = mem::NewDispatchable<CommandPool>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::CommandPool>*>(pool);
	}

	DispatchableObjectResult<vkd::Fence> SoftwareDevice::CreateFence()
	{
		auto* fence = mem::NewDispatchable<Fence>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!fence)
		{
			CCT_ASSERT_FALSE("Failed to allocate Fence");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::Fence>*>(fence);
	}
}
