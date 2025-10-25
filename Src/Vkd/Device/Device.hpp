//
// Created by arthur on 23/04/2025.
//

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
	
		virtual DispatchableObjectResult<Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags) = 0;
		virtual DispatchableObjectResult<CommandPool> CreateCommandPool() = 0;
		virtual DispatchableObjectResult<Fence> CreateFence() = 0;

	private:
		PhysicalDevice* m_owner;

		// Queues organized by family index, then queue index
		std::unordered_map<cct::UInt32 /*family index*/, std::vector<DispatchableObject<Queue>*>> m_queues;
	};
}
