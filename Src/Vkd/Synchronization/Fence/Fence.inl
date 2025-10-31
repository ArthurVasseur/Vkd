/**
 * @file Fence.inl
 * @brief Inline implementations for Fence
 * @date 2025-10-25
 */

#pragma once

#include "Vkd/Synchronization/Fence/Fence.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline Fence::Fence() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_flags(0)
	{
	}

	inline VkResult Fence::Create(Device& owner, const VkFenceCreateInfo& createInfo)
	{
		m_owner = &owner;
		m_flags = createInfo.flags;

		SetAllocationCallbacks(m_owner->GetAllocationCallbacks());

		return VK_SUCCESS;
	}

	inline Device* Fence::GetOwner() const
	{
		return m_owner;
	}

	inline VkFenceCreateFlags Fence::GetFlags() const
	{
		return m_flags;
	}
}
