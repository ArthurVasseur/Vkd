/**
 * @file Device.cpp
 * @brief Implementation of Vulkan logical device
 * @date 2025-04-23
 */

#include <algorithm>
#include <chrono>
#include <thread>

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/Framebuffer/Framebuffer.hpp"
#include "Vkd/Image/Image.hpp"
#include "Vkd/ImageView/ImageView.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"
#include "Vkd/Queue/Queue.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/ShaderModule/ShaderModule.hpp"
#include "Vkd/Synchronization/Fence/Fence.hpp"

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

#ifdef VKD_DEBUG_CHECKS
		m_createResult = VK_SUCCESS; // avoid false positive in AssertValid()
#endif // VKD_DEBUG_CHECKS

		m_createResult = CreateQueues(pDeviceCreateInfo);
		return m_createResult;
	}

	PhysicalDevice* Device::GetOwner() const
	{
		AssertValid();
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

#define VKD_ENTRYPOINT_LOOKUP(klass, name) \
	if (strcmp(pName, "vk" #name) == 0)    \
	return (PFN_vkVoidFunction) static_cast<PFN_vk##name>(klass::name)

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
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateImage);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyImage);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetImageMemoryRequirements);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, BindImageMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, AllocateMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, FreeMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, MapMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, UnmapMemory);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateGraphicsPipelines);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateComputePipelines);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyPipeline);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateRenderPass);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyRenderPass);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateImageView);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyImageView);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateFramebuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyFramebuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateShaderModule);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyShaderModule);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateSampler);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroySampler);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateSemaphore);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroySemaphore);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateEvent);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyEvent);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetEventStatus);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, SetEvent);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, ResetEvent);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateQueryPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyQueryPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetQueryPoolResults);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreatePipelineLayout);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyPipelineLayout);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateDescriptorSetLayout);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyDescriptorSetLayout);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateDescriptorPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyDescriptorPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, ResetDescriptorPool);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, AllocateDescriptorSets);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, FreeDescriptorSets);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, UpdateDescriptorSets);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreatePipelineCache);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DestroyPipelineCache);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetPipelineCacheData);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, MergePipelineCaches);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, DeviceWaitIdle);

		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueSubmit);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueWaitIdle);
		VKD_ENTRYPOINT_LOOKUP(vkd::Queue, QueueBindSparse);

		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, BeginCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, EndCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, ResetCommandBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdFillBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyBuffer2);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdUpdateBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyImage);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyBufferToImage);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdCopyImageToBuffer);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdClearColorImage);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdBindPipeline);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdBindVertexBuffers);
		VKD_ENTRYPOINT_LOOKUP(vkd::CommandBuffer, CmdDraw);

#undef VKD_ENTRYPOINT_LOOKUP
		// cct::Logger::Warning("Could not find '{}' function", pName);

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
		VkResult result = pool->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Free(*pAllocator, pool);
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

		mem::Delete(poolObj->GetAllocationCallbacks(), poolObj);
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

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto fenceResult = deviceObj->CreateFence();
		if (fenceResult.IsError())
			return fenceResult.GetError();

		auto* fenceObj = std::move(fenceResult).GetValue();
		VkResult result = fenceObj->Create(*deviceObj, *pCreateInfo);
		if (result != VK_SUCCESS)
		{
			mem::Free(*pAllocator, fenceObj);
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
		mem::Delete(fenceObj->GetAllocationCallbacks(), dispatchable);
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
				if (r == VK_SUCCESS)
					return VK_SUCCESS;
				if (r != VK_TIMEOUT)
					return r;
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
				if (r == VK_SUCCESS)
					return VK_SUCCESS;
				if (r != VK_TIMEOUT)
					return r;
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
		VkResult result = bufferObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, bufferObj);
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

		mem::Delete(bufferObj->GetAllocationCallbacks(), bufferObj);
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

	VkResult Device::CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pImage);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto imageResult = deviceObj->CreateImage();
		if (imageResult.IsError())
			return imageResult.GetError();

		auto* imageObj = std::move(imageResult).GetValue();
		VkResult result = imageObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, imageObj);
			return result;
		}

		*pImage = VKD_TO_HANDLE(VkImage, imageObj);
		return VK_SUCCESS;
	}

	void Device::DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Image, imageObj, image);

		mem::Delete(imageObj->GetAllocationCallbacks(), imageObj);
	}

	void Device::GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Image, imageObj, image);
		VKD_CHECK(pMemoryRequirements);

		imageObj->GetMemoryRequirements(*pMemoryRequirements);
	}

	VkResult Device::BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Image, imageObj, image);
		VKD_FROM_HANDLE(DeviceMemory, memoryObj, memory);
		VKD_CHECK(!imageObj->IsBound());

		imageObj->BindImageMemory(*memoryObj, memoryOffset);

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
		VkResult result = memoryObj->Create(*deviceObj, *pAllocateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, memoryObj);
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

		mem::Delete(memoryObj->GetAllocationCallbacks(), memoryObj);
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
			VkResult result = pipelineObj->CreateGraphicsPipeline(*deviceObj, pCreateInfos[i], *pAllocator);
			if (result != VK_SUCCESS)
			{
				mem::Delete(*pAllocator, pipelineObj);
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
			VkResult result = pipelineObj->CreateComputePipeline(*deviceObj, pCreateInfos[i], *pAllocator);
			if (result != VK_SUCCESS)
			{
				mem::Delete(*pAllocator, pipelineObj);
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

		mem::Delete(pipelineObj->GetAllocationCallbacks(), pipelineObj);
	}

	VkResult Device::CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pRenderPass);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto renderPassResult = deviceObj->CreateRenderPass();
		if (renderPassResult.IsError())
			return renderPassResult.GetError();

		auto* renderPassObj = std::move(renderPassResult).GetValue();
		VkResult result = renderPassObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, renderPassObj);
			return result;
		}

		*pRenderPass = VKD_TO_HANDLE(VkRenderPass, renderPassObj);
		return VK_SUCCESS;
	}

	void Device::DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(RenderPass, renderPassObj, renderPass);

		mem::Delete(renderPassObj->GetAllocationCallbacks(), renderPassObj);
	}

	VkResult Device::CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pView);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto imageViewResult = deviceObj->CreateImageView();
		if (imageViewResult.IsError())
			return imageViewResult.GetError();

		auto* imageViewObj = std::move(imageViewResult).GetValue();
		VkResult result = imageViewObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, imageViewObj);
			return result;
		}

		*pView = VKD_TO_HANDLE(VkImageView, imageViewObj);
		return VK_SUCCESS;
	}

	void Device::DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(ImageView, imageViewObj, imageView);

		mem::Delete(imageViewObj->GetAllocationCallbacks(), imageViewObj);
	}

	VkResult Device::CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pFramebuffer);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto framebufferResult = deviceObj->CreateFramebuffer();
		if (framebufferResult.IsError())
			return framebufferResult.GetError();

		auto* framebufferObj = std::move(framebufferResult).GetValue();
		VkResult result = framebufferObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, framebufferObj);
			return result;
		}

		*pFramebuffer = VKD_TO_HANDLE(VkFramebuffer, framebufferObj);
		return VK_SUCCESS;
	}

	void Device::DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(Framebuffer, framebufferObj, framebuffer);

		mem::Delete(framebufferObj->GetAllocationCallbacks(), framebufferObj);
	}

	VkResult Device::CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_CHECK(pCreateInfo && pShaderModule);

		if (!pAllocator)
			pAllocator = &deviceObj->GetAllocationCallbacks();

		auto shaderModuleResult = deviceObj->CreateShaderModule();
		if (shaderModuleResult.IsError())
			return shaderModuleResult.GetError();

		auto* shaderModuleObj = std::move(shaderModuleResult).GetValue();
		VkResult result = shaderModuleObj->Create(*deviceObj, *pCreateInfo, *pAllocator);
		if (result != VK_SUCCESS)
		{
			mem::Delete(*pAllocator, shaderModuleObj);
			return result;
		}

		*pShaderModule = VKD_TO_HANDLE(VkShaderModule, shaderModuleObj);
		return VK_SUCCESS;
	}

	void Device::DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(Device, deviceObj, device);
		VKD_FROM_HANDLE(ShaderModule, shaderModuleObj, shaderModule);

		mem::Delete(shaderModuleObj->GetAllocationCallbacks(), shaderModuleObj);
	}

	VkResult Device::CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateSampler not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroySampler not implemented");
	}

	VkResult Device::CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateSemaphore not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroySemaphore not implemented");
	}

	VkResult Device::CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateEvent not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyEvent not implemented");
	}

	VkResult Device::GetEventStatus(VkDevice device, VkEvent event)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetEventStatus not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::SetEvent(VkDevice device, VkEvent event)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkSetEvent not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::ResetEvent(VkDevice device, VkEvent event)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkResetEvent not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateQueryPool not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyQueryPool not implemented");
	}

	VkResult Device::GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetQueryPoolResults not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreatePipelineLayout not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyPipelineLayout not implemented");
	}

	VkResult Device::CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateDescriptorSetLayout not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyDescriptorSetLayout not implemented");
	}

	VkResult Device::CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreateDescriptorPool not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyDescriptorPool not implemented");
	}

	VkResult Device::ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkResetDescriptorPool not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkAllocateDescriptorSets not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkFreeDescriptorSets not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkUpdateDescriptorSets not implemented");
	}

	VkResult Device::CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkCreatePipelineCache not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	void Device::DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDestroyPipelineCache not implemented");
	}

	VkResult Device::GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPipelineCacheData not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkMergePipelineCaches not implemented");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkResult Device::DeviceWaitIdle(VkDevice device)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkDeviceWaitIdle not implemented");
		return VK_SUCCESS;
	}
} // namespace vkd
