//
// Created by arthur on 23/04/2025.
//

#pragma once

#include "Vkd/Defines.hpp"

namespace vkd
{
	class Icd
	{
	public:
		static VkResult VKAPI_CALL NegotiateLoaderICDInterfaceVersion(uint32_t* pVersion);
		static PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName);
		static PFN_vkVoidFunction VKAPI_CALL GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);
#ifdef CCT_PLATFORM_WINDOWS
		static VkResult VKAPI_CALL EnumerateAdapterPhysicalDevices(VkInstance instance, LUID adapterLUID, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
#endif
	private:
	};

}

VKD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion);
VKD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
VKD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);

#if defined(CCT_PLATFORM_WINDOWS)
VKD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateAdapterPhysicalDevices(VkInstance instance, LUID adapterLUID, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
#endif