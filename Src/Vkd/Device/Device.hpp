/**
 * @file Device.hpp
 * @brief Vulkan logical device abstraction
 * @date 2025-04-23
 *
 * Represents a logical device and manages queues, command pools, buffers, pipelines,
 * and device memory allocations.
 */

#pragma once

#include <unordered_map>
#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class PhysicalDevice;
	class Queue;
	class CommandPool;
	class Fence;
	class Buffer;
	class DeviceMemory;
	class Pipeline;

	class Device : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_DEVICE;
		VKD_DISPATCHABLE_HANDLE(Device);

		Device();
		~Device() override;

		VkResult Create(PhysicalDevice& owner, const VkDeviceCreateInfo& pDeviceCreateInfo, const VkAllocationCallbacks& allocationCallbacks);
		[[nodiscard]] PhysicalDevice* GetOwner() const;

		VkResult CreateQueues(const VkDeviceCreateInfo& pCreateInfo);
		[[nodiscard]] DispatchableObject<Queue>* GetQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const;
		[[nodiscard]] DispatchableObject<Queue>* GetQueue(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) const;

		// Vulkan API entry points
		static VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
		static void VKAPI_CALL DestroyDevice(VkDevice pDevice, const VkAllocationCallbacks* pAllocator);
		static PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice pDevice, const char* pName);

		static void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);
		static void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue);

		static VkResult VKAPI_CALL CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
		static void VKAPI_CALL DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator);
		static VkResult VKAPI_CALL ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);

		static VkResult VKAPI_CALL AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);
		static void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);

		static VkResult VKAPI_CALL CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence);
		static void VKAPI_CALL DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator);
		static VkResult VKAPI_CALL WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);
		static VkResult VKAPI_CALL ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences);
		static VkResult VKAPI_CALL GetFenceStatus(VkDevice device, VkFence fence);

		static VkResult VKAPI_CALL CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
		static void VKAPI_CALL DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator);
		static void VKAPI_CALL GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements);
		static VkResult VKAPI_CALL BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);

		static VkResult VKAPI_CALL AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
		static void VKAPI_CALL FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator);
		static VkResult VKAPI_CALL MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
		static void VKAPI_CALL UnmapMemory(VkDevice device, VkDeviceMemory memory);

		static VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
		static VkResult VKAPI_CALL CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
		static void VKAPI_CALL DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator);

		virtual DispatchableObjectResult<Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) = 0;
		virtual Result<CommandPool*, VkResult> CreateCommandPool() = 0;
		virtual Result<Fence*, VkResult> CreateFence() = 0;
		virtual Result<Buffer*, VkResult> CreateBuffer() = 0;
		virtual Result<DeviceMemory*, VkResult> CreateDeviceMemory() = 0;
		virtual Result<Pipeline*, VkResult> CreatePipeline() = 0;

	private:
		PhysicalDevice* m_owner;

		// Queues organized by family index, then queue index
		std::unordered_map<UInt32 /*family index*/, std::vector<DispatchableObject<Queue>*>> m_queues;
	};
}
