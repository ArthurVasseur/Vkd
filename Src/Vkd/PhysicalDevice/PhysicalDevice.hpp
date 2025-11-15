/**
 * @file PhysicalDevice.hpp
 * @brief Vulkan physical device abstraction
 * @date 2025-04-23
 *
 * Represents a physical device and provides device properties, queue family information,
 * and device creation capabilities.
 */

#pragma once

#include <array>
#include <span>

#include "Vkd/ObjectBase/ObjectBase.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	class Instance;

	class PhysicalDevice : public ObjectBase
	{
	public:
		static constexpr VkObjectType ObjectType = VK_OBJECT_TYPE_PHYSICAL_DEVICE;
		VKD_DISPATCHABLE_HANDLE(PhysicalDevice);

		PhysicalDevice();
		~PhysicalDevice() override = default;

		[[nodiscard]] const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const;
		[[nodiscard]] std::span<VkQueueFamilyProperties> GetQueueFamilyProperties();

		virtual VkResult Create(Instance& owner, const VkAllocationCallbacks& allocationCallbacks) = 0;
		virtual DispatchableObjectResult<Device> CreateDevice() = 0;

		// Vulkan API entry points
		static void VKAPI_CALL GetPhysicalDeviceFeatures(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceFeatures* pFeatures);
		static void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceFeatures2* pFeatures);
		static void VKAPI_CALL GetPhysicalDeviceFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);
		static VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties);
		static void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceProperties* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceProperties2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceProperties2* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pPhysicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties);
		static void VKAPI_CALL GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);
		static VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice pPhysicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties);
		static void VKAPI_CALL DestroyPhysicalDevice(VkPhysicalDevice pPhysicalDevice);

	protected:
		VkResult Create(Instance& owner, const VkPhysicalDeviceProperties& physicalDeviceProperties, const std::array<VkQueueFamilyProperties, 3>& queueFamilyProperties, const VkAllocationCallbacks& allocationCallbacks);
	private:
		static std::array<VkExtensionProperties, 0> s_supportedExtensions;

		Instance* m_instance;
		VkPhysicalDeviceProperties m_physicalDeviceProperties;
		std::array<VkQueueFamilyProperties, 3 /*(graphics + compute + transfer), graphics, transfer*/> m_queueFamilyProperties;
	};
}
