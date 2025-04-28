//
// Created by arthur on 23/04/2025.
//

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

namespace vkd
{
	std::array<VkExtensionProperties, 2> PhysicalDevice::s_supportedExtensions = {
		VkExtensionProperties{.extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .specVersion = 1},
		VkExtensionProperties{.extensionName = VK_EXT_DEBUG_MARKER_EXTENSION_NAME, .specVersion = 1},
	};

	PhysicalDevice::PhysicalDevice() :
		ObjectBase(ObjectType),
		m_instance(nullptr),
		m_physicalDeviceProperties(),
		m_queueFamilyProperties()
	{
	}

	void PhysicalDevice::SetInstance(Instance& instance)
	{
		m_instance = &instance;
	}

	void PhysicalDevice::SetPhysicalDeviceProperties(const VkPhysicalDeviceProperties& physicalDeviceProperties)
	{
		m_physicalDeviceProperties = physicalDeviceProperties;
	}

	const VkPhysicalDeviceProperties& PhysicalDevice::GetPhysicalDeviceProperties() const
	{
		return m_physicalDeviceProperties;
	}

	void PhysicalDevice::SetQueueFamilyProperties(const VkQueueFamilyProperties& queueFamilyProperties)
	{
		m_queueFamilyProperties = queueFamilyProperties;
	}

	const VkQueueFamilyProperties& PhysicalDevice::GetQueueFamilyProperties() const
	{
		return m_queueFamilyProperties;
	}

	VkResult PhysicalDevice::Create(const VkPhysicalDeviceProperties& physicalDeviceProperties)
	{
		m_physicalDeviceProperties = physicalDeviceProperties;
		return VK_SUCCESS;
	}

	void PhysicalDevice::GetPhysicalDeviceFeatures(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceFeatures* pFeatures)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
	}

	void PhysicalDevice::GetPhysicalDeviceFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
	}

	VkResult PhysicalDevice::GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format,
		VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
		VkImageFormatProperties* pImageFormatProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}

	void PhysicalDevice::GetPhysicalDeviceProperties(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceProperties* pProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		*pProperties = physicalDevice->GetPhysicalDeviceProperties();
	}

		void PhysicalDevice::GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pPhysicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
		{
			VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);

			if (pQueueFamilyPropertyCount && !pQueueFamilyProperties)
			{
				*pQueueFamilyPropertyCount = 0;
			}
			CCT_ASSERT_FALSE("Not Implemented");
		}

	void PhysicalDevice::GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pPhysicalDevice,
		VkPhysicalDeviceMemoryProperties* pMemoryProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
	}

	VkResult PhysicalDevice::EnumerateDeviceExtensionProperties(VkPhysicalDevice pPhysicalDevice, const char* pLayerName,
		uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");

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

	void PhysicalDevice::GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pPhysicalDevice,
		VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
		uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
	}

	void PhysicalDevice::DestroyPhysicalDevice(VkPhysicalDevice pPhysicalDevice)
	{
		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		if (!physicalDevice)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<PhysicalDevice>*>(pPhysicalDevice);
		mem::DeleteDispatchable(dispatchable);
	}
}
