#pragma once

#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd
{
	inline CommandBuffer::CommandBuffer() :
		ObjectBase(VK_OBJECT_TYPE_COMMAND_BUFFER),
		m_owner(nullptr),
		m_level(VK_COMMAND_BUFFER_LEVEL_PRIMARY)
	{
	}

	inline VkResult CommandBuffer::Create(CommandPool& owner, VkCommandBufferLevel level)
	{
		m_owner = &owner;
		m_level = level;

		SetAllocationCallbacks(m_owner->GetAllocationCallbacks());

		return VK_SUCCESS;
	}

	inline CommandPool* CommandBuffer::GetOwner() const
	{
		return m_owner;
	}

	inline VkCommandBufferLevel CommandBuffer::GetLevel() const
	{
		return m_level;
	}
}
