#pragma once

#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"

namespace vkd
{
	inline CommandBuffer::CommandBuffer() :
		ObjectBase(VK_OBJECT_TYPE_COMMAND_BUFFER),
		m_owner(nullptr),
		m_level(VK_COMMAND_BUFFER_LEVEL_PRIMARY),
		m_state(State::Initial)
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

	inline void CommandBuffer::PushFill(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, uint32_t data)
	{
		VKD_FROM_HANDLE(Buffer, bufferObj, dst);
		m_ops.emplace_back(Buffer::OpFill{ bufferObj, off, size, data });
	}

	inline void CommandBuffer::PushCopy(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		std::vector<VkBufferCopy> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferCopy));
		m_ops.emplace_back(Buffer::OpCopy{ srcBufferObj, dstBufferObj, std::move(regions)});
	}

	inline VkResult CommandBuffer::MarkSubmitted()
	{
		return Transition(State::Pending, { State::Executable });
	}

	inline VkResult CommandBuffer::MarkComplete()
	{
		return Transition(State::Executable, { State::Pending });
	}

	inline VkResult CommandBuffer::Transition(State to, std::initializer_list<State> allowed)
	{
		for (State s : allowed)
		{
			if (m_state == s)
			{
				m_state = to;
				return VK_SUCCESS;
			}
		}
		m_state = State::Invalid;
		CCT_ASSERT_FALSE("Invalid CB state transition {} -> {}", (int)m_state, (int)to);
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}
}
