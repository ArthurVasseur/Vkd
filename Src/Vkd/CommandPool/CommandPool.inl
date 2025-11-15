/**
 * @file CommandPool.inl
 * @brief Inline implementations for CommandPool
 * @date 2025-10-27
 */

#pragma once

#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Device/Device.hpp"

namespace vkd
{
	inline CommandPool::CommandPool() :
		ObjectBase(ObjectType),
		m_owner(nullptr),
		m_flags(0),
		m_queueFamilyIndex(0)
	{
	}

	inline VkResult CommandPool::Create(Device& owner, const VkCommandPoolCreateInfo& createInfo, const VkAllocationCallbacks& pAllocator)
	{
		m_owner = &owner;
		m_flags = createInfo.flags;
		m_queueFamilyIndex = createInfo.queueFamilyIndex;

		SetAllocationCallbacks(pAllocator);

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline Device* CommandPool::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkCommandPoolCreateFlags CommandPool::GetFlags() const
	{
		AssertValid();
		return m_flags;
	}

	inline UInt32 CommandPool::GetQueueFamilyIndex() const
	{
		AssertValid();
		return m_queueFamilyIndex;
	}
}
