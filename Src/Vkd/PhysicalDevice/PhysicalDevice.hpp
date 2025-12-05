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

#include "Vkd/Device/Device.hpp"
#include "Vkd/ObjectBase/ObjectBase.hpp"

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

		static void VKAPI_CALL GetPhysicalDeviceFormatProperties2(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties);
		static VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties);
		static void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice pPhysicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties);
		static void VKAPI_CALL GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties);
		static void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties);
		static void VKAPI_CALL GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties);
		static void VKAPI_CALL GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties);
		static VkResult VKAPI_CALL GetPhysicalDeviceToolProperties(VkPhysicalDevice pPhysicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties);

	protected:
		VkResult Create(Instance& owner, const VkPhysicalDeviceProperties& physicalDeviceProperties, const std::array<VkQueueFamilyProperties, 3>& queueFamilyProperties, const VkAllocationCallbacks& allocationCallbacks);

	private:
		static std::array<VkExtensionProperties, 0> s_supportedExtensions;

		Instance* m_instance;
		VkPhysicalDeviceProperties m_physicalDeviceProperties;
		std::array<VkQueueFamilyProperties, 3 /*(graphics + compute + transfer), graphics, transfer*/> m_queueFamilyProperties;
	};
} // namespace vkd
