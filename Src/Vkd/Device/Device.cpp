//
// Created by arthur on 23/04/2025.
//

#include <algorithm>

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/Queue/Queue.hpp"
#include "Vkd/Memory/Memory.hpp"

namespace vkd
{
	Device::Device() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_queues()
	{
	}

	Device::~Device()
	{
		for (auto& queueFamily : m_queues)
		{
			for (auto* queue : queueFamily)
			{
				if (queue)
					mem::DeleteDispatchable(queue);
			}
		}
		m_queues.clear();
	}

	VkResult Device::Create(PhysicalDevice& owner, const VkDeviceCreateInfo& pDeviceCreateInfo, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		SetAllocationCallbacks(allocationCallbacks);

		return CreateQueues(pDeviceCreateInfo);
	}

	PhysicalDevice* Device::GetOwner() const
	{
		return m_owner;
	}

	VkResult Device::CreateQueues(const VkDeviceCreateInfo& pCreateInfo)
	{
		if (pCreateInfo.queueCreateInfoCount == 0)
			return VK_SUCCESS;

		// Find the maximum queue family index to size our vector
		uint32_t maxQueueFamilyIndex = 0;
		for (uint32_t i = 0; i < pCreateInfo.queueCreateInfoCount; ++i)
		{
			maxQueueFamilyIndex = std::max(pCreateInfo.pQueueCreateInfos[i].queueFamilyIndex, maxQueueFamilyIndex);
		}

		m_queues.resize(maxQueueFamilyIndex + 1);

		// Create queues for each family
		for (uint32_t i = 0; i < pCreateInfo.queueCreateInfoCount; ++i)
		{
			const auto& queueCreateInfo = pCreateInfo.pQueueCreateInfos[i];
			uint32_t queueFamilyIndex = queueCreateInfo.queueFamilyIndex;
			uint32_t queueCount = queueCreateInfo.queueCount;

			m_queues[queueFamilyIndex].resize(queueCount);

			for (uint32_t queueIndex = 0; queueIndex < queueCount; ++queueIndex)
			{
				auto queue = CreateQueueForFamily(queueFamilyIndex, queueIndex);
				if (queue.IsError())
				{
					CCT_ASSERT_FALSE("Failed to create queue");
					return queue.GetError();
				}
				m_queues[queueFamilyIndex][queueIndex] = std::move(queue).GetValue();
			}
		}

		return VK_SUCCESS;
	}

	DispatchableObject<Queue>* Device::GetQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const
	{
		if (queueFamilyIndex >= m_queues.size())
			return nullptr;

		if (queueIndex >= m_queues[queueFamilyIndex].size())
			return nullptr;

		return m_queues[queueFamilyIndex][queueIndex];
	}

	VkResult Device::CreateDevice(VkPhysicalDevice pPhysicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		if (!pAllocator)
			pAllocator = &physicalDevice->GetAllocationCallbacks();

		auto deviceResult = physicalDevice->CreateDevice();
		if (deviceResult.IsError())
			return deviceResult.GetError();

		auto device = std::move(deviceResult).GetValue();
		VkResult result = device->Object->Create(*physicalDevice, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(device);
			return result;
		}

		*pDevice = VKD_TO_HANDLE(VkDevice, device);
		return VK_SUCCESS;
	}

	void Device::DestroyDevice(VkDevice pDevice, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);
		if (!device)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<Device>*>(pDevice);
		mem::DeleteDispatchable(dispatchable);
	}

	PFN_vkVoidFunction Device::GetDeviceProcAddr(VkDevice pDevice, const char* pName)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);

		if (pName == nullptr)
			return nullptr;

#define VKD_ENTRYPOINT_LOOKUP(klass, name)	\
	if (strcmp(pName, "vk" #name) == 0) \
		return (PFN_vkVoidFunction)static_cast<PFN_vk##name>(klass::name)

		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyDevice);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateDevice);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceProcAddr);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceQueue);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueSubmit);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueWaitIdle);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueBindSparse);
#undef VKD_ENTRYPOINT_LOOKUP

		return nullptr;
	}

	void Device::GetDeviceQueue(VkDevice pDevice, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);
		if (!device || !pQueue)
		{
			CCT_ASSERT_FALSE("Invalid parameters to GetDeviceQueue");
			return;
		}

		auto* queue = device->GetQueue(queueFamilyIndex, queueIndex);
		if (!queue)
		{
			CCT_ASSERT_FALSE("Invalid queue family index or queue index");
			*pQueue = VK_NULL_HANDLE;
			return;
		}

		*pQueue = VKD_TO_HANDLE(VkQueue, queue);
	}
}
