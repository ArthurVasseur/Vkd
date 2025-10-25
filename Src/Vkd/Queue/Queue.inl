#pragma once

#include "Vkd/Queue/Queue.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline Queue::Queue() :
		ObjectBase(VK_OBJECT_TYPE_QUEUE),
		m_owner(nullptr),
		m_queueFamilyIndex(0),
		m_queueIndex(0),
		m_flags(0)
	{
	}

	inline VkResult Queue::Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex, VkDeviceQueueCreateFlags flags)
	{
		m_owner = &owner;
		m_queueFamilyIndex = queueFamilyIndex;
		m_queueIndex = queueIndex;
		m_flags = flags;

		SetAllocationCallbacks(m_owner->GetAllocationCallbacks());

		return VK_SUCCESS;
	}

	inline Device* Queue::GetOwner() const
	{
		return m_owner;
	}

	inline uint32_t Queue::GetQueueFamilyIndex() const
	{
		return m_queueFamilyIndex;
	}

	inline uint32_t Queue::GetQueueIndex() const
	{
		return m_queueIndex;
	}

	inline VkDeviceQueueCreateFlags Queue::GetFlags() const
	{
		return m_flags;
	}
}
