//
// Created by arthur on 23/04/2025.
//

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

		static VkResult EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
		static VkResult EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties);
		static VkResult EnumerateInstanceVersion(uint32_t* pApiVersion);
		static VkResult CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
		static void DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator);
		static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* pName);
		static VkResult EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);

		VkResult EnumeratePlatformPhysicalDevices();
		void AddPhysicalDevice(DispatchableObject<PhysicalDevice>* physicalDevice);
		std::span<DispatchableObject<PhysicalDevice>*> GetPhysicalDevices();
	private:
		static void* AllocationFunction(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
		static void* ReallocationFunction(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
		static void FreeFunction(void* pUserData, void* pMemory);

		static std::array<VkExtensionProperties, 4> s_supportedExtensions;
		static VkAllocationCallbacks s_allocationCallbacks;

		std::vector<DispatchableObject<PhysicalDevice>*> m_physicalDevices;
		bool m_physicalDevicesAlreadyEnumerated;
	};
}
