/**
 * @file DeviceMemory.inl
 * @brief Inline implementations for software renderer device memory
 * @date 2025-10-27
 */

#pragma once

#include "VkdSoftware/Device/Device.hpp"
#include "VkdSoftware/DeviceMemory/DeviceMemory.hpp"

namespace vkd::software
{
	inline DeviceMemory::DeviceMemory() :
		m_allocation{0, 0},
		m_mapOffset(0)
	{
	}

	inline UByte* DeviceMemory::Data()
	{
		if (m_allocation.size == 0)
			return nullptr;
		auto* softwareDevice = static_cast<SoftwareDevice*>(m_owner);
		return softwareDevice->GetAllocator().GetPoolBase() + m_allocation.offset;
	}

	inline const UByte* DeviceMemory::Data() const
	{
		if (m_allocation.size == 0)
			return nullptr;
		auto* softwareDevice = static_cast<const SoftwareDevice*>(m_owner);
		return const_cast<SoftwareDevice*>(softwareDevice)->GetAllocator().GetPoolBase() + m_allocation.offset;
	}
} // namespace vkd::software
