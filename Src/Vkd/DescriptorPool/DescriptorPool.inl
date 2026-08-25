/**
 * @file DescriptorPool.inl
 * @brief Inline implementations for DescriptorPool
 * @date 2026-08-25
 */

#pragma once

#include "Vkd/DescriptorPool/DescriptorPool.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline DescriptorPool::DescriptorPool() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_flags(0)
	{
	}

	inline VkResult DescriptorPool::Create(Device& owner, const VkDescriptorPoolCreateInfo& info, const VkAllocationCallbacks& allocationCallbacks)
	{
		m_owner = &owner;
		m_flags = info.flags;

		SetAllocationCallbacks(allocationCallbacks);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* DescriptorPool::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkDescriptorPoolCreateFlags DescriptorPool::GetFlags() const
	{
		AssertValid();
		return m_flags;
	}
} // namespace vkd
