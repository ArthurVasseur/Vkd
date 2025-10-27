//
// Created by arthur on 23/04/2025.
//

#include <mimalloc.h>

#include "Vkd/Instance/Instance.hpp"
#include "Vkd/Icd/Icd.hpp"
#include "VkdSoftware/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	std::array<VkExtensionProperties, 4> Instance::s_supportedExtensions = {
		VkExtensionProperties{.extensionName = VK_KHR_SURFACE_EXTENSION_NAME, .specVersion = 1},
		VkExtensionProperties{.extensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME , .specVersion = 1},
		VkExtensionProperties{.extensionName = VK_EXT_DEBUG_REPORT_EXTENSION_NAME, .specVersion = 1},
		VkExtensionProperties{.extensionName = VK_KHR_WIN32_SURFACE_EXTENSION_NAME, .specVersion = 1},
	};

	VkAllocationCallbacks Instance::s_allocationCallbacks = {
		.pUserData = nullptr,
		.pfnAllocation = AllocationFunction,
		.pfnReallocation = ReallocationFunction,
		.pfnFree = FreeFunction,
		.pfnInternalAllocation = nullptr,
		.pfnInternalFree = nullptr
	};

	Instance::Instance() :
		ObjectBase(ObjectType),
		m_physicalDevicesAlreadyEnumerated(false)
	{
	}

	VkResult Instance::Create(const VkAllocationCallbacks& allocationCallbacks)
	{
		SetAllocationCallbacks(allocationCallbacks);

		return VK_SUCCESS;
	}

	VkResult Instance::EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE;

		if (pLayerName)
			return VK_ERROR_LAYER_NOT_PRESENT;

		if (pPropertyCount && !pProperties)
		{
			*pPropertyCount = s_supportedExtensions.size();
			return VK_SUCCESS;
		}

		std::size_t max = std::min(static_cast<std::size_t>(*pPropertyCount), s_supportedExtensions.size());
		for (std::size_t i = 0; i < max; ++i)
			pProperties[i] = s_supportedExtensions[i];

		if (max < s_supportedExtensions.size())
			return VK_INCOMPLETE;

		return VK_SUCCESS;
	}

	VkResult Instance::EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE;

		CCT_ASSERT_FALSE("Not Implemented");
		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}

	VkResult Instance::EnumerateInstanceVersion(uint32_t* pApiVersion)
	{
		*pApiVersion = VKD_VK_API_VERSION;
		return VK_SUCCESS;
	}

	VkResult Instance::CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
	{
		VKD_AUTO_PROFILER_SCOPE;

		if (!pAllocator)
			pAllocator = &s_allocationCallbacks;

		auto* instance = mem::NewDispatchable<Instance>(*pAllocator, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
		if (!instance)
			return Error(VK_ERROR_OUT_OF_HOST_MEMORY, "Out of host memory");

		instance->Object->Create(*pAllocator);
		

		auto result = instance->Object->EnumeratePlatformPhysicalDevices(); // Force physical device enumeration
		if (result != VK_SUCCESS)
		{
			mem::DeleteDispatchable(instance);
			return result;
		}

		*pInstance = VKD_TO_HANDLE(VkInstance, instance);
		return VK_SUCCESS;
	}

	void Instance::DestroyInstance(VkInstance pInstance, const VkAllocationCallbacks* pAllocator)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(Instance, instance, pInstance);
		if (!instance)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<Instance>*>(pInstance);
		mem::DeleteDispatchable(dispatchable);
	}

	PFN_vkVoidFunction Instance::GetInstanceProcAddr(VkInstance instance, const char* pName)
	{
		VKD_AUTO_PROFILER_SCOPE;

		CCT_ASSERT_FALSE("Not Implemented");
		return nullptr;
	}

	VkResult Instance::EnumeratePhysicalDevices(VkInstance pInstance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices)
	{
		VKD_AUTO_PROFILER_SCOPE;

		VKD_FROM_HANDLE(vkd::Instance, instance, pInstance);

		instance->EnumeratePlatformPhysicalDevices();
		if (pPhysicalDeviceCount && !pPhysicalDevices)
		{
			*pPhysicalDeviceCount = static_cast<UInt32>(instance->GetPhysicalDevices().size());
			return VK_SUCCESS;
		}

		auto physicalDevices = instance->GetPhysicalDevices();

		for (std::size_t i = 0; i < *pPhysicalDeviceCount; ++i)
		{
			pPhysicalDevices[i] = VKD_TO_HANDLE(VkPhysicalDevice, physicalDevices[i]);
		}

		return VK_SUCCESS;
	}

	VkResult Instance::EnumeratePlatformPhysicalDevices()
	{
		VKD_AUTO_PROFILER_SCOPE;

		if (m_physicalDevicesAlreadyEnumerated)
			return VK_SUCCESS; // Enumerate Physical devices only once during the lifetime of this VkInstance

		DispatchableObject<software::PhysicalDevice>* softwarePhysicalDevice = mem::NewDispatchable<software::PhysicalDevice>(GetAllocationCallbacks(), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
		if (!softwarePhysicalDevice)
			return Error(VK_ERROR_OUT_OF_HOST_MEMORY, "Out of host memory");
		DispatchableObject<PhysicalDevice>* physicalDevice = reinterpret_cast<DispatchableObject<PhysicalDevice>*>(softwarePhysicalDevice);

		VkResult result = physicalDevice->Object->Create(*this, GetAllocationCallbacks());
		if (result != VK_SUCCESS)
			return result;

		AddPhysicalDevice(physicalDevice);
		m_physicalDevicesAlreadyEnumerated = true;
		return VK_SUCCESS;
	}

	void Instance::AddPhysicalDevice(DispatchableObject<PhysicalDevice>* physicalDevice)
	{
		VKD_AUTO_PROFILER_SCOPE;

		m_physicalDevices.emplace_back(physicalDevice);
	}

	std::span<DispatchableObject<PhysicalDevice>*> Instance::GetPhysicalDevices()
	{
		return m_physicalDevices;
	}

	void* Instance::AllocationFunction(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
	{
		VKD_AUTO_PROFILER_SCOPE;

		void* alloc = mi_malloc_aligned(size, alignment);
		if (!alloc)
		{
			CCT_ASSERT_FALSE("Could not allocate memory: size={}, alignment={}", size, alignment);
			return nullptr;
		}

		return alloc;
	}

	void* Instance::ReallocationFunction(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
	{
		VKD_AUTO_PROFILER_SCOPE;

		void* alloc = mi_realloc_aligned(pOriginal, size, alignment);
		if (!alloc)
		{
			CCT_ASSERT_FALSE("Could not allocate memory: size={}, alignment={}", size, alignment);
			return nullptr;
		}

		return alloc;
	}

	void Instance::FreeFunction(void* pUserData, void* pMemory)
	{
		VKD_AUTO_PROFILER_SCOPE;

		mi_free(pMemory);
	}
}
