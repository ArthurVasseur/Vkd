/**
 * @file Buffer.inl
 * @brief Inline implementations for Buffer
 * @date 2025-10-26
 */

#pragma once

#include "Vkd/Buffer/Buffer.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline Buffer::Buffer() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_size(0),
		m_usage(0),
		m_memory(nullptr),
		m_memoryOffset(0)
	{
	}

	inline VkResult Buffer::Create(Device& owner, const VkBufferCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_size = info.size;
		m_usage = info.usage;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline void Buffer::BindBufferMemory(DeviceMemory& deviceMemory, VkDeviceSize memoryOffset)
	{
		m_memory = &deviceMemory;
		m_memoryOffset = memoryOffset;
	}

	inline void Buffer::GetMemoryRequirements(VkMemoryRequirements& memoryRequirements) const
	{
		memoryRequirements.size = GetSize();
		memoryRequirements.alignment = 16;
		memoryRequirements.memoryTypeBits = 0xFFFFFFFF;
	}

	inline Device* Buffer::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkDeviceSize Buffer::GetSize() const
	{
		AssertValid();
		return m_size;
	}

	inline VkBufferUsageFlags Buffer::GetUsage() const
	{
		AssertValid();
		return m_usage;
	}

	inline DeviceMemory* Buffer::GetMemory() const
	{
		AssertValid();
		return m_memory;
	}

	inline VkDeviceSize Buffer::GetMemoryOffset() const
	{
		AssertValid();
		return m_memoryOffset;
	}

	inline bool Buffer::IsBound() const
	{
		AssertValid();
		return m_memory != nullptr;
	}
}
