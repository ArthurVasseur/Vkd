/**
 * @file Ops.hpp
 * @brief Command buffer operation structures
 * @date 2025-10-25
 *
 * Defines structures for command buffer operations used in command recording.
 */

#pragma once

#include <vector>

#include <Concerto/Core/Types/Types.hpp>

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

	struct OpBindVertexBuffer
	{
		std::vector<Buffer*> Buffers;
		std::vector<VkDeviceSize> Offsets;
		UInt32 FirstBinding;
	};

	struct OpBindDescriptorSets
	{
		VkPipelineBindPoint BindPoint;
		UInt32 FirstSet;
		std::vector<DescriptorSet*> DescriptorSets;
	};

	struct OpBeginRenderPass
	{
		RenderPass* RenderPassObject;
		Framebuffer* FramebufferObject;
		VkRect2D RenderArea;
		std::vector<VkClearValue> ClearValues;
		VkSubpassContents Contents;
	};

	struct OpEndRenderPass
	{
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

	using Op = Nz::TypeList<
		OpBindVertexBuffer,
		OpBindDescriptorSets,
		OpDraw,
		OpDrawIndexed,
		OpDrawIndirect,
		OpDrawIndexedIndirect,
		OpBindPipeline,
		OpBeginRenderPass,
		OpEndRenderPass>;
} // namespace vkd
