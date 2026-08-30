/**
 * @file Semaphore.inl
 * @brief Inline implementations for Semaphore
 * @date 2026-08-30
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/Synchronization/Semaphore/Semaphore.hpp"

namespace vkd
{
	inline Semaphore::Semaphore(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr)
	{
	}

	inline VkResult Semaphore::Create(Device& owner, const VkSemaphoreCreateInfo& createInfo)
	{
		m_owner = &owner;

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* Semaphore::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}
} // namespace vkd
