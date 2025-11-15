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

		m_createResult = VK_SUCCESS;
		return m_createResult;
	}

	inline CommandPool* CommandBuffer::GetOwner() const
	{
		AssertValid();
		return m_owner;
	}

	inline VkCommandBufferLevel CommandBuffer::GetLevel() const
	{
		AssertValid();
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

	inline void CommandBuffer::PushFillBuffer(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, uint32_t data)
	{
		VKD_FROM_HANDLE(Buffer, bufferObj, dst);
		m_ops.emplace_back(Buffer::OpFill{ bufferObj, off, size, data });
	}

	inline void CommandBuffer::PushCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		std::vector<VkBufferCopy> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferCopy));
		m_ops.emplace_back(Buffer::OpCopy{ srcBufferObj, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyBuffer2(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy2* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		std::vector<VkBufferCopy2> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferCopy2));
		m_ops.emplace_back(Buffer::OpCopy2{ srcBufferObj, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushUpdateBuffer(VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
	{
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		std::vector<UInt8> data;
		data.resize(dataSize);
		std::memcpy(data.data(), pData, dataSize);
		m_ops.emplace_back(Buffer::OpUpdate{ dstBufferObj, dstOffset, std::move(data)});
	}

	inline void CommandBuffer::PushCopyImage(VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		std::vector<VkImageCopy> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkImageCopy));
		m_ops.emplace_back(Image::OpCopy{ srcImageObj, dstImageObj, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyBufferToImage(VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		std::vector<VkBufferImageCopy> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferImageCopy));
		m_ops.emplace_back(Buffer::OpCopyBufferToImage{ srcBufferObj, dstImageObj, dstImageLayout, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyImageToBuffer(VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		std::vector<VkBufferImageCopy> regions;
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferImageCopy));
		m_ops.emplace_back(Buffer::OpCopyImageToBuffer{ srcImageObj, srcImageLayout, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushClearColorImage(VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, UInt32 rangeCount, const VkImageSubresourceRange* pRanges)
	{
		VKD_FROM_HANDLE(Image, imageObj, image);

		std::vector<VkImageSubresourceRange> ranges;
		ranges.resize(rangeCount);
		std::memcpy(ranges.data(), pRanges, ranges.size() * sizeof(VkImageSubresourceRange));
		m_ops.emplace_back(Image::OpClearColorImage{ imageObj, imageLayout, *pColor, std::move(ranges)});
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
