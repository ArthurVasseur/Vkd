//
// Created by arthur on 23/04/2025.
//

#include <algorithm>

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/Queue/Queue.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/Synchronization/Fence/Fence.hpp"
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
			for (auto* queue : queueFamily.second)
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

		std::unordered_map<uint32_t, uint32_t> totalPerFamily;
		for (uint32_t i = 0; i < pCreateInfo.queueCreateInfoCount; ++i)
		{
			const auto& queueCreateInfo = pCreateInfo.pQueueCreateInfos[i];

			if (queueCreateInfo.pQueuePriorities == nullptr && queueCreateInfo.queueCount > 0)
			{
				CCT_ASSERT_FALSE("pQueuePriorities is null but queueCount > 0");
				return VK_ERROR_INITIALIZATION_FAILED;
			}

			totalPerFamily[queueCreateInfo.queueFamilyIndex] += queueCreateInfo.queueCount;
		}

		for (const auto& [family, total] : totalPerFamily)
		{
			auto& vec = m_queues[family];
			vec.clear();
			vec.resize(total);
		}

		std::unordered_map<uint32_t, uint32_t> nextOffset;
		for (uint32_t i = 0; i < pCreateInfo.queueCreateInfoCount; ++i)
		{
			const auto& queueCreateInfo = pCreateInfo.pQueueCreateInfos[i];
			const uint32_t family = queueCreateInfo.queueFamilyIndex;
			const uint32_t count = queueCreateInfo.queueCount;
			const VkDeviceQueueCreateFlags flags = queueCreateInfo.flags;

			auto& vec = m_queues[family];
			uint32_t& at = nextOffset[family];

			for (uint32_t q = 0; q < count; ++q)
			{
				auto queue = CreateQueueForFamily(family, at + q, flags);
				if (queue.IsError())
				{
					CCT_ASSERT_FALSE("Failed to create queue");
					return queue.GetError();
				}

				vec[at + q] = std::move(queue).GetValue();
			}

			at += count;
		}

		return VK_SUCCESS;
	}

	DispatchableObject<Queue>* Device::GetQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const
	{
		auto it = m_queues.find(queueFamilyIndex);
		if (it == m_queues.end())
		{
			CCT_ASSERT_FALSE("GetQueue: unknown queueFamilyIndex '{}'", queueFamilyIndex);
			return nullptr;
		}

		const auto& familyQueues = it->second;
		if (queueIndex >= familyQueues.size())
		{
			CCT_ASSERT_FALSE("GetQueue: queueIndex '{}' out of range (size '{}') for family '{}'",
				queueIndex, familyQueues.size(), queueFamilyIndex);
			return nullptr;
		}

		return familyQueues[queueIndex];
	}

	DispatchableObject<Queue>* Device::GetQueue(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) const
	{
		auto it = m_queues.find(queueFamilyIndex);
		if (it == m_queues.end())
		{
			CCT_ASSERT_FALSE("GetQueue: unknown queueFamilyIndex '{}'", queueFamilyIndex);
			return nullptr;
		}

		const auto& familyQueues = it->second;
		if (queueIndex >= familyQueues.size())
		{
			CCT_ASSERT_FALSE("GetQueue: queueIndex '{}' out of range (size '{}') for family '{}'",
				queueIndex, familyQueues.size(), queueFamilyIndex);
			return nullptr;
		}

		auto* queue = familyQueues[queueIndex];
		if (!queue)
			return nullptr;

		// Verify that the queue has the matching flags
		if (queue->Object->GetFlags() != flags)
		{
			CCT_ASSERT_FALSE("GetQueue: queue at family '{}' index '{}' has flags '{}' but requested flags '{}'",
				queueFamilyIndex, queueIndex, queue->Object->GetFlags(), flags);
			return nullptr;
		}

		return queue;
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
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceQueue2);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateCommandPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyCommandPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, ResetCommandPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, AllocateCommandBuffers);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, FreeCommandBuffers);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateFence);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyFence);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, WaitForFences);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, ResetFences);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetFenceStatus);

		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueSubmit);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueWaitIdle);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueBindSparse);


		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, BeginCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, EndCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, ResetCommandBuffer);

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

	void Device::GetDeviceQueue2(VkDevice pDevice, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue)
	{
		VKD_FROM_HANDLE(Device, device, pDevice);
		if (!device || !pQueueInfo || !pQueue)
		{
			CCT_ASSERT_FALSE("Invalid parameters to GetDeviceQueue2");
			return;
		}

		auto* queue = device->GetQueue(pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueueInfo->flags);
		if (!queue)
		{
			CCT_ASSERT_FALSE("Invalid queue family index, queue index, or flags mismatch");
			*pQueue = VK_NULL_HANDLE;
			return;
		}

		*pQueue = VKD_TO_HANDLE(VkQueue, queue);
	}

	VkResult Device::CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
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

	void Device::DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);
		if (!poolObj)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<CommandPool>*>(commandPool);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Device::ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
	{
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);
		if (!poolObj)
		{
			CCT_ASSERT_FALSE("Invalid VkCommandPool handle");
			return VK_ERROR_DEVICE_LOST;
		}

		return poolObj->Reset(flags);
	}

	VkResult Device::AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
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

	void Device::FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
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

	VkResult Device::CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
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

	void Device::DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
	{
		VKD_FROM_HANDLE(Fence, fenceObj, fence);
		if (!fenceObj)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<Fence>*>(fence);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Device::WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
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

	VkResult Device::ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
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

	VkResult Device::GetFenceStatus(VkDevice device, VkFence fence)
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
