/**
 * @file EntryPoint.cpp
 * @brief ICD entry points for the Vulkan driver
 * @date 2026-08-31
 *
 * Exports the required ICD interface functions for loader communication.
 */

#include <Vkd/Icd/Icd.hpp>

namespace
{
#if defined(VKD_PROFILING) && defined(CCT_PLATFORM_WINDOWS)
	const char ModuleAnchor = 0;

	void PinDriverModule()
	{
		[[maybe_unused]] static const bool pinned = []
		{
			HMODULE module = nullptr;
			return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
									  reinterpret_cast<LPCWSTR>(&ModuleAnchor),
									  &module) != FALSE;
		}();
	}
#else
	void PinDriverModule()
	{
	}
#endif
} // namespace

VKD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion)
{
	PinDriverModule();
	return vkd::Icd::NegotiateLoaderICDInterfaceVersion(pVersion);
}

VKD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName)
{
	PinDriverModule();
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
