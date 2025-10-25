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
		static VKAPI_ATTR VkResult VKAPI_CALL NegotiateLoaderICDInterfaceVersion(uint32_t* pVersion);
		static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName);
		static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);

#if defined(CCT_PLATFORM_WINDOWS)
		static VKAPI_ATTR VkResult VKAPI_CALL EnumerateAdapterPhysicalDevices(VkInstance instance, LUID adapterLUID, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
#endif
	};

}

