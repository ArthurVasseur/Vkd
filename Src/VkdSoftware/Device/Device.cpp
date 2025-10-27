//
// Created by arthur on 23/04/2025.
//

#include "Vkd/Memory/Memory.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/Queue/Queue.hpp"
#include "VkdSoftware/CommandPool/CommandPool.hpp"
#include "VkdSoftware/Synchronization/Fence/Fence.hpp"
#include "VkdSoftware/Buffer/Buffer.hpp"
#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"
#include "VkdSoftware/Pipeline/Pipeline.hpp"

namespace vkd::software
{
	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		VKD_CHECK(queueFamilyIndex > properties.size());

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

	DispatchableObjectResult<vkd::Buffer> SoftwareDevice::CreateBuffer()
	{
		auto* buffer = mem::NewDispatchable<Buffer>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Buffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::Buffer>*>(buffer);
	}

	DispatchableObjectResult<vkd::DeviceMemory> SoftwareDevice::CreateDeviceMemory()
	{
		auto* memory = mem::NewDispatchable<DeviceMemory>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!memory)
		{
			CCT_ASSERT_FALSE("Failed to allocate DeviceMemory");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::DeviceMemory>*>(memory);
	}

	DispatchableObjectResult<vkd::Pipeline> SoftwareDevice::CreatePipeline()
	{
		auto* pipeline = mem::NewDispatchable<Pipeline>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!pipeline)
		{
			CCT_ASSERT_FALSE("Failed to allocate Pipeline");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return reinterpret_cast<DispatchableObject<vkd::Pipeline>*>(pipeline);
	}
}
