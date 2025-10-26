#pragma once

#include "Vkd/DeviceMemory/DeviceMemory.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline DeviceMemory::DeviceMemory() :
		ObjectBase(VK_OBJECT_TYPE_DEVICE_MEMORY),
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

		return VK_SUCCESS;
	}

	inline Device* DeviceMemory::GetOwner() const
	{
		return m_owner;
	}

	inline VkDeviceSize DeviceMemory::GetSize() const
	{
		return m_size;
	}

	inline uint32_t DeviceMemory::GetTypeIndex() const
	{
		return m_typeIndex;
	}

	inline bool DeviceMemory::IsMapped() const
	{
		return m_mapped;
	}
}
