#pragma once

#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"

namespace vkd::software
{
	inline DeviceMemory::DeviceMemory() :
		m_mapOffset(0)
	{
	}

	inline uint8_t* DeviceMemory::Data()
	{
		return m_data.data();
	}

	inline const uint8_t* DeviceMemory::Data() const
	{
		return m_data.data();
	}
}
