/**
 * @file PhysicalDevice.cpp
 * @brief Implementation of Vulkan physical device
 * @date 2025-04-23
 */

#include "Vkd/PhysicalDevice/PhysicalDevice.hpp"

#include <algorithm>
#include <bit>

#include "VkdUtils/System/System.hpp"

namespace vkd
{
	// Supported device extensions for the CPU backend
	std::array<VkExtensionProperties, 0> PhysicalDevice::s_supportedExtensions = {};

	PhysicalDevice::PhysicalDevice(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_instance(nullptr),
		m_physicalDeviceProperties(),
		m_queueFamilyProperties()
	{
	}

	VkResult PhysicalDevice::Create(Instance& owner, const VkPhysicalDeviceProperties& physicalDeviceProperties,
									const std::array<VkQueueFamilyProperties, 3>& queueFamilyProperties,
									const VkAllocationCallbacks& allocationCallbacks)
	{
		m_instance = &owner;
		m_physicalDeviceProperties = physicalDeviceProperties;
		m_queueFamilyProperties = queueFamilyProperties;

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	const VkPhysicalDeviceProperties& PhysicalDevice::GetPhysicalDeviceProperties() const
	{
		AssertValid();
		return m_physicalDeviceProperties;
	}

	std::span<VkQueueFamilyProperties> PhysicalDevice::GetQueueFamilyProperties()
	{
		AssertValid();
		return m_queueFamilyProperties;
	}

	void PhysicalDevice::GetPhysicalDeviceFeatures(VkPhysicalDevice pPhysicalDevice,
												   VkPhysicalDeviceFeatures* pFeatures)
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
		pFeatures->shaderFloat64 = VK_FALSE; // 64-bit floats not required for basic compute
		pFeatures->shaderInt64 = VK_FALSE; // 64-bit integers not required for basic compute

		// All other features remain VK_FALSE:
		// - No tessellation/geometry shader support (CPU backend)
		// - No advanced image operations (focus on buffers)
		// - No dual source blending, logic operations, etc. (no rendering pipeline)
		// - No sparse resources (not needed for basic CPU backend)
		// This minimal feature set supports vkCmdFillBuffer, vkCmdCopyBuffer,
		// and basic compute shaders for a software-based Vulkan implementation
	}

	void PhysicalDevice::GetPhysicalDeviceFeatures2(VkPhysicalDevice pPhysicalDevice,
													VkPhysicalDeviceFeatures2* pFeatures)
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
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
				{
					// Query state is on the host (QueryPool), so vkResetQueryPool is a host operation.
					reinterpret_cast<VkPhysicalDeviceHostQueryResetFeatures*>(pNext)->hostQueryReset = VK_TRUE;
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan11Features) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan12Features) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan13Features) - sizeof(VkBaseOutStructure));
					break;
				}
				default:
					break;
			}
			pNext = pNext->pNext;
		}
	}

	void PhysicalDevice::GetPhysicalDeviceFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format,
														   VkFormatProperties* pFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pFormatProperties, "pFormatProperties cannot be null");

		// Initialize to no support by default
		pFormatProperties->linearTilingFeatures = 0;
		pFormatProperties->optimalTilingFeatures = 0;
		pFormatProperties->bufferFeatures = 0;

		switch (format)
		{
			case VK_FORMAT_R8G8B8A8_UNORM:
			{
				const VkFormatFeatureFlags features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
													  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
													  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
													  VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
													  VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
				pFormatProperties->linearTilingFeatures = features;
				pFormatProperties->optimalTilingFeatures = features;
				pFormatProperties->bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
				break;
			}

			// All other formats: no support
			default:
				// Properties already initialized to 0
				break;
		}
	}

	VkResult PhysicalDevice::GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format,
																	VkImageType type, VkImageTiling tiling,
																	VkImageUsageFlags usage, VkImageCreateFlags flags,
																	VkImageFormatProperties* pImageFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pImageFormatProperties, "pImageFormatProperties cannot be null");

		std::memset(pImageFormatProperties, 0, sizeof(VkImageFormatProperties));

		const VkImageUsageFlags supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
													  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		if (format != VK_FORMAT_R8G8B8A8_UNORM || type != VK_IMAGE_TYPE_2D ||
			(tiling != VK_IMAGE_TILING_OPTIMAL && tiling != VK_IMAGE_TILING_LINEAR) ||
			(usage & ~supportedUsageFlags) != 0 || (flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) != 0)
			return VK_ERROR_FORMAT_NOT_SUPPORTED;

		const VkPhysicalDeviceLimits& limits = physicalDevice->GetPhysicalDeviceProperties().limits;

		pImageFormatProperties->maxExtent = {limits.maxImageDimension2D, limits.maxImageDimension2D, 1};
		pImageFormatProperties->maxMipLevels = std::bit_width(limits.maxImageDimension2D);
		pImageFormatProperties->maxArrayLayers = limits.maxImageArrayLayers;
		pImageFormatProperties->sampleCounts = (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
												   ? limits.framebufferColorSampleCounts
												   : VK_SAMPLE_COUNT_1_BIT;
		pImageFormatProperties->maxResourceSize = static_cast<VkDeviceSize>(limits.maxImageDimension2D) *
												  limits.maxImageDimension2D * limits.maxImageArrayLayers * 4;

		return VK_SUCCESS;
	}

	void PhysicalDevice::GetPhysicalDeviceProperties(VkPhysicalDevice pPhysicalDevice,
													 VkPhysicalDeviceProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);

		std::memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
		*pProperties = physicalDevice->GetPhysicalDeviceProperties();
	}

	void PhysicalDevice::GetPhysicalDeviceProperties2(VkPhysicalDevice pPhysicalDevice,
													  VkPhysicalDeviceProperties2* pProperties)
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
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan11Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan12Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:
				{
					std::memset(reinterpret_cast<char*>(pNext) + sizeof(VkBaseOutStructure), 0,
								sizeof(VkPhysicalDeviceVulkan13Properties) - sizeof(VkBaseOutStructure));
					break;
				}
				default:
					break;
			}
			pNext = pNext->pNext;
		}
	}

	void PhysicalDevice::GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pPhysicalDevice,
																uint32_t* pQueueFamilyPropertyCount,
																VkQueueFamilyProperties* pQueueFamilyProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);

		VKD_CHECK(pQueueFamilyPropertyCount);

		const auto properties = physicalDevice->GetQueueFamilyProperties();
		const UInt32 available = static_cast<UInt32>(properties.size());

		if (!pQueueFamilyProperties)
		{
			*pQueueFamilyPropertyCount = available;
			return;
		}

		const UInt32 toWrite = std::min(*pQueueFamilyPropertyCount, available);
		if (toWrite > 0)
			std::memcpy(pQueueFamilyProperties, properties.data(), toWrite * sizeof(VkQueueFamilyProperties));
		*pQueueFamilyPropertyCount = toWrite;
	}

	void PhysicalDevice::GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pPhysicalDevice,
														   VkPhysicalDeviceMemoryProperties* pMemoryProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");
		CCT_ASSERT(pMemoryProperties, "pMemoryProperties cannot be null");

		System system;
		const std::optional<UInt64> availableRam = system.GetAvailableRamBytes();
		const UInt64 heapSize = availableRam ? System::ComputeDeviceMemoryHeapSize(*availableRam) : 256ULL * 1024ULL * 1024ULL;

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
		pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
														  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
														  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		pMemoryProperties->memoryTypes[0].heapIndex = 0; // References the heap defined above
	}

	VkResult PhysicalDevice::EnumerateDeviceExtensionProperties(VkPhysicalDevice pPhysicalDevice,
																const char* pLayerName, uint32_t* pPropertyCount,
																VkExtensionProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		CCT_ASSERT(physicalDevice, "Invalid VkPhysicalDevice pointer");

		if (pLayerName)
			return VK_ERROR_LAYER_NOT_PRESENT;

		VKD_CHECK(pPropertyCount);

		const UInt32 available = static_cast<UInt32>(s_supportedExtensions.size());
		if (!pProperties)
		{
			*pPropertyCount = available;
			return VK_SUCCESS;
		}

		const UInt32 toWrite = std::min(*pPropertyCount, available);
		if (toWrite > 0)
			std::memcpy(pProperties, s_supportedExtensions.data(), toWrite * sizeof(VkExtensionProperties));
		*pPropertyCount = toWrite;
		return toWrite < available ? VK_INCOMPLETE : VK_SUCCESS;
	}

	void PhysicalDevice::GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pPhysicalDevice, VkFormat format,
																	  VkImageType type, VkSampleCountFlagBits samples,
																	  VkImageUsageFlags usage, VkImageTiling tiling,
																	  uint32_t* pPropertyCount,
																	  VkSparseImageFormatProperties* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		VKD_CHECK(pPropertyCount);

		if (!pProperties)
		{
			*pPropertyCount = 0;
			return;
		}

		*pPropertyCount = 0;
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

	void PhysicalDevice::GetPhysicalDeviceFormatProperties2(VkPhysicalDevice pPhysicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceFormatProperties2 not implemented (Vulkan 1.1)");
	}

	VkResult PhysicalDevice::GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceImageFormatProperties2 not implemented (Vulkan 1.1)");
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	void PhysicalDevice::GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice pPhysicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		VKD_CHECK(pQueueFamilyPropertyCount);

		const auto properties = physicalDevice->GetQueueFamilyProperties();
		const UInt32 available = static_cast<UInt32>(properties.size());

		if (!pQueueFamilyProperties)
		{
			*pQueueFamilyPropertyCount = available;
			return;
		}

		const UInt32 toWrite = std::min(*pQueueFamilyPropertyCount, available);
		for (UInt32 i = 0; i < toWrite; ++i)
		{
			pQueueFamilyProperties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
			pQueueFamilyProperties[i].queueFamilyProperties = properties[i];
		}
		*pQueueFamilyPropertyCount = toWrite;
	}

	void PhysicalDevice::GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pPhysicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceMemoryProperties2 not implemented (Vulkan 1.1)");
	}

	void PhysicalDevice::GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VKD_FROM_HANDLE(PhysicalDevice, physicalDevice, pPhysicalDevice);
		VKD_CHECK(pPropertyCount);

		if (!pProperties)
		{
			*pPropertyCount = 0;
			return;
		}

		*pPropertyCount = 0;
	}

	void PhysicalDevice::GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceExternalBufferProperties not implemented (Vulkan 1.1)");
	}

	void PhysicalDevice::GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceExternalFenceProperties not implemented (Vulkan 1.1)");
	}

	void PhysicalDevice::GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice pPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceExternalSemaphoreProperties not implemented (Vulkan 1.1)");
	}

	VkResult PhysicalDevice::GetPhysicalDeviceToolProperties(VkPhysicalDevice pPhysicalDevice, uint32_t* pToolCount, VkPhysicalDeviceToolProperties* pToolProperties)
	{
		VKD_AUTO_PROFILER_SCOPE();
		cct::Logger::Warning("vkGetPhysicalDeviceToolProperties not implemented (Vulkan 1.3)");
		VKD_CHECK(pToolCount);

		if (!pToolProperties)
		{
			*pToolCount = 0;
			return VK_SUCCESS;
		}

		*pToolCount = 0;
		return VK_SUCCESS;
	}
} // namespace vkd
