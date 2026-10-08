/**
 * @file CommandBuffer.inl
 * @brief Inline implementations for CommandBuffer
 * @date 2025-10-25
 */

#pragma once

#include <new>
#include <utility>

#include "Vkd/CommandBuffer/CommandBuffer.hpp"
#include "Vkd/CommandPool/CommandPool.hpp"
#include "Vkd/DescriptorSet/DescriptorSet.hpp"
#include "Vkd/DescriptorSetLayout/DescriptorSetLayout.hpp"
#include "Vkd/Framebuffer/Framebuffer.hpp"
#include "Vkd/Pipeline/Pipeline.hpp"
#include "Vkd/QueryPool/QueryPool.hpp"
#include "Vkd/RenderPass/RenderPass.hpp"
#include "Vkd/Synchronization/Event/Event.hpp"

namespace vkd
{
	inline CommandBuffer::CommandBuffer(const VkAllocationCallbacks& allocationCallbacks) :
		ObjectBase(ObjectType, allocationCallbacks),
		m_owner(nullptr),
		m_level(VK_COMMAND_BUFFER_LEVEL_PRIMARY),
		m_state(State::Initial),
		m_usageFlags(0),
		m_pendingSubmits(0),
		m_ops(CommandAllocator<Op>(allocationCallbacks)),
		m_recordingError(VK_SUCCESS)
	{
	}

	inline VkResult CommandBuffer::Create(CommandPool& owner, VkCommandBufferLevel level)
	{
		m_owner = &owner;
		m_level = level;

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

		if (m_state == State::Pending)
		{
			m_state = State::Invalid;
			CCT_ASSERT_FALSE("Cannot begin a pending command buffer");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		const bool resetAllowed = m_owner && (m_owner->GetFlags() & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		if (m_state != State::Initial && !resetAllowed)
		{
			m_state = State::Invalid;
			CCT_ASSERT_FALSE("Command buffer begin requires VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		m_ops.clear();
		m_usageFlags = beginInfo.flags;
		m_pendingSubmits = 0;
		m_recordingError = VK_SUCCESS;
		m_state = State::Recording;
		return VK_SUCCESS;
	}

	inline VkResult CommandBuffer::End()
	{
		VKD_AUTO_PROFILER_SCOPE();

		if (m_recordingError != VK_SUCCESS)
		{
			const VkResult error = m_recordingError;
			m_recordingError = VK_SUCCESS;
			m_state = State::Invalid;
			return error;
		}

		Transition(State::Executable, {State::Recording});

		return VK_SUCCESS;
	}

	inline VkResult CommandBuffer::Reset(VkCommandBufferResetFlags flags)
	{
		VKD_AUTO_PROFILER_SCOPE();
		(void)flags;

		if (m_state == State::Pending)
		{
			m_state = State::Invalid;
			CCT_ASSERT_FALSE("Cannot reset a pending command buffer");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		m_ops.clear();
		m_usageFlags = 0;
		m_pendingSubmits = 0;
		m_recordingError = VK_SUCCESS;
		m_state = State::Initial;
		return VK_SUCCESS;
	}

	inline void CommandBuffer::PushFillBuffer(VkBuffer dst, VkDeviceSize off, VkDeviceSize size, uint32_t data)
	{
		VKD_FROM_HANDLE(Buffer, bufferObj, dst);
		Record(Buffer::OpFill{bufferObj, off, size, data});
	}

	inline void CommandBuffer::PushCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		vkd::vector<VkBufferCopy> regions(CommandAllocator<VkBufferCopy>(GetAllocationCallbacks()));
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferCopy));
		Record(Buffer::OpCopy{srcBufferObj, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyBuffer2(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy2* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		vkd::vector<VkBufferCopy2> regions(CommandAllocator<VkBufferCopy2>(GetAllocationCallbacks()));
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferCopy2));
		Record(Buffer::OpCopy2{srcBufferObj, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushUpdateBuffer(VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
	{
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		vkd::vector<UInt8> data(CommandAllocator<UInt8>(GetAllocationCallbacks()));
		data.resize(dataSize);
		std::memcpy(data.data(), pData, dataSize);
		Record(Buffer::OpUpdate{dstBufferObj, dstOffset, std::move(data)});
	}

	inline void CommandBuffer::PushCopyImage(VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		vkd::vector<VkImageCopy> regions(CommandAllocator<VkImageCopy>(GetAllocationCallbacks()));
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkImageCopy));
		Record(Image::OpCopy{srcImageObj, dstImageObj, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyBufferToImage(VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, UInt32 regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Buffer, srcBufferObj, srcBuffer);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		vkd::vector<VkBufferImageCopy> regions(CommandAllocator<VkBufferImageCopy>(GetAllocationCallbacks()));
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferImageCopy));
		Record(Buffer::OpCopyBufferToImage{srcBufferObj, dstImageObj, dstImageLayout, std::move(regions)});
	}

	inline void CommandBuffer::PushCopyImageToBuffer(VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, UInt32 regionCount, const VkBufferImageCopy* pRegions)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		vkd::vector<VkBufferImageCopy> regions(CommandAllocator<VkBufferImageCopy>(GetAllocationCallbacks()));
		regions.resize(regionCount);
		std::memcpy(regions.data(), pRegions, regions.size() * sizeof(VkBufferImageCopy));
		Record(Buffer::OpCopyImageToBuffer{srcImageObj, srcImageLayout, dstBufferObj, std::move(regions)});
	}

	inline void CommandBuffer::PushClearColorImage(VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, UInt32 rangeCount, const VkImageSubresourceRange* pRanges)
	{
		VKD_FROM_HANDLE(Image, imageObj, image);

		vkd::vector<VkImageSubresourceRange> ranges(CommandAllocator<VkImageSubresourceRange>(GetAllocationCallbacks()));
		ranges.resize(rangeCount);
		std::memcpy(ranges.data(), pRanges, ranges.size() * sizeof(VkImageSubresourceRange));
		Record(Image::OpClearColorImage{imageObj, imageLayout, *pColor, std::move(ranges)});
	}

	inline void CommandBuffer::PushBindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
	{
		VKD_FROM_HANDLE(Pipeline, pipelineObject, pipeline);

		Record(OpBindPipeline{
			.BindPoint = pipelineBindPoint,
			.PipelineObject = pipelineObject,
		});
	}

	inline void CommandBuffer::PushBindVertexBuffer(std::span<const VkBuffer> pBuffers, std::span<const VkDeviceSize> pOffsets, UInt32 firstBinding)
	{
		vkd::vector<Buffer*> buffers(CommandAllocator<Buffer*>(GetAllocationCallbacks()));
		vkd::vector<VkDeviceSize> offsets(CommandAllocator<VkDeviceSize>(GetAllocationCallbacks()));
		buffers.resize(pBuffers.size());
		offsets.resize(pBuffers.size());

		for (std::size_t i = 0; i < pBuffers.size(); ++i)
		{
			VKD_FROM_HANDLE(Buffer, bufferObject, pBuffers[i]);
			buffers[i] = bufferObject;
			offsets[i] = pOffsets[i];
		}

		Record(OpBindVertexBuffer{
			.Buffers = std::move(buffers),
			.Offsets = std::move(offsets),
			.FirstBinding = firstBinding,
		});
	}

	inline void CommandBuffer::PushBindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
	{
		VKD_FROM_HANDLE(Buffer, bufferObject, buffer);

		Record(OpBindIndexBuffer{
			.BufferHandle = bufferObject,
			.Offset = offset,
			.IndexType = indexType,
		});
	}

	inline void CommandBuffer::PushBindDescriptorSets(VkPipelineBindPoint pipelineBindPoint, UInt32 firstSet, std::span<const VkDescriptorSet> pDescriptorSets, std::span<const UInt32> pDynamicOffsets)
	{
		vkd::vector<DescriptorSet*> descriptorSets(CommandAllocator<DescriptorSet*>(GetAllocationCallbacks()));
		descriptorSets.resize(pDescriptorSets.size());

		vkd::vector<DynamicOffsetEntry> dynamicOffsets(CommandAllocator<DynamicOffsetEntry>(GetAllocationCallbacks()));

		std::size_t dynamicOffsetCursor = 0;
		for (std::size_t i = 0; i < pDescriptorSets.size(); ++i)
		{
			VKD_FROM_HANDLE(DescriptorSet, descriptorSetObject, pDescriptorSets[i]);
			descriptorSets[i] = descriptorSetObject;

			if (!descriptorSetObject || !descriptorSetObject->GetLayout())
				continue;

			for (const DescriptorSetLayout::Binding& binding : descriptorSetObject->GetLayout()->GetBindings())
			{
				const bool isDynamic = binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
				if (!isDynamic)
					continue;

				for (UInt32 element = 0; element < binding.descriptorCount && dynamicOffsetCursor < pDynamicOffsets.size(); ++element)
				{
					dynamicOffsets.emplace_back(DynamicOffsetEntry{
						.SetIndex = static_cast<UInt32>(i),
						.Binding = binding.binding,
						.ArrayElement = element,
						.Offset = pDynamicOffsets[dynamicOffsetCursor++],
					});
				}
			}
		}

		Record(OpBindDescriptorSets{
			.BindPoint = pipelineBindPoint,
			.FirstSet = firstSet,
			.DescriptorSets = std::move(descriptorSets),
			.DynamicOffsets = std::move(dynamicOffsets),
		});
	}

	inline void CommandBuffer::PushDraw(UInt32 vertexCount, UInt32 instanceCount, UInt32 firstVertex, UInt32 firstInstance)
	{
		Record(OpDraw{
			.VertexCount = vertexCount,
			.InstanceCount = instanceCount,
			.FirstVertex = firstVertex,
			.FirstInstance = firstInstance,
		});
	}

	inline void CommandBuffer::PushDrawIndexed(UInt32 indexCount, UInt32 instanceCount, UInt32 firstIndex, cct::Int32 vertexOffset, UInt32 firstInstance)
	{
		Record(OpDrawIndexed{
			.IndexCount = indexCount,
			.InstanceCount = instanceCount,
			.FirstIndex = firstIndex,
			.VertexOffset = vertexOffset,
			.FirstInstance = firstInstance,
		});
	}

	inline void CommandBuffer::PushSetViewport(std::span<const VkViewport> pViewports, UInt32 firstViewport)
	{
		Record(OpSetViewport{
			.Viewports = vkd::vector<VkViewport>(pViewports.begin(), pViewports.end(), CommandAllocator<VkViewport>(GetAllocationCallbacks())),
			.FirstViewport = firstViewport,
		});
	}

	inline void CommandBuffer::PushSetScissor(std::span<const VkRect2D> pScissors, UInt32 firstScissor)
	{
		Record(OpSetScissor{
			.Scissors = vkd::vector<VkRect2D>(pScissors.begin(), pScissors.end(), CommandAllocator<VkRect2D>(GetAllocationCallbacks())),
			.FirstScissor = firstScissor,
		});
	}

	inline void CommandBuffer::PushSetBlendConstants(const float constants[4])
	{
		OpSetBlendConstants op{};
		std::copy_n(constants, 4, op.Constants);
		Record(std::move(op));
	}

	inline void CommandBuffer::PushBeginRenderPass(const VkRenderPassBeginInfo& beginInfo, VkSubpassContents contents)
	{
		VKD_FROM_HANDLE(RenderPass, renderPassObj, beginInfo.renderPass);
		VKD_FROM_HANDLE(Framebuffer, framebufferObj, beginInfo.framebuffer);

		vkd::vector<VkClearValue> clearValues(CommandAllocator<VkClearValue>(GetAllocationCallbacks()));
		if (beginInfo.clearValueCount > 0 && beginInfo.pClearValues != nullptr)
			clearValues.assign(beginInfo.pClearValues, beginInfo.pClearValues + beginInfo.clearValueCount);

		Record(OpBeginRenderPass{
			.RenderPassObject = renderPassObj,
			.FramebufferObject = framebufferObj,
			.RenderArea = beginInfo.renderArea,
			.ClearValues = std::move(clearValues),
			.Contents = contents,
		});
	}

	inline void CommandBuffer::PushEndRenderPass()
	{
		Record(OpEndRenderPass{});
	}

	inline void CommandBuffer::PushClearDepthStencilImage(VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue& value, UInt32 rangeCount, const VkImageSubresourceRange* pRanges)
	{
		VKD_FROM_HANDLE(Image, imageObj, image);

		vkd::vector<VkImageSubresourceRange> ranges(CommandAllocator<VkImageSubresourceRange>(GetAllocationCallbacks()));
		ranges.assign(pRanges, pRanges + rangeCount);
		Record(Image::OpClearDepthStencilImage{imageObj, imageLayout, value, std::move(ranges)});
	}

	inline void CommandBuffer::PushClearAttachments(UInt32 attachmentCount, const VkClearAttachment* pAttachments, UInt32 rectCount, const VkClearRect* pRects)
	{
		vkd::vector<VkClearAttachment> attachments(CommandAllocator<VkClearAttachment>(GetAllocationCallbacks()));
		attachments.assign(pAttachments, pAttachments + attachmentCount);
		vkd::vector<VkClearRect> rects(CommandAllocator<VkClearRect>(GetAllocationCallbacks()));
		rects.assign(pRects, pRects + rectCount);
		Record(OpClearAttachments{
			.Attachments = std::move(attachments),
			.Rects = std::move(rects),
		});
	}

	inline void CommandBuffer::PushResolveImage(VkImage srcImage, VkImage dstImage, UInt32 regionCount, const VkImageResolve* pRegions)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		vkd::vector<VkImageResolve> regions(CommandAllocator<VkImageResolve>(GetAllocationCallbacks()));
		regions.assign(pRegions, pRegions + regionCount);
		Record(Image::OpResolve{srcImageObj, dstImageObj, std::move(regions)});
	}

	inline void CommandBuffer::PushBlitImage(VkImage srcImage, VkImage dstImage, UInt32 regionCount, const VkImageBlit* pRegions, VkFilter filter)
	{
		VKD_FROM_HANDLE(Image, srcImageObj, srcImage);
		VKD_FROM_HANDLE(Image, dstImageObj, dstImage);

		vkd::vector<VkImageBlit> regions(CommandAllocator<VkImageBlit>(GetAllocationCallbacks()));
		regions.assign(pRegions, pRegions + regionCount);
		Record(Image::OpBlit{srcImageObj, dstImageObj, std::move(regions), filter});
	}

	inline void CommandBuffer::PushSetEvent(VkEvent event, bool signaled)
	{
		VKD_FROM_HANDLE(Event, eventObj, event);

		Record(OpSetEvent{
			.EventObject = eventObj,
			.Signaled = signaled,
		});
	}

	inline void CommandBuffer::PushWaitEvents(UInt32 eventCount, const VkEvent* pEvents)
	{
		vkd::vector<Event*> events(CommandAllocator<Event*>(GetAllocationCallbacks()));
		events.reserve(eventCount);
		for (UInt32 i = 0; i < eventCount; ++i)
		{
			VKD_FROM_HANDLE(Event, eventObj, pEvents[i]);
			events.push_back(eventObj);
		}
		Record(OpWaitEvents{.Events = std::move(events)});
	}

	inline void CommandBuffer::PushExecuteCommands(UInt32 commandBufferCount, const VkCommandBuffer* pCommandBuffers)
	{
		vkd::vector<CommandBuffer*> commandBuffers(CommandAllocator<CommandBuffer*>(GetAllocationCallbacks()));
		commandBuffers.reserve(commandBufferCount);
		for (UInt32 i = 0; i < commandBufferCount; ++i)
		{
			VKD_FROM_HANDLE(CommandBuffer, commandBufferObj, pCommandBuffers[i]);
			commandBuffers.push_back(commandBufferObj);
		}
		Record(OpExecuteCommands{.CommandBuffers = std::move(commandBuffers)});
	}

	inline void CommandBuffer::PushPushConstants(VkShaderStageFlags stageFlags, UInt32 offset, UInt32 size, const void* pValues)
	{
		vkd::vector<UInt8> data(CommandAllocator<UInt8>(GetAllocationCallbacks()));
		data.assign(static_cast<const UInt8*>(pValues), static_cast<const UInt8*>(pValues) + size);
		Record(OpPushConstants{
			.Stages = stageFlags,
			.Offset = offset,
			.Data = std::move(data),
		});
	}

	inline void CommandBuffer::PushDispatch(UInt32 groupCountX, UInt32 groupCountY, UInt32 groupCountZ)
	{
		Record(OpDispatch{
			.GroupCountX = groupCountX,
			.GroupCountY = groupCountY,
			.GroupCountZ = groupCountZ,
		});
	}

	inline void CommandBuffer::PushResetQueryPool(VkQueryPool queryPool, UInt32 firstQuery, UInt32 queryCount)
	{
		VKD_FROM_HANDLE(QueryPool, queryPoolObj, queryPool);

		Record(OpResetQueryPool{
			.QueryPoolObject = queryPoolObj,
			.FirstQuery = firstQuery,
			.QueryCount = queryCount,
		});
	}

	inline void CommandBuffer::PushBeginQuery(VkQueryPool queryPool, UInt32 query, VkQueryControlFlags flags)
	{
		VKD_FROM_HANDLE(QueryPool, queryPoolObj, queryPool);

		Record(OpBeginQuery{
			.QueryPoolObject = queryPoolObj,
			.Query = query,
			.Flags = flags,
		});
	}

	inline void CommandBuffer::PushEndQuery(VkQueryPool queryPool, UInt32 query)
	{
		VKD_FROM_HANDLE(QueryPool, queryPoolObj, queryPool);

		Record(OpEndQuery{
			.QueryPoolObject = queryPoolObj,
			.Query = query,
		});
	}

	inline void CommandBuffer::PushWriteTimestamp(VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, UInt32 query)
	{
		VKD_FROM_HANDLE(QueryPool, queryPoolObj, queryPool);

		Record(OpWriteTimestamp{
			.QueryPoolObject = queryPoolObj,
			.PipelineStage = pipelineStage,
			.Query = query,
		});
	}

	inline void CommandBuffer::PushCopyQueryPoolResults(VkQueryPool queryPool, UInt32 firstQuery, UInt32 queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
	{
		VKD_FROM_HANDLE(QueryPool, queryPoolObj, queryPool);
		VKD_FROM_HANDLE(Buffer, dstBufferObj, dstBuffer);

		Record(OpCopyQueryPoolResults{
			.QueryPoolObject = queryPoolObj,
			.FirstQuery = firstQuery,
			.QueryCount = queryCount,
			.DstBuffer = dstBufferObj,
			.DstOffset = dstOffset,
			.Stride = stride,
			.Flags = flags,
		});
	}

	inline VkResult CommandBuffer::MarkSubmitted()
	{
		if (m_state == State::Pending)
		{
			if ((m_usageFlags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0)
			{
				m_state = State::Invalid;
				CCT_ASSERT_FALSE("Command buffer resubmitted without VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT");
				return VK_ERROR_VALIDATION_FAILED_EXT;
			}
			++m_pendingSubmits;
			return VK_SUCCESS;
		}

		const VkResult result = Transition(State::Pending, {State::Executable});
		if (result == VK_SUCCESS)
			m_pendingSubmits = 1;
		return result;
	}

	inline VkResult CommandBuffer::MarkComplete()
	{
		if (m_state != State::Pending || m_pendingSubmits == 0)
		{
			m_state = State::Invalid;
			CCT_ASSERT_FALSE("Invalid command buffer complete");
			return VK_ERROR_VALIDATION_FAILED_EXT;
		}

		--m_pendingSubmits;
		if (m_pendingSubmits > 0)
			return VK_SUCCESS;

		if (m_usageFlags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
		{
			m_ops.clear();
			m_state = State::Invalid;
			return VK_SUCCESS;
		}

		m_state = State::Executable;
		return VK_SUCCESS;
	}

	inline std::span<const CommandBuffer::Op> CommandBuffer::GetOps() const
	{
		return m_ops;
	}

	inline bool CommandBuffer::IsSealed() const
	{
		return m_state == State::Executable || m_state == State::Pending;
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
		const State previous = m_state;
		m_state = State::Invalid;
		CCT_ASSERT_FALSE("Invalid CB state transition {} -> {}", (int)previous, (int)to);
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}

	template<typename OpT>
	inline void CommandBuffer::Record(OpT&& op)
	{
		try
		{
			m_ops.emplace_back(std::forward<OpT>(op));
		}
		catch (const std::bad_alloc&)
		{
			if (m_recordingError == VK_SUCCESS)
				m_recordingError = VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}
} // namespace vkd
