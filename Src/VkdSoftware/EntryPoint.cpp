//
// Created by arthur on 25/11/2025.
//

#include <Vkd/Icd/Icd.hpp>

VKD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion)
{
	return vkd::Icd::NegotiateLoaderICDInterfaceVersion(pVersion);
}

VKD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName)
{
	return vkd::Icd::GetInstanceProcAddr(instance, pName);
}

VKD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName)
{
	return vkd::Icd::GetPhysicalDeviceProcAddr(instance, pName);
}

#if defined(CCT_PLATFORM_WINDOWS)
VKD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdEnumerateAdapterPhysicalDevices(VkInstance instance, LUID adapterLUID, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices)
{
	return vkd::Icd::EnumerateAdapterPhysicalDevices(instance, adapterLUID, pPhysicalDeviceCount, pPhysicalDevices);
}
#endif