/**
 * @file Fence.inl
 * @brief Inline implementations for Fence
 * @date 2025-10-25
 */

#pragma once

#include "Vkd/Device/Device.hpp"
#include "Vkd/Synchronization/Fence/Fence.hpp"

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

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* Fence::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkFenceCreateFlags Fence::GetFlags() const
	{
		AssertValid();
		return m_flags;
	}
} // namespace vkd
