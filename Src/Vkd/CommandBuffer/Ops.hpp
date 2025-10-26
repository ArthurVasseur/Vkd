#pragma once

#include <NazaraUtils/TypeList.hpp>

namespace vkd
{
	struct OpBindVertexBuffer
	{
		std::vector<Buffer*> Buffers;
		std::vector<VkDeviceSize> Offsets;
		cct::UInt32 FirstBinding;
	};

	struct OpDraw
	{
		cct::UInt32 VertexCount;
		cct::UInt32 InstanceCount;
		cct::UInt32 FirstVertex;
		cct::UInt32 FirstInstance;
	};

	struct OpDrawIndexed
	{
		cct::UInt32 IndexCount;
		cct::UInt32 InstanceCount;
		cct::UInt32 FirstIndex;
		cct::Int32 VertexOffset;
		cct::UInt32 FirstInstance;
	};

	struct OpDrawIndirect
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
		cct::UInt32 DrawCount;
		cct::UInt32 Stride;
	};

	struct OpDrawIndexedIndirect
	{
		Buffer* BufferHandle;
		VkDeviceSize Offset;
		cct::UInt32 DrawCount;
		cct::UInt32 Stride;
	};

	struct OpBindPipeline
	{
		VkPipelineBindPoint BindPoint;
		Pipeline* Pipeline;
	};

	using Op = Nz::TypeList<
		OpBindVertexBuffer,
		OpDraw,
		OpDrawIndexed,
		OpDrawIndirect,
		OpDrawIndexedIndirect,
		OpBindPipeline
	>;
}
