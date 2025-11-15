/**
 * @file PhysicalDevice.cpp
 * @brief Implementation of Vulkan physical device
 * @date 2025-04-23
 */

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"
#include "VkdUtils/System/System.hpp"

namespace vkd
{
	// Supported device extensions for the CPU backend
	std::array<VkExtensionProperties, 0> PhysicalDevice::s_supportedExtensions = {};

	PhysicalDevice::PhysicalDevice() :
		ObjectBase(ObjectType),
		m_instance(nullptr),
		m_physicalDeviceProperties(),
		m_queueFamilyProperties()
	{
	}

	VkResult PhysicalDevice::Create(Instance& owner, const VkPhysicalDeviceProperties& physicalDeviceProperties, const std::array<VkQueueFamilyProperties, 3>& queueFamilyProperties, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_instance = &owner;
		m_physicalDeviceProperties = physicalDeviceProperties;
		m_queueFamilyProperties = queueFamilyProperties;
		SetAllocationCallbacks(allocationCallbacks);

		return VK_SUCCESS;
	}

	const VkPhysicalDeviceProperties& PhysicalDevice::GetPhysicalDeviceProperties() const
	{
		return m_physicalDeviceProperties;
	}

	std::span<VkQueueFamilyProperties> PhysicalDevice::GetQueueFamilyProperties()
	{
		return m_queueFamilyProperties;
	}


	void PhysicalDevice::GetPhysicalDeviceFeatures(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceFeatures* pFeatures)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pFeatures, "pFeatures cannot be null");

		// Initialize all features to VK_FALSE
		std::memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures));

		// robustBufferAccess is required by the Vulkan specification
		// All implementations must support this feature
		pFeatures->robustBufferAccess = VK_TRUE;

		// Enable basic compute shader support for simple compute pipelines
		// This is necessary for vkCmdDispatch and compute operations
		pFeatures->shaderFloat64 = VK_FALSE;  // 64-bit floats not required for basic compute
		pFeatures->shaderInt64 = VK_FALSE;    // 64-bit integers not required for basic compute

		// All other features remain VK_FALSE:
		// - No tessellation/geometry shader support (CPU backend)
		// - No advanced image operations (focus on buffers)
		// - No dual source blending, logic operations, etc. (no rendering pipeline)
		// - No sparse resources (not needed for basic CPU backend)
		// This minimal feature set supports vkCmdFillBuffer, vkCmdCopyBuffer,
		// and basic compute shaders for a software-based Vulkan implementation
	}

	void PhysicalDevice::GetPhysicalDeviceFeatures2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceFeatures2* pFeatures)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pFeatures, "pFeatures cannot be null");

		pFeatures->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		GetPhysicalDeviceFeatures(pPhysicalDevice, &pFeatures->features);

		VkBaseOutStructure* pNext = static_cast<VkBaseOutStructure*>(pFeatures->pNext);
		while (pNext)
		{
			switch (pNext->sType)
			{
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,sizeof(VkPhysicalDeviceVulkan11Features) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,sizeof(VkPhysicalDeviceVulkan12Features) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,sizeof(VkPhysicalDeviceVulkan13Features) - sizeof(VkBaseOutStructure));
					break;
				}
				default:
					break;
			}
			pNext = pNext->pNext;
		}
	}

	void PhysicalDevice::GetPhysicalDeviceFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pFormatProperties, "pFormatProperties cannot be null");

		// Initialize to no support by default
		pFormatProperties->linearTilingFeatures = 0;
		pFormatProperties->optimalTilingFeatures = 0;
		pFormatProperties->bufferFeatures = 0;

		// CPU backend primarily supports buffer operations
		// Image operations are not yet implemented
		switch (format)
		{
			// TODO: Add support for VK_FORMAT_R8G8B8A8_UNORM when image operations are implemented
			// This would enable basic RGBA8 image support for the CPU backend
			case VK_FORMAT_R8G8B8A8_UNORM:
				// Currently no image support - all features remain 0
				// Future: enable VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
				break;

			// All other formats: no support
			default:
				// Properties already initialized to 0
				break;
		}
	}

	VkResult PhysicalDevice::GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format,
		VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
		VkImageFormatProperties* pImageFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pImageFormatProperties, "pImageFormatProperties cannot be null");

		// CPU backend does not currently support image operations
		// Images require significant infrastructure (tiling, sampling, format conversion, etc.)

		// TODO: Implement support for VK_FORMAT_R8G8B8A8_UNORM with VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		// TODO: Implement support for VK_FORMAT_R8G8B8A8_UNORM with VK_IMAGE_USAGE_TRANSFER_DST_BIT
		// TODO: Implement support for VK_IMAGE_TYPE_2D with VK_IMAGE_TILING_LINEAR

		// Check for unsupported image usage flags
		const VkImageUsageFlags unsupportedUsageFlags =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		if (usage & unsupportedUsageFlags)
		{
			// All image usages are currently unsupported
			return VK_ERROR_FORMAT_NOT_SUPPORTED;
		}

		// Check for unsupported tiling modes
		if (tiling == VK_IMAGE_TILING_OPTIMAL || tiling == VK_IMAGE_TILING_LINEAR)
		{
			// Both tiling modes are currently unsupported
			return VK_ERROR_FORMAT_NOT_SUPPORTED;
		}

		// All image formats are currently unsupported
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	void PhysicalDevice::GetPhysicalDeviceProperties(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);

		std::memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
		*pProperties = physicalDevice->GetPhysicalDeviceProperties();
	}

	void PhysicalDevice::GetPhysicalDeviceProperties2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceProperties2* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pProperties, "pProperties cannot be null");

		pProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		pProperties->properties = physicalDevice->GetPhysicalDeviceProperties();

		VkBaseOutStructure* pNext = reinterpret_cast<VkBaseOutStructure*>(pProperties->pNext);
		while (pNext)
		{
			switch (pNext->sType)
			{
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0, sizeof(VkPhysicalDeviceVulkan11Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0, sizeof(VkPhysicalDeviceVulkan12Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0, sizeof(VkPhysicalDeviceVulkan13Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				default:
					break;
			}
			pNext = pNext->pNext;
		}
	}

	void PhysicalDevice::GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pPhysicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);

		if (pQueueFamilyPropertyCount && !pQueueFamilyProperties)
		{
			*pQueueFamilyPropertyCount = static_cast<UInt32>(physicalDevice->GetQueueFamilyProperties().size());
			return;
		}

		// Return the queue family properties
		if (pQueueFamilyPropertyCount && pQueueFamilyProperties && *pQueueFamilyPropertyCount > 0)
		{
			auto properties = physicalDevice->GetQueueFamilyProperties();
			std::memcpy(pQueueFamilyProperties, properties.data(), properties.size_bytes());
			*pQueueFamilyPropertyCount = static_cast<UInt32>(properties.size());
		}
	}

	void PhysicalDevice::GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pMemoryProperties, "pMemoryProperties cannot be null");

		System system;
		const UInt64 totalRam = system.GetTotalRamBytes();
		const UInt64 heapSize = System::ComputeDeviceMemoryHeapSize(totalRam);

		// Initialize memory properties
		pMemoryProperties->memoryHeapCount = 1;
		pMemoryProperties->memoryTypeCount = 1;

		// Define the single memory heap (system RAM)
		// No VK_MEMORY_HEAP_DEVICE_LOCAL_BIT because this is CPU-accessible system memory
		pMemoryProperties->memoryHeaps[0].size = heapSize;
		pMemoryProperties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

		// Define the single memory type
		// HOST_VISIBLE: CPU can map and access this memory
		// HOST_COHERENT: No explicit cache management needed (simplifies CPU backend)
		pMemoryProperties->memoryTypes[0].propertyFlags =
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		pMemoryProperties->memoryTypes[0].heapIndex = 0; // References the heap defined above
	}

	VkResult PhysicalDevice::EnumerateDeviceExtensionProperties(VkPhysicalDevice pPhysicalDevice, const char* pLayerName,
		uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");

		if (pLayerName)
			return VK_ERROR_LAYER_NOT_PRESENT;

		if (pPropertyCount && !pProperties)
		{
			*pPropertyCount = s_supportedExtensions.size();
			return VK_SUCCESS;
		}

		// std::size_t max = std::min(static_cast<std::size_t>(*pPropertyCount), s_supportedExtensions.size());
		// if (max > 0)
		// 	std::memcpy(pProperties, s_supportedExtensions.data(), max * sizeof(VkExtensionProperties));

		// if (max < s_supportedExtensions.size())
		// 	return VK_INCOMPLETE;

		return VK_SUCCESS;
	}

	void PhysicalDevice::GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pPhysicalDevice,
		VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
		uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT_FALSE("Not Implemented");
	}

	void PhysicalDevice::DestroyPhysicalDevice(VkPhysicalDevice pPhysicalDevice)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		if (!physicalDevice)
			return;

		auto* dispatchable = reinterpret_cast<DispatchableObject<PhysicalDevice>*>(pPhysicalDevice);
		mem::DeleteDispatchable(dispatchable);
	}
}
