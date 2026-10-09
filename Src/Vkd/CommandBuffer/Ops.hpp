/**
 * @file Ops.hpp
 * @brief Command buffer operation structures
 * @date 2025-10-25
 *
 * Defines structures for command buffer operations used in command recording.
 */

#pragma once

#include <Concerto/Core/Types/Types.hpp>

#include "Vkd/Memory/Stl.hpp"

#include <NazaraUtils/TypeList.hpp>
#include <vulkan/vulkan.h>

namespace vkd
{
	using namespace cct;

	class Buffer;
	class Pipeline;
	class Framebuffer;
	class RenderPass;
	class DescriptorSet;
	class QueryPool;
	class Event;
	class CommandBuffer;

	struct OpBindVertexBuffer
	{
		vkd::vector<Buffer*> Buffers;
		vkd::vector<VkDeviceSize> Offsets;
		UInt32 FirstBinding;
	};

	struct OpBindIndexBuffer
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
		VkIndexType IndexType;
	};

	struct DynamicOffsetEntry
	{
		UInt32 SetIndex;
		UInt32 Binding;
		UInt32 ArrayElement;
		UInt32 Offset;
	};

	struct OpBindDescriptorSets
	{
		VkPipelineBindPoint BindPoint;
		UInt32 FirstSet;
		vkd::vector<DescriptorSet*> DescriptorSets;
		vkd::vector<DynamicOffsetEntry> DynamicOffsets;
	};

	struct OpBeginRenderPass
	{
		RenderPass* RenderPassObject;
		Framebuffer* FramebufferObject;
		VkRect2D RenderArea;
		vkd::vector<VkClearValue> ClearValues;
		VkSubpassContents Contents;
	};

	struct OpEndRenderPass
	{
	};

	struct OpResetQueryPool
	{
		QueryPool* QueryPoolObject;
		UInt32 FirstQuery;
		UInt32 QueryCount;
	};

	struct OpBeginQuery
	{
		QueryPool* QueryPoolObject;
		UInt32 Query;
		VkQueryControlFlags Flags;
	};

	struct OpEndQuery
	{
		QueryPool* QueryPoolObject;
		UInt32 Query;
	};

	struct OpWriteTimestamp
	{
		QueryPool* QueryPoolObject;
		VkPipelineStageFlagBits PipelineStage;
		UInt32 Query;
	};

	struct OpCopyQueryPoolResults
	{
		QueryPool* QueryPoolObject;
		UInt32 FirstQuery;
		UInt32 QueryCount;
		Buffer* DstBuffer;
		VkDeviceSize DstOffset;
		VkDeviceSize Stride;
		VkQueryResultFlags Flags;
	};

	struct OpDraw
	{
		UInt32 VertexCount;
		UInt32 InstanceCount;
		UInt32 FirstVertex;
		UInt32 FirstInstance;
	};

	struct OpDrawIndexed
	{
		UInt32 IndexCount;
		UInt32 InstanceCount;
		UInt32 FirstIndex;
		cct::Int32 VertexOffset;
		UInt32 FirstInstance;
	};

	struct OpDrawIndirect
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
		UInt32 DrawCount;
		UInt32 Stride;
	};

	struct OpDrawIndexedIndirect
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
		UInt32 DrawCount;
		UInt32 Stride;
	};

	struct OpBindPipeline
	{
		VkPipelineBindPoint BindPoint;
		Pipeline* PipelineObject;
	};

	struct OpSetViewport
	{
		vkd::vector<VkViewport> Viewports;
		UInt32 FirstViewport;
	};

	struct OpSetScissor
	{
		vkd::vector<VkRect2D> Scissors;
		UInt32 FirstScissor;
	};

	struct OpSetBlendConstants
	{
		float Constants[4];
	};

	struct OpSetDepthBias
	{
		float ConstantFactor;
		float Clamp;
		float SlopeFactor;
	};

	struct OpSetDepthBounds
	{
		float Min;
		float Max;
	};

	struct OpSetStencilValue
	{
		enum class Field : UInt8
		{
			CompareMask,
			WriteMask,
			Reference,
		};
		Field Which;
		VkStencilFaceFlags FaceMask;
		UInt32 Value;
	};

	// vkCmdPushConstants: Data lands at Offset in the command buffer's push-constant block.
	struct OpPushConstants
	{
		VkShaderStageFlags Stages;
		UInt32 Offset;
		vkd::vector<UInt8> Data;
	};

	struct OpDispatch
	{
		UInt32 GroupCountX;
		UInt32 GroupCountY;
		UInt32 GroupCountZ;
	};

	struct OpDispatchIndirect
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
	};

	// vkCmdClearAttachments: attachments of the current subpass, inside a render pass.
	struct OpClearAttachments
	{
		vkd::vector<VkClearAttachment> Attachments;
		vkd::vector<VkClearRect> Rects;
	};

	// vkCmdSetEvent (Signaled = true) / vkCmdResetEvent (Signaled = false).
	struct OpSetEvent
	{
		Event* EventObject;
		bool Signaled;
	};

	struct OpWaitEvents
	{
		vkd::vector<Event*> Events;
	};

	// vkCmdExecuteCommands: the secondary command buffers' ops run inline, in order.
	struct OpExecuteCommands
	{
		vkd::vector<CommandBuffer*> CommandBuffers;
	};

	using Op = Nz::TypeList<
		OpBindVertexBuffer,
		OpBindIndexBuffer,
		OpBindDescriptorSets,
		OpDraw,
		OpDrawIndexed,
		OpDrawIndirect,
		OpDrawIndexedIndirect,
		OpBindPipeline,
		OpSetViewport,
		OpSetScissor,
		OpSetBlendConstants,
		OpSetDepthBias,
		OpSetDepthBounds,
		OpSetStencilValue,
		OpBeginRenderPass,
		OpEndRenderPass,
		OpResetQueryPool,
		OpBeginQuery,
		OpEndQuery,
		OpWriteTimestamp,
		OpCopyQueryPoolResults,
		OpClearAttachments,
		OpSetEvent,
		OpWaitEvents,
		OpExecuteCommands,
		OpPushConstants,
		OpDispatch,
		OpDispatchIndirect>;
} // namespace vkd
