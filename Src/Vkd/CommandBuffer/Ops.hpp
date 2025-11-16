/**
 * @file Ops.hpp
 * @brief Command buffer operation structures
 * @date 2025-10-25
 *
 * Defines structures for command buffer operations used in command recording.
 */

#pragma once

#include <NazaraUtils/TypeList.hpp>

namespace vkd
{
	struct OpBindVertexBuffer
	{
		std::vector<Buffer*> Buffers;
		std::vector<VkDeviceSize> Offsets;
		UInt32 FirstBinding;
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
		OpDraw,
		OpDrawIndexed,
		OpDrawIndirect,
		OpDrawIndexedIndirect,
		OpBindPipeline>;
} // namespace vkd
