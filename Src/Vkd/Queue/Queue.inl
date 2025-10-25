#pragma once

#include "Vkd/Queue/Queue.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline Queue::Queue() :
		ObjectBase(VK_OBJECT_TYPE_QUEUE),
		m_owner(nullptr),
		m_queueFamilyIndex(0),
		m_queueIndex(0)
	{
	}

	inline VkResult Queue::Create(Device& owner, uint32_t queueFamilyIndex, uint32_t queueIndex)
	{
		m_owner = &owner;
		m_queueFamilyIndex = queueFamilyIndex;
		m_queueIndex = queueIndex;

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
}
