/**
 * @file CommandBuffer.inl
 * @brief Inline implementations for CommandBuffer
 * @date 2025-10-25
 */

#pragma once

#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"

namespace vkd
{
	inline CommandBuffer::CommandBuffer() :
		ObjectBase(ObjectType),
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

	inline VkResult CommandBuffer::Begin(const VkCommandBufferBeginInfo& beginInfo)
	{
		VKD_AUTO_PROFILER_SCOPE();
		Transition(State::Recording, { State::Initial });

		return VK_SUCCESS;
	}

	inline VkResult CommandBuffer::End()
	{
		VKD_AUTO_PROFILER_SCOPE();
		Transition(State::Executable, { State::Recording });

		return VK_SUCCESS;
	}

	inline VkResult CommandBuffer::Reset(VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		Transition(State::Initial, { State::Executable, State::Pending });

		return VK_SUCCESS;
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

	inline void CommandBuffer::PushBindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
	{
		VKD_FROM_HANDLE(Pipeline, pipelineObject, pipeline);

		m_ops.emplace_back(OpBindPipeline{
			.BindPoint = pipelineBindPoint,
			.PipelineObject = pipelineObject,
		});
	}

	inline void CommandBuffer::PushBindVertexBuffer(std::span<const VkBuffer> pBuffers, std::span<const VkDeviceSize> pOffsets, UInt32 firstBinding)
	{
		std::vector<Buffer*> buffers;
		std::vector<VkDeviceSize> offsets;
		buffers.resize(pBuffers.size());
		offsets.resize(pBuffers.size());

		for (std::size_t i = 0; i < pBuffers.size(); ++i)
		{
			VKD_FROM_HANDLE(Buffer, bufferObject, pBuffers[i]);
			buffers[i] = bufferObject;
			offsets[i] = pOffsets[i];
		}

		m_ops.emplace_back(OpBindVertexBuffer{
				.Buffers = std::move(buffers),
				.Offsets = std::move(offsets),
				.FirstBinding = firstBinding,
		});
	}

	inline void CommandBuffer::PushDraw(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstVertex, UInt32 firstInstance)
	{
		m_ops.emplace_back(OpDraw{
			.VertexCount = vertexCount,
			.InstanceCount = instanceCount,
			.FirstVertex = firstVertex,
			.FirstInstance = firstInstance,
		});
	}

	inline VkResult CommandBuffer::MarkSubmitted()
	{
		return Transition(State::Pending, { State::Executable });
	}

	inline VkResult CommandBuffer::MarkComplete()
	{
		return Transition(State::Executable, { State::Pending });
	}

	inline std::span<const CommandBuffer::Op> CommandBuffer::GetOps() const
	{
		return m_ops;
	}

	inline bool CommandBuffer::IsSealed() const
	{
		return m_state == State::Executable;
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
