//
// Created by arthur on 23/04/2025.
//

#pragma once

// STL
#include <array>

// Project
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
		
		void SetInstance(Instance& instance);
		void SetPhysicalDeviceProperties(const VkPhysicalDeviceProperties& physicalDeviceProperties);
		const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const;
		void SetQueueFamilyProperties(const VkQueueFamilyProperties& queueFamilyProperties);
		const VkQueueFamilyProperties& GetQueueFamilyProperties() const;

		virtual VkResult Create(const VkPhysicalDeviceProperties& physicalDeviceProperties);

		static void VKAPI_CALL GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures);
		static void VKAPI_CALL GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);
		static VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties);
		static void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties);
		static void VKAPI_CALL GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);
		static VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
		static void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties);
		static void VKAPI_CALL DestroyPhysicalDevice(VkPhysicalDevice pPhysicalDevice);
	private:
		static std::array<VkExtensionProperties, 2> s_supportedExtensions;

		Instance* m_instance;
		VkPhysicalDeviceProperties m_physicalDeviceProperties;
		VkQueueFamilyProperties m_queueFamilyProperties;
	};
}
