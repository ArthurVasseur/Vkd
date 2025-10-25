//
// Created by arthur on 23/04/2025.
//

#pragma once

#include <vector>

#include "Vkd/ObjectBase/ObjectBase.hpp"

namespace vkd
{
	class PhysicalDevice;
	class Queue;

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

		static VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
		static void VKAPI_CALL DestroyDevice(VkDevice pDevice, const VkAllocationCallbacks* pAllocator);
		static PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice pDevice, const char* pName);
		static void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

	protected:
		virtual DispatchableObjectResult<Queue> CreateQueueForFamily(uint32_t queueFamilyIndex, uint32_t queueIndex) = 0;

	private:
		PhysicalDevice* m_owner;
		// Queues organized by family index, then queue index
		std::vector<std::vector<DispatchableObject<Queue>*>> m_queues;
	};
}
