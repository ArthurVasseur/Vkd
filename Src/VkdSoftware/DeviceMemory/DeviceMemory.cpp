//
// Created by arthur on 27/10/2025.
//

#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"

namespace vkd::software
{
	VkResult DeviceMemory::Create(vkd::Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		VKD_AUTO_PROFILER_SCOPE();

		VkResult result = vkd::DeviceMemory::Create(owner, info, allocationCallbacks);
		if (result != VK_SUCCESS)
			return result;

		try
		{
			m_data.resize(info.allocationSize);
		}
		catch (const std::bad_alloc&)
		{
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}

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
		*ppData = m_data.data() + m_mapOffset;

		return VK_SUCCESS;
	}

	void DeviceMemory::Unmap()
	{
		VKD_AUTO_PROFILER_SCOPE();

		m_mapOffset = 0;
	}
}
