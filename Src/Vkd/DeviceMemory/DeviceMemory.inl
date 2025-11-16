/**
 * @file DeviceMemory.inl
 * @brief Inline implementations for DeviceMemory
 * @date 2025-10-27
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/DeviceMemory/DeviceMemory.hpp"

namespace vkd
{
	inline DeviceMemory::DeviceMemory() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_size(0),
		m_typeIndex(0),
		m_mapped(false)
	{
	}

	inline VkResult DeviceMemory::Create(Device& owner, const VkMemoryAllocateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_size = info.allocationSize;
		m_typeIndex = info.memoryTypeIndex;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* DeviceMemory::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkDeviceSize DeviceMemory::GetSize() const
	{
		AssertValid();
		return m_size;
	}

	inline UInt32 DeviceMemory::GetTypeIndex() const
	{
		AssertValid();
		return m_typeIndex;
	}

	inline bool DeviceMemory::IsMapped() const
	{
		AssertValid();
		return m_mapped;
	}
} // namespace vkd
