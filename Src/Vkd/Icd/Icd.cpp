/**
 * @file Icd.cpp
 * @brief Implementation of ICD interface functions
 * @date 2025-04-23
 */

#include "Vkd/Icd/Icd.hpp"
#include "Vkd/Instance/Instance.hpp"
#include "Vkd/Device/Device.hpp"
#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	VkResult Icd::NegotiateLoaderICDInterfaceVersion(uint32_t* pVersion)
	{
		*pVersion = 7;
		return VK_SUCCESS;
	}

	PFN_vkVoidFunction Icd::GetInstanceProcAddr(VkInstance pInstance, const char* pName)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (pName == nullptr)
			return nullptr;

		#define VKD_ENTRYPOINT_LOOKUP(klass, name)	\
			if (strcmp(pName, "vk" #name) == 0) \
				return (PFN_vkVoidFunction)static_cast<PFN_vk##name>(klass::name)
		#define VKD_ENTRYPOINT_LOOKUP_KHR(klass, name)	\
			if (strcmp(pName, "vk" #name "KHR") == 0)	\
				return (PFN_vkVoidFunction) static_cast<PFN_vk##name>(klass::name)

		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, EnumerateInstanceExtensionProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, EnumerateInstanceLayerProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, EnumerateInstanceVersion);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, CreateInstance);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, GetInstanceProcAddr);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, DestroyInstance);
		VKD_ENTRYPOINT_LOOKUP(vkd::Instance, EnumeratePhysicalDevices);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceFeatures);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceFeatures2);
		VKD_ENTRYPOINT_LOOKUP_KHR(vkd::PhysicalDevice, GetPhysicalDeviceFeatures2);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceFormatProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceImageFormatProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceProperties2);
		VKD_ENTRYPOINT_LOOKUP_KHR(vkd::PhysicalDevice, GetPhysicalDeviceProperties2);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceQueueFamilyProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceMemoryProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, EnumerateDeviceExtensionProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::PhysicalDevice, GetPhysicalDeviceSparseImageFormatProperties);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, GetDeviceProcAddr);
		VKD_ENTRYPOINT_LOOKUP(vkd::Device, CreateDevice);

		#undef VKD_ENTRYPOINT_LOOKUP
		#undef VKD_ENTRYPOINT_LOOKUP_KHR


		#define VKD_ICD_ENTRYPOINT_LOOKUP(klass, name)	\
			if (strcmp(pName, "vk_icd" #name) == 0) \
				return (PFN_vkVoidFunction) klass::name

		VKD_ICD_ENTRYPOINT_LOOKUP(vkd::Icd, NegotiateLoaderICDInterfaceVersion);
		VKD_ICD_ENTRYPOINT_LOOKUP(vkd::Icd, GetInstanceProcAddr);
		VKD_ICD_ENTRYPOINT_LOOKUP(vkd::Icd, GetPhysicalDeviceProcAddr);
	#ifdef CCT_PLATFORM_WINDOWS
		VKD_ICD_ENTRYPOINT_LOOKUP(vkd::Icd, EnumerateAdapterPhysicalDevices);
	#endif
	#undef VKD_ICD_ENTRYPOINT_LOOKUP

#ifdef VKD_DEBUG_CHECKS
		cct::Logger::Warning("Could not find '{}' function", pName);
#endif

		return nullptr;
	}

	PFN_vkVoidFunction Icd::GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName)
	{
		CCT_ASSERT_FALSE("Not Implemented");
		return nullptr;
	}

#if defined(CCT_PLATFORM_WINDOWS)

	VkResult Icd::EnumerateAdapterPhysicalDevices(VkInstance pInstance, LUID adapterLUID, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(vkd::Instance, instance, pInstance);

		instance->EnumeratePlatformPhysicalDevices();
		if (pPhysicalDeviceCount && !pPhysicalDevices)
		{
			*pPhysicalDeviceCount = static_cast<UInt32>(instance->GetPhysicalDevices().size());
			return VK_SUCCESS;
		}

		auto physicalDevices = instance->GetPhysicalDevices();

		std::size_t swapWith = 0;
		for (std::size_t i = 0; i < *pPhysicalDeviceCount; ++i)
		{
			//auto luid = static_cast<vkd::WddmPhysicalDevice&>(*physicalDevices[i]->Object).GetLuid();
			//
			//if (*reinterpret_cast<UInt64*>(&luid) == *reinterpret_cast<UInt64*>(&adapterLUID))
			//{
			//	swapWith = i;
			//}

			pPhysicalDevices[i] = VKD_TO_HANDLE(VkPhysicalDevice, physicalDevices[i]);
		}

		std::swap(pPhysicalDevices[0], pPhysicalDevices[swapWith]);

		return VK_SUCCESS;
	}
#endif
}