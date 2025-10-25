//
// Created by arthur on 23/04/2025.
//

#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd::software
{
	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex)
	{
		auto* queue = mem::NewDispatchable<Queue>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!queue)
		{
			CCT_ASSERT_FALSE("Failed to allocate SoftwareQueue");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		VkResult result = queue->Object->Create(*this, queueFamilyIndex, queueIndex);
		if (result != VK_SUCCESS)
			return result;

		return reinterpret_cast<DispatchableObject<vkd::Queue>*>(queue);
	}
}
