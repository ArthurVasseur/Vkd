//
// Created by arthur on 23/04/2025.
//

#include <algorithm>

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Device/Device.hpp"

#include <chrono>
#include <thread>

#include "Vkd/Queue/Queue.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/Synchronization/Fence/Fence.hpp"
#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"

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
		VKD_AUTO_PROFILER_SCOPE();

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, device, pDevice);

		auto* dispatchable = reinterpret_cast<DispatchableObject<Device>*>(pDevice);
		mem::DeleteDispatchable(dispatchable);
	}

	PFN_vkVoidFunction Device::GetDeviceProcAddr(VkDevice pDevice, const char* pName)
	{
		VKD_AUTO_PROFILER_SCOPE();

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
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetBufferMemoryRequirements);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, BindBufferMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, AllocateMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, FreeMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, MapMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, UnmapMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateGraphicsPipelines);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateComputePipelines);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyPipeline);

		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueSubmit);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueWaitIdle);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueBindSparse);

		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, BeginCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, EndCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, ResetCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdFillBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyBuffer);

#undef VKD_ENTRYPOINT_LOOKUP

		return nullptr;
	}

	void Device::GetDeviceQueue(VkDevice pDevice, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, device, pDevice);
		VKD_CHECK(pQueue);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, device, pDevice);
		VKD_CHECK(pQueueInfo && pQueue);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pCommandPool);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);

		auto* dispatchable = reinterpret_cast<DispatchableObject<CommandPool>*>(commandPool);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Device::ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(CommandPool, poolObj, commandPool);

		return poolObj->Reset(flags);
	}

	VkResult Device::AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pAllocateInfo && pCommandBuffers);
		VKD_FROM_HANDLE(CommandPool, poolObj, pAllocateInfo->commandPool);


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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(CommandPool, commandPoolObj, commandPool);
		VKD_CHECK(pCommandBuffers && commandBufferCount != 0);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pFence);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Fence, fenceObj, fence);
		VKD_FROM_HANDLE(Device, deviceObj, device);

		auto* dispatchable = reinterpret_cast<DispatchableObject<Fence>*>(fence);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Device::WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pFences && fenceCount != 0);

		const bool infinite = timeout == std::numeric_limits<uint64_t>::max();

		using Clock = std::chrono::steady_clock;
		const auto start = Clock::now();
		const auto deadline = infinite ? Clock::time_point::max() : (start + std::chrono::nanoseconds(timeout));

		if (waitAll)
		{
			for (uint32_t i = 0; i < fenceCount; ++i)
			{
				VKD_FROM_HANDLE(Fence, fenceObj, pFences[i]);
				if (!fenceObj)
				{
					CCT_ASSERT_FALSE("Invalid VkFence handle at index {}", i);
					return VK_ERROR_DEVICE_LOST;
				}

				while (true)
				{
					const uint64_t remainingNs = (deadline == Clock::time_point::max())
						? std::numeric_limits<uint64_t>::max()
						: static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count());

					if (!infinite && Clock::now() >= deadline)
						return VK_TIMEOUT;

					VkResult result = fenceObj->Wait(remainingNs);
					if (result == VK_SUCCESS)
						break;
					if (result == VK_TIMEOUT)
					{
						if (!infinite && Clock::now() >= deadline)
							return VK_TIMEOUT;
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
						continue;
					}
					return result;
				}
			}
		}
		if (timeout == 0)
		{
			for (uint32_t i = 0; i < fenceCount; ++i)
			{
				VKD_FROM_HANDLE(Fence, fenceObj, pFences[i]);
				if (!fenceObj)
				{
					CCT_ASSERT_FALSE("Invalid VkFence handle at index {}", i);
					return VK_ERROR_DEVICE_LOST;
				}
				VkResult r = fenceObj->Wait(0);
				if (r == VK_SUCCESS) return VK_SUCCESS;
				if (r != VK_TIMEOUT) return r;
			}
			return VK_TIMEOUT;
		}

		while (true)
		{
			for (uint32_t i = 0; i < fenceCount; ++i)
			{
				VKD_FROM_HANDLE(Fence, fenceObj, pFences[i]);
				if (!fenceObj)
				{
					CCT_ASSERT_FALSE("Invalid VkFence handle at index {}", i);
					return VK_ERROR_DEVICE_LOST;
				}

				VkResult r = fenceObj->Wait(0);
				if (r == VK_SUCCESS) return VK_SUCCESS;
				if (r != VK_TIMEOUT) return r;
			}

			if (!infinite && Clock::now() >= deadline)
				return VK_TIMEOUT;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return VK_SUCCESS;
	}

	VkResult Device::ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pFences && fenceCount != 0);

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
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Fence, fenceObj, fence);

		return fenceObj->GetStatus();
	}

	VkResult Device::CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pBuffer);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto bufferResult = deviceObj->CreateBuffer();
		if (bufferResult.IsError())
			return bufferResult.GetError();

		auto* bufferObj = std::move(bufferResult).GetValue();
		VkResult result = bufferObj->Object->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(bufferObj);
			return result;
		}

		*pBuffer = VKD_TO_HANDLE(VkBuffer, bufferObj);
		return VK_SUCCESS;
	}

	void Device::DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Buffer, bufferObj, buffer);

		auto* dispatchable = reinterpret_cast<DispatchableObject<Buffer>*>(buffer);
		mem::DeleteDispatchable(dispatchable);
	}

	void Device::GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Buffer, bufferObj, buffer);
		VKD_CHECK(pMemoryRequirements);

		bufferObj->GetMemoryRequirements(*pMemoryRequirements);
	}

	VkResult Device::BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Buffer, bufferObj, buffer);
		VKD_FROM_HANDLE(DeviceMemory, memoryObj, memory);
		VKD_CHECK(!bufferObj->IsBound());

		bufferObj->BindBufferMemory(*memoryObj, memoryOffset);

		return VK_SUCCESS;
	}

	VkResult Device::AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pAllocateInfo && pMemory);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto memoryResult = deviceObj->CreateDeviceMemory();
		if (memoryResult.IsError())
			return memoryResult.GetError();

		auto* memoryObj = std::move(memoryResult).GetValue();
		VkResult result = memoryObj->Object->Create(*deviceObj, *pAllocateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(memoryObj);
			return result;
		}

		*pMemory = VKD_TO_HANDLE(VkDeviceMemory, memoryObj);
		return VK_SUCCESS;
	}

	void Device::FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(DeviceMemory, memoryObj, memory);

		auto* dispatchable = reinterpret_cast<DispatchableObject<DeviceMemory>*>(memory);
		mem::DeleteDispatchable(dispatchable);
	}

	VkResult Device::MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(DeviceMemory, memoryObj, memory);
		VKD_CHECK(!memoryObj->m_mapped);

		VkResult result = memoryObj->Map(offset, size, ppData);
		if (result == VK_SUCCESS)
			memoryObj->m_mapped = true;

		return result;
	}

	void Device::UnmapMemory(VkDevice device, VkDeviceMemory memory)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(DeviceMemory, memoryObj, memory);
		VKD_CHECK(memoryObj->m_mapped);

		memoryObj->Unmap();
		memoryObj->m_mapped = false;
	}

	VkResult Device::CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfos || pPipelines || createInfoCount);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		for (uint32_t i = 0; i < createInfoCount; ++i)
		{
			auto pipelineResult = deviceObj->CreatePipeline();
			if (pipelineResult.IsError())
			{
				// Clean up any pipelines created so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(Pipeline, pipelineObj, pPipelines[j]);
					if (pipelineObj)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<Pipeline>*>(pPipelines[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return pipelineResult.GetError();
			}

			auto* pipelineObj = std::move(pipelineResult).GetValue();
			VkResult result = pipelineObj->Object->CreateGraphicsPipeline(*deviceObj, pCreateInfos[i], *pAllocator);
			if (result != VK_SUCCESS)
			{
				mem::DeleteDispatchable(pipelineObj);
				// Clean up any pipelines created so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(Pipeline, pipelineObjToDelete, pPipelines[j]);
					if (pipelineObjToDelete)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<Pipeline>*>(pPipelines[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return result;
			}

			pPipelines[i] = VKD_TO_HANDLE(VkPipeline, pipelineObj);
		}

		return VK_SUCCESS;
	}

	VkResult Device::CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfos && pPipelines && createInfoCount);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		for (uint32_t i = 0; i < createInfoCount; ++i)
		{
			auto pipelineResult = deviceObj->CreatePipeline();
			if (pipelineResult.IsError())
			{
				// Clean up any pipelines created so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(Pipeline, pipelineObj, pPipelines[j]);
					if (pipelineObj)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<Pipeline>*>(pPipelines[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return pipelineResult.GetError();
			}

			auto* pipelineObj = std::move(pipelineResult).GetValue();
			VkResult result = pipelineObj->Object->CreateComputePipeline(*deviceObj, pCreateInfos[i], *pAllocator);
			if (result != VK_SUCCESS)
			{
				mem::DeleteDispatchable(pipelineObj);
				// Clean up any pipelines created so far
				for (uint32_t j = 0; j < i; ++j)
				{
					VKD_FROM_HANDLE(Pipeline, pipelineObjToDelete, pPipelines[j]);
					if (pipelineObjToDelete)
					{
						auto* dispatchable = reinterpret_cast<DispatchableObject<Pipeline>*>(pPipelines[j]);
						mem::DeleteDispatchable(dispatchable);
					}
				}
				return result;
			}

			pPipelines[i] = VKD_TO_HANDLE(VkPipeline, pipelineObj);
		}

		return VK_SUCCESS;
	}

	void Device::DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Pipeline, pipelineObj, pipeline);

		auto* dispatchable = reinterpret_cast<DispatchableObject<Pipeline>*>(pipeline);
		mem::DeleteDispatchable(dispatchable);
	}
}
