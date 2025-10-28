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

		return VK_SUCCESS;
	}

	inline Device* CommandPool::GetOwner() const
	{
		return m_owner;
	}

	inline VkCommandPoolCreateFlags CommandPool::GetFlags() const
	{
		return m_flags;
	}

	inline UInt32 CommandPool::GetQueueFamilyIndex() const
	{
		return m_queueFamilyIndex;
	}
}
