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
	SoftwareDevice::~SoftwareDevice()
	{
		m_threadPool.RequestStop();
	}

	ThreadPool& SoftwareDevice::GetThreadPool()
	{
		return m_threadPool;
	}

	DispatchableObjectResult<vkd::Queue> SoftwareDevice::CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		PhysicalDevice* physicalDevice = GetOwner();
		auto properties = physicalDevice->GetQueueFamilyProperties();
		VKD_CHECK(queueFamilyIndex < properties.size());

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

	Result<vkd::CommandPool*, VkResult> SoftwareDevice::CreateCommandPool()
	{
		auto* pool = mem::New<CommandPool>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!pool)
		{
			CCT_ASSERT_FALSE("Failed to allocate CommandPool");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pool;
	}

	Result<vkd::Fence*, VkResult> SoftwareDevice::CreateFence()
	{
		auto* fence = mem::New<Fence>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!fence)
		{
			CCT_ASSERT_FALSE("Failed to allocate Fence");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return fence;
	}

	Result<vkd::Buffer*, VkResult> SoftwareDevice::CreateBuffer()
	{
		auto* buffer = mem::New<Buffer>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!buffer)
		{
			CCT_ASSERT_FALSE("Failed to allocate Buffer");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return buffer;
	}

	Result<vkd::DeviceMemory*, VkResult> SoftwareDevice::CreateDeviceMemory()
	{
		auto* memory = mem::New<DeviceMemory>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!memory)
		{
			CCT_ASSERT_FALSE("Failed to allocate DeviceMemory");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return memory;
	}

	Result<vkd::Pipeline*, VkResult> SoftwareDevice::CreatePipeline()
	{
		auto* pipeline = mem::New<Pipeline>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (!pipeline)
		{
			CCT_ASSERT_FALSE("Failed to allocate Pipeline");
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

		return pipeline;
	}
}
