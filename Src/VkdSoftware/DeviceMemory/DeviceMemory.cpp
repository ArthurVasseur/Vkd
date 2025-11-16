/**
 * @file DeviceMemory.cpp
 * @brief Implementation of software renderer device memory
 * @date 2025-10-27
 */

#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"

#include "VkdSoftware/Device/Device.hpp"

namespace vkd::software
{
	DeviceMemory::~DeviceMemory()
	{
		if (m_allocation.size > 0 && m_owner)
		{
			auto* softwareDevice = static_cast<SoftwareDevice*>(m_owner);
			softwareDevice->GetAllocator().Free(m_allocation);
		}
	}

	VkResult DeviceMemory::Create(vkd::Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VkResult result = vkd::DeviceMemory::Create(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		auto* softwareDevice = static_cast<SoftwareDevice*>(&owner);
		if (!softwareDevice->GetAllocator().Allocate(info.allocationSize, 16, m_allocation))
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;

		return VK_SUCCESS;
	}

	VkResult DeviceMemory::Map(VkDeviceSize offset, VkDeviceSize size, void** ppData)
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (offset >= m_size)
		{
			CCT_ASSERT_FALSE("Map offset out of range");
			return VK_ERROR_MEMORY_MAP_FAILED;
		}

		m_mapOffset = offset;
		*ppData = Data() + m_mapOffset;

		return VK_SUCCESS;
	}

	void DeviceMemory::Unmap()
	{
		VKD_AUTO_PROFILER_SCOPE();

		m_mapOffset = 0;
	}
} // namespace vkd::software
