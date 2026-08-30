/**
 * @file Event.inl
 * @brief Inline implementations for Event
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/Synchronization/Event/Event.hpp"

namespace vkd
{
	inline Event::Event(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr)
	{
	}

	inline VkResult Event::Create(Device& owner, const VkEventCreateInfo& createInfo)
	{
		m_owner = &owner;

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* Event::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}
} // namespace vkd
