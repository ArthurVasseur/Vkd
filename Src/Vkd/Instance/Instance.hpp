/**
 * @file Instance.hpp
 * @brief Vulkan instance implementation
 * @date 2025-04-23
 *
 * Represents a Vulkan instance and manages physical device enumeration
 * and instance-level extensions.
 */

#pragma once

// STL
#include <array>
#include <vector>
#include <span>

// Project
#include "Vkd/ObjectBase/ObjectBase.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	class Instance : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_INSTANCE;
		VKD_DISPATCHABLE_HANDLE(Instance)

		Instance();

		VkResult Create(const VkAllocationCallbacks& allocationCallbacks);

		// Vulkan API entry points
		static  VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
		static  VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties);
		static  VkResult VKAPI_CALL EnumerateInstanceVersion(uint32_t* pApiVersion);
		static  VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
		static  void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator);
		static  PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName);
		static  VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);

		VkResult EnumeratePlatformPhysicalDevices();
		void AddPhysicalDevice(DispatchableObject<PhysicalDevice>* physicalDevice);
		std::span<DispatchableObject<PhysicalDevice>*> GetPhysicalDevices();
	private:
		static void* VKAPI_PTR AllocationFunction(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
		static void* VKAPI_PTR ReallocationFunction(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
		static void VKAPI_PTR FreeFunction(void* pUserData, void* pMemory);

		static VkAllocationCallbacks s_allocationCallbacks;

		std::vector<DispatchableObject<PhysicalDevice>*> m_physicalDevices;
		bool m_physicalDevicesAlreadyEnumerated;
	};
}
